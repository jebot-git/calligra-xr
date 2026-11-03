// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
int main(int argc, char **argv) {
  TempDir directory;
  auto input = directory.path() + "/source.odp",
       output = directory.path() + "/saved.odp";
  if (argc == 5 && Text(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto baseline = content(p);
    auto original = p.scene(0).shapes;
    auto grouped = p.group_objects(0, {ref(p, "A"), ref(p, "B")});
    auto group_source = content(p);
    p.save(Text(argv[3]));
    same_geometry(p, original);
    auto separated = p.ungroup_objects(0, grouped.objects);
    auto ungrouped_source = content(p);
    p.save(Text(argv[4]));
    same_geometry(p, original);
    assert(p.undo() == grouped && content(p) == group_source && p.modified());
    assert(p.undo() && content(p) == baseline);
    assert(p.redo() == grouped && content(p) == group_source);
    assert(p.redo() == separated && content(p) == ungrouped_source &&
           !p.modified());
    Presentation reopened;
    reopened.load(Text(argv[4]));
    same_geometry(reopened, original);
    std::cout << "PASS independent native grouping/ungrouping, exact history "
                 "and affine geometry/save/reopen\n";
    return 0;
  }
  // Structural moves can leave adjacent text nodes. Serialized clones merge
  // those nodes, so element addressing must ignore their physical node count.
  auto xml = read_document_xml("<root><a/><b/></root>");
  auto root = xml.documentElement(), target = root.lastChild();
  root.insertBefore(xml.createTextNode("first "), target);
  root.insertBefore(xml.createTextNode("second "), target);
  auto copy = read_document_xml(xml.toByteArray());
  assert(document_source_clone(target, copy).localName() == "b");
  rejects([&] { document_source_clone({}, copy); });
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  const auto baseline = content(p);
  auto &scene = p.scene(0);
  assert(
      native_object_source(scene, ref(p, "X", 1)).attributeNS(DRAW, "name") ==
      "Outer");
  auto selection = native_object_selection(
      scene, {ref(p, "X"), ref(p, "X"), ref(p, "Y"), ref(p, "N1", 2)});
  assert(selection == std::vector<NativeObjectRef>({ref(p, "N1", 2)}));
  assert(native_object_leaves(scene, selection).size() == 4);
  rejects([&] { native_object_source(scene, ref(p, "A", 1)); });
  rejects([&] { native_object_source(scene, {-1, 0}); });
  rejects([&] { native_object_source(scene, {0, -1}); });
  for (const auto &names : {std::pair(Text("A"), Text("B")),
                            {Text("A"), Text("C")},
                            {Text("X"), Text("Y")}}) {
    p.load(input);
    auto shapes = p.scene(0).shapes;
    std::vector<NativeObjectRef> before{ref(p, names.first),
                                        ref(p, names.second)};

    auto at = p.group_objects(0, before);
    assert(at.objects.size() == 1 && at.objects[0].ancestors >= 1 &&
           p.modified());
    auto group = native_object_source(p.scene(0), at.objects[0]);
    assert(group.localName() == "g" && group.attributes().size() == 1);
    same_geometry(p, shapes);
    auto grouped = content(p);
    if (names.first == "A" && names.second == "C")
      order(p, {"B", "X", "Y", "N1", "N2", "A", "C"});
    p.save(output);
    assert(!p.modified());
    auto previous = p.undo();
    assert(previous && previous->objects == before && content(p) == baseline &&
           p.modified());
    rejects([&] { p.group_objects(0, {ref(p, "A"), ref(p, "X")}); });
    assert(content(p) == baseline && p.can_redo());
    assert(p.redo() == at && content(p) == grouped && !p.modified());
    Presentation reopened;
    reopened.load(output);
    same_geometry(reopened, shapes);
    auto archive = read_document_archive(input),
         saved = read_document_archive(output);
    for (auto it = archive.cbegin(); it != archive.cend(); ++it)
      if (it.key() != "content.xml")
        assert(saved.value(it.key()) == it.value());
    auto ungrouped = p.ungroup_objects(0, at.objects);
    assert(ungrouped.objects.size() == 2);
    same_geometry(p, shapes);
    assert(p.undo() == at && content(p) == grouped && !p.modified());
    assert(p.redo() == ungrouped && p.modified());
    assert(p.undo() && p.undo() && content(p) == baseline && !p.can_undo());
  }
  p.load(input);
  auto at = p.group_objects(0, {ref(p, "X", 1), ref(p, "C")});
  assert(native_object_leaves(p.scene(0), at.objects).size() == 5);
  assert(p.ungroup_objects(0, at.objects).objects.size() == 2);
  assert(p.undo() && p.undo() && content(p) == baseline);
  auto before = p.scene(0).shapes;
  auto separated = p.ungroup_objects(0, {ref(p, "X")});
  assert(separated.objects.size() == 3);
  same_geometry(p, before);
  order(p, {"A", "B", "X", "Y", "N1", "N2", "C"});
  assert(p.undo() && content(p) == baseline);
  p.ungroup_objects(0, {ref(p, "N1", 1)});
  same_geometry(p, before);
  assert(p.undo() && content(p) == baseline);
  // Legacy transformed groups are a consumer compatibility test, separate
  // from strict ODF1.2 producer evidence supplied by the independent corpus.
  auto transformed = fixture();
  auto outer = transformed.xml.elementsByTagNameNS(DRAW, "g").at(0);
  outer.setAttributeNS(DRAW, "draw:transform", "matrix(1.2 .3 -.2 .8 40 30)");
  auto inner = transformed.xml.elementsByTagNameNS(DRAW, "g").at(1);
  inner.setAttributeNS(DRAW, "draw:transform", "matrix(0 1 -1 0 20 30)");
  transformed.save(input);
  p.load(input);
  const auto transformed_source = content(p);
  before = p.scene(0).shapes;
  p.ungroup_objects(0, {ref(p, "X", 1)});
  same_geometry(p, before);
  assert(p.undo() && content(p) == transformed_source);
  p.ungroup_objects(0, {ref(p, "N1", 1)});
  same_geometry(p, before);
  assert(p.undo() && content(p) == transformed_source);
  rejects([&] { p.group_objects(0, {}); });
  rejects([&] { p.group_objects(0, {ref(p, "A"), ref(p, "A")}); });
  rejects([&] { p.ungroup_objects(0, {ref(p, "A")}); });
  assert(content(p) == transformed_source && p.can_redo());
  // Removing a referenced group must not orphan links from retained children,
  // other slides, navigation order or styles. Failed edits retain source/redo.
  for (const auto &kind : {Text("child-link"), Text("navigation"),
                           Text("metadata"), Text("style")}) {
    auto guarded = fixture();
    auto group = guarded.xml.elementsByTagNameNS(DRAW, "g").at(0);
    if (kind == "child-link") {
      auto link = guarded.xml.createElementNS(XLINK, "xlink:link");
      link.setAttributeNS(XLINK, "xlink:href", "#outer");
      group.firstChild().appendChild(link);
    } else if (kind == "navigation")
      group.parentNode().setAttributeNS(DRAW, "draw:nav-order", "outer a b c");
    else if (kind == "metadata") {
      auto title = guarded.xml.createElementNS(SVG, "svg:title");
      title.appendChild(guarded.xml.createTextNode("Keep accessibility text"));
      group.insertBefore(title, group.firstChild());
    } else
      group.setAttributeNS(DRAW, "draw:style-name", "UnknownGroupEffect");
    guarded.save(input);
    p.load(input);
    auto original = content(p);
    rejects([&] { p.ungroup_objects(0, {ref(p, "X", 1)}); });
    assert(content(p) == original && !p.modified() && !p.can_undo());
  }
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  rejects([&] { p.group_objects(0, {ref(p, "A"), ref(p, "B")}); });
  rejects([&] { p.ungroup_objects(0, {ref(p, "X", 1)}); });
  assert(!p.modified());
  std::cout << "PASS native group-aware selection, sibling/nested grouping and "
               "ungrouping, affine geometry/source/history/archive "
               "preservation and reference/read-only guards\n";
}
