// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include <set>
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
XmlElement layer_set(XmlElement parent) {
  auto doc = parent.ownerDocument();
  auto set = doc.createElementNS(DRAW, "draw:layer-set");
  parent.appendChild(set);
  return set;
}
void layer(XmlElement set, Text name, Text display = "always",
           Text locked = "false") {
  auto doc = set.ownerDocument();
  auto node = doc.createElementNS(DRAW, "draw:layer");
  node.setAttributeNS(DRAW, "draw:name", name);
  node.setAttributeNS(DRAW, "draw:display", display);
  node.setAttributeNS(DRAW, "draw:protected", locked);
  set.appendChild(node);
}
XmlElement named(OdfPackage &p, const Text &name) {
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
XmlDocument styles(OdfPackage &p) {
  return read_document_xml(p.entry("styles.xml"));
}
void store_styles(OdfPackage &p, const XmlDocument &doc) {
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
}
std::set<Text> names(const SlideScene &scene) {
  std::set<Text> result;
  for (const auto &shape : scene.shapes)
    result.insert(shape.name);
  return result;
}
XmlElement master_rect(XmlElement master, Text name, Text assignment) {
  auto doc = master.ownerDocument();
  auto rect = doc.createElementNS(DRAW, "draw:rect");
  rect.setAttributeNS(DRAW, "draw:name", name);
  rect.setAttributeNS(DRAW, "draw:layer", assignment);
  rect.setAttributeNS(SVG, "svg:width", "10pt");
  rect.setAttributeNS(SVG, "svg:height", "10pt");
  master.appendChild(rect);
  return rect;
}
void visibility() {
  auto p = fixture();
  auto s = styles(p);
  auto masters = child(s.documentElement(), OFFICE, "master-styles");
  auto set = layer_set(masters);
  layer(set, "All");
  layer(set, "Screen", "screen");
  layer(set, "Print", "printer");
  layer(set, "Hidden", "none");
  layer(set, "Locked", "always", "1");
  auto master = child(masters, STYLE, "master-page");
  master_rect(master, "MasterScreen", "Screen");
  master_rect(master, "MasterPrint", "Print");
  named(p, "A").setAttributeNS(DRAW, "draw:layer", "All");
  named(p, "B").setAttributeNS(DRAW, "draw:layer", "Screen");
  named(p, "X").setAttributeNS(DRAW, "draw:layer", "Print");
  named(p, "Y").setAttributeNS(DRAW, "draw:layer", "Hidden");
  named(p, "N1").setAttributeNS(DRAW, "draw:layer", "Locked");
  named(p, "N2").setAttributeNS(DRAW, "draw:layer", "Undefined");
  store_styles(p, s);
  auto screen = read_presentation_scenes(p),
       printed = read_presentation_scenes(p, {}, SceneTarget::Print);
  assert((names(screen[0]) ==
          std::set<Text>{"A", "B", "N1", "N2", "C", "MasterScreen"}));
  assert((names(printed[0]) ==
          std::set<Text>{"A", "X", "N1", "N2", "C", "MasterPrint"}));
  assert(screen[0].editable_sources.size() == 3);
  assert(printed[0].editable_sources.size() == 3);
  assert(screen[0].unsupported.size() == 1 &&
         printed[0].unsupported.size() == 1);
  assert(screen[0].unsupported.front().contains("Missing drawing layer"));
  auto page = children(p.body(), DRAW, "page")[0];
  // This local set replaces every global definition; it does not merge them.
  layer(layer_set(page), "Screen", "none");
  screen = read_presentation_scenes(p);
  assert((names(screen[0]) ==
          std::set<Text>{"A", "X", "Y", "N1", "N2", "C", "MasterScreen"}));
  auto local_master = layer_set(master);
  layer(local_master, "Screen", "none");
  layer(local_master, "Print");
  store_styles(p, s);
  screen = read_presentation_scenes(p);
  printed = read_presentation_scenes(p, {}, SceneTarget::Print);
  assert(names(screen[0]).contains("MasterPrint") &&
         !names(screen[0]).contains("MasterScreen"));
  assert(names(printed[0]).contains("MasterPrint") &&
         !names(printed[0]).contains("MasterScreen"));
  // An empty local set likewise suppresses inheritance, including protection.
  page.removeChild(child(page, DRAW, "layer-set"));
  layer_set(page);
  screen = read_presentation_scenes(p);
  assert(screen[0].editable(named(p, "N1")));
  assert(names(screen[0]).contains("B") && names(screen[0]).contains("Y"));
}
void malformed() {
  for (int mode = 0; mode < 5; ++mode) {
    auto p = fixture();
    auto set = layer_set(children(p.body(), DRAW, "page")[0]);
    layer(set, "L");
    auto first = child(set, DRAW, "layer");
    if (mode == 0)
      first.setAttributeNS(DRAW, "draw:display", "sometimes");
    if (mode == 1)
      first.setAttributeNS(DRAW, "draw:protected", "yes");
    if (mode == 2)
      layer(set, "L");
    if (mode == 3)
      first.removeAttributeNS(DRAW, "name");
    if (mode == 4)
      layer_set(children(p.body(), DRAW, "page")[0]);
    rejects([&] { read_presentation_scenes(p); });
  }
  auto p = fixture();
  auto set = layer_set(children(p.body(), DRAW, "page")[0]);
  auto doc = set.ownerDocument();
  auto def = doc.createElementNS(DRAW, "draw:layer");
  def.setAttributeNS(DRAW, "draw:name", "Defaults");
  set.appendChild(def);
  named(p, "A").setAttributeNS(DRAW, "draw:layer", "Defaults");
  auto scene = read_presentation_scenes(p)[0];
  assert(names(scene).contains("A") && scene.editable(named(p, "A")));
}
void protection() {
  TempDir dir;
  auto input = dir.path() + "/locked.odp", output = dir.path() + "/saved.odp";
  auto package = fixture();
  auto page = children(package.body(), DRAW, "page")[0];
  auto set = layer_set(page);
  layer(set, "Locked", "always", "true");
  layer(set, "HiddenLock", "none", "true");
  named(package, "A").setAttributeNS(DRAW, "draw:layer", "Locked");
  named(package, "N2").setAttributeNS(DRAW, "draw:layer", "HiddenLock");
  // Ample labels permit moving editable peers across a fixed protected object.
  named(package, "A").setAttributeNS(DRAW, "draw:z-index", "100");
  package.save(input);
  Presentation p;
  p.load(input);
  assert(p.scene(0).shapes[find(p, "A")].layer_protected);
  assert(!names(p.scene(0)).contains("N2"));
  assert(
      !p.scene(0).editable(native_object_source(p.scene(0), ref(p, "X", 1))));
  const auto initial = content(p);
  p.move_shape(0, find(p, "C"), {5, 7});
  p.undo();
  assert(p.can_redo());
  const auto revision = p.revision();
  auto reject = [&](auto f) {
    rejects(f);
    assert(content(p) == initial && p.revision() == revision && !p.modified() &&
           p.can_redo() && !p.can_undo());
  };
  reject([&] { p.move_shape(0, find(p, "A"), {2, 3}); });
  reject([&] {
    p.begin_shape_transform(0, find(p, "A"), NativeShapeHandle::Rotate, {2, 3});
  });
  reject([&] {
    NativeShapeGeometryEdit e;
    e.width = 90;
    p.set_shape_geometry(0, find(p, "A"), e);
  });
  reject([&] { p.remove_shape(0, find(p, "A")); });
  reject([&] { p.remove_objects(0, {ref(p, "C"), ref(p, "A")}); });
  reject([&] { p.remove_objects(0, {ref(p, "X", 1)}); });
  reject([&] { p.begin_objects_move(0, {ref(p, "X", 1)}, {}); });
  reject([&] {
    p.begin_objects_transform(0, {ref(p, "X", 1)}, NativeShapeHandle::SouthEast,
                              {});
  });
  reject([&] { p.group_objects(0, {ref(p, "A"), ref(p, "C")}); });
  reject([&] { p.ungroup_objects(0, {ref(p, "X", 1)}); });
  reject([&] { p.reorder_shape(0, find(p, "A"), NativeShapeOrder::Back); });
  reject([&] {
    p.reorder_shape(0, find(p, "X"), NativeShapeOrder::Front,
                    NativeOrderScope::ContainingGroup);
  });
  reject([&] {
    p.reorder_objects(0, {ref(p, "X")}, NativeShapeOrder::Front,
                      NativeOrderScope::ContainingGroup);
  });
  const auto box = p.scene(0).shapes[find(p, "A")].editor_index;
  (void)p.box_format_info(0, box, {0, 1});
  reject([&] { p.set_box_text(0, box, "changed"); });
  reject([&] { p.replace_box_text(0, box, 0, 1, "changed"); });
  reject([&] {
    WordsCharacterFormat f;
    f.size = 30;
    p.format_box_characters(0, box, {0, 1}, f);
  });
  reject([&] {
    WordsCharacterFormat f;
    f.size = 30;
    p.format_box_characters(0, box, {0, 0}, f);
  });
  reject([&] {
    WordsParagraphFormat f;
    f.left = 5;
    p.format_box_paragraphs(0, box, {0, 1}, f);
  });
  reject([&] { p.remove_slide(0); });
  // Printing is observational and cannot disturb screen indices, history or
  // typing.
  auto screen_names = names(p.scene(0));
  auto prints = p.print_scenes();
  assert(names(p.scene(0)) == screen_names && p.can_redo() &&
         p.revision() == revision);
  p.reorder_shape(0, find(p, "C"), NativeShapeOrder::Front);
  auto locked = p.scene(0).shapes[find(p, "A")].source;
  assert(locked.attributeNS(DRAW, "z-index") == "100");
  assert(content(p) != initial);
  p.undo();
  assert(content(p) == initial);
  p.reorder_objects(0, {ref(p, "B"), ref(p, "C")}, NativeShapeOrder::Front);
  assert(p.scene(0).shapes[find(p, "A")].source.attributeNS(DRAW, "z-index") ==
         "100");
  p.undo();
  assert(content(p) == initial);
  p.save(output);
  Presentation reopened;
  reopened.load(output);
  assert(!reopened.scene(0).editable(
      reopened.scene(0).shapes[find(reopened, "A")].source));
  assert(content(reopened) == initial);
}
void external(const Text &input, const Text &output) {
  Presentation p;
  p.load(input);
  assert(
      (names(p.scene(0)) == std::set<Text>{"Editable", "ScreenOnly", "Locked",
                                           "GroupVisible", "MasterScreen"}));
  auto printed = p.print_scenes();
  assert((names(printed[0]) == std::set<Text>{"Editable", "PrintOnly", "Locked",
                                              "GroupVisible", "MasterPrint"}));
  assert((names(p.scene(1)) ==
          std::set<Text>{"LocalScreen", "LocalDefault", "MasterScreen"}));
  assert((names(printed[1]) ==
          std::set<Text>{"LocalPrint", "LocalDefault", "MasterPrint"}));
  assert((names(printed[2]) == std::set<Text>{"EmptyLocal", "MasterScreen"}));
  assert((names(p.scene(2)) == std::set<Text>{"EmptyLocal", "MasterScreen"}));
  const auto before = p.scene(0).source.ownerDocument().toByteArray();
  p.move_shape(0, find(p, "Editable"), {10, 5});
  p.undo();
  const auto revision = p.revision();
  for (auto name : {Text("Locked"), Text("GroupVisible")}) {
    auto ref0 = ref(p, name, name == "GroupVisible" ? 1 : 0);
    rejects([&] { p.remove_objects(0, {ref0}); });
    rejects([&] { p.begin_objects_move(0, {ref0}, {}); });
  }
  assert(p.scene(0).source.ownerDocument().toByteArray() == before &&
         p.can_redo() && p.revision() == revision);
  p.redo();
  p.save(output);
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--document") {
    external(Text(argv[2]), Text(argv[3]));
    return 0;
  }
  visibility();
  malformed();
  protection();
  std::cout << "PASS ODF layer scopes/defaults/visibility, independent print "
               "scenes, protected edits/hidden group members, fixed stacking "
               "labels and exact history\n";
}
