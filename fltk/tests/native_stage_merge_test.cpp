// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment_fixture.h"
#include "native_stage_merge.h"
#include "native_stage_order.h"
#include "native_stage_styles.h"
#include <filesystem>
namespace {
std::optional<Text> documents;
Text freeze(XmlElement node) {
  TextList attrs;
  for (const auto &a : node.attributes())
    attrs.push_back(a.uri + "|" + a.name.mid(a.name.indexOf(':') + 1) + "=" +
                    a.value);
  std::sort(attrs.begin(), attrs.end());
  Text out = node.namespaceURI() + "|" + node.localName() + "[" +
             attrs.join(';') + "]";
  for (auto child = node.firstChild(); !child.isNull();
       child = child.nextSibling())
    out += child.isElement() ? freeze(child.toElement()) : child.nodeValue();
  return out;
}
NativeStageFragment fragment(const OdfPackage &p, int parents = 0) {
  auto scenes = read_presentation_scenes(p);
  for (size_t i = 0; i < scenes[0].shapes.size(); ++i)
    if (scenes[0].shapes[i].name == "X")
      return native_stage_fragment(p, scenes[0], {{int(i), parents}});
  std::abort();
}
XmlElement page(OdfPackage &p, int index = 0) {
  return children(p.body(), DRAW, "page").at(index);
}
void same_parts(const OdfPackage &a, const OdfPackage &b) {
  assert(a.entry_names() == b.entry_names());
  for (const auto &name : a.entry_names())
    assert(a.entry(name) == b.entry(name));
}
void definitions(const OdfPackage &before, const NativeStageMergeDraft &draft) {
  NativeStageStyleCatalog a(before), b(draft.package);
  for (const auto &old : a.definitions) {
    auto found = std::find_if(
        b.definitions.begin(), b.definitions.end(), [&](const auto &d) {
          return d.key == old.key && d.source == old.source &&
                 d.group == old.group;
        });
    assert(found != b.definitions.end() &&
           freeze(found->node) == freeze(old.node));
  }
  NativeStageStyleCatalog imported(draft.source);
  for (const auto &entry : imported.definitions) {
    auto kind = std::get<0>(entry.key);
    if (kind == "default-style")
      continue;
    const int expected_source =
        kind == "font" || entry.group == "styles" ? 1 : entry.source;
    auto found = std::find_if(
        b.definitions.begin(), b.definitions.end(), [&](const auto &d) {
          return d.key == entry.key && d.source == expected_source;
        });
    assert(found != b.definitions.end() &&
           freeze(found->node) == freeze(entry.node));
  }
  // Stored source declaration identities remain alive after the assembler and
  // its temporary catalog have been destroyed.
  for (auto node :
       children(child(draft.source_styles.documentElement(), OFFICE, "styles"),
                STYLE, "style"))
    assert(freeze(draft.candidate_node(node)) == freeze(node));
}
OdfPackage assemble(bool local_empty = false, bool master_local = false) {
  auto original = source();
  auto source_faces = element(original.xml, OFFICE, "office:font-face-decls");
  original.xml.documentElement().insertBefore(
      source_faces, original.xml.documentElement().firstChild());
  auto face = element(original.xml, STYLE, "style:font-face", source_faces);
  face.setAttributeNS(STYLE, "style:name", "DocBitmap");
  face.setAttributeNS(SVG, "svg:font-family", "monospace");
  for (auto s : children(
           child(original.xml.documentElement(), OFFICE, "automatic-styles"),
           STYLE, "style"))
    if (s.attributeNS(STYLE, "name") == "P")
      child(s, STYLE, "text-properties")
          .setAttributeNS(STYLE, "style:font-name", "DocBitmap");
  auto target = source();
  for (auto name : {Text("X"), Text("N2")})
    named(target.xml, name)
        .setAttributeNS(DRAW, "draw:transform",
                        "matrix(.8 .2 -.1 1.1 0 0) translate(250pt 170pt)");
  for (auto frame : target.xml.elementsByTagNameNS(DRAW, "frame"))
    frame.setAttributeNS(DRAW, "draw:transform",
                         "matrix(.8 .2 -.1 1.1 0 0) translate(250pt 170pt)");
  named(target.xml, "A").setAttributeNS(XML, "xml:id", "NativePaste1");
  page(target).setAttributeNS(DRAW, "draw:nav-order",
                              "NativePaste1 b x y n1 n2 c");
  target.set_entry("opaque/NativePaste2", "reserved imported prefix",
                   "application/octet-stream");
  target.set_entry(
      "Pictures/image.svg",
      "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'><rect "
      "width='20' height='10' fill='blue'/></svg>",
      "image/svg+xml");
  auto external = read_document_xml(target.entry("styles.xml"));
  auto master_styles =
      child(external.documentElement(), OFFICE, "master-styles");
  if (master_local) {
    auto master = children(master_styles, STYLE, "master-page").front();
    auto set = element(external, DRAW, "draw:layer-set", master);
    auto layer = element(external, DRAW, "draw:layer", set);
    layer.setAttributeNS(DRAW, "draw:name", "Art");
    layer.setAttributeNS(DRAW, "draw:display", "none");
  }
  auto old_art =
      children(child(master_styles, DRAW, "layer-set"), DRAW, "layer").front();
  old_art.setAttributeNS(DRAW, "draw:protected", "true");
  target.set_entry("styles.xml", external.toByteArray(), "text/xml");
  if (local_empty) {
    auto set = element(target.xml, DRAW, "draw:layer-set");
    auto target_page = page(target);
    target_page.insertBefore(set, target_page.firstChild());
  }
  // Receiving visual order differs from physical source order. A missing
  // reference remains missing after new layer definitions have been added.
  auto old_a = named(target.xml, "A");
  old_a.setAttributeNS(DRAW, "draw:z-index", "00027");
  named(target.xml, "C").setAttributeNS(DRAW, "draw:layer", "missing");
  const auto xml = target.xml.toByteArray(),
             source_xml = original.xml.toByteArray();
  const auto styles = target.entry("styles.xml"),
             source_styles = original.entry("styles.xml");
  std::vector<Text> old_nodes;
  for (auto node : children(page(target), DRAW))
    old_nodes.push_back(freeze(node));
  auto old_second = freeze(page(target, 1));
  auto old_model = read_presentation_scenes(target);
  auto old_print = read_presentation_scenes(
      target, std::make_shared<OdfTextReader>(target), SceneTarget::Print);
  const auto selected = fragment(original, 1);
  const Text case_name = local_empty    ? Text("empty-local")
                         : master_local ? Text("master")
                                        : Text("global");
  if (documents) {
    target.save(*documents + "/" + case_name + "-before.odp");
    original.save(*documents + "/" + case_name + "-source.odp");
  }
  auto draft = prepare_stage_merge(target, 0, encode_stage_fragment(selected));
  assert(target.xml.toByteArray() == xml &&
         target.entry("styles.xml") == styles);
  assert(original.xml.toByteArray() == source_xml &&
         original.entry("styles.xml") == source_styles);
  assert(draft.roots.size() == 1 && draft.selected.size() == 1 &&
         draft.selected.front().localName() == "g");
  auto merged_page = page(draft.package);
  auto old_iter = old_nodes.begin();
  for (auto node : children(merged_page, DRAW)) {
    if (node == draft.roots.front())
      continue;
    if (!local_empty && node.localName() == "layer-set")
      continue;
    if (local_empty && node.localName() == "layer-set") {
      assert(!old_iter->empty());
      ++old_iter;
      continue;
    }
    assert(old_iter != old_nodes.end() && freeze(node) == *old_iter++);
  }
  assert(old_iter == old_nodes.end());
  assert(freeze(page(draft.package, 1)) == old_second);
  definitions(target, draft);
  for (const auto &name : target.entry_names())
    if (!TextList{"styles.xml", "META-INF/manifest.xml"}.contains(name))
      assert(target.entry(name) == draft.package.entry(name));
  auto scenes = read_presentation_scenes(draft.package);
  const auto remapped_source = read_presentation_scenes(draft.source);
  for (const auto &incoming : remapped_source[0].shapes) {
    auto counterpart = draft.candidate_node(incoming.source);
    auto found =
        std::find_if(scenes[0].shapes.begin(), scenes[0].shapes.end(),
                     [&](const auto &s) { return s.source == counterpart; });
    assert(found != scenes[0].shapes.end());
    assert(found->paint.font == incoming.paint.font &&
           found->paint.fill_color == incoming.paint.fill_color);
    assert(found->text == incoming.text && found->image == incoming.image &&
           found->transform.e == incoming.transform.e);
    assert(found->text_flow.paragraphs.size() ==
           incoming.text_flow.paragraphs.size());
    for (size_t i = 0; i < incoming.text_flow.paragraphs.size(); ++i) {
      auto &a = incoming.text_flow.paragraphs[i];
      auto &b = found->text_flow.paragraphs[i];
      assert(a.font == b.font && a.color == b.color &&
             a.spans.size() == b.spans.size());
      for (size_t j = 0; j < a.spans.size(); ++j)
        assert(a.spans[j].font == b.spans[j].font &&
               a.spans[j].color == b.spans[j].color);
    }
    if (!incoming.image.empty())
      assert(draft.source.entry(incoming.image) ==
             draft.package.entry(incoming.image));
  }
  auto print = read_presentation_scenes(
      draft.package, std::make_shared<OdfTextReader>(draft.package),
      SceneTarget::Print);
  for (size_t s = 0; s < old_model.size(); ++s)
    for (const auto &old : old_model[s].shapes) {
      const auto found =
          std::find_if(scenes[s].shapes.begin(), scenes[s].shapes.end(),
                       [&](const auto &x) { return x.name == old.name; });
      assert(found != scenes[s].shapes.end());
      assert(found->layer_protected == old.layer_protected &&
             found->text == old.text &&
             found->paint.fill_color == old.paint.fill_color);
      assert(found->transform.e == old.transform.e &&
             found->transform.a == old.transform.a);
    }
  assert(scenes[0].shapes.size() ==
         old_model[0].shapes.size() +
             2); // X and image; hidden members retained.
  assert(print[0].shapes.size() ==
         old_print[0].shapes.size() + 3); // Includes printer-only line.
  assert(scenes[0].layers.missing(named(draft.package.xml, "C")));
  auto items = native_stacking_children(merged_page);
  assert(items.back().node == draft.roots[0] && items.back().z == 28);
  assert(named(draft.package.xml, "A").attributeNS(DRAW, "z-index") == "00027");
  auto layers = children(child(merged_page, DRAW, "layer-set"), DRAW, "layer");
  assert(layers.size() == (local_empty ? 3 : 7));
  auto changes = draft.package.part_changes_from(target);
  OdfPackage replay = draft.package;
  replay.xml = read_document_xml(xml);
  replay.replay_part_changes(changes, false);
  same_parts(replay, target);
  replay.replay_part_changes(changes, true);
  same_parts(replay, draft.package);
  return draft.package;
}
void selected_context() {
  auto p = source();
  auto selected = fragment(p);
  auto d = prepare_stage_merge(p, 1, encode_stage_fragment(selected));
  assert(d.selected.size() == 1 && d.selected[0].localName() == "rect");
  assert(d.roots.size() == 1 && d.roots[0].localName() == "g" &&
         d.selected[0] != d.roots[0]);
  auto first = page(d.package);
  assert(freeze(first) == freeze(page(p)));
  auto bytes = d.package.archive_bytes();
  OdfPackage reopened(Kind::Stage);
  reopened.load_bytes(bytes);
  assert(read_presentation_scenes(reopened)[1].shapes.size() == 2);
  rejects([&] { d.candidate_node(named(p.xml, "X")); });
}
void separate_roots() {
  auto target = source();
  auto incoming = source();
  named(incoming.xml, "A").setAttributeNS(DRAW, "draw:z-index", "26");
  auto scenes = read_presentation_scenes(incoming);
  std::vector<NativeObjectRef> refs;
  for (size_t i = 0; i < scenes[0].shapes.size(); ++i)
    if (scenes[0].shapes[i].name == "A" || scenes[0].shapes[i].name == "X")
      refs.push_back({int(i), 0});
  auto wire =
      encode_stage_fragment(native_stage_fragment(incoming, scenes[0], refs));
  auto draft = prepare_stage_merge(target, 0, wire);
  assert(draft.roots.size() == 2 && draft.selected.size() == 2);
  assert(draft.roots[0].localName() == "rect" &&
         draft.roots[1].localName() == "g");
  assert(draft.roots[0].attributeNS(DRAW, "z-index") == "5");
  assert(draft.roots[1].attributeNS(DRAW, "z-index") == "4");
  assert(draft.selected[0] != draft.roots[1] &&
         draft.selected[1] != draft.roots[1]);
}
void bounds() {
  auto p = source();
  auto wire = encode_stage_fragment(fragment(p));
  rejects([&] { prepare_stage_merge(p, -1, wire); });
  rejects([&] { prepare_stage_merge(p, 2, wire); });
  rejects([&] { prepare_stage_merge(p, 0, "bad"); });
  OdfPackage words(Kind::Words);
  rejects([&] { prepare_stage_merge(words, 0, wire); });
  named(p.xml, "A").setAttributeNS(DRAW, "draw:z-index", "2147483647");
  auto xml = p.xml.toByteArray();
  auto styles = p.entry("styles.xml");
  rejects([&] { prepare_stage_merge(p, 0, wire); });
  assert(p.xml.toByteArray() == xml && p.entry("styles.xml") == styles);
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    documents = Text(argv[2]);
  }
  auto global = assemble();
  auto local = assemble(true);
  auto master = assemble(false, true);
  selected_context();
  separate_roots();
  bounds();
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    auto root = Text(argv[2]);
    global.save(root + "/global-merged.odp");
    local.save(root + "/empty-local-merged.odp");
    master.save(root + "/master-merged.odp");
  }
  std::cout << "PASS detached receiving graph/asset/layer/stack assembly, "
               "selected roots, exact old source and package replay; context "
               "isolation/publication still pending\n";
}
