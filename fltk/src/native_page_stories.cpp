// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_page_stories.h"
#include "native_table_layout.h"
#include "native_table_model.h"
#include "native_list_number.h"
#include <cmath>
#include <map>
#include <tuple>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text SVG = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void note(std::vector<Text> &notes, const Text &message) {
  if (std::find(notes.begin(), notes.end(), message) == notes.end())
    notes.push_back(message);
}
double measure(const Text &text, double base) {
  static const Regex pattern(
      "^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))(cm|mm|in|pt|pc|px|%)$");
  auto match = pattern.match(text.trimmed());
  if (!match.hasMatch())
    fail("Invalid header/footer measure");
  auto unit = match.captured(2);
  double scale = unit == "cm"   ? 72 / 2.54
                 : unit == "mm" ? 72 / 25.4
                 : unit == "in" ? 72
                 : unit == "pc" ? 12
                 : unit == "px" ? .75
                 : unit == "%"  ? base / 100
                                : 1;
  double value = match.captured(1).toDouble() * scale;
  if (!std::isfinite(value) || std::abs(value) > 100000)
    fail("Header/footer measure exceeds native limit");
  return value;
}
bool boolean(const Text &value) {
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  fail("Invalid header/footer boolean");
}
void visit(XmlElement node, const std::function<void(XmlElement)> &fn) {
  fn(node);
  for (auto child = node.firstChild(); !child.isNull();
       child = child.nextSibling())
    if (child.isElement())
      visit(child, fn);
}
void fields(XmlElement root, const TextPageFieldContext &context,
            const Text &default_format, bool default_sync,
            std::vector<Text> &unsupported) {
  visit(root, [&](XmlElement node) {
    if (node.namespaceURI() != TEXT ||
        (node.localName() != "page-number" && node.localName() != "page-count"))
      return;
    int64_t value = int64_t(context.total);
    if (node.localName() == "page-number") {
      if (boolean(node.attributeNS(TEXT, "fixed", "false")))
        return;
      value = int64_t(context.logical);
      auto select = node.attributeNS(TEXT, "select-page", "current");
      std::optional<size_t> selected = context.logical;
      if (select == "previous")
        selected = context.previous;
      else if (select == "next")
        selected = context.next;
      else if (select != "current")
        fail("Invalid page-number selection");
      if (!selected) {
        set_plain_text(node, {});
        return;
      }
      value = int64_t(*selected);
      bool valid = false;
      auto adjust =
          node.attributeNS(TEXT, "page-adjust", "0").toLongLong(&valid);
      if (!valid || adjust < -1000000 || adjust > 1000000)
        fail("Invalid page-number adjustment");
      value += adjust;
    }
    auto format = node.attributeNS(
        STYLE, "num-format",
        node.localName() == "page-number" ? default_format : "1");
    if (!supported_list_number_format(format)) {
      note(unsupported, "Unsupported header/footer page-number format");
      return;
    }
    auto sync = boolean(node.attributeNS(
        STYLE, "num-letter-sync",
        node.localName() == "page-number" && default_sync ? "true" : "false"));
    set_plain_text(node, value >= 0 ? format_list_number(value, format, sync)
                                    : Text{});
  });
}
std::vector<TextStoryContent>
read_story(const std::shared_ptr<OdfTextReader> &reader, XmlElement source,
           const TextPageFieldContext &context, const Text &format, bool sync) {
  auto document = read_document_xml(Bytes("<story/>"));
  auto copy = document.importNode(source, true).toElement();
  document.documentElement().appendChild(copy);
  std::vector<Text> unsupported;
  fields(copy, context, format, sync, unsupported);
  std::vector<TextStoryContent> result;
  for (auto node : children(copy, STYLE)) {
    std::optional<TextParagraph::Alignment> alignment;
    if (node.localName() == "region-left")
      alignment = TextParagraph::Left;
    else if (node.localName() == "region-center")
      alignment = TextParagraph::Center;
    else if (node.localName() == "region-right")
      alignment = TextParagraph::Right;
    if (alignment) {
      result.push_back({reader->story(node, true, true), alignment});
      copy.removeChild(node);
    }
  }
  auto direct = reader->story(copy, true, true);
  direct.unsupported.insert(direct.unsupported.end(), unsupported.begin(),
                            unsupported.end());
  result.insert(result.begin(), {std::move(direct), {}});
  return result;
}
void properties(TextPageStory &story, const XmlElement &props, double width) {
  apply_text_box_properties(story.box, props);
  if (props.hasAttributeNS(FO, "margin"))
    story.margins.fill(measure(props.attributeNS(FO, "margin"), width));
  const char *sides[] = {"top", "right", "bottom", "left"};
  for (size_t side = 0; side < 4; ++side)
    if (props.hasAttributeNS(FO, Text("margin-") + sides[side]))
      story.margins[side] =
          measure(props.attributeNS(FO, Text("margin-") + sides[side]), width);
  if (props.hasAttributeNS(FO, "min-height"))
    story.minimum = measure(props.attributeNS(FO, "min-height"), width);
  if (props.hasAttributeNS(SVG, "height"))
    story.height = measure(props.attributeNS(SVG, "height"), width);
  if (props.hasAttributeNS(STYLE, "dynamic-spacing"))
    story.dynamic_spacing =
        boolean(props.attributeNS(STYLE, "dynamic-spacing"));
  if (story.minimum < 0 || (story.height && *story.height <= 0))
    fail("Invalid header/footer height");
  for (double margin : story.margins)
    if (margin < 0)
      fail("Negative header/footer margin");
  if (props.hasAttributeNS(STYLE, "shadow") &&
      props.attributeNS(STYLE, "shadow") != "none")
    note(story.unsupported, "Header/footer box shadows");
}
void append(TextBoxLayout &to, TextBoxLayout from) {
  to.height = std::max(to.height, from.height);
  to.lines.insert(to.lines.end(), std::make_move_iterator(from.lines.begin()),
                  std::make_move_iterator(from.lines.end()));
  to.cells.insert(to.cells.end(), std::make_move_iterator(from.cells.begin()),
                  std::make_move_iterator(from.cells.end()));
  to.paints.insert(to.paints.end(),
                   std::make_move_iterator(from.paints.begin()),
                   std::make_move_iterator(from.paints.end()));
  for (const auto &message : from.unsupported)
    note(to.unsupported, message);
}
} // namespace
void read_page_stories(const OdfPackage &package, TextFlow &flow) {
  if (package.entry("styles.xml").empty())
    return;
  auto styles = read_document_xml(package.entry("styles.xml"));
  std::map<Text, XmlElement> layouts;
  XmlElement defaults;
  for (const auto &document : {styles, package.xml}) {
    for (auto group : children(document.documentElement(), OFFICE))
      if (group.localName() == "styles" ||
          group.localName() == "automatic-styles") {
        for (auto layout : children(group, STYLE, "page-layout"))
          layouts[layout.attributeNS(STYLE, "name")] = layout;
        for (auto layout : children(group, STYLE, "default-page-layout"))
          defaults = layout;
      }
  }
  std::shared_ptr<OdfTextReader> reader;
  for (auto master :
       children(child(styles.documentElement(), OFFICE, "master-styles"), STYLE,
                "master-page")) {
    auto found = flow.masters.find(master.attributeNS(STYLE, "name"));
    if (found == flow.masters.end())
      continue;
    auto &page = found->second;
    auto layout = layouts.find(master.attributeNS(STYLE, "page-layout-name"));
    auto stories = std::make_shared<TextPageStories>();
    bool any = false;
    for (auto kind : {TextPageStory::Header, TextPageStory::Footer})
      for (auto variant : {TextPageStory::Default, TextPageStory::First,
                           TextPageStory::Left}) {
        Text side = kind == TextPageStory::Header ? "header" : "footer";
        Text tag = side + (variant == TextPageStory::First  ? "-first"
                           : variant == TextPageStory::Left ? "-left"
                                                            : "");
        auto source = child(master, STYLE, tag);
        if (source.isNull())
          continue;
        if (!reader)
          reader = std::make_shared<OdfTextReader>(package);
        auto story = std::make_shared<TextPageStory>();
        story->kind = kind;
        story->variant = variant;
        story->master = page.name;
        story->source = source;
        story->displayed =
            boolean(source.attributeNS(STYLE, "display", "true"));
        for (auto page_layout :
             {defaults,
              layout == layouts.end() ? XmlElement{} : layout->second}) {
          auto props = child(page_layout, STYLE, "page-layout-properties");
          if (props.hasAttributeNS(STYLE, "num-format"))
            story->number_format = props.attributeNS(STYLE, "num-format");
          if (props.hasAttributeNS(STYLE, "num-letter-sync"))
            story->number_sync =
                boolean(props.attributeNS(STYLE, "num-letter-sync"));
        }
        properties(*story,
                   child(child(defaults, STYLE, side + "-style"), STYLE,
                         "header-footer-properties"),
                   page.width - page.left - page.right);
        if (layout != layouts.end())
          properties(*story,
                     child(child(layout->second, STYLE, side + "-style"), STYLE,
                           "header-footer-properties"),
                     page.width - page.left - page.right);
        visit(source, [&](XmlElement node) {
          if (node.namespaceURI() == TEXT &&
              node.localName() == "page-number" &&
              !boolean(node.attributeNS(TEXT, "fixed", "false")))
            story->page_field = true;
          if (node.namespaceURI() == TEXT && node.localName() == "page-count")
            story->count_field = true;
        });
        story->read =
            [reader, source, format = story->number_format,
             sync = story->number_sync](const TextPageFieldContext &context) {
              return read_story(reader, source, context, format, sync);
            };
        (kind == TextPageStory::Header ? stories->headers
                                       : stories->footers)[variant] = story;
        any = true;
      }
    if (any)
      page.stories = std::move(stories);
  }
}
struct PageStoryLayouter::Impl {
  MeasureText measure;
  NativeTextShaper *shaper;
  using Key = std::tuple<const TextPageStory *, double, size_t, size_t, size_t,
                         std::optional<size_t>, std::optional<size_t>>;
  std::map<Key, std::shared_ptr<const TextBoxLayout>> cache;
  size_t characters = 0, lines = 0, paints = 0;
  Impl(const MeasureText &m, NativeTextShaper *s) : measure(m), shaper(s) {}
  std::shared_ptr<const TextBoxLayout>
  layout(const TextPageStory &story, double width,
         const TextPageFieldContext &context) {
    Key key{&story,
            width,
            story.page_field ? context.page : 0,
            (story.page_field || story.count_field) ? context.total : 0,
            story.page_field ? context.logical : 0,
            story.page_field ? context.previous : std::nullopt,
            story.page_field ? context.next : std::nullopt};
    if (auto found = cache.find(key); found != cache.end())
      return found->second;
    auto contents = story.read(context);
    for (const auto &content : contents)
      for (const auto &paragraph : content.flow.paragraphs) {
        characters += paragraph.text.size();
        if (characters > 4000000)
          fail("Resolved header/footer text exceeds native limit");
      }
    TextFlow outer;
    outer.paragraphs.emplace_back();
    TextTable table;
    table.columns.resize(1);
    table.columns[0].relative = 1;
    table.rows.resize(1);
    table.rows[0].minimum = story.minimum;
    if (story.height)
      table.rows[0].height = std::max(*story.height, story.minimum);
    TextTableCell cell;
    cell.rows = cell.columns = 1;
    cell.style = story.box;
    cell.blocks.push_back({TextBlock::Paragraph, 0});
    table.cells.push_back(cell);
    outer.tables.push_back(table);
    NativeBlockLayouter box(outer, [&](const TextBlock &, double inner) {
      auto merged = std::make_shared<TextBoxLayout>();
      for (auto &content : contents) {
        if (content.alignment) {
          for (auto &p : content.flow.paragraphs)
            p.alignment = *content.alignment;
          for (auto &b : content.flow.blocks)
            if (b.kind == TextBlock::Paragraph && b.formatted) {
              auto p = std::make_shared<TextParagraph>(*b.formatted);
              p->alignment = *content.alignment;
              b.formatted = p;
            }
        }
        NativeBlockLayouter layout(
            content.flow,
            table_paragraph_layouter(content.flow, measure, shaper));
        auto text = layout.blocks(content.flow.blocks, inner);
        for (const auto &warning : content.flow.unsupported)
          note(text.unsupported, warning);
        append(*merged, std::move(text));
      }
      return merged;
    });
    auto layout = box.table({TextBlock::Table, 0}, width);
    auto result = std::make_shared<TextBoxLayout>(layout->display);
    result->height = layout->height;
    for (const auto &warning : story.unsupported)
      note(result->unsupported, warning);
    cache.emplace(key, result);
    return result;
  }
};
PageStoryLayouter::PageStoryLayouter(const MeasureText &measure,
                                     NativeTextShaper *shaper)
    : impl_(std::make_shared<Impl>(measure, shaper)) {}
void PageStoryLayouter::prepare(TextPage &page, size_t number, size_t total,
                                bool first, std::vector<Text> &unsupported,
                                std::optional<size_t> previous,
                                std::optional<size_t> next) {
  page.stories.clear();
  page.body_top = page.style.top;
  page.body_bottom = page.style.height - page.style.bottom;
  page.first_master_page = first;
  if (!page.style.stories)
    return;
  for (auto kind : {TextPageStory::Header, TextPageStory::Footer}) {
    const auto &choices = kind == TextPageStory::Header
                              ? page.style.stories->headers
                              : page.style.stories->footers;
    auto source = first && choices[TextPageStory::First]
                      ? choices[TextPageStory::First]
                  : page.number % 2 == 0 && choices[TextPageStory::Left]
                      ? choices[TextPageStory::Left]
                      : choices[TextPageStory::Default];
    if (!source || !source->displayed)
      continue;
    double width = page.style.width - page.style.left - page.style.right -
                   source->margins[1] - source->margins[3];
    if (!std::isfinite(width) || width < 1)
      fail("Header/footer margins leave no text area");
    auto layout = impl_->layout(*source, width,
                                {number, total, page.number, previous, next});
    double gap = source->margins[kind == TextPageStory::Header ? 2 : 0];
    double reserved = layout->height + gap;
    if (source->dynamic_spacing && !source->height)
      reserved = std::max(layout->height, source->minimum + gap);
    double x = page.style.left + source->margins[3], y;
    if (kind == TextPageStory::Header) {
      y = page.style.top + source->margins[0];
      page.body_top = y + reserved;
    } else {
      double bottom =
          page.style.height - page.style.bottom - source->margins[2];
      y = bottom - layout->height;
      page.body_bottom = bottom - reserved;
    }
    auto content = std::make_shared<TextBoxLayout>(*layout);
    offset_text_layout(*content, x, y);
    clip_text_layout(*content, {0, 0, page.style.width, page.style.height});
    impl_->lines += content->lines.size();
    impl_->paints += content->paints.size();
    if (impl_->lines > 1000000 || impl_->paints > 4000000)
      fail("Header/footer display exceeds native limit");
    for (const auto &warning : content->unsupported)
      note(unsupported, warning);
    page.stories.push_back({source, content, {x, y, width, layout->height}});
  }
  if (!std::isfinite(page.body_top) || !std::isfinite(page.body_bottom) ||
      page.body_bottom - page.body_top < 1)
    fail("Headers and footers leave no page body area");
}
} // namespace DomeOffice
