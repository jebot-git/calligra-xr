// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_layout.h"
#include "native_list_number.h"
#include "native_text_shaper.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <unicode/ubrk.h>
#include <unicode/utext.h>

namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
void note(std::vector<Text> &notes, const Text &value) {
  if (std::find(notes.begin(), notes.end(), value) == notes.end())
    notes.push_back(value);
}
double length(const Text &value) {
  static const Regex re(
      "^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))(cm|mm|in|pt|pc|px)$");
  auto match = re.match(value.trimmed());
  if (!match.hasMatch())
    fail("Unsupported text-layout length");
  const auto unit = match.captured(2);
  double scale = unit == "cm"   ? 72 / 2.54
                 : unit == "mm" ? 72 / 25.4
                 : unit == "in" ? 72
                 : unit == "pc" ? 12
                 : unit == "px" ? .75
                                : 1;
  double result = match.captured(1).toDouble() * scale;
  if (!std::isfinite(result) || std::abs(result) > 100'000)
    fail("Text-layout length exceeds the limit");
  return result;
}
double proportion(const Text &value, double base) {
  if (!value.endsWith('%'))
    return length(value);
  bool ok;
  double result = value.left(value.size() - 1).toDouble(&ok) / 100 * base;
  if (!ok || !std::isfinite(result) || std::abs(result) > 100'000)
    fail("Invalid text-layout percentage");
  return result;
}
void check_page(const TextPageStyle &p) {
  if (!std::isfinite(p.background.alpha) || p.background.alpha < 0 ||
      p.background.alpha > 1)
    fail("Invalid page background color");
  if (p.width < 1 || p.height < 1 || p.width > 10000 || p.height > 10000 ||
      p.left < 0 || p.right < 0 || p.top < 0 || p.bottom < 0 ||
      p.width - p.left - p.right < 1 || p.height - p.top - p.bottom < 1)
    fail("Invalid text page geometry");
}
TextColor text_color(const Text &value) {
  auto text = value.toStdString();
  if (text == "transparent")
    return {0, 0, 0, 0};
  if (text == "black")
    return {};
  if (text == "white")
    return {255, 255, 255};
  if (text.size() != 7 || text[0] != '#')
    fail("Invalid document text color");
  unsigned n;
  auto parsed = std::from_chars(text.data() + 1, text.data() + 7, n, 16);
  if (parsed.ec != std::errc() || parsed.ptr != text.data() + 7)
    fail("Invalid document text color");
  return {static_cast<unsigned char>(n >> 16),
          static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)};
}
struct Reader {
  TextFlow flow;
  std::map<Text, XmlElement> paragraph_styles, text_styles;
  std::map<Text, XmlElement> common_paragraph_styles, common_text_styles;
  std::map<Text, XmlElement> master_paragraph_styles, master_text_styles;
  XmlElement paragraph_default, text_default;
  std::map<Text, Text> fonts, master_fonts;
  std::map<Text, XmlElement> layouts;
  std::vector<XmlDocument> documents;
  size_t text_size = 0;
  bool styles_source = false;
  XmlElement styles_root;
  std::optional<TextParagraph> frame_base;
  Text frame_paragraph_style;
  std::map<Text, XmlElement> list_styles, common_list_styles,
      master_list_styles;
  struct ListLevel {
    XmlElement node;
    Text format = "1", prefix, suffix = ".", bullet, character_style;
    std::optional<Text> number_template;
    enum Kind { Number, Bullet, Image } kind = Number;
    int display = 1;
    int64_t start = 1;
    bool synchronized = false;
    TextListLabel layout;
    double margin = 0, relative_size = 1;
  };
  struct ListStyle {
    Text name;
    bool consecutive = false;
    std::map<int, ListLevel> levels;
    std::vector<Text> unsupported;
  };
  std::map<std::pair<bool, Text>, ListStyle> list_definitions;
  struct Counter {
    int64_t value = 0;
    Text format = "1";
    bool synchronized = false, active = false;
  };
  struct Domain {
    int64_t serial = 0;
    bool initialized = false;
    bool counted = false;
    std::vector<Counter> numbers;
  };
  struct ListState {
    Text style;
    int64_t last = -1, serial = 0;
  };
  std::map<Text, ListState> list_ids;
  std::map<int, ListState> previous_lists;
  std::map<Text, std::shared_ptr<Domain>> numbered_lists;
  Text anonymous_numbered_style;
  std::shared_ptr<Domain> anonymous_numbered;
  struct LabelImage {
    XmlElement node;
    std::shared_ptr<const ImageAsset> asset;
    std::vector<Text> warnings;
  };
  std::vector<LabelImage> label_images;
  void load_images(const OdfPackage &package) {
    std::set<uint64_t> used;
    size_t total = 0;
    for (const auto &document : documents)
      for (auto node :
           document.elementsByTagNameNS(TEXT, "list-level-style-image")) {
        if (label_images.size() >= 4096)
          fail("Too many list image declarations");
        LabelImage image;
        image.node = node;
        auto href = node.attributeNS(XLINK, "href");
        if (!href.empty())
          image.asset = package.image_asset(href);
        if (!image.asset) {
          auto binary = child(node, OFFICE, "binary-data");
          if (!binary.isNull()) {
            auto data = inline_image_data(binary.text());
            if (data)
              image.asset =
                  make_image_asset("inline list image", std::move(*data));
          }
        }
        if (image.asset && !used.contains(image.asset->id)) {
          if (image.asset->data->size() > 128 * 1024 * 1024 - total) {
            image.asset.reset();
            image.warnings.push_back("List image data exceeds native limit");
          } else {
            used.insert(image.asset->id);
            total += image.asset->data->size();
          }
        }
        if (!image.asset)
          image.warnings.push_back(
              "Unavailable or unsupported list image source");
        label_images.push_back(std::move(image));
      }
  }
  size_t label_size = 0;
  struct ListItem {
    Text style;
    int level = 1;
    bool first = true, header = false;
    std::vector<Counter> numbers;
    std::shared_ptr<Domain> domain;
  };
  static int64_t integer(const XmlElement &e, const Text &ns, const Text &name,
                         int64_t fallback, int64_t min, int64_t max) {
    auto value =
        e.attributeNS(ns, name, Text::number(fallback)).trimmed().toUtf8();
    const char *first = value.data();
    if (!value.empty() && value.front() == '+') {
      ++first;
      if (first == value.data() + value.size() || *first == '-' ||
          *first == '+')
        fail("Invalid list integer sign");
    }
    int64_t result;
    auto parsed = std::from_chars(first, value.data() + value.size(), result);
    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() ||
        result < min || result > max)
      fail("Invalid list counter or level");
    return result;
  }
  static bool boolean(const XmlElement &e, const Text &ns, const Text &name,
                      bool fallback = false) {
    auto value = e.attributeNS(ns, name, fallback ? "true" : "false").trimmed();
    if (value == "true" || value == "1")
      return true;
    if (value == "false" || value == "0")
      return false;
    fail("Invalid list boolean");
  }
  void reset_lists() {
    list_ids.clear();
    previous_lists.clear();
    numbered_lists.clear();
    anonymous_numbered.reset();
    anonymous_numbered_style.clear();
    label_size = 0;
  }

  void scan(const XmlDocument &document, bool master = false) {
    if (master)
      styles_root = document.documentElement();
    documents.push_back(document);
    for (auto e : document.elementsByTagNameNS(STYLE, "font-face"))
      (master ? master_fonts : fonts)[e.attributeNS(STYLE, "name")] =
          e.attributeNS(SVG, "font-family");
    for (auto e : document.elementsByTagNameNS(STYLE, "default-style")) {
      if (e.attributeNS(STYLE, "family") == "paragraph")
        paragraph_default = e;
      if (e.attributeNS(STYLE, "family") == "text")
        text_default = e;
    }
    for (auto group : children(document.documentElement(), OFFICE)) {
      bool automatic = group.localName() == "automatic-styles";
      if (!automatic && group.localName() != "styles")
        continue;
      for (auto e : children(group, STYLE, "style")) {
        auto family = e.attributeNS(STYLE, "family"),
             name = e.attributeNS(STYLE, "name");
        if (family == "paragraph")
          (automatic ? (master ? master_paragraph_styles : paragraph_styles)
                     : common_paragraph_styles)[name] = e;
        if (family == "text")
          (automatic ? (master ? master_text_styles : text_styles)
                     : common_text_styles)[name] = e;
      }
      for (auto e : children(group, TEXT, "list-style"))
        (automatic ? (master ? master_list_styles : list_styles)
                   : common_list_styles)[e.attributeNS(STYLE, "name")] = e;
    }
    for (auto e : document.elementsByTagNameNS(STYLE, "page-layout"))
      layouts[e.attributeNS(STYLE, "name")] = e;
  }
  void font(TextFont &font, const XmlElement &style) {
    auto p = child(style, STYLE, "text-properties");
    if (p.hasAttributeNS(FO, "font-size"))
      font.size = proportion(p.attributeNS(FO, "font-size"), font.size);
    if (font.size <= 0 || font.size > 1000)
      fail("Invalid text font size");
    if (p.hasAttributeNS(FO, "font-weight")) {
      auto weight = p.attributeNS(FO, "font-weight");
      bool valid = true;
      font.weight = weight == "bold"     ? 700
                    : weight == "normal" ? 400
                                         : weight.toInt(&valid);
      if (!valid || font.weight < 1 || font.weight > 1000)
        fail("Invalid text font weight");
      font.bold = font.weight >= 600;
    }
    if (p.hasAttributeNS(FO, "font-style"))
      font.italic = p.attributeNS(FO, "font-style") != "normal";
    if (p.hasAttributeNS(STYLE, "font-name")) {
      auto name = p.attributeNS(STYLE, "font-name");
      bool declaration_in_styles =
          !styles_root.isNull() &&
          style.ownerDocument().documentElement() == styles_root;
      font.family = !declaration_in_styles && fonts.contains(name)
                        ? fonts.at(name)
                    : master_fonts.contains(name) ? master_fonts.at(name)
                                                  : name;
    }
    if (p.hasAttributeNS(FO, "font-family"))
      font.family = p.attributeNS(FO, "font-family");
    if (p.hasAttributeNS(FO, "language")) {
      font.language = p.attributeNS(FO, "language");
      auto country = p.attributeNS(FO, "country");
      if (!country.empty() && country != "none")
        font.language += "-" + country;
    }
  }
  void character(TextFont &f, std::optional<TextColor> &color,
                 const XmlElement &style) {
    font(f, style);
    auto p = child(style, STYLE, "text-properties");
    if (p.hasAttributeNS(FO, "color"))
      color = text_color(p.attributeNS(FO, "color"));
    if (p.attributeNS(STYLE, "use-window-font-color") == "true")
      color.reset();
  }
  void properties(TextParagraph &p, const XmlElement &style) {
    character(p.font, p.color, style);
    if (style.hasAttributeNS(STYLE, "master-page-name"))
      p.master = style.attributeNS(STYLE, "master-page-name");
    auto e = child(style, STYLE, "paragraph-properties");
    if (e.hasAttributeNS(FO, "margin"))
      p.left = p.right = p.before = p.after =
          length(e.attributeNS(FO, "margin"));
    for (auto [name, target] :
         std::initializer_list<std::pair<const char *, double *>>{
             {"margin-left", &p.left},
             {"margin-right", &p.right},
             {"margin-top", &p.before},
             {"margin-bottom", &p.after},
             {"text-indent", &p.indent}})
      if (e.hasAttributeNS(FO, name))
        *target = length(e.attributeNS(FO, name));
    if (e.hasAttributeNS(FO, "line-height")) {
      auto value = e.attributeNS(FO, "line-height");
      p.line_height = 0;
      if (value == "normal")
        p.line_factor = 1.2;
      else if (value.endsWith('%'))
        p.line_factor = proportion(value, 1);
      else
        p.line_height = length(value);
      if (p.line_factor <= 0 || p.line_height < 0)
        fail("Invalid paragraph line height");
    }
    if (e.hasAttributeNS(FO, "text-align")) {
      auto align = e.attributeNS(FO, "text-align");
      p.alignment = align == "center"    ? TextParagraph::Center
                    : align == "right"   ? TextParagraph::Right
                    : align == "end"     ? TextParagraph::End
                    : align == "start"   ? TextParagraph::Start
                    : align == "justify" ? TextParagraph::Justify
                                         : TextParagraph::Left;
    }
    for (auto [name, target] :
         std::initializer_list<std::pair<const char *, bool *>>{
             {"break-before", &p.break_before},
             {"break-after", &p.break_after}})
      if (e.hasAttributeNS(FO, name)) {
        auto value = e.attributeNS(FO, name);
        *target = value == "page";
        if (value != "auto" && value != "page")
          note(flow.unsupported, "Column breaks");
      }
    if (e.hasAttributeNS(FO, "keep-together"))
      p.keep_together = e.attributeNS(FO, "keep-together") == "always";
    if (e.hasAttributeNS(FO, "keep-with-next"))
      p.keep_next = e.attributeNS(FO, "keep-with-next") == "always";
    for (auto [name, target] :
         std::initializer_list<std::pair<const char *, int *>>{
             {"orphans", &p.orphans}, {"widows", &p.widows}})
      if (e.hasAttributeNS(FO, name)) {
        bool ok;
        *target = e.attributeNS(FO, name).toInt(&ok);
        if (!ok || *target < 0 || *target > 1000)
          fail("Invalid widow/orphan threshold");
      }
    if (e.hasAttributeNS(STYLE, "writing-mode")) {
      auto direction = e.attributeNS(STYLE, "writing-mode");
      if (direction == "rl-tb")
        p.direction = TextParagraph::RTL;
      else if (direction == "lr-tb")
        p.direction = TextParagraph::LTR;
      else if (direction == "page")
        p.direction = TextParagraph::Auto;
      else
        note(flow.unsupported, "Vertical paragraph layout");
    }
  }
  template <class Apply>
  std::vector<XmlElement> chain(bool text, const Text &name, Apply apply,
                                std::optional<bool> source = {}) {
    const bool from_styles = source.value_or(styles_source);
    const auto &styles =
        text ? (from_styles ? master_text_styles : text_styles)
             : (from_styles ? master_paragraph_styles : paragraph_styles);
    const auto &common = text ? common_text_styles : common_paragraph_styles;
    std::vector<XmlElement> stack;
    XmlElement current;
    if (auto found = styles.find(name); found != styles.end())
      current = found->second;
    else if (auto found = common.find(name); found != common.end())
      current = found->second;
    while (!current.isNull()) {
      if (std::find(stack.begin(), stack.end(), current) != stack.end() ||
          stack.size() >= 256)
        fail("Cyclic or excessive text style inheritance");
      stack.push_back(current);
      auto parent = current.attributeNS(STYLE, "parent-style-name");
      current = {};
      if (!parent.empty()) {
        if (auto found = common.find(parent); found != common.end())
          current = found->second;
        else if (auto found = styles.find(parent); found != styles.end())
          current = found->second;
      }
    }
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      apply(*it);
    return stack;
  }
  void inlines(TextParagraph &paragraph, const XmlElement &parent,
               TextFont current, std::optional<TextColor> color,
               int depth = 0) {
    if (depth > 256)
      fail("Excessive inline text depth");
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
      auto append = [&](const Text &text) {
        if (paragraph.text.size() + text.size() > 1'000'000)
          fail("Paragraph exceeds layout text limit");
        text_size += text.size();
        if (text_size > 4'000'000)
          fail("Document exceeds native layout text limit");
        size_t begin = paragraph.text.size();
        paragraph.text += text;
        if (!text.empty()) {
          if (!paragraph.spans.empty() &&
              paragraph.spans.back().font == current &&
              paragraph.spans.back().color == color)
            paragraph.spans.back().end = paragraph.text.size();
          else
            paragraph.spans.push_back(
                {begin, paragraph.text.size(), current, color});
        }
      };
      if (n.isText() || n.isCDATASection())
        append(n.nodeValue());
      else if (n.isElement()) {
        auto e = n.toElement();
        if (e.namespaceURI() == TEXT &&
            (e.localName() == "span" || e.localName() == "a")) {
          auto next = current;
          auto next_color = color;
          chain(true, e.attributeNS(TEXT, "style-name"),
                [&](const XmlElement &s) { character(next, next_color, s); });
          inlines(paragraph, e, next, next_color, depth + 1);
        } else if (e.namespaceURI() == TEXT && e.localName() == "s") {
          bool ok;
          int count = e.attributeNS(TEXT, "c", "1").toInt(&ok);
          if (!ok || count < 1 || count > 1'000'000)
            fail("Invalid repeated text space");
          append(Text(size_t(count), U' '));
        } else if (e.namespaceURI() == TEXT && e.localName() == "tab")
          append("\t");
        else if (e.namespaceURI() == TEXT && e.localName() == "line-break")
          append("\n");
        else if (e.namespaceURI() == TEXT &&
                 e.localName() ==
                     "soft-page-break") { /* Reflow from styles and metrics. */
        } else {
          note(flow.unsupported, "Inline fields, notes and anchored objects");
          inlines(paragraph, e, current, color, depth + 1);
        }
      }
    }
  }
  const ListStyle &list_definition(const Text &name) {
    auto key = std::pair{styles_source, name};
    if (auto it = list_definitions.find(key); it != list_definitions.end()) {
      for (const auto &warning : it->second.unsupported)
        note(flow.unsupported, warning);
      return it->second;
    }
    ListStyle result;
    result.name = name;
    auto warning = [&](const Text &message) {
      note(result.unsupported, message);
      note(flow.unsupported, message);
    };
    XmlElement node;
    const auto &automatic = styles_source ? master_list_styles : list_styles;
    if (auto it = automatic.find(name); it != automatic.end())
      node = it->second;
    else if (auto it = common_list_styles.find(name);
             it != common_list_styles.end())
      node = it->second;
    if (node.isNull()) {
      if (!name.empty())
        warning("Missing list style; native default used");
      return list_definitions.emplace(key, std::move(result)).first->second;
    }
    result.consecutive = boolean(node, TEXT, "consecutive-numbering");
    for (auto e : children(node, TEXT)) {
      auto type = e.localName();
      if (type != "list-level-style-number" &&
          type != "list-level-style-bullet" && type != "list-level-style-image")
        continue;
      int level = integer(e, TEXT, "level", 1, 1, 64);
      if (result.levels.contains(level))
        fail("Duplicate list level");
      ListLevel l;
      l.node = e;
      l.layout.min_width = 0;
      l.suffix = e.attributeNS(STYLE, "num-suffix");
      l.prefix = e.attributeNS(STYLE, "num-prefix");
      l.format = e.attributeNS(STYLE, "num-format");
      l.character_style = e.attributeNS(TEXT, "style-name");
      l.start = integer(e, TEXT, "start-value", 1, 0, INT64_MAX - 1);
      l.display = integer(e, TEXT, "display-levels", 1, 1, 64);
      l.synchronized = boolean(e, STYLE, "num-letter-sync");
      if (type == "list-level-style-bullet") {
        l.kind = ListLevel::Bullet;
        l.bullet = e.attributeNS(TEXT, "bullet-char");
        if (l.bullet.size() > 64)
          fail("List bullet exceeds label limit");
        if (e.hasAttributeNS(TEXT, "bullet-relative-size"))
          l.relative_size =
              proportion(e.attributeNS(TEXT, "bullet-relative-size"), 1);
        if (l.relative_size < 0 || l.relative_size > 100)
          fail("Invalid relative bullet size");
      } else if (type == "list-level-style-image")
        l.kind = ListLevel::Image;
      else if (!supported_list_number_format(l.format)) {
        warning("Unsupported list number format; decimal used");
        l.format = "1";
      }
      if (e.hasAttributeNS(STYLE, "num-list-format-name"))
        warning("Named list number formats");
      const Text LOEXT = "urn:org:documentfoundation:names:experimental:office:"
                         "xmlns:loext:1.0";
      if (e.hasAttributeNS(LOEXT, "num-list-format")) {
        l.number_template = e.attributeNS(LOEXT, "num-list-format");
        if (l.number_template->size() > 4096)
          fail("List number template exceeds limit");
      }
      auto props = child(e, STYLE, "list-level-properties");
      if (l.kind == ListLevel::Image) {
        ListImage image;
        image.source = e.attributeNS(XLINK, "href", "inline list image");
        for (const auto &loaded : label_images)
          if (loaded.node == e) {
            image.asset = loaded.asset;
            for (const auto &message : loaded.warnings)
              warning(message);
            break;
          }
        if (props.hasAttributeNS(FO, "width"))
          image.width = length(props.attributeNS(FO, "width"));
        if (props.hasAttributeNS(FO, "height"))
          image.height = length(props.attributeNS(FO, "height"));
        if (image.width <= 0 || image.height <= 0 || image.width > 10000 ||
            image.height > 10000)
          fail("Invalid list image size");
        auto pos = props.attributeNS(STYLE, "vertical-pos", "middle");
        if (pos == "top")
          image.position = ListImage::Top;
        else if (pos == "bottom")
          image.position = ListImage::Bottom;
        else if (pos == "from-top")
          image.position = ListImage::FromTop;
        else if (pos != "middle")
          warning("Unsupported list image vertical position; middle used");
        auto rel = props.attributeNS(STYLE, "vertical-rel", "line");
        if (rel == "baseline")
          image.reference = ListImage::Baseline;
        else if (rel == "text")
          image.reference = ListImage::TextBox;
        else if (rel == "char")
          image.reference = ListImage::Character;
        else if (rel != "line")
          warning("Unsupported list image reference; line used");
        if (props.hasAttributeNS(SVG, "y"))
          image.offset = length(props.attributeNS(SVG, "y"));
        l.layout.image = std::move(image);
      }
      auto align = props.attributeNS(FO, "text-align", "start");
      l.layout.alignment = align == "left"     ? TextListLabel::Left
                           : align == "right"  ? TextListLabel::Right
                           : align == "center" ? TextListLabel::Center
                           : align == "end"    ? TextListLabel::End
                                               : TextListLabel::Start;
      if (align != "left" && align != "right" && align != "center" &&
          align != "end" && align != "start")
        fail("Invalid list label alignment");
      auto mode = props.attributeNS(TEXT, "list-level-position-and-space-mode",
                                    "label-width-and-position");
      if (mode == "label-alignment") {
        l.layout.alignment_mode = true;
        auto a = child(props, STYLE, "list-level-label-alignment");
        if (a.hasAttributeNS(FO, "margin-left"))
          l.margin = length(a.attributeNS(FO, "margin-left"));
        if (a.hasAttributeNS(FO, "text-indent"))
          l.layout.indent = length(a.attributeNS(FO, "text-indent"));
        auto follow = a.attributeNS(TEXT, "label-followed-by", "space");
        if (follow == "space")
          l.layout.follow = TextListLabel::Space;
        else if (follow == "nothing")
          l.layout.follow = TextListLabel::Nothing;
        else if (follow == "listtab")
          l.layout.follow = TextListLabel::Tab;
        else
          fail("Invalid list label separator");
        if (a.hasAttributeNS(TEXT, "list-tab-stop-position"))
          l.layout.tab_stop =
              length(a.attributeNS(TEXT, "list-tab-stop-position"));
      } else if (mode == "label-width-and-position") {
        for (auto [attr, target] :
             std::initializer_list<std::pair<const char *, double *>>{
                 {"space-before", &l.layout.space_before},
                 {"min-label-width", &l.layout.min_width},
                 {"min-label-distance", &l.layout.min_distance}})
          if (props.hasAttributeNS(TEXT, attr))
            *target = length(props.attributeNS(TEXT, attr));
        if (l.layout.min_width < 0 || l.layout.min_distance < 0)
          fail("Negative list label width or gap");
      } else
        fail("Invalid list positioning mode");
      result.levels[level] = std::move(l);
    }
    return list_definitions.emplace(key, std::move(result)).first->second;
  }
  ListLevel list_level(const Text &style, int level) {
    const auto &definition = list_definition(style);
    auto it = definition.levels.upper_bound(level);
    ListLevel result;
    if (it != definition.levels.begin()) {
      --it;
      result = it->second;
      result.margin += 18 * (level - it->first);
      result.layout.space_before += 18 * (level - it->first);
    } else {
      result.layout.space_before = 18 * (level - 1);
      result.margin = 18 * level;
    }
    result.layout.style = style;
    result.layout.level = level;
    return result;
  }
  Text paragraph_list_style(const XmlElement &e) {
    Text result = paragraph_default.attributeNS(STYLE, "list-style-name");
    chain(false, frame_paragraph_style, [&](const XmlElement &s) {
      if (s.hasAttributeNS(STYLE, "list-style-name"))
        result = s.attributeNS(STYLE, "list-style-name");
    });
    chain(false, e.attributeNS(TEXT, "style-name"), [&](const XmlElement &s) {
      if (s.hasAttributeNS(STYLE, "list-style-name"))
        result = s.attributeNS(STYLE, "list-style-name");
    });
    return result;
  }
  Text default_list_style(const XmlElement &node, int depth = 0) {
    if (depth > 256)
      fail("Excessive list style lookup depth");
    for (auto e : children(node, TEXT)) {
      if (e.localName() == "p" || e.localName() == "h") {
        auto style = paragraph_list_style(e);
        if (!style.empty())
          return style;
      } else if (e.localName() != "list") {
        auto style = default_list_style(e, depth + 1);
        if (!style.empty())
          return style;
      }
    }
    return {};
  }
  void decorate_list(TextParagraph &p, ListItem &item,
                     const std::vector<XmlElement> &styles) {
    auto level = list_level(item.style, item.level);
    auto label = level.layout;
    label.visible = item.first && !item.header;
    label.font = p.spans.empty() ? p.font : p.spans.front().font;
    label.color = p.spans.empty() ? p.color : p.spans.front().color;
    bool source = !level.node.isNull() && !styles_root.isNull() &&
                  level.node.ownerDocument().documentElement() == styles_root;
    chain(
        true, level.character_style,
        [&](const XmlElement &s) { character(label.font, label.color, s); },
        source);
    character(label.font, label.color, level.node);
    if (level.kind == ListLevel::Bullet) {
      if (level.relative_size == 0)
        label.visible = false;
      else
        label.font.size *= level.relative_size;
      label.text = level.bullet;
    } else if (level.kind == ListLevel::Image) {
      label.text.clear();
    } else if (!level.format.empty()) {
      TextList pieces;
      size_t begin = item.numbers.size() > size_t(level.display)
                         ? item.numbers.size() - level.display
                         : 0;
      for (size_t i = begin; i < item.numbers.size(); ++i) {
        const auto &number = item.numbers[i];
        if (number.active && !number.format.empty())
          pieces << format_list_number(number.value, number.format,
                                       number.synchronized);
      }
      label.text = level.prefix + pieces.join('.') + level.suffix;
      if (level.number_template) {
        label.text.clear();
        const auto &pattern = *level.number_template;
        for (size_t i = 0; i < pattern.size();) {
          if (pattern[i] != '%') {
            label.text += pattern[i++];
            continue;
          }
          if (i + 1 < pattern.size() && pattern[i + 1] == '%') {
            label.text += '%';
            i += 2;
            continue;
          }
          size_t next = i + 1;
          int index = 0;
          while (next < pattern.size() && pattern[next] >= '0' &&
                 pattern[next] <= '9') {
            index = index * 10 + int(pattern[next++] - '0');
            if (index > 64)
              fail("List template level exceeds limit");
          }
          if (index < 1 || next >= pattern.size() || pattern[next] != '%')
            fail("Invalid list number template");
          if (size_t(index) <= item.numbers.size()) {
            const auto &n = item.numbers[index - 1];
            if (n.active && !n.format.empty())
              label.text +=
                  format_list_number(n.value, n.format, n.synchronized);
          }
          i = next + 1;
          if (label.text.size() > 4096)
            fail("List label exceeds native text limit");
        }
      }
    }
    if (label.text.size() > 4096)
      fail("List label exceeds native text limit");
    if (label.visible)
      label_size += label.text.size();
    if (label_size > 4000000)
      fail("List labels exceed native document limit");
    bool left_set = false, indent_set = false;
    for (auto s : styles) {
      auto props = child(s, STYLE, "paragraph-properties");
      left_set = left_set || props.hasAttributeNS(FO, "margin-left") ||
                 props.hasAttributeNS(FO, "margin");
      indent_set = indent_set || props.hasAttributeNS(FO, "text-indent");
      if (props.hasAttributeNS(STYLE, "tab-stop-distance"))
        label.default_tab =
            length(props.attributeNS(STYLE, "tab-stop-distance"));
      if (props.hasAttributeNS(TEXT, "relative-tab-stop-position"))
        label.relative_tabs =
            boolean(props, TEXT, "relative-tab-stop-position", true);
      auto tabs = child(props, STYLE, "tab-stops");
      if (!tabs.isNull()) {
        label.paragraph_tabs.clear();
        for (auto tab : children(tabs, STYLE, "tab-stop"))
          label.paragraph_tabs.push_back(
              length(tab.attributeNS(STYLE, "position")));
      }
    }
    std::sort(label.paragraph_tabs.begin(), label.paragraph_tabs.end());
    if (label.paragraph_tabs.size() > 4096 || label.default_tab <= 0)
      fail("Invalid list label tab stops");
    if (label.alignment_mode) {
      if (!left_set)
        p.left = level.margin;
      if (indent_set)
        label.indent = p.indent;
    }
    if (!label.visible)
      label.text.clear();
    p.indent = label.alignment_mode ? 0 : p.indent;
    p.list = std::move(label);
    item.first = false;
  }
  void paragraph(const XmlElement &e, ListItem *item) {
    if (flow.paragraphs.size() >= 100000)
      fail("Too many text paragraphs");
    TextParagraph p;
    p.color = TextColor{};
    character(p.font, p.color, text_default);
    std::vector<XmlElement> applied;
    auto apply = [&](const XmlElement &s) {
      properties(p, s);
      applied.push_back(s);
    };
    apply(paragraph_default);
    if (frame_base) {
      p.font = frame_base->font;
      p.color = frame_base->color;
    }
    auto explicit_styles = chain(false, e.attributeNS(TEXT, "style-name"),
                                 [](const XmlElement &) {});
    auto frame_styles =
        chain(false, frame_paragraph_style, [](const XmlElement &) {});
    if (frame_styles.empty() ||
        std::find(explicit_styles.begin(), explicit_styles.end(),
                  frame_styles.front()) == explicit_styles.end())
      for (auto it = frame_styles.rbegin(); it != frame_styles.rend(); ++it)
        apply(*it);
    for (auto it = explicit_styles.rbegin(); it != explicit_styles.rend(); ++it)
      apply(*it);
    inlines(p, e, p.font, p.color);
    if (item)
      decorate_list(p, *item, applied);
    flow.paragraphs.push_back(std::move(p));
  }
  void list(const XmlElement &node, int depth, ListItem *parent,
            bool numbered = false) {
    if (depth > 256)
      fail("Excessive list depth");
    int level = numbered ? integer(node, TEXT, "level", 1, 1, 64)
                         : (parent ? parent->level + 1 : 1);
    if (level > 64)
      fail("Too many list levels");
    auto style = node.attributeNS(TEXT, "style-name");
    if (style.empty() && parent)
      style = parent->style;
    if (style.empty())
      style = default_list_style(node);
    auto initial = list_level(style, level);
    auto domain = parent ? parent->domain : std::make_shared<Domain>();
    int64_t current = initial.start - 1;
    bool continued = false;
    Text id = node.attributeNS("http://www.w3.org/XML/1998/namespace", "id");
    if (!id.empty() && list_ids.contains(id))
      fail("Duplicate list identifier");
    if (numbered) {
      auto key = node.attributeNS(TEXT, "list-id");
      if (!key.empty()) {
        auto &stored = numbered_lists[key];
        if (!stored)
          stored = std::make_shared<Domain>();
        domain = stored;
      } else {
        if (!anonymous_numbered || anonymous_numbered_style != style)
          anonymous_numbered = std::make_shared<Domain>();
        anonymous_numbered_style = style;
        domain = anonymous_numbered;
      }
      if (domain->numbers.size() >= size_t(level) &&
          domain->numbers[level - 1].active) {
        current = domain->numbers[level - 1].value;
        continued = true;
      }
    } else {
      auto reference = node.attributeNS(TEXT, "continue-list");
      const ListState *previous = nullptr;
      if (node.hasAttributeNS(TEXT, "continue-list")) {
        if (auto it = list_ids.find(reference); it != list_ids.end())
          previous = &it->second;
        else
          note(flow.unsupported,
               "Unresolved list continuation; numbering restarted");
      } else if (boolean(node, TEXT, "continue-numbering")) {
        if (auto it = previous_lists.find(level);
            it != previous_lists.end() && it->second.style == style)
          previous = &it->second;
      }
      if (previous) {
        current = previous->last;
        continued = true;
        if (!parent)
          domain->serial = previous->serial, domain->initialized = true;
      }
    }
    if (!domain->initialized) {
      domain->serial = current;
      domain->initialized = true;
    }
    auto state = [&] { return ListState{style, current, domain->serial}; };
    if (!id.empty())
      list_ids[id] = state();
    auto process = [&](const XmlElement &item_node, bool header) {
      auto item_style = item_node.attributeNS(TEXT, "style-override", style);
      auto definition = list_level(item_style, level);
      if (!header) {
        if (!continued) {
          current = definition.start - 1;
          if (!domain->counted)
            domain->serial = current;
        }
        if (current == INT64_MAX || domain->serial == INT64_MAX)
          fail("List counter overflow");
        ++current;
        ++domain->serial;
        domain->counted = true;
        if (list_definition(item_style).consecutive)
          current = domain->serial;
        auto start_node = numbered ? node : item_node;
        if (start_node.hasAttributeNS(TEXT, "start-value"))
          current = domain->serial =
              integer(start_node, TEXT, "start-value", 1, 0, INT64_MAX - 1);
        continued = true;
      }
      ListItem item;
      item.style = item_style;
      item.level = level;
      item.header = header;
      item.domain = domain;
      item.numbers = numbered
                         ? domain->numbers
                         : (parent ? parent->numbers : std::vector<Counter>{});
      item.numbers.resize(level);
      item.numbers[level - 1] = {
          current,
          definition.kind == ListLevel::Number ? definition.format : Text(),
          definition.synchronized, !header};
      if (numbered)
        domain->numbers = item.numbers;
      if (!id.empty())
        list_ids[id] = state();
      blocks(item_node, depth + 1, &item);
      if (item.first)
        note(flow.unsupported, "List item without an editable paragraph");
    };
    if (numbered)
      process(node, false);
    else
      for (auto item : children(node, TEXT)) {
        if (item.localName() == "list-item")
          process(item, false);
        else if (item.localName() == "list-header")
          process(item, true);
      }
    if (!id.empty())
      list_ids[id] = state();
    previous_lists[level] = state();
  }
  void blocks(const XmlElement &parent, int depth = 0,
              ListItem *item = nullptr) {
    if (depth > 256)
      fail("Excessive text flow depth");
    for (auto node = parent.firstChild(); !node.isNull();
         node = node.nextSibling()) {
      if (!node.isElement())
        continue;
      auto e = node.toElement();
      if (e.namespaceURI() == TEXT &&
          (e.localName() == "p" || e.localName() == "h")) {
        paragraph(e, item);
      } else if (e.namespaceURI() == TEXT &&
                 (e.localName() == "list" ||
                  e.localName() == "numbered-paragraph"))
        list(e, depth + 1, item, e.localName() == "numbered-paragraph");
      else if (e.namespaceURI() == TEXT && (e.localName() == "sequence-decls" ||
                                            e.localName() == "number" ||
                                            e.localName() == "soft-page-break"))
        continue;
      else if ((e.namespaceURI() == DRAW &&
                (e.localName() == "enhanced-geometry" ||
                 e.localName() == "glue-point")) ||
               (e.namespaceURI() == SVG &&
                (e.localName() == "title" || e.localName() == "desc")) ||
               (e.namespaceURI() == OFFICE &&
                e.localName() == "event-listeners"))
        continue;
      else {
        note(flow.unsupported, "Tables, sections and floating frames");
        bool reset = e.namespaceURI() == TABLE || e.namespaceURI() == DRAW;
        blocks(e, depth + 1, reset ? nullptr : item);
      }
    }
  }
  void pages() {
    for (const auto &doc : documents)
      for (auto master : doc.elementsByTagNameNS(STYLE, "master-page")) {
        TextPageStyle p;
        p.name = master.attributeNS(STYLE, "name");
        p.next = master.attributeNS(STYLE, "next-style-name", p.name);
        auto name = master.attributeNS(STYLE, "page-layout-name");
        if (layouts.contains(name)) {
          auto props = child(layouts.at(name), STYLE, "page-layout-properties");
          if (props.hasAttributeNS(FO, "background-color"))
            p.background =
                text_color(props.attributeNS(FO, "background-color"));
          if (props.hasAttributeNS(FO, "margin"))
            p.left = p.right = p.top = p.bottom =
                length(props.attributeNS(FO, "margin"));
          for (auto [name, target] :
               std::initializer_list<std::pair<const char *, double *>>{
                   {"page-width", &p.width},
                   {"page-height", &p.height},
                   {"margin-left", &p.left},
                   {"margin-right", &p.right},
                   {"margin-top", &p.top},
                   {"margin-bottom", &p.bottom}})
            if (props.hasAttributeNS(FO, name))
              *target = length(props.attributeNS(FO, name));
          if (!child(props, STYLE, "columns").isNull())
            note(flow.unsupported, "Multi-column text frames");
        }
        if (!children(master, STYLE).empty())
          note(flow.unsupported, "Headers and footers");
        check_page(p);
        if (flow.first_master.empty())
          flow.first_master = p.name;
        flow.masters[p.name] = p;
      }
    if (flow.masters.empty())
      flow.masters[{}] = TextPageStyle{};
  }
};

struct Cluster {
  size_t begin, end;
  TextFont font;
  TextMetrics metrics;
  bool allowed = false, hard = false;
  std::optional<TextColor> color;
};
void check_metrics(const TextMetrics &metrics) {
  if (!std::isfinite(metrics.width) || !std::isfinite(metrics.ascent) ||
      !std::isfinite(metrics.descent) || metrics.width < 0 ||
      metrics.ascent < 0 || metrics.descent < 0 ||
      metrics.ascent + metrics.descent <= 0)
    fail("Invalid native text metrics");
}
struct Breaks {
  using Iterator = std::unique_ptr<UBreakIterator, decltype(&ubrk_close)>;
  Iterator line{nullptr, ubrk_close}, character{nullptr, ubrk_close};
  Breaks() {
    UErrorCode status = U_ZERO_ERROR;
    line.reset(ubrk_open(UBRK_LINE, "und", nullptr, 0, &status));
    character.reset(ubrk_open(UBRK_CHARACTER, "und", nullptr, 0, &status));
    if (U_FAILURE(status))
      fail("Cannot initialize Unicode layout boundaries");
  }
  std::vector<size_t> boundaries(UBreakIterator *iterator, const Bytes &utf8,
                                 const std::vector<size_t> &indices) {
    UErrorCode status = U_ZERO_ERROR;
    UText text = UTEXT_INITIALIZER;
    utext_openUTF8(&text, utf8.data(), utf8.size(), &status);
    ubrk_setUText(iterator, &text, &status);
    utext_close(&text);
    if (U_FAILURE(status))
      fail("Cannot analyze Unicode layout boundaries");
    std::vector<size_t> result;
    for (int at = ubrk_first(iterator); at != UBRK_DONE;
         at = ubrk_next(iterator))
      result.push_back(indices.at(at));
    return result;
  }
  std::vector<Cluster> clusters(const TextParagraph &p,
                                const MeasureText &measure,
                                const ShapedText *shaped = nullptr) {
    auto utf8 = p.text.toUtf8();
    std::vector<size_t> indices(utf8.size() + 1);
    size_t scalar = 0;
    for (size_t i = 0; i < utf8.size(); ++i) {
      indices[i] = scalar;
      if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
        ++scalar;
    }
    // Only character-start byte indices are queried by ICU.
    indices[utf8.size()] = p.text.size();
    auto legal = boundaries(line.get(), utf8, indices);
    auto graphemes = boundaries(character.get(), utf8, indices);
    std::vector<Cluster> result;
    size_t span = 0;
    if (shaped) {
      size_t cursor = 0;
      Cluster pending{};
      for (const auto &cluster : shaped->clusters) {
        if (cluster.begin != cursor || cluster.end <= cursor ||
            cluster.end > p.text.size())
          fail("Invalid native shaped source coverage");
        if (pending.end == pending.begin) {
          pending.begin = cluster.begin;
          while (span + 1 < p.spans.size() &&
                 p.spans[span].end <= cluster.begin)
            ++span;
          pending.font = p.spans.empty() ? p.font : p.spans[span].font;
          pending.color = p.spans.empty() ? p.color : p.spans[span].color;
        }
        pending.end = cluster.end;
        pending.metrics.width += cluster.metrics.width;
        pending.metrics.ascent =
            std::max(pending.metrics.ascent, cluster.metrics.ascent);
        pending.metrics.descent =
            std::max(pending.metrics.descent, cluster.metrics.descent);
        auto value = p.text.mid(cluster.begin, cluster.end - cluster.begin);
        pending.hard = pending.hard || value.contains('\n') ||
                       value.contains('\r') || value.contains(Char(0x2028)) ||
                       value.contains(Char(0x2029));
        cursor = cluster.end;
        if (std::binary_search(graphemes.begin(), graphemes.end(), cursor)) {
          check_metrics(pending.metrics);
          pending.allowed =
              std::binary_search(legal.begin(), legal.end(), cursor);
          result.push_back(pending);
          pending = {};
        }
      }
      if (cursor != p.text.size() || pending.begin != pending.end)
        fail("Incomplete native shaped source coverage");
      return result;
    }
    for (size_t i = 1; i < graphemes.size(); ++i) {
      size_t begin = graphemes[i - 1], end = graphemes[i];
      while (span + 1 < p.spans.size() && p.spans[span].end <= begin)
        ++span;
      TextFont font = p.spans.empty() ? p.font : p.spans[span].font;
      auto value = p.text.mid(begin, end - begin);
      bool hard = value.contains('\n') || value.contains('\r') ||
                  value.contains(Char(0x2028));
      auto metrics = measure(hard            ? Text(" ")
                             : value == "\t" ? Text("    ")
                                             : value,
                             font);
      check_metrics(metrics);
      if (hard)
        metrics.width = 0;
      result.push_back({begin, end, font, metrics,
                        std::binary_search(legal.begin(), legal.end(), end),
                        hard, p.spans.empty() ? p.color : p.spans[span].color});
    }
    return result;
  }
};
struct WrappedLine {
  TextLine line;
  bool hard = false;
};
double image_position(const ListImage &image, double top, double bottom) {
  if (image.position == ListImage::Top)
    return top;
  if (image.position == ListImage::Bottom)
    return bottom - image.height;
  if (image.position == ListImage::FromTop)
    return top + image.offset;
  return (top + bottom - image.height) / 2;
}
std::vector<WrappedLine> wrap(const TextParagraph &p,
                              const std::vector<Cluster> &clusters,
                              const TextPageStyle &page, size_t start,
                              const MeasureText &measure,
                              const ShapingParagraph *shaping = nullptr,
                              NativeTextShaper *shaper = nullptr) {
  const bool rtl =
      shaping ? shaping->whole().rtl : p.direction == TextParagraph::RTL;
  double body_start = p.left, first_start = p.left + p.indent, label_start = 0;
  TextMetrics label_metrics;
  std::shared_ptr<ShapedText> label_shape;
  if (p.list) {
    const auto &label = *p.list;
    if (label.visible && (label.image || !label.text.empty())) {
      if (label.image)
        label_metrics.width = label.image->width;
      else if (shaper) {
        TextParagraph marker;
        marker.text = label.text;
        marker.font = label.font;
        marker.color = label.color;
        marker.direction = rtl ? TextParagraph::RTL : TextParagraph::LTR;
        label_shape = shaper->prepare(marker).line(0, marker.text.size());
        label_metrics = label_shape->metrics;
      } else
        label_metrics = measure(label.text, label.font);
      if (!label.image)
        check_metrics(label_metrics);
    }
    auto alignment = label.alignment;
    if (alignment == TextListLabel::Left)
      alignment = rtl ? TextListLabel::End : TextListLabel::Start;
    if (alignment == TextListLabel::Right)
      alignment = rtl ? TextListLabel::Start : TextListLabel::End;
    if (label.alignment_mode) {
      label_start = body_start + label.indent;
      if (alignment == TextListLabel::End)
        label_start -= label_metrics.width;
      else if (alignment == TextListLabel::Center)
        label_start -= label_metrics.width / 2;
      first_start = body_start;
      if (label.visible && (label.image || !label.text.empty())) {
        first_start = label_start + label_metrics.width;
        if (label.follow == TextListLabel::Space) {
          auto space = measure(" ", label.font);
          check_metrics(space);
          first_start += space.width;
        } else if (label.follow == TextListLabel::Tab) {
          double next = std::numeric_limits<double>::infinity();
          auto tab = [&](double value) {
            if (value > first_start + 1e-7)
              next = std::min(next, value);
          };
          tab(label.tab_stop.value_or(body_start));
          for (double value : label.paragraph_tabs)
            tab(value + (label.relative_tabs ? body_start : 0));
          double origin = label.relative_tabs ? body_start : 0;
          if (!std::isfinite(next))
            tab(origin +
                (std::floor((first_start - origin) / label.default_tab) + 1) *
                    label.default_tab);
          first_start = next;
        }
      }
    } else {
      double box_start = body_start + label.space_before;
      body_start = box_start + label.min_width;
      double offset =
          alignment == TextListLabel::End
              ? std::max(0., label.min_width - label_metrics.width)
          : alignment == TextListLabel::Center
              ? std::max(0., (label.min_width - label_metrics.width) / 2)
              : 0;
      label_start = box_start + offset;
      if (body_start - label_start - label_metrics.width < label.min_distance)
        label_start = std::max(box_start, body_start - label_metrics.width -
                                              label.min_distance);
      first_start = body_start;
      if (label.visible && (label.image || !label.text.empty()))
        first_start =
            std::max(body_start + p.indent,
                     label_start + label_metrics.width + label.min_distance);
    }
  }
  const double available =
      page.width - page.left - page.right - body_start - p.right;
  if (!std::isfinite(available) || available < 1)
    fail("Paragraph margins leave no text area");
  std::vector<WrappedLine> lines;
  size_t i = std::lower_bound(
                 clusters.begin(), clusters.end(), start,
                 [](const Cluster &c, size_t pos) { return c.begin < pos; }) -
             clusters.begin();
  bool trailing = !clusters.empty() && clusters.back().hard;
  do {
    size_t begin = i, end = i, allowed = i;
    double width = 0, max_ascent = 0, max_descent = 0;
    double indent = (i == 0 ? first_start - body_start : 0);
    double limit = available - indent;
    if (limit < 1)
      fail("Paragraph indent leaves no text area");
    bool hard = false;
    while (end < clusters.size()) {
      auto &c = clusters[end];
      if (!c.hard && end > begin && width + c.metrics.width > limit + 1e-7)
        break;
      width += c.metrics.width;
      ++end;
      if (c.allowed)
        allowed = end;
      if (c.hard) {
        hard = true;
        break;
      }
    }
    if (!hard && end < clusters.size() && allowed > begin)
      end = allowed;
    std::shared_ptr<ShapedText> shape;
    auto shaped_line = [&](size_t stop) {
      size_t a =
          begin < clusters.size() ? clusters[begin].begin : p.text.size();
      size_t b = stop > begin ? clusters[stop - 1].end : a;
      while (b > a && (p.text[b - 1] == '\n' || p.text[b - 1] == '\r' ||
                       p.text[b - 1] == 0x2028 || p.text[b - 1] == 0x2029))
        --b;
      return shaping->line(a, b);
    };
    if (shaping) {
      shape = shaped_line(end);
      if (shape->metrics.width > limit + 1e-7 && end > begin + 1) {
        // Context at a forced line break can change joining/ligature advances.
        // Find a fitting source boundary and then prefer a legal word boundary.
        size_t lo = begin + 1, hi = end;
        while (lo + 1 < hi) {
          size_t mid = lo + (hi - lo) / 2;
          if (shaped_line(mid)->metrics.width <= limit + 1e-7)
            lo = mid;
          else
            hi = mid;
        }
        end = lo;
        for (size_t at = end; at > begin; --at)
          if (clusters[at - 1].allowed) {
            end = at;
            break;
          }
        shape = shaped_line(end);
        hard = end > begin && clusters[end - 1].hard;
      }
    }
    TextLine line;
    line.begin =
        begin < clusters.size() ? clusters[begin].begin : p.text.size();
    line.end = end ? clusters[end - 1].end : line.begin;
    width = 0;
    for (size_t at = begin; at < end; ++at) {
      const auto &c = clusters[at];
      max_ascent = std::max(max_ascent, c.metrics.ascent);
      max_descent = std::max(max_descent, c.metrics.descent);
      if (c.hard)
        continue;
      auto text = p.text.mid(c.begin, c.end - c.begin);
      if (text == "\t")
        text = "    ";
      // Separate whitespace fragments so justification can expand their
      // advance.
      if (!line.fragments.empty() && line.fragments.back().font == c.font &&
          line.fragments.back().color == c.color && text != " " &&
          !line.fragments.back().text.endsWith(' ')) {
        line.fragments.back().text += text;
        line.fragments.back().width += c.metrics.width;
      } else
        line.fragments.push_back(
            {text, c.font, width, c.metrics.width, c.color});
      width += c.metrics.width;
    }
    if (begin == end) {
      auto m = measure(" ", p.font);
      check_metrics(m);
      max_ascent = m.ascent;
      max_descent = m.descent;
    }
    if (shape) {
      max_ascent = shape->metrics.ascent;
      max_descent = shape->metrics.descent;
      width = shape->metrics.width;
    }
    if (begin == 0 && start == 0 && lines.empty() && p.list &&
        p.list->visible && (p.list->image || !p.list->text.empty())) {
      const auto &marker = *p.list;
      line.label = TextLine::Label{marker.text,
                                   marker.font,
                                   marker.color,
                                   rtl ? page.width - page.right - label_start -
                                             label_metrics.width
                                       : page.left + label_start,
                                   label_metrics.width,
                                   label_shape};
      if (marker.image) {
        line.label->image = marker.image;
        line.label->height = marker.image->height;
        double top = -max_ascent, bottom = max_descent;
        if (marker.image->reference == ListImage::Baseline)
          top = bottom = 0;
        else if (marker.image->reference == ListImage::Character) {
          auto font_metrics = measure("M", marker.font);
          check_metrics(font_metrics);
          top = -font_metrics.ascent;
          bottom = font_metrics.descent;
        }
        line.label->y = image_position(*marker.image, top, bottom);
        max_ascent = std::max(max_ascent, -line.label->y);
        max_descent = std::max(max_descent, line.label->y + line.label->height);
      } else {
        max_ascent = std::max(max_ascent, label_metrics.ascent);
        max_descent = std::max(max_descent, label_metrics.descent);
      }
    }
    double natural = max_ascent + max_descent;
    line.height = p.line_height > 0 ? p.line_height : natural * p.line_factor;
    if (!std::isfinite(line.height) || line.height <= 0)
      fail("Invalid native line height");
    line.baseline = (line.height - natural) / 2 + max_ascent;
    if (line.label && line.label->image &&
        line.label->image->reference == ListImage::Line)
      line.label->y = image_position(*line.label->image, -line.baseline,
                                     line.height - line.baseline);
    // Trailing wrap spaces stay in the source range but do not affect
    // alignment.
    double visible = width;
    for (size_t at = end; at > begin && !clusters[at - 1].hard; --at) {
      const auto &c = clusters[at - 1];
      auto t = p.text.mid(c.begin, c.end - c.begin);
      if (t != " " && t != "\t")
        break;
      visible -= c.metrics.width;
    }
    line.width = std::max(0., visible);
    line.x = page.left + (rtl ? p.right : body_start) + (rtl ? 0 : indent);
    if (shape && rtl)
      for (auto &glyph : shape->glyphs)
        glyph.x -= width - line.width;
    if (p.alignment == TextParagraph::Right ||
        (p.alignment == TextParagraph::Start && rtl) ||
        (p.alignment == TextParagraph::End && !rtl))
      line.x += std::max(0., limit - line.width);
    if (p.alignment == TextParagraph::Center)
      line.x += std::max(0., (limit - line.width) / 2);
    if (p.alignment == TextParagraph::Justify && end < clusters.size() &&
        !hard) {
      if (shape) {
        size_t trim = end;
        while (trim > begin &&
               (p.text.mid(clusters[trim - 1].begin,
                           clusters[trim - 1].end - clusters[trim - 1].begin) ==
                    " " ||
                p.text.mid(clusters[trim - 1].begin,
                           clusters[trim - 1].end - clusters[trim - 1].begin) ==
                    "\t"))
          --trim;
        std::set<size_t> spaces;
        for (const auto &cluster : shape->clusters)
          if (cluster.begin <
                  (trim > begin ? clusters[trim - 1].end : line.begin) &&
              p.text.mid(cluster.begin, cluster.end - cluster.begin) == " ")
            spaces.insert(cluster.begin);
        double extra = spaces.empty()
                           ? 0
                           : std::max(0., limit - line.width) / spaces.size(),
               offset = 0;
        for (size_t n = 0; n < shape->glyphs.size(); ++n) {
          auto &glyph = shape->glyphs[n];
          glyph.x += offset;
          if (spaces.contains(glyph.begin) &&
              (n + 1 == shape->glyphs.size() ||
               shape->glyphs[n + 1].begin != glyph.begin)) {
            glyph.advance += extra;
            offset += extra;
          }
        }
        shape->metrics.width += offset;
        for (auto &cluster : shape->clusters)
          if (spaces.contains(cluster.begin))
            cluster.metrics.width += extra;
        if (!spaces.empty())
          line.width = limit;
      } else {
        size_t count = 0;
        for (const auto &f : line.fragments)
          if (f.text == " " && f.x < visible)
            ++count;
        double extra = count ? std::max(0., limit - visible) / count : 0,
               offset = 0;
        for (auto &f : line.fragments) {
          f.x += offset;
          if (f.text == " " && f.x - offset < visible)
            offset += extra;
        }
        if (count)
          line.width = limit;
      }
    }
    line.shaped = std::move(shape);
    lines.push_back({std::move(line), hard});
    i = end;
    if (i == clusters.size()) {
      if (hard && trailing) {
        trailing = false;
        continue;
      }
      break;
    }
  } while (true);
  return lines;
}
} // namespace

struct OdfTextReader::Impl {
  Reader reader;
};
OdfTextReader::OdfTextReader(const OdfPackage &package)
    : impl_(std::make_shared<Impl>()) {
  auto styles = package.entry("styles.xml");
  if (!styles.empty())
    impl_->reader.scan(read_document_xml(styles), true);
  impl_->reader.scan(package.xml);
  impl_->reader.load_images(package);
}
TextFlow OdfTextReader::frame(const XmlElement &container,
                              const TextParagraph &base,
                              const Text &paragraph_style,
                              bool styles_source) const {
  auto &reader = impl_->reader;
  reader.flow = {};
  reader.text_size = 0;
  reader.reset_lists();
  reader.styles_source = styles_source;
  reader.frame_base = base;
  reader.frame_paragraph_style = paragraph_style;
  reader.blocks(container);
  return std::move(reader.flow);
}

TextFlow read_text_flow(const OdfPackage &package) {
  if (package.kind() != Kind::Words)
    fail("Text flow requires a Words document");
  Reader reader;
  auto styles = package.entry("styles.xml");
  if (!styles.empty())
    reader.scan(read_document_xml(styles), true);
  reader.scan(package.xml);
  reader.load_images(package);
  reader.pages();
  reader.blocks(package.body());
  size_t total = 0;
  for (const auto &p : reader.flow.paragraphs) {
    total += p.text.size();
    if (total > 4'000'000)
      fail("Document exceeds native layout text limit");
  }
  if (reader.flow.paragraphs.empty())
    reader.flow.paragraphs.emplace_back();
  return std::move(reader.flow);
}

static void check_paragraph(const TextParagraph &p) {
  for (double value : {p.left, p.right, p.indent, p.before, p.after,
                       p.line_factor, p.line_height})
    if (!std::isfinite(value))
      fail("Invalid paragraph geometry");
  if (p.orphans < 0 || p.widows < 0 || p.orphans > 1000 || p.widows > 1000 ||
      p.line_factor <= 0 || p.line_height < 0)
    fail("Invalid paragraph pagination settings");
  auto font_valid = [](const TextFont &font) {
    return std::isfinite(font.size) && font.size > 0 && font.size <= 1000 &&
           font.weight >= 1 && font.weight <= 1000;
  };
  auto color_valid = [](const std::optional<TextColor> &color) {
    return !color || (std::isfinite(color->alpha) && color->alpha >= 0 &&
                      color->alpha <= 1);
  };
  if (!font_valid(p.font) || !color_valid(p.color))
    fail("Invalid paragraph text style");
  if (p.list) {
    const auto &label = *p.list;
    if (!font_valid(label.font) || !color_valid(label.color) ||
        label.text.size() > 4096 || label.level < 1 || label.level > 64)
      fail("Invalid list label style");
    for (double value : {label.space_before, label.min_width,
                         label.min_distance, label.indent, label.default_tab})
      if (!std::isfinite(value) || std::abs(value) > 100000)
        fail("Invalid list label geometry");
    if (label.min_width < 0 || label.min_distance < 0 ||
        label.default_tab <= 0 || label.paragraph_tabs.size() > 4096)
      fail("Invalid list label spacing");
    if (label.tab_stop &&
        (!std::isfinite(*label.tab_stop) || std::abs(*label.tab_stop) > 100000))
      fail("Invalid list label tab");
    for (double tab : label.paragraph_tabs)
      if (!std::isfinite(tab) || std::abs(tab) > 100000)
        fail("Invalid paragraph tab");
    if (label.image) {
      const auto &image = *label.image;
      if (!label.text.empty() || !std::isfinite(image.width) ||
          !std::isfinite(image.height) || !std::isfinite(image.offset) ||
          image.width <= 0 || image.height <= 0 || image.width > 10000 ||
          image.height > 10000 || std::abs(image.offset) > 100000)
        fail("Invalid list image geometry");
    }
  }
  size_t end = 0;
  for (const auto &span : p.spans) {
    if (span.begin != end || span.end <= span.begin ||
        span.end > p.text.size() || !font_valid(span.font) ||
        !color_valid(span.color))
      fail("Invalid inline text span");
    end = span.end;
  }
  if (!p.spans.empty() && end != p.text.size())
    fail("Incomplete inline text spans");
}
static TextBoxLayout text_box_impl(const TextFlow &flow, double width,
                                   const MeasureText &measure,
                                   NativeTextShaper *shaper) {
  if (!measure || !std::isfinite(width) || width < 1 || width > 10000)
    fail("Invalid native text frame width");
  TextBoxLayout result;
  result.unsupported = flow.unsupported;
  TextPageStyle frame;
  frame.width = width;
  frame.left = frame.right = frame.top = frame.bottom = 0;
  Breaks breaks;
  double y = 0, previous_after = 0;
  size_t total = 0;
  for (size_t i = 0; i < flow.paragraphs.size(); ++i) {
    const auto &p = flow.paragraphs[i];
    total +=
        p.text.size() + (p.list && p.list->visible ? p.list->text.size() : 0);
    if (total > 4000000 || p.text.size() > 1000000 ||
        flow.paragraphs.size() > 100000)
      fail("Text frame exceeds native layout limits");
    check_paragraph(p);
    auto prepared = shaper
                        ? std::make_shared<ShapingParagraph>(shaper->prepare(p))
                        : nullptr;
    if (prepared && prepared->whole().missing_glyphs)
      note(result.unsupported, "Missing font glyphs");
    auto clusters =
        breaks.clusters(p, measure, prepared ? &prepared->whole() : nullptr);
    auto lines = wrap(p, clusters, frame, 0, measure, prepared.get(), shaper);
    y += std::max(previous_after, std::max(0., p.before));
    for (auto &wrapped : lines) {
      auto line = std::move(wrapped.line);
      if (line.label && line.label->shaped &&
          line.label->shaped->missing_glyphs)
        note(result.unsupported, "Missing font glyphs");
      line.paragraph = i;
      line.y = y;
      line.baseline += y;
      y += line.height;
      result.lines.push_back(std::move(line));
      if (result.lines.size() > 100000 || y > 10000000)
        fail("Text frame exceeds native line limits");
    }
    previous_after = std::max(0., p.after);
    if (p.break_before || p.break_after || p.keep_next || p.keep_together ||
        !p.master.empty())
      note(result.unsupported,
           "Page and keep settings in unlinked text frames");
  }
  result.height = y + previous_after;
  return result;
}
TextBoxLayout layout_text_box(const TextFlow &flow, double width,
                              const MeasureText &measure) {
  return text_box_impl(flow, width, measure, nullptr);
}
TextBoxLayout layout_text_box(const TextFlow &flow, double width,
                              NativeTextShaper &shaper) {
  return text_box_impl(
      flow, width,
      [&](const Text &t, const TextFont &f) {
        return shaper.shape(t, f).metrics;
      },
      &shaper);
}

static TextPagination paginate_impl(const TextFlow &flow,
                                    const MeasureText &measure,
                                    NativeTextShaper *shaper) {
  if (!measure)
    fail("Native text measurement is required");
  TextPagination result;
  result.unsupported = flow.unsupported;
  Breaks breaks;
  std::vector<std::vector<Cluster>> clusters;
  std::vector<std::shared_ptr<ShapingParagraph>> shaping;
  size_t total = 0;
  for (const auto &p : flow.paragraphs) {
    total +=
        p.text.size() + (p.list && p.list->visible ? p.list->text.size() : 0);
    if (total > 4'000'000 || p.text.size() > 1'000'000 ||
        flow.paragraphs.size() > 100'000)
      fail("Document exceeds native layout limits");
    check_paragraph(p);
    shaping.push_back(
        shaper ? std::make_shared<ShapingParagraph>(shaper->prepare(p))
               : nullptr);
    if (shaping.back() && shaping.back()->whole().missing_glyphs)
      note(result.unsupported, "Missing font glyphs");
    clusters.push_back(breaks.clusters(
        p, measure, shaping.back() ? &shaping.back()->whole() : nullptr));
  }
  Text master = flow.first_master;
  auto style = [&]() {
    auto found = flow.masters.find(master);
    auto p = found == flow.masters.end() ? TextPageStyle{} : found->second;
    check_page(p);
    return p;
  };
  auto new_page = [&](bool follow) {
    if (result.pages.size() >= 10000)
      fail("Document exceeds native page limit");
    if (follow && !result.pages.empty())
      master = result.pages.back().style.next;
    result.pages.push_back({style(), {}});
  };
  new_page(false);
  double y = result.pages.back().style.top, previous_after = 0;
  bool pending_break = false;
  auto advance = [&](bool follow) {
    new_page(follow);
    y = result.pages.back().style.top;
    previous_after = 0;
  };
  for (size_t paragraph = 0; paragraph < flow.paragraphs.size(); ++paragraph) {
    const auto &p = flow.paragraphs[paragraph];
    bool changed_master = !p.master.empty() && p.master != master;
    if (changed_master) {
      master = p.master;
      if (result.pages.back().lines.empty())
        result.pages.back().style = style();
      else
        advance(false);
      y = result.pages.back().style.top;
    } else if ((p.break_before || pending_break) &&
               !result.pages.back().lines.empty())
      advance(p.master.empty());
    pending_break = false;
    size_t start = 0;
    bool first = true;
    std::vector<WrappedLine> wrapped;
    std::vector<double> heights;
    std::tuple<double, double, double> wrap_geometry;
    size_t line_offset = 0;
    while (true) {
      const auto &page = result.pages.back().style;
      auto geometry = std::make_tuple(page.width, page.left, page.right);
      if (wrapped.empty() || geometry != wrap_geometry) {
        wrapped = wrap(p, clusters[paragraph], page, start, measure,
                       shaping[paragraph].get(), shaper);
        wrap_geometry = geometry;
        line_offset = 0;
        heights = {0};
        for (const auto &line : wrapped)
          heights.push_back(heights.back() + line.line.height);
      }
      std::span<WrappedLine> lines(wrapped.data() + line_offset,
                                   wrapped.size() - line_offset);
      double gap = first ? std::max(previous_after, std::max(0., p.before)) : 0;
      double bottom = page.height - page.bottom;
      double height = heights.back() - heights[line_offset];
      // Keep chains together when their complete content fits a fresh page.
      double group_height = height;
      if (first && p.keep_next) {
        size_t next = paragraph;
        while (next + 1 < flow.paragraphs.size() &&
               flow.paragraphs[next].keep_next &&
               group_height <= bottom - page.top && next - paragraph < 1000) {
          const auto &following = flow.paragraphs[next + 1];
          if (flow.paragraphs[next].break_after || following.break_before ||
              !following.master.empty())
            break;
          group_height += std::max(std::max(0., flow.paragraphs[next].after),
                                   std::max(0., following.before));
          auto next_lines = wrap(following, clusters[next + 1], page, 0,
                                 measure, shaping[next + 1].get(), shaper);
          for (const auto &line : next_lines)
            group_height += line.line.height;
          ++next;
        }
      }
      if (first && !result.pages.back().lines.empty() &&
          ((p.keep_together && height <= bottom - page.top &&
            y + gap + height > bottom) ||
           (p.keep_next && group_height <= bottom - page.top &&
            y + gap + group_height > bottom))) {
        advance(true);
        continue;
      }
      size_t fit = 0;
      double used = gap;
      while (fit < lines.size() &&
             y + used + lines[fit].line.height <= bottom + 1e-7)
        used += lines[fit++].line.height;
      if (fit < lines.size()) {
        if (fit && lines.size() - fit < size_t(p.widows))
          fit = lines.size() > size_t(p.widows) ? lines.size() - p.widows : 0;
        if ((!fit || (first && fit < size_t(p.orphans))) &&
            !result.pages.back().lines.empty()) {
          advance(true);
          continue;
        }
        if (!fit) {
          // Unsatisfiable keep/widow rules or an oversized line must still
          // progress.
          while (fit < lines.size() &&
                 y + gap + lines[fit].line.height <= bottom + 1e-7) {
            gap += lines[fit].line.height;
            ++fit;
          }
          gap = first ? std::max(0., p.before) : 0;
          if (!fit) {
            fit = 1;
            note(result.unsupported, "A line exceeds the page text height");
          }
        }
      }
      y += gap;
      for (size_t at = 0; at < fit; ++at) {
        auto line = std::move(lines[at].line);
        if (line.label && line.label->shaped &&
            line.label->shaped->missing_glyphs)
          note(result.unsupported, "Missing font glyphs");
        line.paragraph = paragraph;
        line.y = y;
        line.baseline += y;
        y += line.height;
        result.pages.back().lines.push_back(std::move(line));
      }
      if (fit == lines.size())
        break;
      start = lines[fit].line.begin;
      line_offset += fit;
      first = false;
      advance(true);
    }
    previous_after = std::max(0., p.after);
    pending_break = p.break_after;
  }
  return result;
}
TextPagination paginate_text(const TextFlow &flow, const MeasureText &measure) {
  return paginate_impl(flow, measure, nullptr);
}
TextPagination paginate_text(const TextFlow &flow, NativeTextShaper &shaper) {
  return paginate_impl(
      flow,
      [&](const Text &text, const TextFont &font) {
        return shaper.shape(text, font).metrics;
      },
      &shaper);
}
} // namespace DomeOffice
