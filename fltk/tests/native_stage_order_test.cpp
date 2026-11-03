// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
int find(const Presentation &p, const Text &name) {
  for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
    if (p.scene(0).shapes[i].name == name)
      return int(i);
  std::abort();
}
Bytes content(const Presentation &p) {
  return p.scene(0)
      .shapes.at(find(p, "A"))
      .source.ownerDocument()
      .toByteArray();
}
void order(const Presentation &p, std::initializer_list<Text> expected) {
  std::vector<Text> names;
  for (const auto &shape : p.scene(0).shapes)
    if (!shape.master)
      names.push_back(shape.name);
  assert(names == std::vector<Text>(expected));
}
// Independent semantic comparison: only z-index on the explicitly permitted
// siblings may change. Child order, text, IDs, links, navigation and all other
// attributes are invariant, including in opaque and hidden objects.
void same(XmlNode a, XmlNode b, const std::set<Text> &allowed) {
  assert(a.isNull() == b.isNull());
  if (a.isNull())
    return;
  assert(a.isElement() == b.isElement());
  assert(a.namespaceURI() == b.namespaceURI() &&
         a.localName() == b.localName());
  assert(a.nodeValue() == b.nodeValue());
  auto attributes = [&](XmlNode n) {
    std::vector<std::tuple<Text, Text, Text>> result;
    for (const auto &attr : n.attributes()) {
      if (allowed.contains(n.attributeNS(DRAW, "name")) && attr.uri == DRAW &&
          attr.name == "draw:z-index")
        continue;
      result.emplace_back(attr.uri, attr.name, attr.value);
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  assert(attributes(a) == attributes(b));
  auto x = a.firstChild(), y = b.firstChild();
  for (; !x.isNull() && !y.isNull(); x = x.nextSibling(), y = y.nextSibling())
    same(x, y, allowed);
  assert(x.isNull() && y.isNull());
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:body><office:presentation>
<draw:page draw:name="Order" draw:master-page-name="Default" draw:nav-order="a b x y n1 n2 last">
 <svg:title>Source order and keyboard navigation stay unchanged</svg:title>
 <draw:rect draw:name="A" xml:id="a" draw:z-index="20" svg:x="10pt" svg:y="10pt" svg:width="100pt" svg:height="80pt"><text:p>A<text:span>rich</text:span></text:p></draw:rect>
 <draw:g draw:name="Group" draw:z-index="4">
  <draw:rect draw:name="X" xml:id="x" draw:z-index="7" svg:x="20pt" svg:y="20pt" svg:width="100pt" svg:height="80pt"><text:p>X</text:p></draw:rect>
  <draw:rect draw:name="Y" xml:id="y" draw:z-index="2" svg:x="25pt" svg:y="25pt" svg:width="100pt" svg:height="80pt"><text:p>Y</text:p></draw:rect>
  <draw:g draw:name="Inner" draw:z-index="4"><draw:rect draw:name="N1" xml:id="n1" svg:x="30pt" svg:y="30pt" svg:width="100pt" svg:height="80pt"/><draw:rect draw:name="N2" xml:id="n2" draw:z-index="7" svg:x="35pt" svg:y="35pt" svg:width="100pt" svg:height="80pt"/></draw:g>
 </draw:g>
 <draw:rect draw:name="B" xml:id="b" draw:z-index="4" svg:x="40pt" svg:y="40pt" svg:width="100pt" svg:height="80pt"><text:p>B</text:p></draw:rect>
 <draw:rect draw:name="C" svg:x="45pt" svg:y="45pt" svg:width="100pt" svg:height="80pt"/>
 <draw:rect draw:name="Hidden" draw:display="none" draw:z-index="6" svg:width="10pt" svg:height="10pt"/>
 <draw:connector draw:name="Opaque" draw:z-index="8" draw:start-shape="a" draw:end-shape="b"/>
 <draw:rect draw:name="Last" xml:id="last" draw:z-index="2147483647" svg:x="50pt" svg:y="50pt" svg:width="100pt" svg:height="80pt"/>
</draw:page><draw:page draw:name="Unchanged" draw:master-page-name="Default"><draw:rect draw:name="Other" svg:width="50pt" svg:height="50pt"/></draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry("opaque/keep", Bytes("retain\0bytes", 12),
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  TempDir directory;
  auto input = directory.path() + "/source.odp",
       output = directory.path() + "/saved.odp";
  if (argc == 4 && Text(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto baseline = content(p);
    p.reorder_shape(0, find(p, "B"), NativeShapeOrder::Front);
    p.reorder_shape(0, find(p, "X"), NativeShapeOrder::Back);
    auto at = p.reorder_shape(0, find(p, "X"), NativeShapeOrder::Front,
                              NativeOrderScope::ContainingGroup);
    assert(p.scene(0).shapes.at(at.shape).name == "X");
    auto changed = content(p);
    p.save(Text(argv[3]));
    for (int i = 0; i < 3; ++i)
      assert(p.undo());
    assert(content(p) == baseline);
    for (int i = 0; i < 3; ++i)
      assert(p.redo());
    assert(content(p) == changed && !p.modified());
    std::cout << "PASS independent Stage sibling/group ordering, exact history "
                 "and ODP save\n";
    return 0;
  }
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  const auto baseline = content(p);
  order(p, {"C", "Y", "N1", "N2", "X", "B", "A", "Last"});
  for (auto command : {NativeShapeOrder::Back, NativeShapeOrder::Backward,
                       NativeShapeOrder::Forward, NativeShapeOrder::Front}) {
    p.load(input);
    auto index = find(p, "B");
    PresentationPosition previous{0, p.scene(0).shapes.at(index).editor_index,
                                  index};
    auto at = p.reorder_shape(0, index, command);
    assert(p.scene(0).shapes.at(at.shape).name == "B");
    if (command == NativeShapeOrder::Back)
      order(p, {"B", "C", "Y", "N1", "N2", "X", "A", "Last"});
    if (command == NativeShapeOrder::Backward)
      order(p, {"C", "B", "Y", "N1", "N2", "X", "A", "Last"});
    // Forward crosses the hidden object, which remains in the stacking order.
    if (command == NativeShapeOrder::Forward)
      order(p, {"C", "Y", "N1", "N2", "X", "B", "A", "Last"});
    if (command == NativeShapeOrder::Front)
      order(p, {"C", "Y", "N1", "N2", "X", "A", "Last", "B"});
    const auto changed = content(p);
    assert(p.modified());
    same(read_document_xml(baseline).documentElement(),
         read_document_xml(changed).documentElement(),
         {"A", "Group", "B", "C", "Hidden", "Opaque", "Last"});
    p.save(output);
    assert(!p.modified());
    assert(p.undo() == previous && content(p) == baseline && p.modified());
    auto revision = p.revision();
    p.reorder_shape(0, find(p, "Last"), NativeShapeOrder::Front);
    p.reorder_shape(0, find(p, "C"), NativeShapeOrder::Back);
    assert(p.revision() == revision && p.can_redo() && content(p) == baseline);
    assert(p.redo() == at && content(p) == changed && !p.modified());
    assert(p.undo() && content(p) == baseline && !p.can_undo());
    const auto source = read_document_archive(input),
               saved = read_document_archive(output);
    for (auto i = source.cbegin(); i != source.cend(); ++i)
      if (i.key() != "content.xml")
        assert(saved.value(i.key()) == i.value());
    Presentation reopened;
    reopened.load(output);
    assert(content(reopened) == changed && !reopened.can_undo());
  }
  p.load(input);
  p.reorder_shape(0, find(p, "X"), NativeShapeOrder::Back);
  order(p, {"C", "X", "Y", "N1", "N2", "B", "A", "Last"});
  same(read_document_xml(baseline).documentElement(),
       read_document_xml(content(p)).documentElement(), {"X", "Y", "Inner"});
  assert(p.undo());
  auto at = p.reorder_shape(0, find(p, "X"), NativeShapeOrder::Front,
                            NativeOrderScope::ContainingGroup);
  order(p, {"C", "B", "A", "Last", "Y", "N1", "N2", "X"});
  assert(p.scene(0).shapes.at(at.shape).name == "X");
  assert(p.undo() && content(p) == baseline);
  p.reorder_shape(0, find(p, "N1"), NativeShapeOrder::Front,
                  NativeOrderScope::ContainingGroup);
  order(p, {"C", "Y", "X", "N1", "N2", "B", "A", "Last"});
  assert(p.undo() && content(p) == baseline);
  rejects([&] {
    p.reorder_shape(0, find(p, "A"), NativeShapeOrder::Front,
                    NativeOrderScope::ContainingGroup);
  });
  rejects([&] { p.reorder_shape(0, find(p, "A"), NativeShapeOrder(-1)); });
  rejects([&] { p.reorder_shape(0, -1, NativeShapeOrder::Front); });
  assert(content(p) == baseline && p.can_redo());
  // Opaque standard stacking items participate without changing the drawing
  // source paths used to identify editable text frames.
  auto extended = fixture();
  auto page = child(extended.body(), DRAW, "page");
  auto space = extended.xml.createElementNS(
      "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0", "dr3d:scene");
  space.setAttributeNS(DRAW, "draw:name", "Space");
  space.setAttributeNS(DRAW, "draw:z-index", "0");
  page.insertBefore(space, page.firstChild());
  auto annotation = extended.xml.createElementNS(OFFICE, "office:annotation");
  annotation.setAttributeNS(DRAW, "draw:name", "Annotation");
  page.insertBefore(annotation, space.nextSibling());
  extended.save(input);
  p.load(input);
  const auto opaque_before = content(p);
  order(p, {"Y", "N1", "N2", "X", "B", "C", "A", "Last"});
  auto text_index = p.scene(0).shapes.at(find(p, "X")).editor_index;
  assert(text_index >= 0);
  p.set_box_text(0, text_index, "Changed X");
  assert(p.scene(0).shapes.at(find(p, "X")).text == "Changed X");
  assert(p.undo() && content(p) == opaque_before);
  p.reorder_shape(0, find(p, "B"), NativeShapeOrder::Front);
  order(p, {"Y", "N1", "N2", "X", "C", "A", "Last", "B"});
  same(read_document_xml(opaque_before).documentElement(),
       read_document_xml(content(p)).documentElement(),
       {"A", "Group", "B", "C", "Hidden", "Opaque", "Last", "Space",
        "Annotation"});
  assert(p.undo() && content(p) == opaque_before);
  rejects([&] {
    p.reorder_shape(0, find(p, "B"), NativeShapeOrder::Front,
                    NativeOrderScope(-1));
  });
  assert(content(p) == opaque_before);
  auto master_package = fixture();
  master_package.set_entry(
      "styles.xml",
      R"(<office:document-styles office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"><office:master-styles><style:master-page style:name="Default"><draw:rect draw:name="Master" svg:width="10pt" svg:height="10pt"/></style:master-page></office:master-styles></office:document-styles>)", "text/xml");
  master_package.save(input);
  p.load(input);
  assert(p.scene(0).shapes.at(find(p, "Master")).master);
  rejects(
      [&] { p.reorder_shape(0, find(p, "Master"), NativeShapeOrder::Front); });
  assert(content(p) == baseline && !p.modified());
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  rejects([&] { p.reorder_shape(0, find(p, "A"), NativeShapeOrder::Front); });
  assert(content(p) == baseline && !p.modified());
  std::cout << "PASS native sibling/group stacking, ties/missing/max indices, "
               "hidden/opaque siblings, exact source/history/save/reopen and "
               "guards\n";
}
