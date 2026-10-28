// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_layout.h"
#include "native_text_shaper.h"
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
  static const Regex re("^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))(cm|mm|in|pt|pc|px)$");
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
  if (p.width < 1 || p.height < 1 || p.width > 10000 || p.height > 10000 || p.left < 0 ||
      p.right < 0 || p.top < 0 || p.bottom < 0 || p.width - p.left - p.right < 1 ||
      p.height - p.top - p.bottom < 1)
    fail("Invalid text page geometry");
}
struct Reader {
  TextFlow flow;
  std::map<Text, XmlElement> paragraph_styles, text_styles;
  XmlElement paragraph_default, text_default;
  std::map<Text, Text> fonts;
  std::map<Text, XmlElement> layouts;
  std::vector<XmlDocument> documents;
  size_t text_size = 0;

  void scan(const XmlDocument &document) {
    documents.push_back(document);
    for (auto e : document.elementsByTagNameNS(STYLE, "font-face"))
      fonts[e.attributeNS(STYLE, "name")] = e.attributeNS(SVG, "font-family");
    for (auto e : document.elementsByTagNameNS(STYLE, "default-style")) {
      if (e.attributeNS(STYLE, "family") == "paragraph")
        paragraph_default = e;
      if (e.attributeNS(STYLE, "family") == "text")
        text_default = e;
    }
    for (auto e : document.elementsByTagNameNS(STYLE, "style")) {
      auto family = e.attributeNS(STYLE, "family"), name = e.attributeNS(STYLE, "name");
      if (family == "paragraph")
        paragraph_styles[name] = e;
      if (family == "text")
        text_styles[name] = e;
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
      font.weight = weight == "bold" ? 700 : weight == "normal" ? 400 : weight.toInt(&valid);
      if (!valid || font.weight < 1 || font.weight > 1000)
        fail("Invalid text font weight");
      font.bold = font.weight >= 600;
    }
    if (p.hasAttributeNS(FO, "font-style"))
      font.italic = p.attributeNS(FO, "font-style") != "normal";
    if (p.hasAttributeNS(STYLE, "font-name")) {
      auto name = p.attributeNS(STYLE, "font-name");
      font.family = fonts.contains(name) ? fonts.at(name) : name;
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
  void properties(TextParagraph &p, const XmlElement &style) {
    font(p.font, style);
    if (style.hasAttributeNS(STYLE, "master-page-name"))
      p.master = style.attributeNS(STYLE, "master-page-name");
    auto e = child(style, STYLE, "paragraph-properties");
    if (e.hasAttributeNS(FO, "margin"))
      p.left = p.right = p.before = p.after = length(e.attributeNS(FO, "margin"));
    for (auto [name, target] :
         std::initializer_list<std::pair<const char *, double *>>{{"margin-left", &p.left},
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
    for (auto [name, target] : std::initializer_list<std::pair<const char *, bool *>>{
             {"break-before", &p.break_before}, {"break-after", &p.break_after}})
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
    for (auto [name, target] : std::initializer_list<std::pair<const char *, int *>>{
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
  void chain(const std::map<Text, XmlElement> &styles, const Text &name, Apply apply) {
    std::vector<XmlElement> stack;
    std::set<Text> visited;
    Text current = name;
    while (!current.empty() && styles.contains(current)) {
      if (!visited.insert(current).second || stack.size() >= 256)
        fail("Cyclic or excessive text style inheritance");
      auto e = styles.at(current);
      stack.push_back(e);
      current = e.attributeNS(STYLE, "parent-style-name");
    }
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      apply(*it);
  }
  void inlines(TextParagraph &paragraph, const XmlElement &parent, TextFont current,
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
          if (!paragraph.spans.empty() && paragraph.spans.back().font == current)
            paragraph.spans.back().end = paragraph.text.size();
          else
            paragraph.spans.push_back({begin, paragraph.text.size(), current});
        }
      };
      if (n.isText() || n.isCDATASection())
        append(n.nodeValue());
      else if (n.isElement()) {
        auto e = n.toElement();
        if (e.namespaceURI() == TEXT && (e.localName() == "span" || e.localName() == "a")) {
          auto next = current;
          chain(text_styles, e.attributeNS(TEXT, "style-name"),
                [&](const XmlElement &s) { font(next, s); });
          inlines(paragraph, e, next, depth + 1);
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
                 e.localName() == "soft-page-break") { /* Reflow from styles and metrics. */
        } else {
          note(flow.unsupported, "Inline fields, notes and anchored objects");
          inlines(paragraph, e, current, depth + 1);
        }
      }
    }
  }
  void blocks(const XmlElement &parent, int depth = 0) {
    if (depth > 256)
      fail("Excessive text flow depth");
    for (auto node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
      if (!node.isElement())
        continue;
      auto e = node.toElement();
      if (e.namespaceURI() == TEXT && (e.localName() == "p" || e.localName() == "h")) {
        if (flow.paragraphs.size() >= 100'000)
          fail("Too many text paragraphs");
        TextParagraph p;
        font(p.font, text_default);
        properties(p, paragraph_default);
        chain(paragraph_styles, e.attributeNS(TEXT, "style-name"),
              [&](const XmlElement &s) { properties(p, s); });
        inlines(p, e, p.font);
        flow.paragraphs.push_back(std::move(p));
      } else if (e.namespaceURI() == TEXT && e.localName() == "sequence-decls")
        continue;
      else {
        note(flow.unsupported, "Tables, lists, sections and floating frames");
        blocks(e, depth + 1);
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
          if (props.hasAttributeNS(FO, "margin"))
            p.left = p.right = p.top = p.bottom = length(props.attributeNS(FO, "margin"));
          for (auto [name, target] : std::initializer_list<std::pair<const char *, double *>>{
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
};
void check_metrics(const TextMetrics &metrics) {
  if (!std::isfinite(metrics.width) || !std::isfinite(metrics.ascent) ||
      !std::isfinite(metrics.descent) || metrics.width < 0 || metrics.ascent < 0 ||
      metrics.descent < 0 || metrics.ascent + metrics.descent <= 0)
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
    for (int at = ubrk_first(iterator); at != UBRK_DONE; at = ubrk_next(iterator))
      result.push_back(indices.at(at));
    return result;
  }
  std::vector<Cluster> clusters(const TextParagraph &p, const MeasureText &measure,
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
        if (cluster.begin != cursor || cluster.end <= cursor || cluster.end > p.text.size())
          fail("Invalid native shaped source coverage");
        if (pending.end == pending.begin) {
          pending.begin = cluster.begin;
          while (span + 1 < p.spans.size() && p.spans[span].end <= cluster.begin)
            ++span;
          pending.font = p.spans.empty() ? p.font : p.spans[span].font;
        }
        pending.end = cluster.end;
        pending.metrics.width += cluster.metrics.width;
        pending.metrics.ascent = std::max(pending.metrics.ascent, cluster.metrics.ascent);
        pending.metrics.descent = std::max(pending.metrics.descent, cluster.metrics.descent);
        auto value = p.text.mid(cluster.begin, cluster.end - cluster.begin);
        pending.hard = pending.hard || value.contains('\n') || value.contains('\r') ||
                       value.contains(Char(0x2028)) || value.contains(Char(0x2029));
        cursor = cluster.end;
        if (std::binary_search(graphemes.begin(), graphemes.end(), cursor)) {
          check_metrics(pending.metrics);
          pending.allowed = std::binary_search(legal.begin(), legal.end(), cursor);
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
      bool hard = value.contains('\n') || value.contains('\r') || value.contains(Char(0x2028));
      auto metrics = measure(hard ? Text(" ") : value == "\t" ? Text("    ") : value, font);
      check_metrics(metrics);
      if (hard)
        metrics.width = 0;
      result.push_back(
          {begin, end, font, metrics, std::binary_search(legal.begin(), legal.end(), end), hard});
    }
    return result;
  }
};
struct WrappedLine {
  TextLine line;
  bool hard = false;
};
std::vector<WrappedLine> wrap(const TextParagraph &p, const std::vector<Cluster> &clusters,
                              const TextPageStyle &page, size_t start, const MeasureText &measure,
                              const ShapingParagraph *shaping = nullptr) {
  const double available = page.width - page.left - page.right - p.left - p.right;
  if (!std::isfinite(available) || available < 1)
    fail("Paragraph margins leave no text area");
  std::vector<WrappedLine> lines;
  size_t i = std::lower_bound(clusters.begin(), clusters.end(), start,
                              [](const Cluster &c, size_t pos) { return c.begin < pos; }) -
             clusters.begin();
  bool trailing = !clusters.empty() && clusters.back().hard;
  do {
    size_t begin = i, end = i, allowed = i;
    double width = 0, max_ascent = 0, max_descent = 0;
    double indent = (i == 0 ? p.indent : 0);
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
      size_t a = begin < clusters.size() ? clusters[begin].begin : p.text.size();
      size_t b = stop > begin ? clusters[stop - 1].end : a;
      while (b > a && (p.text[b - 1] == '\n' || p.text[b - 1] == '\r' || p.text[b - 1] == 0x2028 ||
                       p.text[b - 1] == 0x2029))
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
    line.begin = begin < clusters.size() ? clusters[begin].begin : p.text.size();
    line.end = end ? clusters[end - 1].end : line.begin;
    line.x = page.left + p.left + indent;
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
      // Separate whitespace fragments so justification can expand their advance.
      if (!line.fragments.empty() && line.fragments.back().font == c.font && text != " " &&
          !line.fragments.back().text.endsWith(' ')) {
        line.fragments.back().text += text;
        line.fragments.back().width += c.metrics.width;
      } else
        line.fragments.push_back({text, c.font, width, c.metrics.width});
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
    double natural = max_ascent + max_descent;
    line.height = p.line_height > 0 ? p.line_height : natural * p.line_factor;
    if (!std::isfinite(line.height) || line.height <= 0)
      fail("Invalid native line height");
    line.baseline = (line.height - natural) / 2 + max_ascent;
    // Trailing wrap spaces stay in the source range but do not affect alignment.
    double visible = width;
    for (size_t at = end; at > begin && !clusters[at - 1].hard; --at) {
      const auto &c = clusters[at - 1];
      auto t = p.text.mid(c.begin, c.end - c.begin);
      if (t != " " && t != "\t")
        break;
      visible -= c.metrics.width;
    }
    line.width = std::max(0., visible);
    bool rtl = shape ? shape->rtl : p.direction == TextParagraph::RTL;
    line.x = page.left + p.left + (rtl ? 0 : indent);
    if (shape && rtl)
      for (auto &glyph : shape->glyphs)
        glyph.x -= width - line.width;
    if (p.alignment == TextParagraph::Right || (p.alignment == TextParagraph::Start && rtl) ||
        (p.alignment == TextParagraph::End && !rtl))
      line.x += std::max(0., limit - line.width);
    if (p.alignment == TextParagraph::Center)
      line.x += std::max(0., (limit - line.width) / 2);
    if (p.alignment == TextParagraph::Justify && end < clusters.size() && !hard) {
      if (shape) {
        size_t trim = end;
        while (trim > begin &&
               (p.text.mid(clusters[trim - 1].begin,
                           clusters[trim - 1].end - clusters[trim - 1].begin) == " " ||
                p.text.mid(clusters[trim - 1].begin,
                           clusters[trim - 1].end - clusters[trim - 1].begin) == "\t"))
          --trim;
        std::set<size_t> spaces;
        for (const auto &cluster : shape->clusters)
          if (cluster.begin < (trim > begin ? clusters[trim - 1].end : line.begin) &&
              p.text.mid(cluster.begin, cluster.end - cluster.begin) == " ")
            spaces.insert(cluster.begin);
        double extra = spaces.empty() ? 0 : std::max(0., limit - line.width) / spaces.size(),
               offset = 0;
        for (size_t n = 0; n < shape->glyphs.size(); ++n) {
          auto &glyph = shape->glyphs[n];
          glyph.x += offset;
          if (spaces.contains(glyph.begin) &&
              (n + 1 == shape->glyphs.size() || shape->glyphs[n + 1].begin != glyph.begin)) {
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
        double extra = count ? std::max(0., limit - visible) / count : 0, offset = 0;
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

TextFlow read_text_flow(const OdfPackage &package) {
  if (package.kind() != Kind::Words)
    fail("Text flow requires a Words document");
  Reader reader;
  auto styles = package.entry("styles.xml");
  if (!styles.empty())
    reader.scan(read_document_xml(styles));
  reader.scan(package.xml);
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

static TextPagination paginate_impl(const TextFlow &flow, const MeasureText &measure,
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
    total += p.text.size();
    if (total > 4'000'000 || p.text.size() > 1'000'000 || flow.paragraphs.size() > 100'000)
      fail("Document exceeds native layout limits");
    for (double value :
         {p.left, p.right, p.indent, p.before, p.after, p.line_factor, p.line_height})
      if (!std::isfinite(value))
        fail("Invalid paragraph geometry");
    if (p.orphans < 0 || p.widows < 0 || p.orphans > 1000 || p.widows > 1000 ||
        p.line_factor <= 0 || p.line_height < 0)
      fail("Invalid paragraph pagination settings");
    auto font_valid = [](const TextFont &font) {
      return std::isfinite(font.size) && font.size > 0 && font.size <= 1000 && font.weight >= 1 &&
             font.weight <= 1000;
    };
    if (!font_valid(p.font))
      fail("Invalid paragraph font size");
    size_t end = 0;
    for (const auto &span : p.spans) {
      if (span.begin != end || span.end <= span.begin || span.end > p.text.size() ||
          !font_valid(span.font))
        fail("Invalid inline text span");
      end = span.end;
    }
    if (!p.spans.empty() && end != p.text.size())
      fail("Incomplete inline text spans");
    shaping.push_back(shaper ? std::make_shared<ShapingParagraph>(shaper->prepare(p)) : nullptr);
    if (shaping.back() && shaping.back()->whole().missing_glyphs)
      note(result.unsupported, "Missing font glyphs");
    clusters.push_back(
        breaks.clusters(p, measure, shaping.back() ? &shaping.back()->whole() : nullptr));
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
    } else if ((p.break_before || pending_break) && !result.pages.back().lines.empty())
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
        wrapped = wrap(p, clusters[paragraph], page, start, measure, shaping[paragraph].get());
        wrap_geometry = geometry;
        line_offset = 0;
        heights = {0};
        for (const auto &line : wrapped)
          heights.push_back(heights.back() + line.line.height);
      }
      std::span<WrappedLine> lines(wrapped.data() + line_offset, wrapped.size() - line_offset);
      double gap = first ? std::max(previous_after, std::max(0., p.before)) : 0;
      double bottom = page.height - page.bottom;
      double height = heights.back() - heights[line_offset];
      // Keep chains together when their complete content fits a fresh page.
      double group_height = height;
      if (first && p.keep_next) {
        size_t next = paragraph;
        while (next + 1 < flow.paragraphs.size() && flow.paragraphs[next].keep_next &&
               group_height <= bottom - page.top && next - paragraph < 1000) {
          const auto &following = flow.paragraphs[next + 1];
          if (flow.paragraphs[next].break_after || following.break_before ||
              !following.master.empty())
            break;
          group_height +=
              std::max(std::max(0., flow.paragraphs[next].after), std::max(0., following.before));
          auto next_lines =
              wrap(following, clusters[next + 1], page, 0, measure, shaping[next + 1].get());
          for (const auto &line : next_lines)
            group_height += line.line.height;
          ++next;
        }
      }
      if (first && !result.pages.back().lines.empty() &&
          ((p.keep_together && height <= bottom - page.top && y + gap + height > bottom) ||
           (p.keep_next && group_height <= bottom - page.top && y + gap + group_height > bottom))) {
        advance(true);
        continue;
      }
      size_t fit = 0;
      double used = gap;
      while (fit < lines.size() && y + used + lines[fit].line.height <= bottom + 1e-7)
        used += lines[fit++].line.height;
      if (fit < lines.size()) {
        if (fit && lines.size() - fit < size_t(p.widows))
          fit = lines.size() > size_t(p.widows) ? lines.size() - p.widows : 0;
        if ((!fit || (first && fit < size_t(p.orphans))) && !result.pages.back().lines.empty()) {
          advance(true);
          continue;
        }
        if (!fit) {
          // Unsatisfiable keep/widow rules or an oversized line must still progress.
          while (fit < lines.size() && y + gap + lines[fit].line.height <= bottom + 1e-7) {
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
      [&](const Text &text, const TextFont &font) { return shaper.shape(text, font).metrics; },
      &shaper);
}
} // namespace DomeOffice
