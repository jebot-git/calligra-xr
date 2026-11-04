// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_merge.h"
#include "native_stage_order.h"
#include "native_stage_styles.h"
#include "native_stage_repeat.h"
#include <limits>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
int rank(const Text &name) {
  const TextList names{"scripts",          "font-face-decls", "styles",
                       "automatic-styles", "master-styles",   "body"};
  auto at = std::find(names.begin(), names.end(), name);
  return at == names.end() ? -1 : int(at - names.begin());
}
XmlElement group(XmlDocument doc, const Text &name) {
  auto root = doc.documentElement();
  auto found = children(root, OFFICE, name);
  if (found.size() > 1)
    fail("Ambiguous receiving style declaration group");
  if (!found.empty())
    return found.front();
  auto result = doc.createElementNS(OFFICE, "office:" + name);
  for (auto node : children(root, OFFICE))
    if (rank(node.localName()) > rank(name)) {
      root.insertBefore(result, node);
      return result;
    }
  root.appendChild(result);
  return result;
}
struct Assembly {
  NativeStageMergeDraft draft;
  XmlDocument external;
  bool styles_dirty = false;
  size_t elements = 0;
  XmlElement page, source_page;
  Assembly(const OdfPackage &target, int slide, const Bytes &wire) {
    if (target.kind() != Kind::Stage || target.read_only())
      fail("Clipboard assembly requires a writable presentation");
    const auto pages = children(target.body(), DRAW, "page");
    if (slide < 0 || size_t(slide) >= pages.size())
      fail("Clipboard destination slide is out of range");
    auto expanded = materialize_stage_repeated_cells(wire);
    auto fragment = remap_stage_fragment_for_destination(
        target, encode_stage_fragment(expanded));
    draft.source.load_bytes(fragment.archive);
    draft.package = target;
    draft.package.xml = read_document_xml(target.xml.toByteArray());
    page = children(draft.package.body(), DRAW, "page").at(slide);
    source_page = children(draft.source.body(), DRAW, "page").at(0);
    if (!target.entry("styles.xml").empty())
      external = read_document_xml(target.entry("styles.xml"));
    else
      external = read_document_xml(
          ("<office:document-styles xmlns:office=\"" + OFFICE +
           "\" office:version=\"" +
           target.xml.documentElement().attributeNS(OFFICE, "version", "1.2") +
           "\"/>")
              .toUtf8());
    definitions();
    assets();
    layers();
    objects(fragment.selection);
    if (styles_dirty)
      draft.package.set_entry("styles.xml", external.toByteArray(), "text/xml");
    draft.package.validate_archive_limits();
  }
  void pair(XmlElement original, XmlElement copy, int depth = 0) {
    if (++elements > 1000000 || depth > 256)
      fail("Clipboard destination mapping exceeds the limit");
    if (!draft.imported.emplace(original.identity(), copy).second)
      fail("An imported source element was assembled twice");
    auto a = original.firstChild(), b = copy.firstChild();
    while (!a.isNull() && !b.isNull()) {
      if (a.isElement())
        pair(a.toElement(), b.toElement(), depth + 1);
      a = a.nextSibling();
      b = b.nextSibling();
    }
    if (!a.isNull() || !b.isNull())
      fail("Imported source mapping is inconsistent");
  }
  XmlElement append(XmlElement original, XmlElement destination) {
    auto copy =
        destination.ownerDocument().importNode(original, true).toElement();
    destination.appendChild(copy);
    pair(original, copy);
    return copy;
  }
  void definitions() {
    NativeStageStyleCatalog catalog(draft.source);
    draft.source_styles = catalog.documents[1];
    for (const auto &definition : catalog.definitions) {
      const auto [kind, family, name] = definition.key;
      if (kind == "default-style")
        continue;
      // Remapping makes both font scopes unique. Moving them to styles.xml
      // gives content and common styles the same reachable definitions.
      const bool content =
          definition.source == 0 && definition.group == "automatic-styles";
      auto doc = content ? draft.package.xml : external;
      const Text container =
          kind == "font" ? Text("font-face-decls") : definition.group;
      append(definition.node, group(doc, container));
      if (!content)
        styles_dirty = true;
    }
  }
  void assets() {
    auto manifest =
        read_document_xml(draft.source.entry("META-INF/manifest.xml"));
    std::map<Text, Text> media;
    for (auto node :
         children(manifest.documentElement(), MANIFEST, "file-entry"))
      media[node.attributeNS(MANIFEST, "full-path")] =
          node.attributeNS(MANIFEST, "media-type");
    for (const auto &name : draft.source.entry_names()) {
      if (TextList{"mimetype", "content.xml", "styles.xml",
                   "META-INF/manifest.xml"}
              .contains(name))
        continue;
      if (draft.package.entry_data(name))
        fail("Imported asset collides with a receiving package part");
      draft.package.set_entry(name, draft.source.entry(name),
                              media.contains(name)
                                  ? media.at(name)
                                  : Text("application/octet-stream"));
    }
  }
  void layers() {
    auto incoming = child(source_page, DRAW, "layer-set");
    if (children(incoming, DRAW, "layer").empty())
      return;
    auto sets = children(page, DRAW, "layer-set");
    if (sets.size() > 1)
      fail("Ambiguous receiving page layer set");
    XmlElement local;
    if (!sets.empty())
      local = sets.front();
    else {
      // Adding fresh document definitions preserves page inheritance and all
      // old master/page scopes. Name reservation covers unresolved references
      // as well, so this cannot heal another object's missing layer.
      XmlElement global_parent;
      for (auto doc : {external, draft.package.xml}) {
        auto parent = child(doc.documentElement(), OFFICE, "master-styles");
        auto found = children(parent, DRAW, "layer-set");
        if (found.size() > 1)
          fail("Ambiguous receiving document layer set");
        if (!found.empty()) {
          global_parent = parent;
          local = found.front();
        }
      }
      if (local.isNull()) {
        global_parent = group(external, "master-styles");
        local = external.createElementNS(DRAW, "draw:layer-set");
        global_parent.insertBefore(local, global_parent.firstChild());
      }
      if (local.ownerDocument().documentElement() == external.documentElement())
        styles_dirty = true;
    }
    std::set<Text> names;
    for (auto layer : children(local, DRAW, "layer"))
      if (!names.insert(layer.attributeNS(DRAW, "name")).second)
        fail("Ambiguous receiving layer name");
    for (auto layer : children(incoming, DRAW, "layer")) {
      if (names.size() >= 100000 ||
          !names.insert(layer.attributeNS(DRAW, "name")).second)
        fail("Imported layers collide or exceed the limit");
      append(layer, local);
    }
  }
  void objects(const std::vector<NativeStageObjectPath> &selection) {
    auto existing = native_stacking_children(page);
    auto incoming = native_stacking_children(source_page);
    long long highest = existing.empty() ? -1 : existing.back().z;
    if (highest + static_cast<long long>(incoming.size()) >
        std::numeric_limits<int>::max())
      fail("No front stacking index is available without changing existing "
           "objects");
    // Keep physical source order (navigation/opaque content) independently of
    // visual z-order. All old indices, including protected labels, stay exact.
    std::map<const void *, int> indices;
    for (const auto &item : incoming)
      indices.emplace(item.node.identity(), int(++highest));
    for (auto node : children(source_page, DRAW)) {
      if (node.localName() == "layer-set")
        continue;
      if (!indices.contains(node.identity()))
        fail("Imported root has no stacking context");
      auto copy = append(node, page);
      copy.setAttributeNS(DRAW, "draw:z-index",
                          Text::number(indices.at(node.identity())));
      draft.roots.push_back(copy);
    }
    if (draft.roots.size() != incoming.size())
      fail("Imported root has an unsupported page scope");
    for (auto selected : stage_fragment_nodes(draft.source, selection))
      draft.selected.push_back(draft.candidate_node(selected));
  }
};
} // namespace
XmlElement NativeStageMergeDraft::candidate_node(XmlElement original) const {
  auto found = imported.find(original.identity());
  if (found == imported.end())
    fail("The element has no receiving import counterpart");
  return found->second;
}
NativeStageMergeDraft prepare_stage_merge(const OdfPackage &target, int slide,
                                          const Bytes &wire) {
  return std::move(Assembly(target, slide, wire).draft);
}
} // namespace DomeOffice
