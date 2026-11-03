// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace DomeOffice;
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
Bytes content(const Presentation &p) {
  for (const auto &shape : p.scene(0).shapes)
    if (!shape.source.isNull())
      return shape.source.ownerDocument().toByteArray();
  std::abort();
}
int find(const Presentation &p, const Text &name) {
  const auto &shapes = p.scene(0).shapes;
  for (size_t i = 0; i < shapes.size(); ++i)
    if (shapes[i].name == name)
      return i;
  std::abort();
}
size_t own(const Presentation &p) {
  size_t n = 0;
  for (const auto &s : p.scene(0).shapes)
    n += !s.master;
  return n;
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-9); }
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.set_entry(
      "styles.xml",
      R"(<office:document-styles office:version="1.3" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles><style:style style:name="NativeGraphic1" style:family="graphic"/><style:style style:name="NativeObject1" style:family="graphic"/></office:styles><office:automatic-styles><style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="400pt" fo:page-height="240pt"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Wide"><draw:rect draw:name="Master" svg:x="0pt" svg:y="0pt" svg:width="400pt" svg:height="20pt"/></style:master-page></office:master-styles></office:document-styles>)",
      "text/xml");
  p.xml = read_document_xml(
      R"(<office:document-content office:version="1.3" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:body><office:presentation><draw:page draw:name="Objects" draw:master-page-name="Default" draw:nav-order="native-object-1 group image line"><draw:rect draw:name="Anchor" draw:style-name="NativeGraphic2" xml:id="native-object-1" svg:width="80pt" svg:height="40pt"><text:p>Anchor</text:p></draw:rect>
 <!-- keep metadata and an empty containing group when its last child is deleted -->
 <draw:g xml:id="group"><svg:title>Retained group metadata</svg:title><draw:frame draw:name="Rich" xml:id="rich" svg:x="10pt" svg:y="60pt" svg:width="100pt" svg:height="40pt"><draw:text-box><text:p>Native <text:span>Český</text:span> tail</text:p></draw:text-box></draw:frame></draw:g>
 <draw:frame draw:name="Image" xml:id="image" svg:x="180pt" svg:y="80pt" svg:width="60pt" svg:height="30pt"><draw:image xlink:href="Pictures/retained.bin"/></draw:frame><draw:line draw:name="Line" xml:id="line" svg:x1="10pt" svg:y1="200pt" svg:x2="190pt" svg:y2="200pt"/></draw:page></office:presentation></office:body></office:document-content>)");
  p.set_entry("Pictures/retained.bin", "opaque image bytes",
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  auto source = temp.path() + "/source.odp", saved = temp.path() + "/saved.odp";
  if (argc == 4 && Text(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto before = content(p);
    const auto n = own(p);
    p.insert_shape(0, NativeShapeKind::Rectangle, {250, 50, 60, 45}, "RECT");
    p.insert_shape(0, NativeShapeKind::Ellipse, {245, 120, 60, 40}, "ELLIPSE");
    p.insert_shape(0, NativeShapeKind::Line, {30, 205, 190, -20});
    p.insert_shape(0, NativeShapeKind::TextBox, {30, 30, 185, 35},
                   "Native Stage text");
    p.remove_shape(0, find(p, "Rectangle"));
    auto after = content(p);
    assert(own(p) == n + 3);
    for (int i = 0; i < 5; ++i)
      assert(p.undo());
    assert(content(p) == before && !p.modified());
    for (int i = 0; i < 5; ++i)
      assert(p.redo());
    assert(content(p) == after);
    p.save(Text(argv[3]));
    Presentation reopened;
    reopened.load(Text(argv[3]));
    assert(own(reopened) == n + 3);
    std::cout << "PASS independent Stage object insertion/deletion, exact "
                 "journal and ODP reopen\n";
    return 0;
  }
  auto package = fixture();
  package.save(source);
  Presentation p;
  p.load(source);
  auto baseline = content(p);
  const auto n = own(p);
  for (auto kind : {NativeShapeKind::Rectangle, NativeShapeKind::Ellipse,
                    NativeShapeKind::Line, NativeShapeKind::TextBox}) {
    auto anchor = find(p, "Anchor");
    PresentationPosition previous{0, p.scene(0).shapes[anchor].editor_index,
                                  anchor};
    auto rect = SceneRect{20, 30, 80, 40};
    if (kind == NativeShapeKind::Line)
      rect = {20, 30, -20, 10};
    auto at = p.insert_shape(
        0, kind, rect,
        kind == NativeShapeKind::Line ? Text() : Text("Český\tx\n第二行"),
        previous);
    const auto &shape = p.scene(0).shapes.at(at.shape);
    // A missing referenced style must stay missing, preserving existing paint.
    const auto &original_anchor = p.scene(0).shapes.at(find(p, "Anchor"));
    assert(!original_anchor.paint.fill && !original_anchor.paint.stroke);
    assert(own(p) == n + 1 && p.modified());
    assert(shape.name == "NativeObject2");
    assert(shape.source.attributeNS(XML, "id") == "native-object-2");
    assert(shape.source.attributeNS(DRAW, "style-name") == "NativeGraphic3");
    auto bounds = shape.bounds();
    near(bounds.x, kind == NativeShapeKind::Line ? 0 : 20);
    near(bounds.y, 30);
    near(bounds.width, kind == NativeShapeKind::Line ? 20 : 80);
    near(bounds.height, kind == NativeShapeKind::Line ? 10 : 40);
    auto page = shape.source.parentNode();
    assert(page.attributeNS(DRAW, "nav-order").endsWith(" native-object-2"));
    auto inserted = content(p);
    if (kind != NativeShapeKind::Line) {
      assert(at.box >= 0 && shape.text == "Český\tx\n第二行");
      p.set_box_text(0, at.box, "Edited Ω");
      assert(p.undo());
      assert(content(p) == inserted);
    }
    p.remove_shape(0, at.shape);
    auto removed = content(p);
    assert(own(p) == n);
    assert(!p.scene(0)
                .shapes[1]
                .source.parentNode()
                .attributeNS(DRAW, "nav-order")
                .contains("native-object-2"));
    auto position = p.undo();
    assert(position && position->shape == at.shape && content(p) == inserted);
    assert(p.undo() == previous && content(p) == baseline && !p.modified());
    assert(p.redo() == at && content(p) == inserted);
    assert(p.redo() && content(p) == removed);
    p.save(saved);
    const auto original = read_document_archive(source),
               output = read_document_archive(saved);
    for (auto it = original.cbegin(); it != original.cend(); ++it)
      if (it.key() != "content.xml")
        assert(output.value(it.key()) == it.value());
    p.load(source);
  }
  // Empty slides remain valid; delete nested leaves without deleting group
  // metadata.
  p.remove_shape(0, find(p, "Rich"));
  assert(content(p).contains("Retained group metadata"));
  assert(p.undo() && content(p) == baseline);
  while (own(p)) {
    int index = -1;
    for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
      if (!p.scene(0).shapes[i].master)
        index = i;
    p.remove_shape(0, index);
  }
  assert(p.count() == 1);
  p.save(saved);
  Presentation empty;
  empty.load(saved);
  assert(own(empty) == 0);
  auto at = empty.insert_shape(0, NativeShapeKind::TextBox, {1, 2, 30, 40},
                               "From empty");
  assert(at.box == 0 && own(empty) == 1);
  assert(empty.undo() && own(empty) == 0 && !empty.modified());
  p.load(source);
  const auto revision = p.revision();
  for (auto action : std::vector<std::function<void()>>{
           [&] {
             p.insert_shape(0, NativeShapeKind::Rectangle, {0, 0, 0, 20});
           },
           [&] { p.insert_shape(0, NativeShapeKind::Line, {0, 0, 0, 0}); },
           [&] {
             p.insert_shape(0, NativeShapeKind::Line, {1e7, 1e7, 1e-300, 0});
           },
           [&] {
             p.insert_shape(0, NativeShapeKind::Line, {0, 0, 1, 2},
                            "Ignored text");
           },
           [&] {
             p.insert_shape(
                 0, NativeShapeKind::Ellipse,
                 {0, 0, std::numeric_limits<double>::quiet_NaN(), 20});
           },
           [&] {
             p.insert_shape(9, NativeShapeKind::Rectangle, {0, 0, 20, 20});
           },
           [&] {
             p.insert_shape(0, static_cast<NativeShapeKind>(99),
                            {0, 0, 20, 20});
           },
           [&] { p.remove_shape(0, 0); }, [&] { p.remove_shape(0, 99); }}) {
    rejects(action);
    assert(content(p) == baseline && p.revision() == revision &&
           !p.modified() && !p.can_undo());
  }
  // External relationships are rejected atomically instead of leaving dangling
  // IDs.
  for (int mode = 0; mode < 4; ++mode) {
    auto linked = fixture();
    auto page = children(linked.body(), DRAW, "page")[0];
    auto owner = linked.xml;
    if (mode < 2) {
      auto a = owner.createElementNS(TEXT, "text:a");
      a.setAttributeNS(XLINK, "xlink:href", mode == 0 ? "#rich" : "#%72ich");
      a.appendChild(owner.createTextNode("Link"));
      child(children(page, DRAW, "rect")[0], TEXT, "p").appendChild(a);
    } else {
      auto animation = owner.createElementNS(
          "urn:oasis:names:tc:opendocument:xmlns:animation:1.0",
          "anim:animate");
      animation.setAttributeNS(SMIL,
                               mode == 2 ? "smil:targetElement" : "smil:begin",
                               mode == 2 ? "rich" : "rich.click");
      page.appendChild(animation);
    }
    linked.save(saved);
    p.load(saved);
    auto before = content(p);
    auto rev = p.revision();
    rejects([&] { p.remove_shape(0, find(p, "Rich")); });
    assert(content(p) == before && p.revision() == rev && !p.modified() &&
           !p.can_undo());
  }
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(saved);
  p.load(saved);
  auto signed_before = content(p);
  rejects(
      [&] { p.insert_shape(0, NativeShapeKind::Rectangle, {1, 2, 30, 40}); });
  rejects([&] { p.remove_shape(0, find(p, "Rich")); });
  assert(content(p) == signed_before && !p.modified());
  std::cout << "PASS native Stage object creation/deletion, text, selection, "
               "journal, names/navigation, empty slides, reference guards and "
               "failures\n";
}
