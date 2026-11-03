// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
namespace {
void transformed(const Presentation &p, const std::vector<SceneShape> &before,
                 SceneTransform action, const std::set<Text> &selected) {
  for (const auto &old : before) {
    const auto &now = p.scene(0).shapes.at(find(p, old.name));
    auto expected =
        selected.contains(old.name) ? action * old.transform : old.transform;
    for (auto pair : {std::pair(expected.a, now.transform.a),
                      {expected.b, now.transform.b},
                      {expected.c, now.transform.c},
                      {expected.d, now.transform.d},
                      {expected.e, now.transform.e},
                      {expected.f, now.transform.f}})
      assert(std::abs(pair.first - pair.second) < 1e-8);
    assert(now.width == old.width && now.height == old.height &&
           now.text == old.text && now.image == old.image);
    assert(now.paint.font.size == old.paint.font.size &&
           now.paint.stroke_width == old.paint.stroke_width);
  }
}
} // namespace
int main() {
  TempDir directory;
  auto input = directory.path() + "/input.odp",
       output = directory.path() + "/output.odp";
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  const auto baseline = content(p);
  // World actions are checked against authored fixture bounds and matrices,
  // independently of the gesture/frame implementation.
  for (int mode = 0; mode < 5; ++mode) {
    p.load(input);
    auto group = p.group_objects(0, {ref(p, "A"), ref(p, "B")});
    auto source = content(p);
    p.save(output);
    auto before = p.scene(0).shapes;
    SceneTransform action;
    if (mode == 0) {
      p.begin_objects_move(0, group.objects, {3, 8});
      p.update_objects_drag(0, {23, -2});
      action.e = 20;
      action.f = -10;
    } else if (mode == 1) {
      p.begin_objects_transform(0, group.objects, NativeShapeHandle::East,
                                {120, 60});
      p.update_objects_drag(0, {230, 60});
      action = {2, 0, 0, 1, -10, 0};
    } else if (mode == 2) {
      p.begin_objects_transform(0, group.objects, NativeShapeHandle::Rotate,
                                {65, -4});
      p.update_objects_drag(0, {129, 60}, true);
      action = {0, 1, -1, 0, 125, -5};
    } else if (mode == 3) {
      p.begin_objects_transform(0, group.objects, NativeShapeHandle::East,
                                {120, 60});
      p.update_objects_drag(0, {-100, 60});
      action = {-1, 0, 0, 1, 20, 0};
    } else {
      p.begin_objects_transform(0, group.objects, NativeShapeHandle::East,
                                {120, 60});
      p.update_objects_drag(0, {230, 60}, true);
      action = {2, 0, 0, 2, -10, -60};
    }
    transformed(p, before, action, {"A", "B"});
    auto changed = content(p);
    p.finish_handle_drag();
    assert(p.undo() == group && content(p) == source && !p.modified());
    // Return-to-start and cancellation preserve exact source and the redo
    // branch.
    p.begin_objects_move(0, group.objects, {3, 8});
    p.update_objects_drag(0, {4, 10});
    p.update_objects_drag(0, {3, 8});
    p.finish_handle_drag();
    assert(content(p) == source && p.can_redo() && !p.modified());
    p.begin_objects_move(0, group.objects, {});
    p.update_objects_drag(0, {50, 20});
    p.finish_handle_drag(true);
    assert(content(p) == source && p.can_redo() && !p.modified());
    p.begin_objects_move(0, group.objects, {});
    rejects([&] { p.update_objects_drag(0, {NAN, 0}); });
    p.finish_handle_drag(true);
    assert(content(p) == source && p.can_redo());
    assert(p.redo() == group && content(p) == changed && p.modified());
    p.save(output);
    assert(!p.modified());
    Presentation reopened;
    reopened.load(output);
    transformed(reopened, before, action, {"A", "B"});
    auto a = read_document_archive(input), b = read_document_archive(output);
    for (auto it = a.cbegin(); it != a.cend(); ++it)
      if (it.key() != "content.xml")
        assert(b.value(it.key()) == it.value());
  }
  // A nested affine group moves in world axes, including its encoded image and
  // nested line. Child source geometry stays in its existing local coordinates.
  auto legacy = fixture();
  auto outer = legacy.xml.elementsByTagNameNS(DRAW, "g").at(0);
  outer.setAttributeNS(DRAW, "draw:transform", "matrix(1.2 .3 -.2 .8 40 30)");
  legacy.save(input);
  p.load(input);
  auto before = p.scene(0).shapes;
  std::vector<NativeObjectRef> selected{ref(p, "X", 1), ref(p, "C")};
  p.begin_objects_move(0, selected, {});
  p.update_objects_drag(0, {20, -10});
  p.finish_handle_drag();
  transformed(p, before, {1, 0, 0, 1, 20, -10}, {"X", "Y", "N1", "N2", "C"});
  assert(p.undo()->objects == selected);
  package.save(input);
  for (auto command : {NativeShapeOrder::Back, NativeShapeOrder::Front,
                       NativeShapeOrder::Forward, NativeShapeOrder::Backward}) {
    p.load(input);
    auto refs = std::vector<NativeObjectRef>{ref(p, "A"), ref(p, "C")};
    auto at = p.reorder_objects(0, refs, command);
    if (command == NativeShapeOrder::Back)
      order(p, {"A", "C", "B", "X", "Y", "N1", "N2"});
    else if (command == NativeShapeOrder::Front)
      order(p, {"B", "X", "Y", "N1", "N2", "A", "C"});
    else if (command == NativeShapeOrder::Forward)
      order(p, {"B", "A", "X", "Y", "N1", "N2", "C"});
    else
      order(p, {"A", "B", "C", "X", "Y", "N1", "N2"});
    assert(at.objects.size() == 2);
    assert(p.undo()->objects == refs && content(p) == baseline);
    auto revision = p.revision();
    p.reorder_objects(0, {ref(p, "C")}, NativeShapeOrder::Front);
    assert(p.revision() == revision && p.can_redo() && content(p) == baseline);
    assert(p.redo() == at);
  }
  p.load(input);
  p.reorder_objects(0, {ref(p, "A"), ref(p, "X")}, NativeShapeOrder::Front);
  order(p, {"B", "Y", "N1", "N2", "X", "C", "A"});
  assert(p.undo() && content(p) == baseline);
  auto child = ref(p, "X");
  auto reordered_group = p.reorder_objects(0, {child}, NativeShapeOrder::Front,
                                           NativeOrderScope::ContainingGroup);
  assert(p.scene(0).shapes.at(reordered_group.shape).name == "X");
  assert(reordered_group.objects.size() == 1 &&
         reordered_group.objects.front().ancestors == 0);
  assert(p.undo()->objects == std::vector<NativeObjectRef>{child} &&
         content(p) == baseline);
  auto refs = std::vector<NativeObjectRef>{ref(p, "A"), ref(p, "B")};
  auto removed = p.remove_objects(0, refs);
  order(p, {"X", "Y", "N1", "N2", "C"});
  auto page = p.scene(0).shapes.at(find(p, "C")).source.parentNode();
  assert(page.attributeNS(DRAW, "nav-order") == "x y n1 n2 c");
  assert(p.undo()->objects == refs && content(p) == baseline);
  assert(p.redo() == removed && p.undo() && content(p) == baseline);
  refs = {ref(p, "A"), ref(p, "B"), ref(p, "X", 1), ref(p, "C")};
  auto empty = p.remove_objects(0, refs);
  assert(empty.objects.empty() && empty.shape == -1 && empty.box == -1 &&
         p.scene(0).shapes.empty());
  p.save(output);
  assert(p.undo()->objects == refs && content(p) == baseline && p.modified());
  assert(p.redo() == empty && p.scene(0).shapes.empty() && !p.modified());
  assert(p.undo() && content(p) == baseline);
  // References from other removed objects disappear atomically; references from
  // retained objects still guard deletion. No partial first-object deletion.
  auto linked = fixture();
  auto x = linked.xml.elementsByTagNameNS(DRAW, "rect").at(2);
  auto link = linked.xml.createElementNS(TEXT, "text:a");
  link.setAttributeNS(XLINK, "xlink:href", "#a");
  x.firstChild().appendChild(link);
  linked.save(input);
  p.load(input);
  auto linked_source = content(p);
  rejects([&] { p.remove_objects(0, {ref(p, "A"), ref(p, "B")}); });
  assert(content(p) == linked_source && !p.modified());
  p.remove_objects(0, {ref(p, "A"), ref(p, "X", 1)});
  order(p, {"B", "C"});
  assert(p.undo() && content(p) == linked_source);
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  rejects([&] { p.begin_objects_move(0, {ref(p, "A")}, {}); });
  rejects(
      [&] { p.reorder_objects(0, {ref(p, "A")}, NativeShapeOrder::Front); });
  rejects([&] { p.remove_objects(0, {ref(p, "A")}); });
  assert(!p.modified());
  std::cout << "PASS native multi/group affine gestures, immutable "
               "cancel/redo/history, group-context ordering and atomic "
               "reference-safe deletion\n";
}
