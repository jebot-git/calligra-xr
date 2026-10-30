// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_page.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void check(const OdfPackage &p) {
  if (p.kind() != Kind::Words)
    fail("Page setup requires a Words document");
}
Text decimal(double value) {
  if (value == 0)
    return "0";
  std::array<char, 512> bytes;
  auto converted = std::to_chars(bytes.data(), bytes.data() + bytes.size(),
                                 value, std::chars_format::fixed);
  if (converted.ec != std::errc{})
    fail("Cannot represent page dimension");
  return Text(std::string(bytes.data(), converted.ptr));
}
struct Pages {
  OdfPackage &package;
  XmlDocument external;
  std::map<Text, XmlElement> masters, layouts;
  std::set<Text> names;
  explicit Pages(OdfPackage &p) : package(p) {
    auto scan = [&](const XmlDocument &doc) {
      for (auto node :
           doc.documentElement().elementsByTagNameNS(STYLE, "master-page"))
        masters[node.attributeNS(STYLE, "name")] = node;
      for (auto node :
           doc.documentElement().elementsByTagNameNS(STYLE, "page-layout"))
        layouts[node.attributeNS(STYLE, "name")] = node;
      std::function<void(XmlNode)> collect = [&](XmlNode node) {
        if (node.hasAttributeNS(STYLE, "name"))
          names.insert(node.attributeNS(STYLE, "name"));
        for (auto child = node.firstChild(); !child.isNull();
             child = child.nextSibling())
          collect(child);
      };
      collect(doc.documentElement());
    };
    auto bytes = p.entry("styles.xml");
    if (!bytes.empty()) {
      external = read_document_xml(bytes);
      scan(external);
    }
    scan(p.xml);
  }
  Text unique(const char *prefix) {
    size_t i = 1;
    Text name;
    do {
      name = Text(prefix) + Text::number(i++);
    } while (names.contains(name));
    names.insert(name);
    return name;
  }
  XmlElement group(const char *kind) {
    if (external.documentElement().isNull()) {
      external = read_document_xml(
          ("<office:document-styles xmlns:office=\"" + OFFICE + "\"/>")
              .toUtf8());
      external.documentElement().setAttributeNS(
          OFFICE, "office:version",
          package.xml.documentElement().attributeNS(OFFICE, "version", "1.3"));
    }
    auto root = external.documentElement();
    auto result = child(root, OFFICE, kind);
    if (result.isNull()) {
      result = external.createElementNS(OFFICE, Text("office:") + kind);
      auto before = Text(kind) == "automatic-styles"
                        ? child(root, OFFICE, "master-styles")
                        : XmlElement{};
      root.insertBefore(result, before);
    }
    return result;
  }
};
} // namespace
bool WordsPageFormat::empty() const {
  return !width && !height && !left && !right && !top && !bottom;
}
std::vector<WordsPageInfo> words_page_styles(const OdfPackage &package) {
  check(package);
  auto copy = package;
  Pages source(copy);
  auto flow = read_text_flow(package);
  std::vector<WordsPageInfo> result;
  for (const auto &[name, geometry] : flow.masters) {
    Text label = name.empty() ? Text("Default page") : name;
    if (source.masters.contains(name))
      label = source.masters.at(name).attributeNS(STYLE, "display-name", label);
    result.push_back({name, label, geometry});
  }
  return result;
}
WordsPageInfo words_page_style(const OdfPackage &package, const Text &name) {
  for (const auto &info : words_page_styles(package))
    if (info.name == name)
      return info;
  fail("The selected page style no longer exists");
}
bool format_words_page(OdfPackage &package, const Text &name,
                       const WordsPageFormat &format) {
  check(package);
  if (package.read_only())
    fail("This document is read-only");
  for (auto value : {format.width, format.height, format.left, format.right,
                     format.top, format.bottom})
    if (value && (!std::isfinite(*value) || *value < 0 || *value > 10000))
      fail("Page dimensions and margins must be between 0 and 10000 points");
  auto original = words_page_style(package, name).geometry;
  auto next = original;
  if (format.empty())
    return false;
  bool changed = false;
  for (auto [value, target] :
       std::initializer_list<std::pair<std::optional<double>, double *>>{
           {format.width, &next.width},
           {format.height, &next.height},
           {format.left, &next.left},
           {format.right, &next.right},
           {format.top, &next.top},
           {format.bottom, &next.bottom}})
    if (value && std::abs(*target - *value) > 1e-9) {
      *target = *value;
      changed = true;
    }
  if (next.width < 1 || next.height < 1 ||
      next.width - next.left - next.right < 1 ||
      next.height - next.top - next.bottom < 1)
    fail("Page margins must leave at least one point of printable space");
  if (!changed)
    return false;
  auto candidate = package;
  candidate.xml = read_document_xml(package.xml.toByteArray());
  Pages source(candidate);
  auto before_external = source.external.toByteArray();
  XmlElement master, layout;
  if (source.masters.contains(name))
    master = source.masters.at(name);
  else {
    if (!name.empty() || !source.masters.empty())
      fail("The selected page style no longer exists");
    auto group = source.group("master-styles");
    master = source.external.createElementNS(STYLE, "style:master-page");
    master.setAttributeNS(STYLE, "style:name", source.unique("WordsPage"));
    group.appendChild(master);
  }
  auto reference = master.attributeNS(STYLE, "page-layout-name");
  if (source.layouts.contains(reference))
    layout = source.layouts.at(reference);
  size_t uses = 0;
  for (const auto &[id, node] : source.masters)
    uses += node.attributeNS(STYLE, "page-layout-name") == reference;
  const bool new_layout = layout.isNull();
  if (new_layout || uses > 1) {
    auto group = source.group("automatic-styles");
    layout = new_layout
                 ? source.external.createElementNS(STYLE, "style:page-layout")
                 : source.external.importNode(layout, true).toElement();
    std::function<void(XmlNode)> strip = [&](XmlNode node) {
      node.removeAttributeNS(XML, "id");
      for (auto child = node.firstChild(); !child.isNull();
           child = child.nextSibling())
        strip(child);
    };
    strip(layout);
    reference = source.unique("WordsPageLayout");
    layout.setAttributeNS(STYLE, "style:name", reference);
    group.appendChild(layout);
    master.setAttributeNS(STYLE, "style:page-layout-name", reference);
  }
  auto props = child(layout, STYLE, "page-layout-properties");
  if (props.isNull()) {
    props = layout.ownerDocument().createElementNS(
        STYLE, "style:page-layout-properties");
    layout.insertBefore(props, layout.firstChild());
  }
  auto put = [&](const char *key, std::optional<double> requested,
                 double value) {
    if (requested || new_layout)
      props.setAttributeNS(FO, Text("fo:") + key, decimal(value) + "pt");
  };
  put("page-width", format.width, next.width);
  put("page-height", format.height, next.height);
  put("margin-left", format.left, next.left);
  put("margin-right", format.right, next.right);
  put("margin-top", format.top, next.top);
  put("margin-bottom", format.bottom, next.bottom);
  if (format.width || format.height || new_layout)
    props.setAttributeNS(STYLE, "style:print-orientation",
                         next.width > next.height ? "landscape" : "portrait");
  if (source.external.toByteArray() != before_external)
    candidate.set_entry("styles.xml", source.external.toByteArray(),
                        "text/xml");
  (void)read_text_flow(candidate);
  package = std::move(candidate);
  return true;
}
} // namespace DomeOffice
