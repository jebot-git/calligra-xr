// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_import.h"
#include "native_stage_names.h"
#include "native_stage_styles.h"
#include "native_stage_identity.h"
#include "native_stage_timing.h"
#include "native_image.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
Text local(const XmlNode::Attribute &a) {
  return a.name.mid(a.name.indexOf(':') + 1);
}
void walk(XmlElement root, const std::function<void(XmlElement)> &visit) {
  size_t count = 0;
  auto next = [&](auto &&self, XmlElement node, int depth) -> void {
    if (++count > 1000000 || depth > 256)
      fail("Clipboard import traversal exceeds the limit");
    if (node.isElement())
      visit(node);
    for (auto child = node.firstChild(); !child.isNull();
         child = child.nextSibling())
      self(self, child, depth + 1);
  };
  next(next, root, 0);
}
TextList tokens(const Text &value) {
  TextList out;
  std::istringstream stream(value.toStdString());
  for (std::string token; stream >> token;) {
    if (out.size() >= 100000)
      fail("Clipboard reference list exceeds the limit");
    out.push_back(token);
  }
  return out;
}

Text encode_path(const Text &path) {
  const char hex[] = "0123456789ABCDEF";
  Bytes out;
  for (unsigned char ch : path.toUtf8()) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '/' || ch == '-' || ch == '_' ||
        ch == '.' || ch == '~')
      out.push_back(char(ch));
    else {
      out.push_back('%');
      out.push_back(hex[ch >> 4]);
      out.push_back(hex[ch & 15]);
    }
  }
  return Text::fromUtf8(out);
}
struct Target {
  const void *node;
  Text name;
};
using Targets = std::map<Text, std::vector<Target>>;
struct Remap {
  NativeStageFragment fragment;
  OdfPackage source{Kind::Stage};
  std::unique_ptr<NativeStageStyleCatalog> catalog;
  NativeStageNames names;
  Text prefix;
  std::map<size_t, Text> styles;
  std::map<Text, Text> layers, media;
  Targets ids, draw_names, text_names, section_names, table_names;
  std::map<const void *, std::map<Text, Text>> definitions;
  XmlElement page;
  NativeStageTimingBudget timing_budget;
  Remap(const OdfPackage &target, const Bytes &wire)
      : fragment(decode_stage_fragment(wire)), names(target) {
    source.load_bytes(fragment.archive);
    names.reserve(source);
    catalog = std::make_unique<NativeStageStyleCatalog>(source);
    page = children(source.body(), DRAW, "page").at(0);
    prefix = "Objects/" + names.take() + "/";
    auto manifest = read_document_xml(source.entry("META-INF/manifest.xml"));
    for (auto node :
         children(manifest.documentElement(), MANIFEST, "file-entry"))
      media[node.attributeNS(MANIFEST, "full-path")] =
          node.attributeNS(MANIFEST, "media-type");
    for (size_t i = 0; i < catalog->definitions.size(); ++i)
      if (std::get<0>(catalog->definitions[i].key) != "default-style")
        styles[i] = names.take();
    for (auto node : children(child(page, DRAW, "layer-set"), DRAW, "layer")) {
      auto old = node.attributeNS(DRAW, "name");
      if (!layers.emplace(old, names.take()).second)
        fail("Ambiguous imported layer definition");
    }
    collect();
  }
  Text identifier(XmlElement node, const Text &old) {
    auto &targets = ids[old];
    for (const auto &entry : targets)
      if (entry.node == node.identity())
        return entry.name;
    if (!targets.empty())
      fail("Duplicate imported XML/drawing identifier");
    auto name = names.take();
    targets.push_back({node.identity(), name});
    return name;
  }
  void collect() {
    auto visit = [&](XmlElement node) {
      for (const auto &a : node.attributes()) {
        const auto attr = local(a);
        if ((a.uri == XML ||
             (a.uri == DRAW && native_stage_object_identity(node))) &&
            attr == "id")
          definitions[node.identity()][a.uri + "|" + attr] =
              identifier(node, a.value);
        if (a.uri == DRAW && attr == "name" &&
            native_stage_object_identity(node)) {
          auto value = names.take();
          draw_names[a.value].push_back({node.identity(), value});
          definitions[node.identity()][a.uri + "|" + attr] = value;
        }
        if (a.uri == TABLE && attr == "formula")
          fail("Copying formula-bearing tables requires structural formula "
               "reference rewriting");
        if (a.uri == TABLE && attr == "name" && node.localName() == "table") {
          auto value = names.take();
          table_names[a.value].push_back({node.identity(), value});
          definitions[node.identity()][a.uri + "|" + attr] = value;
        }
        if (a.uri == TEXT && attr == "name" &&
            (native_stage_text_anchor(node) ||
             native_stage_section_target(node))) {
          if (native_stage_section_target(node) &&
              section_names.contains(a.value))
            fail("Duplicate imported section name");
          // Bookmark/reference-mark start/end pairs intentionally share a name.
          auto &targets =
              (native_stage_section_target(node) ? section_names
                                                 : text_names)[a.value];
          if (targets.empty())
            targets.push_back({node.identity(), names.take()});
          definitions[node.identity()][a.uri + "|" + attr] = targets[0].name;
        }
      }
    };
    walk(page, visit);
    // IDs on style/resource definitions share the outer document ID scope.
    for (const auto &d : catalog->definitions)
      walk(d.node, [&](XmlElement node) {
        for (const auto &a : node.attributes())
          if (a.uri == XML && local(a) == "id")
            definitions[node.identity()][XML + "|id"] =
                identifier(node, a.value);
      });
  }
  Text target(const Targets &map, const Text &old) const {
    auto found = map.find(old);
    if (found == map.end() || found->second.size() != 1)
      fail("Missing or ambiguous imported object reference");
    return found->second.front().name;
  }
  Text any_target(const Text &old) const {
    if (ids.contains(old))
      return target(ids, old);
    Text name;
    for (const auto *map :
         {&draw_names, &text_names, &section_names, &table_names})
      if (map->contains(old)) {
        auto current = target(*map, old);
        if (!name.empty() && name != current)
          fail("Ambiguous imported named target");
        name = current;
      }
    if (name.empty())
      fail("Imported reference has no selected target");
    return name;
  }
  Text resource(const Text &href) const {
    if (href.empty() || href.startsWith('#'))
      return href;
    auto hash = href.indexOf('#');
    auto part = hash < 0 ? href : href.left(hash);
    auto colon = part.indexOf(':'), slash = part.indexOf('/');
    if (colon >= 0 && (slash < 0 || colon < slash))
      return href;
    auto path = package_image_path(part);
    if (!path)
      fail("Invalid imported package resource URI");
    bool directory = !source.entry_data(*path) && media.contains(*path + "/");
    if (!source.entry_data(*path) && !directory)
      fail("Imported resource has no package part");
    return encode_path(prefix + *path) +
           (part.endsWith('/') ? Text("/") : Text()) +
           (hash < 0 ? Text() : href.mid(hash));
  }
  Text timing(const Text &value) {
    auto references = native_stage_timing_references(
        value, [&](const Text &id) { return ids.contains(id); },
        &timing_budget);
    Text out;
    size_t at = 0;
    for (const auto &ref : references) {
      out += value.mid(int(at), int(ref.begin - at));
      out += target(ids, ref.id);
      at = ref.begin + ref.size;
    }
    return out + value.mid(int(at));
  }
  void references(XmlElement root, int source_part, const Text &family = {},
                  const Text &kind = {}, bool common_context = false) {
    walk(root, [&](XmlElement node) {
      for (const auto &ref :
           native_stage_style_references(node, family, kind, common_context)) {
        TextList mapped;
        for (const auto &key : ref.keys)
          mapped.push_back(
              styles.at(catalog->resolve(key, source_part, ref.common_only)));
        node.setAttributeNS(ref.attribute.uri, ref.attribute.name,
                            mapped.join(' '));
      }
      for (const auto &a : node.attributes()) {
        const auto attr = local(a);
        if (auto at = definitions.find(node.identity());
            at != definitions.end())
          if (auto value = at->second.find(a.uri + "|" + attr);
              value != at->second.end()) {
            node.setAttributeNS(a.uri, a.name, value->second);
            continue;
          }
        if (a.uri == DRAW && attr == "layer") {
          if (!layers.contains(a.value))
            fail("Imported object has an undefined layer");
          node.setAttributeNS(a.uri, a.name, layers.at(a.value));
        } else if (a.uri == DRAW && attr == "name" &&
                   node.localName() == "layer")
          node.setAttributeNS(a.uri, a.name, layers.at(a.value));
        else if (a.uri == DRAW &&
                 TextList{"start-shape", "end-shape", "caption-id"}.contains(
                     attr))
          node.setAttributeNS(a.uri, a.name, target(ids, a.value));
        else if (a.uri == DRAW &&
                 TextList{"copy-of", "chain-next-name"}.contains(attr))
          node.setAttributeNS(a.uri, a.name,
                              draw_names.contains(a.value)
                                  ? target(draw_names, a.value)
                                  : target(ids, a.value));
        else if (a.uri == TEXT &&
                 (attr == "ref-name" || attr == "continue-list"))
          node.setAttributeNS(a.uri, a.name,
                              attr == "continue-list" ? target(ids, a.value)
                                                      : any_target(a.value));
        else if (a.uri == TEXT && attr == "section-name" &&
                 native_stage_local_section_source(node))
          node.setAttributeNS(a.uri, a.name, target(section_names, a.value));
        else if (a.uri == SMIL && attr == "targetElement")
          node.setAttributeNS(a.uri, a.name, target(ids, a.value));
        else if (a.uri == SMIL && (attr == "begin" || attr == "end"))
          node.setAttributeNS(a.uri, a.name, timing(a.value));
        else if (a.uri == DRAW && attr == "nav-order") {
          TextList order;
          for (const auto &id : tokens(a.value))
            order.push_back(target(ids, id));
          node.setAttributeNS(a.uri, a.name, order.join(' '));
        } else if (a.uri == XLINK && attr == "href")
          node.setAttributeNS(a.uri, a.name,
                              a.value.startsWith('#')
                                  ? "#" + any_target(Url::fromPercentEncoding(
                                              a.value.mid(1).toUtf8()))
                                  : resource(a.value));
      }
    });
  }
  NativeStageFragment result() {
    for (size_t i = 0; i < catalog->definitions.size(); ++i) {
      auto &d = catalog->definitions[i];
      auto [kind, family, old] = d.key;
      references(d.node, d.source, family, kind, d.group == "styles");
      if (kind != "default-style") {
        const Text uri = kind == "table-template" ? TABLE
                         : TextList{"gradient", "hatch",       "fill-image",
                                    "opacity",  "stroke-dash", "marker"}
                                 .contains(kind)
                             ? DRAW
                             : STYLE;
        d.node.setAttributeNS(uri,
                              (uri == DRAW    ? Text("draw:")
                               : uri == TABLE ? Text("table:")
                                              : Text("style:")) +
                                  "name",
                              styles.at(i));
      }
    }
    references(page, 0);
    OdfPackage out(Kind::Stage);
    out.xml = source.xml;
    out.set_entry("styles.xml", catalog->documents[1].toByteArray(),
                  "text/xml");
    for (const auto &name : source.entry_names()) {
      if (TextList{"mimetype", "content.xml", "styles.xml",
                   "META-INF/manifest.xml"}
              .contains(name))
        continue;
      out.set_entry(prefix + name, source.entry(name),
                    media.contains(name) ? media.at(name)
                                         : Text("application/octet-stream"));
    }
    out.validate_archive_limits();
    return {out.archive_bytes(), fragment.text, fragment.selection};
  }
};
} // namespace
NativeStageFragment
remap_stage_fragment_for_destination(const OdfPackage &target,
                                     const Bytes &wire) {
  if (target.kind() != Kind::Stage)
    fail("Paste requires a presentation destination");
  return Remap(target, wire).result();
}
} // namespace DomeOffice
