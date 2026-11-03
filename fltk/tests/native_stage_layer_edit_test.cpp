// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_layers.h"
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
using Scope = NativeLayerScope;
Bytes source(const Presentation &p) {
  return p.scene(0).source.ownerDocument().toByteArray();
}
XmlElement set(XmlElement parent) {
  auto node = parent.ownerDocument().createElementNS(DRAW, "draw:layer-set");
  parent.insertBefore(node, parent.firstChild());
  return node;
}
void layer(XmlElement set, Text name, Text display = "always",
           bool locked = false) {
  auto node = set.ownerDocument().createElementNS(DRAW, "draw:layer");
  node.setAttributeNS(DRAW, "draw:name", name);
  node.setAttributeNS(DRAW, "draw:display", display);
  node.setAttributeNS(DRAW, "draw:protected", locked ? "true" : "false");
  set.appendChild(node);
}
XmlElement named(OdfPackage &p, Text name) {
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "rect"))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "frame"))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "line"))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  std::abort();
}
OdfPackage layered() {
  auto p = fixture();
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto masters = child(styles.documentElement(), OFFICE, "master-styles");
  auto global = set(masters);
  layer(global, "Base");
  layer(global, "Locked", "always", true);
  layer(global, "Secret", "none", true);
  layer(global, "Unused");
  for (auto name : {"B", "X", "Y", "N1", "C"})
    named(p, name).setAttributeNS(DRAW, "draw:layer", "Base");
  named(p, "A").setAttributeNS(DRAW, "draw:layer", "Locked");
  named(p, "N2").setAttributeNS(DRAW, "draw:layer", "Secret");
  auto second = children(p.body(), DRAW, "page")[1];
  layer(set(second), "Base", "screen");
  child(second, DRAW, "rect").setAttributeNS(DRAW, "draw:layer", "Base");
  auto master = child(masters, STYLE, "master-page");
  auto rect = styles.createElementNS(DRAW, "draw:rect");
  rect.setAttributeNS(DRAW, "draw:name", "M");
  rect.setAttributeNS(DRAW, "draw:layer", "Base");
  rect.setAttributeNS(SVG, "svg:width", "10pt");
  rect.setAttributeNS(SVG, "svg:height", "10pt");
  master.appendChild(rect);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  return p;
}
NativeLayerInfo info(NativeStageLayerEdit &edit, Scope scope, Text name) {
  for (auto layer : edit.layers(scope))
    if (layer.name == name)
      return layer;
  std::abort();
}
void lifecycle() {
  TempDir dir;
  auto input = dir.path() + "/input.odp", out = dir.path() + "/output.odp";
  auto package = layered();
  package.save(input);
  Presentation p;
  p.load(input);
  auto original = source(p), styles = p.image("styles.xml"),
       manifest = p.image("META-INF/manifest.xml");
  p.move_shape(0, find(p, "C"), {3, 4});
  p.undo();
  auto rev = p.revision();
  assert(p.can_redo());
  {
    auto edit = p.edit_layers(0, {ref(p, "B"), ref(p, "C")});
    assert(!edit.own_set(Scope::Slide));
    assert(info(edit, Scope::Document, "Base").uses == 6);
    edit.create(Scope::Document, "Discard");
  }
  assert(source(p) == original && p.revision() == rev && !p.modified() &&
         p.can_redo());
  auto noop = p.edit_layers(0);
  NativeLayerPatch same;
  same.display = SceneLayer::Always;
  same.protected_objects = true;
  noop.update(Scope::Document, "Locked", same);
  assert(!p.commit_layers(std::move(noop)));
  assert(p.can_redo() && p.revision() == rev);
  auto edit = p.edit_layers(0, {ref(p, "B"), ref(p, "C")});
  edit.create(Scope::Document, "New");
  NativeLayerPatch patch;
  patch.name = "Design / Česky & @";
  patch.title = "Title Č";
  patch.description = "First\nsecond\tline";
  patch.display = SceneLayer::Screen;
  edit.update(Scope::Document, "New", patch);
  edit.assign(*patch.name);
  edit.validate();
  auto at = p.commit_layers(std::move(edit), TextSelection{0, 1});
  assert(at && at->objects.size() == 2 && p.modified() && p.can_undo() &&
         !p.can_redo());
  assert(p.scene(0).shapes[find(p, "B")].source.attributeNS(DRAW, "layer") ==
         *patch.name);
  auto query = p.edit_layers(0);
  auto got = info(query, Scope::Document, *patch.name);
  assert(got.uses == 2 && got.title == *patch.title &&
         got.description == *patch.description);
  auto next = source(p), newstyles = p.image("styles.xml");
  p.save(out);
  assert(!p.modified());
  auto undone = p.undo();
  assert(undone && undone->objects.size() == 2 && source(p) == original &&
         p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest && p.modified());
  assert(!p.can_undo());
  p.redo();
  assert(source(p) == next && p.image("styles.xml") == newstyles &&
         !p.modified());
  Presentation reopened;
  reopened.load(out);
  assert(source(reopened) == next && reopened.image("styles.xml") == newstyles);
}
void scoping() {
  TempDir dir;
  auto path = dir.path() + "/input.odp";
  auto pack = layered();
  pack.save(path);
  Presentation p;
  p.load(path);
  const auto before = source(p), oldstyles = p.image("styles.xml");
  auto edit = p.edit_layers(0);
  NativeLayerPatch rename;
  rename.name = "Renamed";
  edit.update(Scope::Document, "Base", rename);
  rejects([&] { edit.update(Scope::Master, "Renamed", rename); });
  auto at = p.commit_layers(std::move(edit));
  assert(at && at->shape < 0);
  assert(p.scene(0).shapes[find(p, "B")].source.attributeNS(DRAW, "layer") ==
         "Renamed");
  auto second = p.scene(1).source;
  assert(child(second, DRAW, "rect").attributeNS(DRAW, "layer") == "Base");
  assert(p.image("styles.xml") != oldstyles);
  p.undo();
  assert(source(p) == before && p.image("styles.xml") == oldstyles);
  auto local = p.edit_layers(0, {ref(p, "A")});
  local.make_local(Scope::Slide);
  NativeLayerPatch unlock;
  unlock.protected_objects = false;
  local.update(Scope::Slide, "Locked", unlock);
  local.create(Scope::Slide, "Only here");
  local.assign(Text("Only here"));
  rejects([&] { local.inherit(Scope::Slide); });
  assert(local.own_set(Scope::Slide));
  local.assign(Text("Base"));
  local.inherit(Scope::Slide);
  assert(!local.own_set(Scope::Slide));
  p.commit_layers(std::move(local));
  assert(p.scene(0).shapes[find(p, "A")].source.attributeNS(DRAW, "layer") ==
         "Base");
  p.undo();
  assert(source(p) == before);
  auto master = p.edit_layers(0);
  master.make_local(Scope::Master);
  NativeLayerPatch hidden;
  hidden.display = SceneLayer::None;
  master.update(Scope::Master, "Base", hidden);
  p.commit_layers(std::move(master));
  for (auto shape : p.scene(0).shapes)
    assert(!shape.master);
  assert(p.scene(0).shapes[find(p, "B")].source.attributeNS(DRAW, "layer") ==
         "Base");
  p.undo();
  assert(source(p) == before && p.image("styles.xml") == oldstyles);
}
void protected_group_and_hidden_selection() {
  TempDir dir;
  auto path = dir.path() + "/input.odp";
  auto pack = layered();
  pack.save(path);
  Presentation p;
  p.load(path);
  auto before = source(p), oldstyles = p.image("styles.xml");
  const auto selected = ref(p, "X", 1);
  auto edit = p.edit_layers(0, {selected});
  rejects([&] { edit.assign(Text("Base")); });
  NativeLayerPatch unlock;
  unlock.protected_objects = false;
  edit.update(Scope::Document, "Secret", unlock);
  edit.create(Scope::Document, "Group layer");
  edit.assign(Text("Group layer"));
  auto at = p.commit_layers(std::move(edit));
  assert(at && at->objects.size() == 1 && at->objects[0].ancestors == 1);
  assert(find(p, "N2") >= 0);
  for (auto name : {"X", "Y", "N1", "N2"})
    assert(p.scene(0).shapes[find(p, name)].source.attributeNS(DRAW, "layer") ==
           "Group layer");
  auto group = native_object_source(p.scene(0), at->objects[0]);
  assert(!group.hasAttributeNS(DRAW, "layer"));
  p.undo();
  assert(source(p) == before && p.image("styles.xml") == oldstyles);
  auto hidden = p.edit_layers(0, {ref(p, "B"), ref(p, "C")});
  NativeLayerPatch hide;
  hide.display = SceneLayer::None;
  hidden.update(Scope::Document, "Base", hide);
  auto none = p.commit_layers(std::move(hidden));
  assert(none && none->shape == -1 && none->objects.empty());
  auto undo = p.undo();
  assert(undo && undo->objects.size() == 2 && source(p) == before);
  p.redo();
  assert(!p.redo());
  p.undo();
}
void missing_and_failures() {
  TempDir dir;
  auto path = dir.path() + "/input.odp";
  auto pack = layered();
  named(pack, "C").setAttributeNS(DRAW, "draw:layer", "Layer 1");
  pack.save(path);
  Presentation p;
  p.load(path);
  auto before = source(p), oldstyles = p.image("styles.xml");
  auto edit = p.edit_layers(0);
  assert(edit.new_name(Scope::Document) == "Layer 2");
  assert(edit.missing_layers(Scope::Document) == std::vector<Text>{"Layer 1"});
  rejects([&] { edit.create(Scope::Document, "Layer 1"); });
  rejects([&] { edit.remove(Scope::Document, "Base"); });
  NativeLayerPatch bad;
  bad.name = "Layer 1";
  rejects([&] { edit.update(Scope::Document, "Base", bad); });
  assert(edit.missing_layers(Scope::Document) == std::vector<Text>{"Layer 1"});
  edit.create(Scope::Document, "Layer 1", true);
  assert(edit.missing_layers(Scope::Document).empty());
  edit.remove(Scope::Document, "Unused");
  p.commit_layers(std::move(edit));
  p.undo();
  assert(source(p) == before && p.image("styles.xml") == oldstyles);
  auto stale = p.edit_layers(0);
  stale.create(Scope::Document, "Stale");
  p.move_shape(0, find(p, "C"), {1, 1});
  auto changed = source(p);
  rejects([&] { p.commit_layers(std::move(stale)); });
  assert(source(p) == changed);
  p.undo();
  Presentation origin, other;
  origin.load(path);
  other.load(path);
  assert(origin.revision() == other.revision());
  auto foreign = origin.edit_layers(0);
  foreign.create(Scope::Document, "Foreign");
  rejects([&] { other.commit_layers(std::move(foreign)); });
  assert(source(other) == before);
  auto invalid = layered();
  named(invalid, "N2").setAttributeNS(SVG, "svg:width", "nanpt");
  invalid.save(path);
  p.load(path);
  before = source(p);
  oldstyles = p.image("styles.xml");
  auto badprint = p.edit_layers(0);
  NativeLayerPatch print;
  print.display = SceneLayer::Printer;
  badprint.update(Scope::Document, "Secret", print);
  rejects([&] { badprint.validate(); });
  rejects([&] { p.commit_layers(std::move(badprint)); });
  assert(source(p) == before && p.image("styles.xml") == oldstyles &&
         !p.modified());
}
void missing_styles_and_readonly() {
  TempDir dir;
  auto path = dir.path() + "/input.odp", out = dir.path() + "/saved.odp";
  auto pack = fixture();
  auto styles = pack.entry("styles.xml"),
       beforemanifest = pack.entry("META-INF/manifest.xml");
  auto manifest = read_document_xml(beforemanifest);
  for (auto node : children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (node.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest.documentElement().removeChild(node);
  auto aftermanifest = manifest.toByteArray();
  pack.replay_part_changes(
      {{"styles.xml", true, false, 0, styles.size(), 0, styles, {}},
       {"META-INF/manifest.xml", true, true, 0, beforemanifest.size(),
        aftermanifest.size(), beforemanifest, aftermanifest}},
      true);
  pack.save(path);
  Presentation p;
  p.load(path);
  auto before = source(p), savedmanifest = p.image("META-INF/manifest.xml");
  auto edit = p.edit_layers(0);
  assert(!edit.available(Scope::Master));
  edit.create(Scope::Document, "Created");
  p.commit_layers(std::move(edit));
  assert(!p.image("styles.xml").empty());
  p.undo();
  assert(source(p) == before && p.image("styles.xml").empty() &&
         p.image("META-INF/manifest.xml") == savedmanifest);
  p.save(out);
  assert(!read_document_archive(out).contains("styles.xml"));
  auto signedpack = fixture();
  signedpack.set_entry("META-INF/documentsignatures.xml", "signature",
                       "text/xml");
  signedpack.save(path);
  p.load(path);
  assert(p.read_only());
  auto locked = p.edit_layers(0);
  rejects([&] { locked.create(Scope::Document, "Denied"); });
  rejects([&] { p.commit_layers(std::move(locked)); });
  assert(!p.modified());
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto edit = p.edit_layers(0, {ref(p, "Editable")});
    edit.create(Scope::Slide, "xy");
    NativeLayerPatch patch;
    patch.display = SceneLayer::Printer;
    patch.title = "x";
    patch.description = "x\ny";
    edit.update(Scope::Slide, "xy", patch);
    edit.assign(Text("xy"));
    p.commit_layers(std::move(edit));
    p.save(Text(std::string(argv[3]) + "-print-only.odp"));
    auto global = p.edit_layers(0);
    NativeLayerPatch rename;
    rename.name = "xx";
    global.update(Scope::Document, "Print", rename);
    p.commit_layers(std::move(global));
    p.save(Text(std::string(argv[3]) + "-global-renamed.odp"));
    return 0;
  }
  lifecycle();
  scoping();
  protected_group_and_hidden_selection();
  missing_and_failures();
  missing_styles_and_readonly();
  std::cout << "PASS native layer drafts, scope-aware references, "
               "protected/group assignment, hidden selection, exact archive "
               "history, repair, stale/print guards and missing styles\n";
}
