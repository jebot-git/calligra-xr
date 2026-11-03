// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace DomeOffice;
namespace {
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void near(double actual, double expected) {
  assert(std::abs(actual - expected) < 1e-9);
}
Bytes content(const Presentation &p) {
  for (const auto &shape : p.scene(0).shapes)
    if (!shape.source.isNull())
      return shape.source.ownerDocument().toByteArray();
  std::abort();
}
void moved(const SceneShape &before, const SceneShape &after, double x,
           double y) {
  near(after.transform.a, before.transform.a);
  near(after.transform.b, before.transform.b);
  near(after.transform.c, before.transform.c);
  near(after.transform.d, before.transform.d);
  near(after.transform.e, before.transform.e + x);
  near(after.transform.f, before.transform.f + y);
  assert(after.kind == before.kind && after.text == before.text &&
         after.image == before.image);
  if (after.kind == SceneShape::Frame)
    assert(after.source.attributeNS(PRESENTATION, "user-transformed") ==
           "true");
  near(after.width, before.width);
  near(after.height, before.height);
  auto a = before.bounds(), b = after.bounds();
  near(b.x, a.x + x);
  near(b.y, a.y + y);
  near(b.width, a.width);
  near(b.height, a.height);
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml = read_document_xml(R"XML(<office:document-content office:version="1.3"
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:xlink="http://www.w3.org/1999/xlink">
 <office:automatic-styles/><office:body><office:presentation>
 <draw:page draw:name="Move" draw:master-page-name="Default">
 <!-- whitespace and non-element siblings are intentionally retained -->
 <draw:g draw:transform="matrix(0 2 -3 0 0 0) translate(40pt 50pt)">
 <draw:g draw:transform="scale(-1 0.5) translate(6pt 8pt)">
 <draw:rect draw:name="Rectangle" svg:x="20pt" svg:y="30pt" svg:width="90pt" svg:height="45pt"/>
 <draw:frame draw:name="Text" svg:x="20pt" svg:y="30pt" svg:width="150pt" svg:height="50pt" draw:transform="skewX(0.1) translate(2pt 3pt)"><draw:text-box><text:p>Rich <text:span>Český</text:span></text:p></draw:text-box></draw:frame>
 <draw:frame draw:name="Image" svg:x="20pt" svg:y="30pt" svg:width="40pt" svg:height="20pt"><draw:image xlink:href="Pictures/opaque.bin"/></draw:frame>
 <draw:ellipse draw:name="Ellipse" svg:cx="20pt" svg:cy="30pt" svg:rx="20pt" svg:ry="10pt"/>
 <draw:line draw:name="Line" svg:x1="10pt" svg:y1="20pt" svg:x2="50pt" svg:y2="60pt"/>
 <draw:path draw:name="Path" svg:x="10pt" svg:y="20pt" svg:width="60pt" svg:height="40pt" svg:viewBox="0 0 100 100" svg:d="M 0 0 C 10 100 90 100 100 0"/>
 <draw:polygon draw:name="Polygon" svg:x="10pt" svg:y="20pt" svg:width="50pt" svg:height="25pt" svg:viewBox="0 0 100 100" draw:points="0,0 100,0 60,100"/>
 <draw:polyline draw:name="Polyline" svg:x="10pt" svg:y="20pt" svg:width="50pt" svg:height="25pt" svg:viewBox="0 0 100 100" draw:points="0,0 100,0 60,100"/>
 <draw:custom-shape draw:name="Custom" svg:x="20pt" svg:y="30pt" svg:width="50pt" svg:height="40pt"><draw:enhanced-geometry svg:viewBox="0 0 100 100" draw:enhanced-path="M 0 0 L 100 0 50 100 Z N"/></draw:custom-shape>
 </draw:g></draw:g>
 <draw:g draw:transform="scale(0 1)"><draw:rect draw:name="Singular" svg:width="10pt" svg:height="10pt"/></draw:g>
 </draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry("Pictures/opaque.bin", "opaque unchanged data",
              "application/octet-stream");
  return p;
}
} // namespace
int main() {
  TempDir temporary;
  auto source = temporary.path() + "/source.odp",
       saved = temporary.path() + "/saved.odp";
  auto package = fixture();
  package.save(source);
  Presentation p;
  p.load(source);
  const auto baseline = content(p);
  assert(p.scene(0).shapes.size() == 10);
  for (int index = 0; index < 9; ++index) {
    const auto original = p.scene(0).shapes[index];
    p.begin_shape_move(0, index, {12, -9});
    p.move_shape_to(0, index, {45, 11});
    moved(original, p.scene(0).shapes[index], 33, 20);
    p.move_shape_to(0, index, {48, -19});
    moved(original, p.scene(0).shapes[index], 36, -10);
    const auto changed = content(p);
    assert(p.modified());
    p.finish_handle_drag();
    assert(p.undo() && !p.modified() && content(p) == baseline);
    assert(p.redo() && content(p) == changed);
    p.save(saved);
    assert(!p.modified());
    Presentation reopened;
    reopened.load(saved);
    moved(original, reopened.scene(0).shapes[index], 36, -10);
    assert(read_document_archive(saved).value("Pictures/opaque.bin") ==
           "opaque unchanged data");
    assert(p.undo() && content(p) == baseline);
    // A canceled gesture and a return to origin preserve the existing redo.
    p.begin_shape_move(0, index, {5, 6});
    p.move_shape_to(0, index, {8, 10});
    p.finish_handle_drag(true);
    assert(content(p) == baseline && p.can_redo());
    p.begin_shape_move(0, index, {5, 6});
    p.move_shape_to(0, index, {8, 10});
    p.move_shape_to(0, index, {5, 6});
    p.finish_handle_drag();
    assert(content(p) == baseline && p.can_redo());
    p.move_shape(0, index, {});
    assert(content(p) == baseline && p.can_redo());
    p.load(source);
  }
  // Saving an active move closes one native gesture and records the save point.
  p.begin_shape_move(0, 0, {});
  p.move_shape_to(0, 0, {17, 3});
  p.save(saved);
  assert(!p.dragging_handle() && !p.modified());
  assert(p.undo() && content(p) == baseline && p.modified());
  assert(p.redo() && !p.modified());
  p.load(source);
  // The input is finite, but the complete candidate exceeds scene bounds.
  rejects([&] { p.move_shape(0, 0, {1e9, 1e9}); });
  rejects([&] { p.begin_shape_move(0, 9, {}); });
  rejects([&] {
    p.move_shape(0, 0, {std::numeric_limits<double>::quiet_NaN(), 0});
  });
  rejects([&] { p.move_shape(0, 0, {1e10, 0}); });
  rejects([&] { p.begin_shape_move(3, 0, {}); });
  rejects([&] { p.begin_shape_move(0, 99, {}); });
  rejects([&] { p.move_shape_to(0, 0, {3, 4}); });
  assert(content(p) == baseline && !p.modified() && !p.can_undo());
  // Starting a different gesture commits the prior one; both remain undoable.
  p.begin_shape_move(0, 0, {});
  p.move_shape_to(0, 0, {7, 9});
  p.move_shape(0, 1, {-2, 4});
  assert(p.undo() && p.undo() && content(p) == baseline);
  p.load(source);
  p.begin_shape_move(0, 0, {});
  p.move_shape_to(0, 0, {7, 9});
  const auto valid = content(p);
  rejects([&] {
    p.move_shape_to(0, 0, {std::numeric_limits<double>::infinity(), 0});
  });
  assert(content(p) == valid);
  p.finish_handle_drag(true);
  assert(content(p) == baseline && !p.modified());
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(saved);
  p.load(saved);
  rejects([&] { p.move_shape(0, 0, {1, 2}); });
  assert(!p.modified());
  std::cout << "PASS native Stage moves, nested affine groups, all supported "
               "leaf kinds, undo/redo, cancel, failures and ODP reopen\n";
}
