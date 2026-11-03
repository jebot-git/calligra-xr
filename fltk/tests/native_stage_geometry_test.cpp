// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_path.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
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
void near(double a, double b, double tolerance = 1e-8) {
  if (std::abs(a - b) > tolerance)
    std::cerr << a << " != " << b << '\n';
  assert(std::abs(a - b) <= tolerance);
}
int find(const Presentation &p, const Text &name) {
  auto &s = p.scene(0).shapes;
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i].name == name)
      return i;
  std::abort();
}
Bytes content(const Presentation &p) {
  for (auto &s : p.scene(0).shapes)
    if (!s.source.isNull())
      return s.source.ownerDocument().toByteArray();
  std::abort();
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.3" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:body><office:presentation><draw:page draw:name="Geometry" draw:master-page-name="Default">
<draw:g draw:transform="matrix(0 2 -3 0 0 0) translate(400pt 80pt)"><draw:g draw:transform="skewX(0.2) scale(-1 0.5)">
<draw:frame draw:name="Text" svg:x="7mm" svg:y="11pt" svg:width="4cm" svg:height="3cm" draw:transform="translate(5pt 8pt)"><draw:text-box><text:p>Native <text:span>Český Ω</text:span> rich text</text:p></draw:text-box></draw:frame>
<draw:frame draw:name="Image" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt"><draw:image xlink:href="Pictures/opaque.bin"/></draw:frame>
<draw:rect draw:name="Rect" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt" draw:corner-radius="4pt"/>
<draw:ellipse draw:name="Ellipse" svg:x="10pt" svg:y="20pt" svg:width="3cm" svg:height="2cm"/>
<draw:custom-shape draw:name="Custom" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt"><draw:enhanced-geometry svg:viewBox="0 0 100 100" draw:enhanced-path="M 0 0 L 100 0 50 100 Z N"/></draw:custom-shape>
<draw:path draw:name="Curve" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt" svg:viewBox="0 0 100 100" svg:d="M0 0 C10 100 90 100 100 0"/>
<draw:polygon draw:name="Polygon" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt" svg:viewBox="0 0 100 100" draw:points="0,0 100,0 60,100"/>
<draw:polyline draw:name="Polyline" svg:x="10pt" svg:y="20pt" svg:width="90pt" svg:height="60pt" svg:viewBox="0 0 100 100" draw:points="0,0 100,0 60,100"/>
<draw:line draw:name="Line" svg:x1="1cm" svg:y1="20pt" svg:x2="100pt" svg:y2="50pt"/>
</draw:g></draw:g>
<draw:circle draw:name="Circle" svg:cx="50pt" svg:cy="70pt" svg:r="30pt"><text:p>Circle source</text:p></draw:circle>
<draw:path draw:name="Unmapped" svg:d="M20 30 C40 150 160 150 180 30"/>
<draw:path draw:name="Vertical" svg:d="M10 20 L10 120"/>
<draw:path draw:name="LargeArc" svg:d="M0 0 L30000000 0 A30 15 27 1 1 30000040 20"/>
<draw:polygon draw:name="RawPolygon" draw:points="20,30 100,20 80,140"/>
<draw:g draw:transform="scale(0 1)"><draw:frame draw:name="Collapsed" svg:width="50pt" svg:height="40pt"><draw:text-box><text:p>Collapsed</text:p></draw:text-box></draw:frame></draw:g>
<draw:line draw:name="Zero" svg:x1="20pt" svg:y1="30pt" svg:x2="20pt" svg:y2="30pt"/>
</draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry("Pictures/opaque.bin", "original image data",
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  TempDir directory;
  auto input = directory.path() + "/input.odp",
       output = directory.path() + "/output.odp";
  if (argc == 4 && Text(argv[1]) == "--curve-document") {
    Presentation curve;
    curve.load(Text(argv[2]));
    NativeShapeGeometryEdit size;
    size.width = 3000000;
    size.height = 5000;
    curve.set_shape_geometry(0, find(curve, "LargeArc"), size);
    curve.save(Text(argv[3]));
    return 0;
  }
  if (argc == 4 && Text(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto before = content(p);
    for (auto name : {Text("Rectangle"), Text("GroupedText"), Text("Image"),
                      Text("Triangle"), Text("Line")}) {
      NativeShapeGeometryEdit e;
      if (name == "Rectangle") {
        e.x = 50;
        e.y = 50;
        e.width = 100;
        e.height = 60;
        e.rotation = 30;
      }
      if (name == "GroupedText") {
        e.x = 230;
        e.y = 70;
        e.width = 95;
        e.height = 115;
        e.rotation = 15;
      }
      if (name == "Image") {
        e.x = 140;
        e.y = 110;
        e.width = 70;
        e.height = 40;
        e.rotation = 15;
      }
      if (name == "Triangle") {
        e.x = 240;
        e.y = 130;
        e.width = 90;
        e.height = 50;
        e.rotation = 0;
      }
      if (name == "Line") {
        e.x = 30;
        e.y = 205;
        e.width = 310;
        e.rotation = -8;
      }
      p.set_shape_geometry(0, find(p, name), e);
    }
    auto after = content(p);
    for (int i = 0; i < 5; ++i)
      assert(p.undo());
    assert(content(p) == before && !p.modified());
    for (int i = 0; i < 5; ++i)
      assert(p.redo());
    assert(content(p) == after);
    p.save(Text(argv[3]));
    std::cout
        << "PASS independent Stage geometry, exact history and ODP export\n";
    return 0;
  }
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  auto baseline = content(p);
  for (auto name : {"Text", "Image", "Rect", "Ellipse", "Custom", "Curve",
                    "Polygon", "Polyline"}) {
    int index = find(p, name);
    auto old = p.scene(0).shapes[index];
    auto initial = p.shape_geometry(0, index);
    assert(initial.width > 0 && initial.height > 0 && initial.rotation);
    NativeShapeGeometryEdit e;
    e.x = 120;
    e.y = 150;
    e.width = initial.width * .7;
    e.height = initial.height * 1.3;
    e.rotation = 20;
    p.set_shape_geometry(0, index, e);
    auto got = p.shape_geometry(0, index);
    near(got.origin.x, 120);
    near(got.origin.y, 150);
    near(got.width, *e.width);
    near(got.height, *e.height);
    near(*got.rotation, 20);
    const auto &changed = p.scene(0).shapes[index];
    assert(changed.text == old.text && changed.image == old.image);
    near(changed.paint.font.size, old.paint.font.size);
    near(std::hypot(changed.transform.a, changed.transform.b),
         std::hypot(old.transform.a, old.transform.b));
    near(std::hypot(changed.transform.c, changed.transform.d),
         std::hypot(old.transform.c, old.transform.d));
    near(changed.transform.a * changed.transform.d -
             changed.transform.b * changed.transform.c,
         old.transform.a * old.transform.d - old.transform.b * old.transform.c);
    if (name == Text("Rect"))
      assert(changed.source.attributeNS(DRAW, "corner-radius") == "4pt");
    auto edited = content(p);
    assert(p.undo() && content(p) == baseline && !p.modified());
    assert(p.redo() && content(p) == edited);
    p.save(output);
    Presentation reopened;
    reopened.load(output);
    near(reopened.shape_geometry(0, index).width, *e.width);
    auto source = read_document_archive(input),
         saved = read_document_archive(output);
    for (auto it = source.cbegin(); it != source.cend(); ++it)
      if (it.key() != "content.xml")
        assert(saved.value(it.key()) == it.value());
    p.load(input);
  }
  // Height-only editing retains untouched dimension units and transforms.
  for (auto name : {"Text", "Ellipse"}) {
    int i = find(p, name);
    auto width = p.scene(0).shapes[i].source.attributeNS(SVG, "width");
    auto transform = p.scene(0).shapes[i].source.attributeNS(DRAW, "transform");
    NativeShapeGeometryEdit e;
    e.height = 80;
    p.set_shape_geometry(0, i, e);
    assert(p.scene(0).shapes[i].source.attributeNS(SVG, "width") == width &&
           p.scene(0).shapes[i].source.attributeNS(DRAW, "transform") ==
               transform);
    p.load(input);
  }
  int circle = find(p, "Circle");
  auto c = p.shape_geometry(0, circle);
  NativeShapeGeometryEdit size;
  size.width = 80;
  size.height = 50;
  p.set_shape_geometry(0, circle, size);
  auto cg = p.shape_geometry(0, circle);
  near(cg.origin.x, c.origin.x);
  near(cg.origin.y, c.origin.y);
  assert(p.scene(0).shapes[circle].source.localName() == "ellipse" &&
         !p.scene(0).shapes[circle].source.hasAttributeNS(SVG, "r"));
  p.load(input);
  int unmapped = find(p, "Unmapped");
  auto u = p.shape_geometry(0, unmapped);
  auto d = p.scene(0).shapes[unmapped].source.attributeNS(SVG, "d");
  size.width = u.width * 2;
  size.height = u.height * .5;
  p.set_shape_geometry(0, unmapped, size);
  assert(p.scene(0).shapes[unmapped].source.attributeNS(SVG, "d") == d &&
         p.scene(0).shapes[unmapped].source.hasAttributeNS(SVG, "viewBox"));
  near(p.shape_geometry(0, unmapped).width, *size.width);
  p.load(input);
  int vertical = find(p, "Vertical");
  assert(!p.shape_geometry(0, vertical).width_enabled);
  NativeShapeGeometryEdit h;
  h.height = 200;
  p.set_shape_geometry(0, vertical, h);
  near(p.shape_geometry(0, vertical).height, 200);
  assert(!p.scene(0).shapes[vertical].source.hasAttributeNS(SVG, "viewBox"));
  p.load(input);
  int large = find(p, "LargeArc");
  auto original_arc = p.scene(0).shapes[large].source.attributeNS(SVG, "d");
  auto arc_before = path_bounds(*p.scene(0).shapes[large].path);
  size.width = 3000000;
  size.height = 5000000;
  p.set_shape_geometry(0, large, size);
  auto arc_after = path_bounds(*p.scene(0).shapes[large].path);
  near(p.shape_geometry(0, large).width, 3000000);
  near(arc_after.width, 3000000, 1e-4);
  near(arc_after.height, 5000000, 1e-4);
  near(arc_after.x, arc_before.x * 3000000 / arc_before.width, 1e-4);
  near(arc_after.y, arc_before.y * 5000000 / arc_before.height, 1e-4);
  assert(p.scene(0).shapes[large].source.attributeNS(SVG, "d") != original_arc);
  p.load(input);
  int polygon = find(p, "RawPolygon");
  auto poly = p.shape_geometry(0, polygon);
  size.width = poly.width * 2;
  size.height = poly.height * 2;
  p.set_shape_geometry(0, polygon, size);
  near(p.shape_geometry(0, polygon).width, *size.width);
  p.load(input);
  int collapsed = find(p, "Collapsed");
  assert(!p.shape_geometry(0, collapsed).position_enabled);
  h.height = 90;
  p.set_shape_geometry(0, collapsed, h);
  near(p.shape_geometry(0, collapsed).height, 90);
  p.load(input);
  int line = find(p, "Line");
  auto l = p.shape_geometry(0, line);
  NativeShapeGeometryEdit le;
  le.width = 120;
  le.rotation = 35;
  p.set_shape_geometry(0, line, le);
  auto lg = p.shape_geometry(0, line);
  near(lg.origin.x, l.origin.x);
  near(lg.origin.y, l.origin.y);
  near(lg.width, 120);
  near(*lg.rotation, 35);
  assert(p.scene(0).shapes[line].source.attributeNS(SVG, "x1") == "1cm");
  p.load(input);
  int zero = find(p, "Zero");
  le = {};
  le.x = 40;
  le.y = 50;
  p.set_shape_geometry(0, zero, le);
  near(p.shape_geometry(0, zero).origin.x, 40);
  le.width = 30;
  le.rotation = 90;
  p.set_shape_geometry(0, zero, le);
  near(p.shape_geometry(0, zero).width, 30);
  p.load(input);
  // No-op/cosmetic rotations retain exact XML and an existing redo branch.
  p.move_shape(0, find(p, "Rect"), {1, 2});
  assert(p.undo());
  auto r = p.shape_geometry(0, find(p, "Rect"));
  NativeShapeGeometryEdit noop;
  noop.rotation = *r.rotation + 360;
  p.set_shape_geometry(0, find(p, "Rect"), noop);
  assert(content(p) == baseline && p.can_redo() && !p.modified());
  auto revision = p.revision();
  for (auto e : std::vector<NativeShapeGeometryEdit>{
           NativeShapeGeometryEdit{.width = -1},
           NativeShapeGeometryEdit{.x =
                                       std::numeric_limits<double>::infinity()},
           NativeShapeGeometryEdit{.height = 1e8}}) {
    rejects([&] { p.set_shape_geometry(0, find(p, "Rect"), e); });
    assert(content(p) == baseline && p.revision() == revision && p.can_redo());
  }
  NativeShapeGeometryEdit invalid;
  invalid.width = 10;
  rejects([&] { p.set_shape_geometry(0, find(p, "Vertical"), invalid); });
  invalid = {};
  invalid.x = 30;
  rejects([&] { p.set_shape_geometry(0, collapsed, invalid); });
  assert(content(p) == baseline);
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(output);
  p.load(output);
  rejects([&] { p.set_shape_geometry(0, find(p, "Rect"), invalid); });
  assert(!p.modified());
  std::cout << "PASS native Stage pose/size/rotation, affine groups, lines, "
               "curves, radius forms, fonts, units, history and failures\n";
}
