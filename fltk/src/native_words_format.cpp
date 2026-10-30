// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_format.h"
#include "native_font_family.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
struct Block {
  XmlElement node;
  Text text;
  size_t start;
};
std::vector<Block> blocks(const OdfPackage &package) {
  std::vector<Block> result;
  size_t offset = 0;
  std::function<void(XmlElement, size_t)> walk = [&](XmlElement parent,
                                                     size_t depth) {
    if (depth > 256)
      fail("Excessive text structure depth");
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        if (n.namespaceURI() == TEXT &&
            (n.localName() == "p" || n.localName() == "h")) {
          auto value = plain_text(n);
          result.push_back({n, value, offset});
          offset += value.size() + 1;
        } else
          walk(n, depth + 1);
      }
  };
  walk(package.body(), 0);
  return result;
}
bool touches(const Block &b, size_t begin, size_t end) {
  return begin == end ? begin >= b.start && begin <= b.start + b.text.size()
                      : b.start < end && b.start + b.text.size() >= begin;
}
void check_range(const std::vector<Block> &all, size_t begin, size_t end) {
  auto length = all.empty() ? 0 : all.back().start + all.back().text.size();
  if (begin > end || end > length)
    fail("Formatting range is outside the document");
}
using Properties = std::map<std::pair<Text, Text>, Text>;
Properties attributes(XmlElement node, bool ignore_name = false) {
  Properties result;
  for (auto a : node.attributes()) {
    auto local = a.name.mid(a.name.indexOf(':') + 1);
    if (ignore_name && a.uri == STYLE && local == "name")
      continue;
    auto name = a.uri == STYLE ? "style:" + local
                : a.uri == FO  ? "fo:" + local
                : a.uri == XML ? "xml:" + local
                               : a.name;
    result[{a.uri, name}] = a.value;
  }
  return result;
}
void put(Properties &p, const Text &ns, const Text &name, const Text &value) {
  p[{ns, name}] = value;
}
// ODF lengths/percentages use decimal lexical forms, never exponent notation.
Text decimal(double value) {
  if (value == 0)
    return "0";
  std::array<char, 512> buffer;
  auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                              value, std::chars_format::fixed);
  if (result.ec != std::errc{})
    fail("Cannot represent formatting number");
  return Text(std::string(buffer.data(), result.ptr));
}
void overlay(Properties &p, const WordsCharacterFormat &f) {
  if (f.family) {
    for (const auto &name : {"style:font-name", "style:font-name-asian",
                             "style:font-name-complex"})
      p.erase({STYLE, name});
    put(p, FO, "fo:font-family", *f.family);
    put(p, STYLE, "style:font-family-asian", *f.family);
    put(p, STYLE, "style:font-family-complex", *f.family);
  }
  auto scripts = [&](const char *plain, const char *asian, const char *complex,
                     const Text &value) {
    put(p, FO, plain, value);
    put(p, STYLE, asian, value);
    put(p, STYLE, complex, value);
  };
  if (f.size)
    scripts("fo:font-size", "style:font-size-asian", "style:font-size-complex",
            decimal(*f.size) + "pt");
  if (f.weight)
    scripts("fo:font-weight", "style:font-weight-asian",
            "style:font-weight-complex", Text::number(*f.weight));
  if (f.italic)
    scripts("fo:font-style", "style:font-style-asian",
            "style:font-style-complex", *f.italic ? "italic" : "normal");
  for (auto [enabled, name] : {std::pair{f.underline, Text("text-underline")},
                               std::pair{f.strike, Text("text-line-through")}})
    if (enabled) {
      put(p, STYLE, "style:" + name + "-style", *enabled ? "solid" : "none");
      p.erase({STYLE, "style:" + name + "-type"});
      if (*enabled)
        put(p, STYLE, "style:" + name + "-type", "single");
    }
  if (f.highlight)
    put(p, FO, "fo:background-color", f.highlight->toLower());
  if (f.color) {
    if (*f.color == "auto") {
      p.erase({FO, "fo:color"});
      put(p, STYLE, "style:use-window-font-color", "true");
    } else {
      put(p, FO, "fo:color", f.color->toLower());
      put(p, STYLE, "style:use-window-font-color", "false");
    }
  }
}
void overlay_paragraph(XmlElement props, const WordsParagraphFormat &f) {
  auto length = [&](const char *key, std::optional<double> value) {
    if (value)
      props.setAttributeNS(FO, Text("fo:") + key, decimal(*value) + "pt");
  };
  if (f.alignment)
    props.setAttributeNS(FO, "fo:text-align", *f.alignment);
  length("margin-left", f.left);
  length("margin-right", f.right);
  length("text-indent", f.indent);
  length("margin-top", f.before);
  length("margin-bottom", f.after);
  if (f.line_factor)
    props.setAttributeNS(FO, "fo:line-height",
                         decimal(*f.line_factor * 100) + "%");
  if (f.keep_next)
    props.setAttributeNS(FO, "fo:keep-with-next",
                         *f.keep_next ? "always" : "auto");
  if (f.keep_together)
    props.setAttributeNS(FO, "fo:keep-together",
                         *f.keep_together ? "always" : "auto");
  if (f.break_before)
    props.setAttributeNS(FO, "fo:break-before",
                         *f.break_before ? "page" : "auto");
  if (f.orphans)
    props.setAttributeNS(FO, "fo:orphans", *f.orphans);
  if (f.widows)
    props.setAttributeNS(FO, "fo:widows", *f.widows);
}
bool equivalent(XmlElement a, XmlElement b, bool style_root = false) {
  if (a.isNull() || b.isNull())
    return a.isNull() == b.isNull();
  if (a.isElement() != b.isElement() || a.namespaceURI() != b.namespaceURI() ||
      a.localName() != b.localName())
    return false;
  if (!a.isElement())
    return a.nodeValue() == b.nodeValue();
  if (attributes(a, style_root) != attributes(b, style_root))
    return false;
  auto x = a.firstChild(), y = b.firstChild();
  for (; !x.isNull() && !y.isNull(); x = x.nextSibling(), y = y.nextSibling())
    if (!equivalent(x, y))
      return false;
  return x.isNull() && y.isNull();
}
class Catalogue {
  OdfPackage &package;
  XmlDocument external;
  std::map<std::pair<Text, Text>, XmlElement> common, automatic;
  std::set<Text> names;
  XmlElement group() {
    auto root = package.xml.documentElement();
    auto value = child(root, OFFICE, "automatic-styles");
    if (value.isNull()) {
      value = package.xml.createElementNS(OFFICE, "office:automatic-styles");
      root.insertBefore(value, child(root, OFFICE, "body"));
    }
    return value;
  }
  Text store(XmlElement style, const char *prefix) {
    for (auto &[key, other] : automatic)
      if (equivalent(style, other, true))
        return key.second;
    size_t number = 1;
    Text name;
    do {
      name = Text(prefix) + Text::number(number++);
    } while (names.contains(name));
    style.setAttributeNS(STYLE, "style:name", name);
    group().appendChild(style);
    automatic[{style.attributeNS(STYLE, "family"), name}] = style;
    names.insert(name);
    return name;
  }

public:
  explicit Catalogue(OdfPackage &p) : package(p) {
    auto scan = [&](const XmlDocument &doc, bool content) {
      for (auto style : doc.elementsByTagNameNS(STYLE, "style")) {
        auto name = style.attributeNS(STYLE, "name");
        names.insert(name);
        if (style.parentNode().namespaceURI() == OFFICE &&
            style.parentNode().localName() == "styles")
          common[{style.attributeNS(STYLE, "family"), name}] = style;
        else if (content && style.parentNode().namespaceURI() == OFFICE &&
                 style.parentNode().localName() == "automatic-styles")
          automatic[{style.attributeNS(STYLE, "family"), name}] = style;
      }
    };
    auto bytes = package.entry("styles.xml");
    if (!bytes.empty()) {
      external = read_document_xml(bytes);
      scan(external, false);
    }
    scan(package.xml, true);
  }
  const auto &named() const { return common; }
  XmlElement named_paragraph(const Text &name) const {
    auto found = common.find({"paragraph", name});
    if (found == common.end())
      fail("Choose an existing named paragraph style");
    return found->second;
  }
  std::vector<XmlElement> paragraph_chain(const Text &name) const {
    std::vector<XmlElement> result;
    std::set<Text> seen;
    for (auto current = name; !current.empty();) {
      if (!seen.insert(current).second || result.size() >= 64)
        fail("Cyclic or excessively deep paragraph style inheritance");
      auto style = named_paragraph(current);
      result.push_back(style);
      current = style.attributeNS(STYLE, "parent-style-name");
    }
    return result;
  }
  std::optional<Text> paragraph_base(Text name) const {
    std::set<Text> seen;
    bool first = true;
    while (!name.empty()) {
      auto automatic_style = automatic.find({"paragraph", name});
      if ((!first || automatic_style == automatic.end()) &&
          common.contains({"paragraph", name}))
        return name;
      if (!seen.insert(name).second || seen.size() > 64)
        return {};
      if (automatic_style == automatic.end())
        return {};
      name = automatic_style->second.attributeNS(STYLE, "parent-style-name");
      first = false;
    }
    return Text{};
  }
  Text paragraph_reference(const Text &name) {
    if (name.empty())
      return {};
    (void)paragraph_chain(name);
    if (!automatic.contains({"paragraph", name}))
      return name;
    auto style = package.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:family", "paragraph");
    style.setAttributeNS(STYLE, "style:parent-style-name", name);
    return store(style, "WordsParagraph");
  }
  std::optional<int> paragraph_outline(const Text &name,
                                       bool common_only = false) const {
    std::vector<XmlElement> seen;
    auto current = name;
    bool first = true;
    while (!current.empty()) {
      XmlElement style;
      auto shared = common.find({"paragraph", current});
      auto direct = automatic.find({"paragraph", current});
      if ((!first || common_only || direct == automatic.end()) &&
          shared != common.end())
        style = shared->second;
      else if (direct != automatic.end())
        style = direct->second;
      else
        return {};
      if (std::find(seen.begin(), seen.end(), style) != seen.end() ||
          seen.size() >= 64)
        fail("Cyclic or excessively deep paragraph style inheritance");
      seen.push_back(style);
      if (style.hasAttributeNS(STYLE, "default-outline-level")) {
        auto value = style.attributeNS(STYLE, "default-outline-level");
        if (value.empty())
          return 0;
        bool ok = false;
        const auto level = value.toInt(&ok);
        if (!ok || level < 1 || level > 1000)
          fail("Unsupported heading outline level");
        return level;
      }
      current = style.attributeNS(STYLE, "parent-style-name");
      first = false;
    }
    return {};
  }
  Text define_paragraph(const WordsParagraphStyleChange &change) {
    const auto before = external.toByteArray();
    const bool creating = change.name.empty();
    Text name = change.name;
    XmlElement original, style;
    XmlDocument document;
    if (!creating) {
      original = named_paragraph(name);
      document = original.ownerDocument();
      style = original.cloneNode(true);
    } else {
      size_t number = 1;
      do {
        name = "WordsNamed" + Text::number(number++);
      } while (names.contains(name));
      if (external.documentElement().isNull()) {
        const auto version =
            package.xml.documentElement().attributeNS(OFFICE, "version", "1.3");
        external = read_document_xml(
            ("<office:document-styles xmlns:office=\"" + OFFICE + "\"/>")
                .toUtf8());
        external.documentElement().setAttributeNS(OFFICE, "office:version",
                                                  version);
      }
      document = external;
      style = document.createElementNS(STYLE, "style:style");
      style.setAttributeNS(STYLE, "style:name", name);
      style.setAttributeNS(STYLE, "style:family", "paragraph");
    }
    auto attribute = [&](const char *key, const std::optional<Text> &value,
                         bool remove_empty = true) {
      if (!value)
        return;
      if (remove_empty && value->empty())
        style.removeAttributeNS(STYLE, key);
      else if (!style.hasAttributeNS(STYLE, key) ||
               style.attributeNS(STYLE, key) != *value)
        style.setAttributeNS(STYLE, Text("style:") + key, *value);
    };
    if (creating && !change.label)
      fail("Enter a style name");
    if (change.label &&
        (change.label->trimmed().empty() || change.label->size() > 1024 ||
         std::any_of(change.label->begin(), change.label->end(),
                     [](auto c) { return c < 32; })))
      fail("Enter a style name without control characters");
    attribute("display-name", change.label);
    attribute("parent-style-name", change.parent);
    auto next = change.next;
    if (next && *next == ":self")
      next = name;
    if (next && !next->empty() && *next != name)
      (void)named_paragraph(*next);
    attribute("next-style-name", next);
    if (change.outline) {
      if (*change.outline == "inherit")
        style.removeAttributeNS(STYLE, "default-outline-level");
      else {
        if (!change.outline->empty()) {
          bool ok = false;
          const auto level = change.outline->toInt(&ok);
          if (!ok || level < 1 || level > 1000)
            fail("Heading level must be between 1 and 1000");
        }
        attribute("default-outline-level", change.outline, false);
      }
    }
    auto properties = [&](const Text &kind) {
      auto node = child(style, STYLE, kind);
      if (node.isNull()) {
        node = document.createElementNS(STYLE, "style:" + kind);
        XmlNode before;
        for (auto n = style.firstChild(); !n.isNull(); n = n.nextSibling())
          if (n.namespaceURI() == STYLE &&
              (n.localName() == "map" ||
               (kind == "paragraph-properties" &&
                n.localName() == "text-properties"))) {
            before = n;
            break;
          }
        style.insertBefore(node, before);
      }
      return node;
    };
    if (!change.character.empty()) {
      auto props = properties("text-properties");
      auto values = attributes(props);
      overlay(values, change.character);
      for (auto a : props.attributes())
        props.removeAttributeNS(a.uri, a.name.mid(a.name.indexOf(':') + 1));
      for (const auto &[key, value] : values)
        props.setAttributeNS(key.first, key.second, value);
    }
    if (!change.paragraph.empty())
      overlay_paragraph(properties("paragraph-properties"), change.paragraph);
    common[{"paragraph", name}] = style;
    (void)paragraph_chain(name); // Includes the prospective parent link.
    if (!creating && equivalent(original, style))
      return name;
    if (creating) {
      auto root = document.documentElement();
      auto group = child(root, OFFICE, "styles");
      if (group.isNull()) {
        group = document.createElementNS(OFFICE, "office:styles");
        auto following = child(root, OFFICE, "automatic-styles");
        if (following.isNull())
          following = child(root, OFFICE, "master-styles");
        root.insertBefore(group, following);
      }
      group.appendChild(style);
    } else {
      auto parent = original.parentNode();
      parent.insertBefore(style, original);
      parent.removeChild(original);
    }
    if (document.documentElement() != package.xml.documentElement() &&
        external.toByteArray() != before)
      package.set_entry("styles.xml", external.toByteArray(), "text/xml");
    return name;
  }
  bool erase_paragraph(const Text &name) {
    auto style = named_paragraph(name);
    auto referenced = [&](const XmlDocument &doc) {
      bool used = false;
      std::function<void(XmlElement)> walk = [&](XmlElement node) {
        if (node == style)
          return;
        for (const auto &a : node.attributes()) {
          auto local = a.name.mid(a.name.indexOf(':') + 1);
          if (a.uri == STYLE && (local == "name" || local == "display-name"))
            continue;
          for (const auto &token : a.value.split(' '))
            if (token == name)
              used = true;
        }
        for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
          if (n.isElement())
            walk(n);
      };
      walk(doc.documentElement());
      return used;
    };
    if (referenced(package.xml) || referenced(external))
      fail("This style is referenced; only unused styles can be deleted");
    bool in_content = style.ownerDocument().documentElement() ==
                      package.xml.documentElement();
    style.parentNode().removeChild(style);
    if (!in_content)
      package.set_entry("styles.xml", external.toByteArray(), "text/xml");
    return true;
  }
  // A simple leaf wrapper can be split/replaced instead of nesting another
  // direct-format span on every toggle. Other source wrappers stay intact.
  std::optional<Properties> simple(XmlElement span) const {
    if (span.namespaceURI() != TEXT || span.localName() != "span" ||
        span.attributes().size() != 1 ||
        !span.hasAttributeNS(TEXT, "style-name"))
      return {};
    auto name = span.attributeNS(TEXT, "style-name");
    XmlElement style;
    if (automatic.contains({"text", name}))
      style = automatic.at({"text", name});
    else if (common.contains({"text", name}))
      style = common.at({"text", name});
    if (style.isNull() || style.attributeNS(STYLE, "family") != "text" ||
        style.attributes().size() != 2)
      return {};
    auto props = child(style, STYLE, "text-properties");
    if (props.isNull() || props.hasAttributeNS(XML, "id"))
      return {};
    for (auto n = style.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement() && n != props)
        return {};
    for (auto n = props.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        return {};
    for (auto n = span.firstChild(); !n.isNull(); n = n.nextSibling())
      if (!n.isText() && !n.isCDATASection() &&
          !(n.namespaceURI() == TEXT &&
            (n.localName() == "s" || n.localName() == "tab" ||
             n.localName() == "line-break") &&
            n.attributes().size() <= (n.localName() == "s" ? 1u : 0u)))
        return {};
    return attributes(props);
  }
  Text text_style(Properties props, const WordsCharacterFormat &f) {
    overlay(props, f);
    for (const auto &[name, style] : automatic)
      if (style.attributeNS(STYLE, "family") == "text" &&
          style.attributes().size() == 2 &&
          attributes(child(style, STYLE, "text-properties")) == props) {
        bool extra = false;
        for (auto n = style.firstChild(); !n.isNull(); n = n.nextSibling())
          if (n.isElement() && !(n.namespaceURI() == STYLE &&
                                 n.localName() == "text-properties"))
            extra = true;
        if (!extra &&
            child(style, STYLE, "text-properties").firstChild().isNull())
          return name.second;
      }
    auto style = package.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:family", "text");
    auto properties =
        package.xml.createElementNS(STYLE, "style:text-properties");
    for (auto &[key, value] : props)
      properties.setAttributeNS(key.first, key.second, value);
    style.appendChild(properties);
    return store(style, "WordsText");
  }
  Text paragraph_style(XmlElement paragraph, const WordsParagraphFormat *format,
                       const WordsCharacterFormat *character) {
    auto name = paragraph.attributeNS(TEXT, "style-name");
    XmlElement style;
    if (automatic.contains({"paragraph", name}))
      style = automatic.at({"paragraph", name}).cloneNode(true);
    else {
      style = package.xml.createElementNS(STYLE, "style:style");
      style.setAttributeNS(STYLE, "style:family", "paragraph");
      if (!name.empty())
        style.setAttributeNS(STYLE, "style:parent-style-name", name);
    }
    // Source IDs stay on the original style, including opaque extensions.
    std::function<void(XmlElement)> strip_ids = [&](XmlElement node) {
      node.removeAttributeNS(XML, "id");
      for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
        if (n.isElement())
          strip_ids(n);
    };
    strip_ids(style);
    auto kind =
        character ? Text("text-properties") : Text("paragraph-properties");
    auto props = child(style, STYLE, kind);
    if (props.isNull()) {
      props = package.xml.createElementNS(STYLE, "style:" + kind);
      XmlNode before;
      for (auto n = style.firstChild(); !n.isNull(); n = n.nextSibling())
        if (n.namespaceURI() == STYLE &&
            (n.localName() == "map" ||
             (!character && n.localName() == "text-properties"))) {
          before = n;
          break;
        }
      style.insertBefore(props, before);
    }
    if (character) {
      auto values = attributes(props);
      overlay(values, *character);
      for (auto a : props.attributes())
        props.removeAttributeNS(a.uri, a.name.mid(a.name.indexOf(':') + 1));
      for (auto &[key, value] : values)
        props.setAttributeNS(key.first, key.second, value);
    } else {
      overlay_paragraph(props, *format);
    }
    return store(style, "WordsParagraph");
  }
};
Text node_text(XmlNode node) {
  if (node.isText() || node.isCDATASection())
    return node.nodeValue();
  auto holder = node.ownerDocument().createElement("holder");
  holder.appendChild(node.cloneNode(true));
  return plain_text(holder);
}
XmlNode leaf_slice(XmlNode node, size_t first, size_t last) {
  if (node.isText() || node.isCDATASection())
    return node.ownerDocument().createTextNode(
        node.nodeValue().mid(first, last - first));
  auto copy = node.cloneNode(true);
  if (node.localName() == "s")
    copy.setAttributeNS(TEXT, "text:c", last - first);
  if (first)
    copy.removeAttributeNS(XML, "id");
  return copy;
}
bool control(XmlNode n) {
  return n.namespaceURI() == TEXT &&
         (n.localName() == "s" || n.localName() == "tab" ||
          n.localName() == "line-break");
}
void format_inlines(XmlElement parent, size_t begin, size_t end,
                    const WordsCharacterFormat &format, Catalogue &styles,
                    size_t depth = 0) {
  if (depth > 256)
    fail("Excessive inline formatting depth");
  size_t offset = 0;
  for (auto n = parent.firstChild(); !n.isNull();) {
    auto next = n.nextSibling();
    auto text = node_text(n);
    auto length = text.size();
    size_t first = std::max(begin, offset),
           last = std::min(end, offset + length);
    if (first < last) {
      if (auto props = styles.simple(n)) {
        auto name = styles.text_style(*props, format);
        if (name != n.attributeNS(TEXT, "style-name")) {
          size_t part_offset = 0;
          for (auto part = n.firstChild(); !part.isNull();
               part = part.nextSibling()) {
            size_t part_length = node_text(part).size();
            std::vector<size_t> cuts{0};
            for (size_t cut : {first - offset, last - offset})
              if (cut > part_offset && cut < part_offset + part_length)
                cuts.push_back(cut - part_offset);
            cuts.push_back(part_length);
            for (size_t i = 1; i < cuts.size(); ++i)
              if (cuts[i] > cuts[i - 1]) {
                auto span = n.cloneNode(false);
                if (part_offset + cuts[i - 1] >= first - offset &&
                    part_offset + cuts[i] <= last - offset)
                  span.setAttributeNS(TEXT, "text:style-name", name);
                span.appendChild(leaf_slice(part, cuts[i - 1], cuts[i]));
                parent.insertBefore(span, n);
              }
            part_offset += part_length;
          }
          parent.removeChild(n);
        }
      } else if (n.isText() || n.isCDATASection() || control(n)) {
        size_t a = first - offset, b = last - offset;
        if (a)
          parent.insertBefore(leaf_slice(n, 0, a), n);
        auto span = n.ownerDocument().createElementNS(TEXT, "text:span");
        span.setAttributeNS(TEXT, "text:style-name",
                            styles.text_style({}, format));
        span.appendChild(leaf_slice(n, a, b));
        parent.insertBefore(span, n);
        if (b < length)
          parent.insertBefore(leaf_slice(n, b, length), n);
        parent.removeChild(n);
      } else if (n.namespaceURI() == TEXT &&
                 (n.localName() == "span" || n.localName() == "a" ||
                  n.localName() == "meta"))
        format_inlines(n, first - offset, last - offset, format, styles,
                       depth + 1);
      else
        fail("Formatting this inline object requires its specialized editor");
    }
    offset += length;
    n = next;
  }
}
template <class T>
void common(std::optional<T> &value, const T &next, bool first) {
  if (first)
    value = next;
  else if (value && *value != next)
    value.reset();
}
Text color_text(std::optional<TextColor> color) {
  if (!color)
    return "auto";
  char text[8];
  std::snprintf(text, sizeof(text), "#%02x%02x%02x", color->red, color->green,
                color->blue);
  return text;
}
const char *alignment(TextParagraph::Alignment value) {
  switch (value) {
  case TextParagraph::Left:
    return "left";
  case TextParagraph::Center:
    return "center";
  case TextParagraph::Right:
    return "right";
  case TextParagraph::Justify:
    return "justify";
  case TextParagraph::End:
    return "end";
  default:
    return "start";
  }
}
} // namespace
bool WordsCharacterFormat::empty() const {
  return !family && !color && !highlight && !size && !weight && !italic &&
         !underline && !strike;
}
void WordsCharacterFormat::overlay(const WordsCharacterFormat &f) {
  if (f.family)
    family = f.family;
  if (f.color)
    color = f.color;
  if (f.highlight)
    highlight = f.highlight;
  if (f.size)
    size = f.size;
  if (f.weight)
    weight = f.weight;
  if (f.italic)
    italic = f.italic;
  if (f.underline)
    underline = f.underline;
  if (f.strike)
    strike = f.strike;
}
bool WordsParagraphFormat::empty() const {
  return !alignment && !left && !right && !indent && !before && !after &&
         !line_factor && !keep_next && !keep_together && !break_before &&
         !orphans && !widows;
}
std::vector<WordsParagraphStyle>
words_paragraph_styles(const OdfPackage &package) {
  auto copy = package;
  Catalogue styles(copy);
  std::vector<WordsParagraphStyle> result;
  for (const auto &[key, style] : styles.named())
    if (key.first == "paragraph") {
      WordsParagraphStyle info;
      info.name = key.second;
      info.label = style.attributeNS(STYLE, "display-name", info.name);
      info.parent = style.attributeNS(STYLE, "parent-style-name");
      info.next = style.attributeNS(STYLE, "next-style-name");
      if (style.hasAttributeNS(STYLE, "default-outline-level"))
        info.outline = style.attributeNS(STYLE, "default-outline-level");
      info.hidden = style.attributeNS(STYLE, "hidden") == "true" ||
                    style.attributeNS(STYLE, "hidden") == "1";
      result.push_back(std::move(info));
    }
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.label == b.label ? a.name < b.name : a.label < b.label;
  });
  return result;
}
std::optional<Text> words_current_paragraph_style(const OdfPackage &package,
                                                  size_t begin, size_t end) {
  auto all = blocks(package);
  check_range(all, begin, end);
  auto copy = package;
  Catalogue styles(copy);
  std::optional<Text> result;
  bool first = true;
  for (const auto &block : all)
    if (touches(block, begin, end)) {
      auto name =
          styles.paragraph_base(block.node.attributeNS(TEXT, "style-name"));
      if (!name)
        return {};
      if (first) {
        result = name;
        first = false;
      } else if (result != name)
        return {};
    }
  return result;
}
Text define_words_paragraph_style(OdfPackage &package,
                                  const WordsParagraphStyleChange &change) {
  validate_words_format(change.character);
  validate_words_format(change.paragraph);
  Catalogue styles(package);
  return styles.define_paragraph(change);
}
bool delete_words_paragraph_style(OdfPackage &package, const Text &name) {
  Catalogue styles(package);
  return styles.erase_paragraph(name);
}
Text words_paragraph_style_reference(OdfPackage &package, const Text &name) {
  Catalogue styles(package);
  return styles.paragraph_reference(name);
}
std::optional<int> words_paragraph_style_outline(const OdfPackage &package,
                                                 const Text &name) {
  auto copy = package;
  Catalogue styles(copy);
  return styles.paragraph_outline(name);
}
WordsFormatInfo words_paragraph_style_format(const OdfPackage &package,
                                             const Text &name) {
  auto copy = package;
  copy.xml = read_document_xml(package.xml.toByteArray());
  Catalogue styles(copy);
  (void)styles.paragraph_chain(name);
  auto body = copy.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  auto paragraph = copy.xml.createElementNS(TEXT, "text:p");
  if (!name.empty())
    paragraph.setAttributeNS(TEXT, "text:style-name",
                             styles.paragraph_reference(name));
  body.appendChild(paragraph);
  return read_words_format(copy, 0, 0);
}
bool apply_words_paragraph_style(OdfPackage &package, size_t begin, size_t end,
                                 const Text &name) {
  auto all = blocks(package);
  check_range(all, begin, end);
  Catalogue styles(package);
  std::optional<int> outline;
  if (name.empty())
    outline = 0;
  else {
    (void)styles.paragraph_chain(name);
    outline = styles.paragraph_outline(name, true);
  }
  auto before = package.xml.toByteArray();
  if (all.empty()) {
    auto paragraph = package.xml.createElementNS(TEXT, "text:p");
    package.body().appendChild(paragraph);
    all.push_back({paragraph, {}, 0});
  }
  auto reference = styles.paragraph_reference(name);
  for (auto &block : all)
    if (touches(block, begin, end)) {
      auto paragraph = block.node;
      if (outline) {
        if (!*outline && !child(paragraph, TEXT, "number").isNull())
          fail("Converting a numbered heading requires the outline-number "
               "editor");
        auto tag = *outline ? Text("h") : Text("p");
        if (paragraph.localName() != tag) {
          auto replacement = package.xml.createElementNS(TEXT, "text:" + tag);
          for (const auto &a : paragraph.attributes()) {
            if (a.uri.empty())
              replacement.setAttribute(a.name, a.value);
            else
              replacement.setAttributeNS(a.uri, a.name, a.value);
          }
          while (!paragraph.firstChild().isNull())
            replacement.appendChild(paragraph.firstChild());
          auto parent = paragraph.parentNode();
          parent.insertBefore(replacement, paragraph);
          parent.removeChild(paragraph);
          paragraph = replacement;
        }
        if (*outline) {
          bool ok = false;
          auto old_level =
              paragraph.attributeNS(TEXT, "outline-level").toInt(&ok);
          if (!ok || old_level != *outline)
            paragraph.setAttributeNS(TEXT, "text:outline-level", *outline);
        } else
          for (const auto &key : {"outline-level", "restart-numbering",
                                  "start-value", "is-list-header"})
            paragraph.removeAttributeNS(TEXT, key);
      }
      if (name.empty())
        paragraph.removeAttributeNS(TEXT, "style-name");
      else if (paragraph.attributeNS(TEXT, "style-name") != reference)
        paragraph.setAttributeNS(TEXT, "text:style-name", reference);
    }
  return package.xml.toByteArray() != before;
}
void WordsParagraphFormat::overlay(const WordsParagraphFormat &other) {
  if (other.alignment)
    alignment = other.alignment;
  if (other.left)
    left = other.left;
  if (other.right)
    right = other.right;
  if (other.indent)
    indent = other.indent;
  if (other.before)
    before = other.before;
  if (other.after)
    after = other.after;
  if (other.line_factor)
    line_factor = other.line_factor;
  if (other.keep_next)
    keep_next = other.keep_next;
  if (other.keep_together)
    keep_together = other.keep_together;
  if (other.break_before)
    break_before = other.break_before;
  if (other.orphans)
    orphans = other.orphans;
  if (other.widows)
    widows = other.widows;
}
void validate_words_format(const WordsCharacterFormat &f) {
  if (f.size && (!std::isfinite(*f.size) || *f.size <= 0 || *f.size > 1000))
    fail("Font size must be greater than 0 and at most 1000 points");
  if (f.weight && (*f.weight < 100 || *f.weight > 900 || *f.weight % 100))
    fail("OpenDocument font weight must be 100, 200, ..., 900");
  if (f.family && (f.family->trimmed().empty() || f.family->size() > 1024 ||
                   f.family->contains('\n') || f.family->contains('\r') ||
                   f.family->contains('\0')))
    fail("Enter a font family name");
  if (f.family && document_font_families(*f.family).empty())
    fail("Enter a font family name");
  if (f.color && *f.color != "auto" &&
      !std::regex_match(f.color->toUtf8().toStdString(),
                        std::regex("#[0-9a-fA-F]{6}")))
    fail("Color must be #RRGGBB or auto");
  if (f.highlight && *f.highlight != "transparent" &&
      !std::regex_match(f.highlight->toUtf8().toStdString(),
                        std::regex("#[0-9a-fA-F]{6}")))
    fail("Highlight must be #RRGGBB or transparent");
}
void validate_words_format(const WordsParagraphFormat &f) {
  if (f.alignment &&
      !TextList{"left", "center", "right", "justify", "start", "end"}.contains(
          *f.alignment))
    fail("Invalid paragraph alignment");
  for (auto value : {f.left, f.right, f.indent, f.before, f.after})
    if (value && (!std::isfinite(*value) || std::abs(*value) > 10000))
      fail("Paragraph spacing must be within 10000 points");
  for (auto value : {f.before, f.after})
    if (value && *value < 0)
      fail("Space before and after a paragraph must not be negative");
  if (f.line_factor && (!std::isfinite(*f.line_factor) || *f.line_factor <= 0 ||
                        *f.line_factor > 100))
    fail("Line spacing must be greater than 0 and at most 100");
  for (auto value : {f.orphans, f.widows})
    if (value && (*value < 0 || *value > 1000))
      fail("Widow/orphan limits must be between 0 and 1000");
}
bool format_words_characters(OdfPackage &package, size_t begin, size_t end,
                             const WordsCharacterFormat &format) {
  validate_words_format(format);
  auto all = blocks(package);
  check_range(all, begin, end);
  if (format.empty())
    return false;
  auto before = package.xml.toByteArray();
  if (all.empty()) {
    auto paragraph = package.xml.createElementNS(TEXT, "text:p");
    package.body().appendChild(paragraph);
    all.push_back({paragraph, {}, 0});
  }
  Catalogue styles(package);
  for (auto &b : all)
    if (touches(b, begin, end)) {
      if (b.text.empty())
        b.node.setAttributeNS(TEXT, "text:style-name",
                              styles.paragraph_style(b.node, nullptr, &format));
      else if (begin != end)
        format_inlines(b.node, begin > b.start ? begin - b.start : 0,
                       std::min(end - b.start, b.text.size()), format, styles);
    }
  return package.xml.toByteArray() != before;
}
bool format_words_paragraphs(OdfPackage &package, size_t begin, size_t end,
                             const WordsParagraphFormat &format) {
  validate_words_format(format);
  auto all = blocks(package);
  check_range(all, begin, end);
  if (format.empty())
    return false;
  auto before = package.xml.toByteArray();
  if (all.empty()) {
    auto paragraph = package.xml.createElementNS(TEXT, "text:p");
    package.body().appendChild(paragraph);
    all.push_back({paragraph, {}, 0});
  }
  Catalogue styles(package);
  for (auto &b : all)
    if (touches(b, begin, end))
      b.node.setAttributeNS(TEXT, "text:style-name",
                            styles.paragraph_style(b.node, &format, nullptr));
  return package.xml.toByteArray() != before;
}
WordsFormatInfo read_words_format(const OdfPackage &package, size_t begin,
                                  size_t end) {
  auto all = blocks(package);
  check_range(all, begin, end);
  // The layout reader supplies a virtual default paragraph for empty text.
  if (all.empty())
    all.push_back({{}, {}, 0});
  auto flow = read_text_flow(package);
  if (flow.paragraphs.size() != all.size())
    fail("Source formatting does not match the layout model");
  std::vector<const TextParagraph *> paragraphs;
  for (auto &p : flow.paragraphs)
    paragraphs.push_back(&p);
  std::set<size_t> mapped;
  std::function<void(const std::vector<TextBlock> &)> walk =
      [&](const auto &list) {
        for (const auto &b : list) {
          if (b.kind == TextBlock::Paragraph && b.formatted &&
              mapped.insert(b.index).second)
            paragraphs.at(b.index) = b.formatted.get();
          else if (b.kind == TextBlock::Table && b.table)
            for (auto &cell : b.table->cells)
              walk(cell.blocks);
        }
      };
  walk(flow.blocks);
  WordsFormatInfo result;
  bool first_p = true, first_c = true;
  for (size_t i = 0; i < all.size(); ++i)
    if (touches(all[i], begin, end)) {
      auto &p = *paragraphs[i];
      auto &f = result.paragraph;
      if (p.text != all[i].text)
        fail("Source formatting does not match paragraph text");
      common(f.alignment, Text(alignment(p.alignment)), first_p);
      common(f.left, p.left, first_p);
      common(f.right, p.right, first_p);
      common(f.indent, p.indent, first_p);
      common(f.before, p.before, first_p);
      common(f.after, p.after, first_p);
      common(f.line_factor,
             p.line_height ? p.line_height / p.font.size : p.line_factor,
             first_p);
      common(f.keep_next, p.keep_next, first_p);
      common(f.keep_together, p.keep_together, first_p);
      common(f.break_before, p.break_before, first_p);
      common(f.orphans, p.orphans, first_p);
      common(f.widows, p.widows, first_p);
      first_p = false;
      auto add = [&](const TextFont &font, std::optional<TextColor> color,
                     std::optional<TextColor> background,
                     const TextDecorations &decorations) {
        auto &c = result.character;
        common(c.family, font.family, first_c);
        common(c.color, color_text(color), first_c);
        common(c.highlight,
               background ? color_text(background) : Text("transparent"),
               first_c);
        common(c.size, font.size, first_c);
        common(c.weight, font.weight, first_c);
        common(result.bold, font.weight >= 600, first_c);
        common(c.italic, font.italic, first_c);
        common(c.underline, decorations.underline.style != TextDecoration::None,
               first_c);
        common(c.strike, decorations.strike.style != TextDecoration::None,
               first_c);
        first_c = false;
      };
      if (p.spans.empty())
        add(p.font, p.color, p.background, p.decorations);
      else
        for (auto &s : p.spans) {
          auto a = all[i].start + s.begin, b = all[i].start + s.end;
          if (begin == end ? (begin >= a && begin <= b)
                           : (a < end && b > begin)) {
            add(s.font, s.color, s.background, s.decorations);
            if (begin == end)
              break;
          }
        }
    }
  return result;
}
} // namespace DomeOffice
