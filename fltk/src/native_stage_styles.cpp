// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_styles.h"
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text NUMBER = "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
Text local(const XmlNode::Attribute &a) {
  return a.name.mid(a.name.indexOf(':') + 1);
}
std::vector<Text> tokens(const Text &text) {
  std::vector<Text> values;
  std::istringstream input(text.toStdString());
  for (std::string token; input >> token;) {
    if (values.size() >= 100000)
      fail("Too many clipboard style class references");
    values.emplace_back(token);
  }
  return values;
}
} // namespace
NativeStageStyleCatalog::NativeStageStyleCatalog(const OdfPackage &package) {
  documents[0] = package.xml;
  if (!package.entry("styles.xml").empty())
    documents[1] = read_document_xml(package.entry("styles.xml"));
  index(1);
  index(0);
}
std::optional<NativeStageStyleCatalog::Key>
NativeStageStyleCatalog::key(XmlElement node) {
  auto uri = node.namespaceURI(), name = node.localName();
  if (uri == STYLE && (name == "style" || name == "default-style"))
    return Key{name, node.attributeNS(STYLE, "family"),
               node.attributeNS(STYLE, "name")};
  if (uri == STYLE && name == "font-face")
    return Key{"font", {}, node.attributeNS(STYLE, "name")};
  if (uri == TEXT && name == "list-style")
    return Key{"list", {}, node.attributeNS(STYLE, "name")};
  if (uri == NUMBER && name.endsWith("-style"))
    return Key{"data", {}, node.attributeNS(STYLE, "name")};
  if (uri == TABLE && name == "table-template")
    return Key{"table-template", {}, node.attributeNS(TABLE, "name")};
  if (uri == DRAW && TextList{"gradient", "hatch", "fill-image", "opacity",
                              "stroke-dash", "marker"}
                         .contains(name))
    return Key{name, {}, node.attributeNS(DRAW, "name")};
  return {};
}
void NativeStageStyleCatalog::index(int source) {
  for (auto group : children(documents[source].documentElement(), OFFICE)) {
    auto name = group.localName();
    if (name != "styles" && name != "automatic-styles" &&
        name != "font-face-decls")
      continue;
    for (auto node = group.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement()) {
        auto k = key(node);
        if (!k)
          continue;
        if (definitions.size() >= 100000)
          fail("Clipboard source style inventory exceeds the limit");
        auto id = definitions.size();
        definitions.push_back({node, *k, source, name});
        if (std::get<0>(*k) == "default-style")
          defaults[std::get<1>(*k)] = id;
        else if (name == "font-face-decls")
          fonts[source][*k] = id;
        else if (name == "automatic-styles")
          automatic[source][*k] = id;
        else
          common[*k] = id;
      }
  }
}
size_t NativeStageStyleCatalog::resolve(const Key &k, int source,
                                        bool common_only) const {
  if (source < 0 || source > 1 || std::get<2>(k).empty())
    fail("Invalid clipboard style reference");
  if (std::get<0>(k) == "font") {
    if (auto at = fonts[source].find(k); at != fonts[source].end())
      return at->second;
    if (source == 0)
      if (auto at = fonts[1].find(k); at != fonts[1].end())
        return at->second;
  } else {
    if (!common_only)
      if (auto at = automatic[source].find(k); at != automatic[source].end())
        return at->second;
    if (auto at = common.find(k); at != common.end())
      return at->second;
  }
  fail("A selected object has a missing style, font or drawing dependency");
}
Text native_stage_text_family(XmlElement node) {
  auto name = node.localName();
  if (name == "p" || name == "h")
    return "paragraph";
  if (name == "section" ||
      TextList{"table-of-content", "user-index", "alphabetical-index",
               "table-index", "object-index", "illustration-index",
               "bibliography"}
          .contains(name))
    return "section";
  return name == "ruby" ? Text("ruby") : Text("text");
}
Text native_stage_table_family(XmlElement node) {
  auto name = node.localName();
  if (name == "table")
    return "table";
  if (name == "table-row")
    return "table-row";
  if (name == "table-column")
    return "table-column";
  return "table-cell";
}
std::vector<NativeStageStyleReference>
native_stage_style_references(XmlElement node, const Text &context_family,
                              const Text &context_kind, bool common_context) {
  std::vector<NativeStageStyleReference> out;
  const auto name = node.localName();
  auto add = [&](const XmlNode::Attribute &a, Text kind, Text family = {},
                 bool common_only = false, bool list = false) {
    NativeStageStyleReference ref{a, {}, common_only || common_context};
    for (auto value : list ? tokens(a.value) : std::vector<Text>{a.value})
      if (!value.empty())
        ref.keys.emplace_back(kind, family, value);
    if (!ref.keys.empty())
      out.push_back(std::move(ref));
  };
  for (const auto &a : node.attributes()) {
    const auto attr = local(a);
    if (a.uri == DRAW && (attr == "style-name" || attr == "class-names"))
      add(a, "style", name == "page" ? Text("drawing-page") : Text("graphic"),
          attr == "class-names", true);
    else if (a.uri == DRAW && attr == "text-style-name")
      add(a, "style", "paragraph");
    else if (a.uri == PRESENTATION &&
             (attr == "style-name" || attr == "class-names"))
      add(a, "style", "presentation", attr == "class-names", true);
    else if (a.uri == TEXT && (attr == "style-name" || attr == "class-names")) {
      if (name == "list" || name == "numbered-paragraph")
        add(a, "list");
      else
        add(a, "style", native_stage_text_family(node), attr == "class-names",
            true);
    } else if (a.uri == TABLE &&
               (attr == "style-name" || attr == "default-cell-style-name" ||
                attr == "paragraph-style-name"))
      add(a, "style",
          attr == "default-cell-style-name" ? Text("table-cell")
          : attr == "paragraph-style-name"  ? Text("paragraph")
                                            : native_stage_table_family(node));
    else if (a.uri == TABLE && attr == "template-name")
      add(a, "table-template");
    else if (a.uri == STYLE &&
             (attr == "parent-style-name" || attr == "next-style-name") &&
             !context_family.empty())
      add(a, "style", context_family, true);
    else if (a.uri == STYLE && (attr == "data-style-name" ||
                                attr == "percentage-data-style-name"))
      add(a, "data");
    else if (a.uri == STYLE && attr == "list-style-name")
      add(a, "list");
    else if (a.uri == STYLE && attr == "leader-text-style")
      add(a, "style", "text");
    else if (a.uri == STYLE && attr == "apply-style-name")
      add(a, context_kind == "data" ? Text("data") : Text("style"),
          context_family);
    else if (a.uri == STYLE &&
             (attr == "font-name" || attr == "font-name-asian" ||
              attr == "font-name-complex"))
      add(a, "font");
    else if (a.uri == DRAW) {
      Text kind;
      if (attr == "fill-gradient-name")
        kind = "gradient";
      else if (attr == "fill-hatch-name")
        kind = "hatch";
      else if (attr == "fill-image-name")
        kind = "fill-image";
      else if (attr == "opacity-name")
        kind = "opacity";
      else if (attr == "stroke-dash")
        kind = "stroke-dash";
      else if (attr == "marker-start" || attr == "marker-end")
        kind = "marker";
      if (!kind.empty())
        add(a, kind);
    }
  }
  return out;
}
} // namespace DomeOffice
