// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_format.h"
#include "native_words_tabs.h"
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
  std::map<std::pair<Text, Text>, XmlElement> common, automatic,
      master_automatic;
  XmlElement paragraph_default;
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
      for (auto node : doc.elementsByTagNameNS(STYLE, "default-style"))
        if (node.attributeNS(STYLE, "family") == "paragraph")
          paragraph_default = node;
      for (auto style : doc.elementsByTagNameNS(STYLE, "style")) {
        auto name = style.attributeNS(STYLE, "name");
        names.insert(name);
        if (style.parentNode().namespaceURI() == OFFICE &&
            style.parentNode().localName() == "styles")
          common[{style.attributeNS(STYLE, "family"), name}] = style;
        else if (style.parentNode().namespaceURI() == OFFICE &&
                 style.parentNode().localName() == "automatic-styles")
          (content
               ? automatic
               : master_automatic)[{style.attributeNS(STYLE, "family"), name}] =
              style;
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
  XmlElement named_style(const Text &family, const Text &name) const {
    auto found = common.find({family, name});
    if (found == common.end())
      fail("Choose an existing named style");
    return found->second;
  }
  std::vector<XmlElement> named_chain(const Text &family,
                                      const Text &name) const {
    std::vector<XmlElement> result;
    std::set<Text> seen;
    for (auto current = name; !current.empty();) {
      if (!seen.insert(current).second || result.size() >= 64)
        fail("Cyclic or excessively deep named style inheritance");
      auto style = named_style(family, current);
      result.push_back(style);
      current = style.attributeNS(STYLE, "parent-style-name");
    }
    return result;
  }
  std::optional<Text> named_base(const Text &family, Text name) const {
    std::set<Text> seen;
    bool first = true;
    while (!name.empty()) {
      auto automatic_style = automatic.find({family, name});
      if ((!first || automatic_style == automatic.end()) &&
          common.contains({family, name}))
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
  Text named_reference(const Text &family, const Text &name) {
    if (name.empty())
      return {};
    (void)named_chain(family, name);
    if (!automatic.contains({family, name}))
      return name;
    auto style = package.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:family", family);
    style.setAttributeNS(STYLE, "style:parent-style-name", name);
    return store(style,
                 family == "paragraph" ? "WordsParagraph" : "WordsCharacter");
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
  Text define_named(const Text &family,
                    const WordsParagraphStyleChange &change) {
    const auto before = external.toByteArray();
    const bool creating = change.name.empty();
    Text name = change.name;
    XmlElement original, style;
    XmlDocument document;
    if (!creating) {
      original = named_style(family, name);
      document = original.ownerDocument();
      style = original.cloneNode(true);
    } else {
      size_t number = 1;
      do {
        name = (family == "paragraph" ? Text("WordsNamed")
                                      : Text("WordsCharacterNamed")) +
               Text::number(number++);
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
      style.setAttributeNS(STYLE, "style:family", family);
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
      (void)named_style(family, *next);
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
    common[{family, name}] = style;
    (void)named_chain(family, name); // Includes the prospective parent link.
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
  bool erase_named(const Text &family, const Text &name) {
    auto style = named_style(family, name);
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
  WordsTabSet tab_set(const TextParagraph &paragraph) const {
    WordsTabSet result;
    result.source = paragraph.tab_source;
    for (auto node : children(paragraph.tab_source, STYLE, "tab-stop")) {
      WordsTabStopSpec spec;
      spec.source = node;
      for (const auto &a : node.attributes())
        if (a.uri == STYLE)
          spec.properties[a.name.mid(a.name.indexOf(':') + 1)] = a.value;
      auto ref = node.attributeNS(STYLE, "leader-text-style");
      if (!paragraph.tab_source_common) {
        if (paragraph.tab_source_master &&
            master_automatic.contains({"text", ref}))
          spec.leader_scope = WordsTabStopSpec::MasterAutomatic;
        else if (!paragraph.tab_source_master &&
                 automatic.contains({"text", ref}))
          spec.leader_scope = WordsTabStopSpec::ContentAutomatic;
      }
      result.stops.push_back(std::move(spec));
    }
    std::stable_sort(result.stops.begin(), result.stops.end(),
                     [](const auto &a, const auto &b) {
                       return words_tab_position(a) < words_tab_position(b);
                     });
    for (size_t i = 1; i < result.stops.size();) {
      if (words_tab_position(result.stops[i - 1]) ==
          words_tab_position(result.stops[i]))
        result.stops.erase(result.stops.begin() + i - 1);
      else
        ++i;
    }
    return result;
  }
  bool direct_tabs(XmlElement paragraph) const {
    auto name = paragraph.attributeNS(TEXT, "style-name");
    auto found = automatic.find({"paragraph", name});
    return found != automatic.end() &&
           !child(child(found->second, STYLE, "paragraph-properties"), STYLE,
                  "tab-stops")
                .isNull();
  }
  bool tab_origin_editable() const {
    if (package.xml.documentElement().attributeNS(OFFICE, "version") != "1.4")
      return false;
    auto root = paragraph_default.isNull()
                    ? external.documentElement()
                    : paragraph_default.ownerDocument().documentElement();
    return root.isNull() || root.attributeNS(OFFICE, "version") == "1.4";
  }
  void tab_defaults(std::optional<double> distance,
                    std::optional<bool> relative) {
    if (!distance && !relative)
      return;
    if (relative && !tab_origin_editable())
      fail("Changing tab origin requires an ODF 1.4 document and style part");
    auto before = external.toByteArray();
    if (paragraph_default.isNull()) {
      if (external.documentElement().isNull()) {
        external = read_document_xml(
            ("<office:document-styles xmlns:office=\"" + OFFICE + "\"/>")
                .toUtf8());
        external.documentElement().setAttributeNS(
            OFFICE, "office:version",
            package.xml.documentElement().attributeNS(OFFICE, "version",
                                                      "1.3"));
      }
      auto root = external.documentElement();
      auto group = child(root, OFFICE, "styles");
      if (group.isNull()) {
        group = external.createElementNS(OFFICE, "office:styles");
        auto next = child(root, OFFICE, "automatic-styles");
        if (next.isNull())
          next = child(root, OFFICE, "master-styles");
        root.insertBefore(group, next);
      }
      paragraph_default =
          external.createElementNS(STYLE, "style:default-style");
      paragraph_default.setAttributeNS(STYLE, "style:family", "paragraph");
      group.insertBefore(paragraph_default, group.firstChild());
    }
    auto doc = paragraph_default.ownerDocument();
    auto props = child(paragraph_default, STYLE, "paragraph-properties");
    if (props.isNull()) {
      props = doc.createElementNS(STYLE, "style:paragraph-properties");
      paragraph_default.insertBefore(props, paragraph_default.firstChild());
    }
    if (distance)
      props.setAttributeNS(STYLE, "style:tab-stop-distance",
                           decimal(*distance) + "pt");
    if (relative)
      props.setAttributeNS(TEXT, "text:relative-tab-stop-position",
                           *relative ? "true" : "false");
    if (external.toByteArray() != before)
      package.set_entry("styles.xml", external.toByteArray(), "text/xml");
  }
  Text master_tab_leader(const Text &name, std::set<Text> &seen) {
    if (!seen.insert(name).second || seen.size() > 64)
      fail("Cyclic tab leader style");
    auto found = master_automatic.find({"text", name});
    if (found == master_automatic.end())
      return name;
    auto node = package.xml.importNode(found->second, true).toElement();
    auto parent = node.attributeNS(STYLE, "parent-style-name");
    if (!parent.empty() && !common.contains({"text", parent}) &&
        master_automatic.contains({"text", parent}))
      node.setAttributeNS(STYLE, "style:parent-style-name",
                          master_tab_leader(parent, seen));
    std::function<void(XmlNode)> strip = [&](XmlNode n) {
      n.removeAttributeNS(XML, "id");
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        strip(c);
    };
    strip(node);
    return store(node, "WordsTabLeader");
  }
  XmlElement tab_node(const WordsTabStopSpec &spec) {
    auto node = spec.source.isNull()
                    ? package.xml.createElementNS(STYLE, "style:tab-stop")
                    : package.xml.importNode(spec.source, true).toElement();
    for (const auto &a : node.attributes())
      if (a.uri == STYLE &&
          !spec.properties.contains(a.name.mid(a.name.indexOf(':') + 1)))
        node.removeAttributeNS(STYLE, a.name.mid(a.name.indexOf(':') + 1));
    for (const auto &[key, value] : spec.properties) {
      auto written = value;
      if (key == "leader-text-style" && !value.empty()) {
        if (spec.leader_scope == WordsTabStopSpec::Common &&
            common.contains({"text", value}))
          written = named_reference("text", value);
        else if (spec.leader_scope == WordsTabStopSpec::MasterAutomatic) {
          std::set<Text> seen;
          written = master_tab_leader(value, seen);
        }
      }
      if (!node.hasAttributeNS(STYLE, key) ||
          node.attributeNS(STYLE, key) != written)
        node.setAttributeNS(STYLE, "style:" + key, written);
    }
    std::function<void(XmlNode)> strip = [&](XmlNode n) {
      n.removeAttributeNS(XML, "id");
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        strip(c);
    };
    strip(node);
    return node;
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
                       const WordsCharacterFormat *character,
                       const WordsTabsFormat *tabs = nullptr,
                       XmlElement tab_source = {}) {
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
    } else if (tabs) {
      auto old = child(props, STYLE, "tab-stops");
      if (!old.isNull())
        props.removeChild(old);
      if (tabs->stops) {
        auto container =
            tab_source.isNull()
                ? package.xml.createElementNS(STYLE, "style:tab-stops")
                : package.xml.importNode(tab_source, true).toElement();
        strip_ids(container);
        for (auto node : children(container, STYLE, "tab-stop"))
          container.removeChild(node);
        for (const auto &stop : tabs->stops->stops)
          container.appendChild(tab_node(stop));
        props.insertBefore(container, props.firstChild());
      }
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
// Preserve containers/IDs once. Displaced ancestor styles are reapplied only
// around unselected fragments, rather than splitting metadata and links.
XmlNode styled_node(XmlNode node, const std::vector<Text> &chain) {
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    auto span = node.ownerDocument().createElementNS(TEXT, "text:span");
    span.setAttributeNS(TEXT, "text:style-name", *it);
    span.appendChild(node);
    node = span;
  }
  return node;
}
void assign_character_inlines(XmlElement parent, size_t begin, size_t end,
                              const Text &reference,
                              const std::vector<Text> &inherited = {},
                              size_t depth = 0) {
  if (depth > 256)
    fail("Excessive inline style depth");
  std::vector<XmlNode> result;
  size_t offset = 0;
  for (auto source = parent.firstChild(); !source.isNull();
       source = source.nextSibling()) {
    auto length = node_text(source).size();
    auto first = std::max(begin, offset), last = std::min(end, offset + length);
    if (first >= last) {
      result.push_back(styled_node(source.cloneNode(true), inherited));
    } else if (source.isText() || source.isCDATASection() || control(source)) {
      auto a = first - offset, b = last - offset;
      if (a)
        result.push_back(styled_node(leaf_slice(source, 0, a), inherited));
      result.push_back(styled_node(leaf_slice(source, a, b),
                                   reference.empty()
                                       ? std::vector<Text>{}
                                       : std::vector<Text>{reference}));
      if (b < length)
        result.push_back(styled_node(leaf_slice(source, b, length), inherited));
    } else if (source.namespaceURI() == TEXT &&
               (source.localName() == "span" || source.localName() == "a" ||
                source.localName() == "meta")) {
      // These source features need a style-aware visited/class cascade before
      // replacing their character format can preserve all states.
      if (source.hasAttributeNS(TEXT, "visited-style-name") ||
          source.hasAttributeNS(TEXT, "class-names"))
        fail("Character style assignment for visited links or style classes "
             "requires their specialized editor");
      auto old = source.attributeNS(TEXT, "style-name");
      bool clean = true;
      std::function<void(XmlNode)> inspect = [&](XmlNode node) {
        for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
          if (n.isElement()) {
            if (n.hasAttributeNS(TEXT, "style-name") ||
                n.hasAttributeNS(TEXT, "class-names") ||
                n.hasAttributeNS(TEXT, "visited-style-name"))
              clean = false;
            inspect(n);
          }
      };
      inspect(source);
      if (inherited.empty() && old == reference && clean) {
        result.push_back(source.cloneNode(true));
      } else {
        auto copy = source.cloneNode(true).toElement();
        copy.removeAttributeNS(TEXT, "style-name");
        auto chain = inherited;
        if (!old.empty())
          chain.push_back(old);
        assign_character_inlines(copy, first - offset, last - offset, reference,
                                 chain, depth + 1);
        // An unstyled span has no remaining semantics. Flatten it so repeated
        // assignments cannot grow one redundant nesting level per edit.
        if (copy.localName() == "span" && copy.attributes().empty()) {
          while (!copy.firstChild().isNull()) {
            auto child = copy.firstChild();
            copy.removeChild(child);
            result.push_back(child);
          }
        } else
          result.push_back(copy);
      }
    } else
      fail("Assigning a character style to this inline object requires its "
           "specialized editor");
    offset += length;
  }
  while (!parent.firstChild().isNull())
    parent.removeChild(parent.firstChild());
  for (auto node : result)
    parent.appendChild(node);
}
Text tab_property(const WordsTabStopSpec &stop, const Text &name,
                  const Text &fallback = {}) {
  auto found = stop.properties.find(name);
  return found == stop.properties.end() ? fallback : found->second;
}
bool same_tab_set(const WordsTabSet &a, const WordsTabSet &b) {
  if (a.stops.size() != b.stops.size())
    return false;
  for (size_t i = 0; i < a.stops.size(); ++i) {
    const auto &x = a.stops[i], &y = b.stops[i];
    if (x.properties != y.properties)
      return false;
    if (!tab_property(x, "leader-text-style").empty() &&
        x.leader_scope != y.leader_scope)
      return false;
    auto opaque = [](XmlElement node) {
      auto attrs = attributes(node);
      for (auto it = attrs.begin(); it != attrs.end();)
        if (it->first.first == STYLE)
          it = attrs.erase(it);
        else
          ++it;
      return attrs;
    };
    if (opaque(x.source) != opaque(y.source))
      return false;
    auto nx = x.source.firstChild(), ny = y.source.firstChild();
    for (; !nx.isNull() && !ny.isNull();
         nx = nx.nextSibling(), ny = ny.nextSibling())
      if (!equivalent(nx, ny))
        return false;
    if (!nx.isNull() || !ny.isNull())
      return false;
  }
  return true;
}
std::vector<const TextParagraph *> tab_paragraphs(const TextFlow &flow) {
  std::vector<const TextParagraph *> result;
  for (const auto &p : flow.paragraphs)
    result.push_back(&p);
  std::set<size_t> seen;
  std::function<void(const std::vector<TextBlock> &)> walk =
      [&](const auto &list) {
        for (const auto &b : list) {
          if (b.kind == TextBlock::Paragraph && b.formatted &&
              seen.insert(b.index).second)
            result.at(b.index) = b.formatted.get();
          else if (b.kind == TextBlock::Table && b.table)
            for (const auto &cell : b.table->cells)
              walk(cell.blocks);
        }
      };
  walk(flow.blocks);
  return result;
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
double words_tab_position(const WordsTabStopSpec &stop) {
  auto value = tab_property(stop, "position").trimmed();
  static const Regex pattern(
      "^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))(cm|mm|in|pt|pc|px)$");
  auto match = pattern.match(value);
  if (!match.hasMatch())
    fail("Enter a valid tab position");
  auto unit = match.captured(2);
  double factor = unit == "cm"   ? 72 / 2.54
                  : unit == "mm" ? 72 / 25.4
                  : unit == "in" ? 72
                  : unit == "pc" ? 12
                  : unit == "px" ? .75
                                 : 1;
  auto result = match.captured(1).toDouble() * factor;
  if (!std::isfinite(result) || std::abs(result) > 100000)
    fail("Tab position exceeds the native limit");
  return result;
}
WordsTabStopSpec words_new_tab_stop(double points) {
  if (!std::isfinite(points) || std::abs(points) > 100000)
    fail("Tab position exceeds the native limit");
  WordsTabStopSpec result;
  result.properties = {{"position", decimal(points) + "pt"}, {"type", "left"}};
  return result;
}
void validate_words_tabs(const WordsTabsFormat &format) {
  if (format.inherit && format.stops)
    fail("Choose replacement tabs or inherited tabs");
  if (format.default_distance &&
      (!std::isfinite(*format.default_distance) ||
       *format.default_distance < 0 || *format.default_distance > 100000))
    fail("Invalid default tab interval");
  if (!format.stops)
    return;
  if (format.stops->stops.size() > 4096)
    fail("Too many paragraph tab stops");
  auto xml_character = [](char32_t c) {
    return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff) ||
           (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
  };
  std::set<double> positions;
  for (const auto &stop : format.stops->stops) {
    static const Regex position_lexical(
        "^-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:cm|mm|in|pt|pc|px)$");
    if (!position_lexical.match(tab_property(stop, "position")).hasMatch())
      fail("Invalid ODF tab position");
    if (!positions.insert(words_tab_position(stop)).second)
      fail("Two tab stops cannot share a position");
    auto type = tab_property(stop, "type", "left");
    if (!TextList{"left", "right", "center", "char"}.contains(type))
      fail("Invalid tab alignment");
    auto delimiter = tab_property(stop, "char");
    if (type == "char" &&
        (delimiter.size() != 1 || !xml_character(delimiter[0])))
      fail("Enter one Unicode tab delimiter");
    if (type != "char" && stop.properties.contains("char"))
      fail("Only character-aligned tabs have a delimiter");
    if (!TextList{"", "none", "single", "double"}.contains(
            tab_property(stop, "leader-type")))
      fail("Invalid tab leader type");
    if (!TextList{"", "none", "solid", "dotted", "dash", "long-dash",
                  "dot-dash", "dot-dot-dash", "wave"}
             .contains(tab_property(stop, "leader-style")))
      fail("Invalid tab leader pattern");
    auto width = tab_property(stop, "leader-width", "auto");
    if (!TextList{"auto", "normal", "bold", "thin", "medium", "thick"}.contains(
            width)) {
      static const Regex integer("^[+]?[0-9]+$");
      if (integer.match(width.trimmed()).hasMatch()) {
        auto value = width.toDouble();
        if (!std::isfinite(value) || value <= 0 || value > 1000)
          fail("Invalid tab leader width");
      } else if (width.endsWith('%')) {
        bool ok = false;
        auto value = width.left(width.size() - 1).toDouble(&ok);
        static const Regex percent("^(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)%$");
        if (!ok || !percent.match(width).hasMatch() || !std::isfinite(value) ||
            value <= 0 || value > 100000)
          fail("Invalid tab leader width");
      } else {
        static const Regex positive_length(
            "^(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:cm|mm|in|pt|pc|px)$");
        if (!positive_length.match(width).hasMatch())
          fail("Invalid tab leader width");
        WordsTabStopSpec length;
        length.properties["position"] = width;
        auto value = words_tab_position(length);
        if (value <= 0 || value > 1000)
          fail("Invalid tab leader width");
      }
    }
    auto color = tab_property(stop, "leader-color", "font-color");
    if (color != "font-color" &&
        !std::regex_match(color.toUtf8().toStdString(),
                          std::regex("#[0-9a-fA-F]{6}")))
      fail("Leader color must be #RRGGBB or font-color");
    auto text = tab_property(stop, "leader-text");
    if (text.size() > 1 || (text.size() == 1 && !xml_character(text[0])))
      fail("Enter one Unicode leader character");
    auto ref = tab_property(stop, "leader-text-style");
    if (!ref.empty() && xmlValidateNCName(BAD_CAST ref.toUtf8().c_str(), 0))
      fail("Invalid tab leader style reference");
    if (stop.leader_scope < WordsTabStopSpec::Common ||
        stop.leader_scope > WordsTabStopSpec::MasterAutomatic)
      fail("Invalid tab leader style scope");
  }
}
WordsTabsInfo read_words_tabs(const OdfPackage &package, size_t begin,
                              size_t end) {
  if (package.kind() != Kind::Words)
    fail("Tab formatting requires a Words document");
  auto copy = package;
  copy.xml = read_document_xml(package.xml.toByteArray());
  auto all = blocks(copy);
  check_range(all, begin, end);
  if (all.empty())
    all.push_back({{}, {}, 0});
  auto flow = read_text_flow(copy);
  auto paragraphs = tab_paragraphs(flow);
  if (paragraphs.size() != all.size())
    fail("Source tabs do not match layout paragraphs");
  Catalogue styles(copy);
  WordsTabsInfo result;
  result.origin_editable = styles.tab_origin_editable();
  bool first = true;
  for (size_t i = 0; i < all.size(); ++i)
    if (touches(all[i], begin, end)) {
      auto current = styles.tab_set(*paragraphs[i]);
      if (first) {
        result.stops = current;
        result.default_distance = paragraphs[i]->default_tab;
        result.relative = paragraphs[i]->relative_tabs;
        first = false;
      } else if (result.stops && !same_tab_set(*result.stops, current))
        result.stops.reset();
    }
  return result;
}
bool format_words_tabs(OdfPackage &package, size_t begin, size_t end,
                       const WordsTabsFormat &format) {
  if (package.kind() != Kind::Words || package.read_only())
    fail("This document cannot be formatted");
  validate_words_tabs(format);
  auto original = blocks(package);
  check_range(original, begin, end);
  if (!format.stops && !format.inherit && !format.default_distance &&
      !format.relative)
    return false;
  const auto before = package.xml.toByteArray();
  auto candidate = package;
  candidate.xml = read_document_xml(before);
  auto info = read_words_tabs(candidate, begin, end);
  Catalogue styles(candidate);
  auto distance = format.default_distance;
  auto relative = format.relative;
  if (distance && std::abs(*distance - info.default_distance) < 1e-9)
    distance.reset();
  if (relative && *relative == info.relative)
    relative.reset();
  styles.tab_defaults(distance, relative);
  if (format.stops || format.inherit) {
    auto all = blocks(candidate);
    if (all.empty() && format.stops) {
      auto empty_flow = read_text_flow(candidate);
      if (!same_tab_set(styles.tab_set(empty_flow.paragraphs.front()),
                        *format.stops)) {
        auto p = candidate.xml.createElementNS(TEXT, "text:p");
        candidate.body().appendChild(p);
        all.push_back({p, {}, 0});
      }
    }
    auto flow = read_text_flow(candidate);
    auto paragraphs = tab_paragraphs(flow);
    for (size_t i = 0; i < all.size(); ++i)
      if (touches(all[i], begin, end)) {
        if (format.inherit && !styles.direct_tabs(all[i].node))
          continue;
        auto current = styles.tab_set(*paragraphs.at(i));
        if (format.stops && same_tab_set(current, *format.stops))
          continue;
        all[i].node.setAttributeNS(
            TEXT, "text:style-name",
            styles.paragraph_style(all[i].node, nullptr, nullptr, &format,
                                   paragraphs.at(i)->tab_source));
      }
  }
  (void)read_text_flow(candidate);
  auto changed = candidate.xml.toByteArray() != before ||
                 !candidate.part_changes_from(package).empty();
  if (changed)
    package = std::move(candidate);
  return changed;
}
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
std::vector<WordsCharacterStyle>
words_character_styles(const OdfPackage &package) {
  auto copy = package;
  Catalogue styles(copy);
  std::vector<WordsCharacterStyle> result;
  for (const auto &[key, style] : styles.named())
    if (key.first == "text")
      result.push_back({key.second,
                        style.attributeNS(STYLE, "display-name", key.second),
                        style.attributeNS(STYLE, "parent-style-name"),
                        style.attributeNS(STYLE, "hidden") == "true" ||
                            style.attributeNS(STYLE, "hidden") == "1"});
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.label == b.label ? a.name < b.name : a.label < b.label;
  });
  return result;
}
Text define_words_character_style(OdfPackage &package,
                                  const WordsCharacterStyleChange &change) {
  validate_words_format(change.character);
  WordsParagraphStyleChange shared;
  shared.name = change.name;
  shared.label = change.label;
  shared.parent = change.parent;
  shared.character = change.character;
  Catalogue styles(package);
  return styles.define_named("text", shared);
}
bool delete_words_character_style(OdfPackage &package, const Text &name) {
  Catalogue styles(package);
  return styles.erase_named("text", name);
}
std::optional<Text> words_current_character_style(const OdfPackage &package,
                                                  size_t begin, size_t end) {
  auto all = blocks(package);
  check_range(all, begin, end);
  auto copy = package;
  Catalogue styles(copy);
  std::optional<Text> result;
  bool first = true, mixed = false, caret_found = false;
  std::function<void(XmlNode, std::optional<Text>, size_t &)> walk =
      [&](XmlNode parent, std::optional<Text> base, size_t &position) {
        for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
          if (begin == end && caret_found)
            return;
          auto name = n.attributeNS(TEXT, "style-name");
          auto next = base;
          if (!name.empty()) {
            auto own = styles.named_base("text", name);
            if (!own || !own->empty())
              next = own;
          }
          if (n.namespaceURI() == TEXT &&
              (n.localName() == "span" || n.localName() == "a" ||
               n.localName() == "meta"))
            walk(n, next, position);
          else {
            auto length = node_text(n).size();
            if (length &&
                (begin == end ? begin >= position && begin <= position + length
                              : position < end && position + length > begin)) {
              if (!next)
                mixed = true;
              if (first) {
                result = next;
                first = false;
              } else if (result != next)
                mixed = true;
              caret_found = true;
            }
            position += length;
          }
        }
      };
  for (const auto &block : all)
    if (touches(block, begin, end)) {
      auto position = block.start;
      walk(block.node, Text{}, position);
      if (begin == end && caret_found)
        break;
    }
  return mixed ? std::nullopt : first ? std::optional<Text>(Text{}) : result;
}
WordsFormatInfo words_character_style_format(const OdfPackage &package,
                                             const Text &name,
                                             std::optional<size_t> position) {
  auto copy = package;
  copy.xml = read_document_xml(package.xml.toByteArray());
  Catalogue styles(copy);
  (void)styles.named_chain("text", name);
  auto reference = styles.named_reference("text", name);
  auto all = blocks(copy);
  size_t start = 0;
  XmlElement paragraph;
  if (position) {
    check_range(all, *position, *position);
    for (const auto &block : all)
      if (touches(block, *position, *position)) {
        paragraph = block.node;
        start = block.start;
        break;
      }
  } else {
    auto body = copy.body();
    while (!body.firstChild().isNull())
      body.removeChild(body.firstChild());
  }
  if (paragraph.isNull()) {
    paragraph = copy.xml.createElementNS(TEXT, "text:p");
    copy.body().appendChild(paragraph);
  }
  while (!paragraph.firstChild().isNull())
    paragraph.removeChild(paragraph.firstChild());
  auto text = copy.xml.createTextNode("x");
  paragraph.appendChild(reference.empty() ? XmlNode(text)
                                          : styled_node(text, {reference}));
  return read_words_format(copy, start, start + 1);
}
bool apply_words_character_style(OdfPackage &package, size_t begin, size_t end,
                                 const Text &name) {
  auto all = blocks(package);
  check_range(all, begin, end);
  Catalogue styles(package);
  (void)styles.named_chain("text", name);
  if (begin == end)
    return false;
  auto before = package.xml.toByteArray();
  auto reference = styles.named_reference("text", name);
  for (const auto &block : all)
    if (block.start < end && block.start + block.text.size() > begin)
      assign_character_inlines(
          block.node, begin > block.start ? begin - block.start : 0,
          std::min(end - block.start, block.text.size()), reference);
  return package.xml.toByteArray() != before;
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
      auto name = styles.named_base("paragraph",
                                    block.node.attributeNS(TEXT, "style-name"));
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
  return styles.define_named("paragraph", change);
}
bool delete_words_paragraph_style(OdfPackage &package, const Text &name) {
  Catalogue styles(package);
  return styles.erase_named("paragraph", name);
}
Text words_paragraph_style_reference(OdfPackage &package, const Text &name) {
  Catalogue styles(package);
  return styles.named_reference("paragraph", name);
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
  (void)styles.named_chain("paragraph", name);
  auto body = copy.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  auto paragraph = copy.xml.createElementNS(TEXT, "text:p");
  if (!name.empty())
    paragraph.setAttributeNS(TEXT, "text:style-name",
                             styles.named_reference("paragraph", name));
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
    (void)styles.named_chain("paragraph", name);
    outline = styles.paragraph_outline(name, true);
  }
  auto before = package.xml.toByteArray();
  if (all.empty()) {
    auto paragraph = package.xml.createElementNS(TEXT, "text:p");
    package.body().appendChild(paragraph);
    all.push_back({paragraph, {}, 0});
  }
  auto reference = styles.named_reference("paragraph", name);
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
