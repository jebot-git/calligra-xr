// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
void near(ScenePoint a, ScenePoint b) {
  near(a.x, b.x);
  near(a.y, b.y);
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
int shape(const Presentation &p, const Text &name) {
  for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
    if (p.scene(0).shapes[i].name == name)
      return int(i);
  std::abort();
}
Bytes content(const Presentation &p) {
  return p.scene(0)
      .shapes.at(shape(p, "Text"))
      .source.ownerDocument()
      .toByteArray();
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  // Include a legacy affine group as a consumer-extension regression. The
  // independent odfpy UI corpus supplies the strict ODF1.2 package cases.
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:body><office:presentation><draw:page draw:name="Gesture" draw:master-page-name="Default"><draw:g draw:transform="matrix(1.2 0.3 -0.2 0.8 40 30)"><draw:frame draw:name="Text" xml:id="text-frame" draw:transform="matrix(-1.2 0.4 0.3 1.7 200 20)" svg:x="10pt" svg:y="20pt" svg:width="100pt" svg:height="80pt"><draw:text-box><text:p xml:id="paragraph">A<text:span>Český</text:span> Ω</text:p></draw:text-box></draw:frame><draw:frame draw:name="Image" svg:x="10pt" svg:y="20pt" svg:width="100pt" svg:height="80pt"><draw:image xlink:type="simple" xlink:href="Pictures/source.svg"/></draw:frame></draw:g><draw:line draw:name="Line" svg:x1="10pt" svg:y1="20pt" svg:x2="110pt" svg:y2="50pt"/><draw:circle draw:name="Circle" svg:cx="200pt" svg:cy="150pt" svg:r="30pt"/></draw:page><draw:page draw:name="Unchanged" draw:master-page-name="Default"><draw:rect svg:x="20pt" svg:y="20pt" svg:width="50pt" svg:height="40pt"/></draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry(
      "Pictures/source.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>)",
      "image/svg+xml");
  p.set_entry("opaque/source", Bytes("keep\0source", 11),
              "application/octet-stream");
  return p;
}
} // namespace
int main() {
  TempDir directory;
  auto input = directory.path() + "/input.odp",
       output = directory.path() + "/output.odp";
  auto package = fixture();
  package.save(input);
  Presentation p;
  for (const auto &name : {Text("Text"), Text("Image"), Text("Circle")}) {
    p.load(input);
    auto index = shape(p, name);
    const auto original = p.scene(0).shapes.at(index);
    const auto before = content(p);
    auto old = p.shape_geometry(0, index);
    ScenePoint start = original.transform.map({old.width, old.height});
    p.begin_shape_transform(0, index, NativeShapeHandle::SouthEast, start);
    p.transform_shape_to(0, index, {start.x + 20, start.y + 30});
    assert(p.modified() && p.can_undo());
    auto changed = content(p);
    assert(changed != before);
    // A preview is always based on the immutable starting frame.
    p.transform_shape_to(0, index, {start.x + 20, start.y + 30});
    assert(content(p) == changed);
    const auto &preview = p.scene(0).shapes.at(index);
    near(preview.transform.map({}), original.transform.map({}));
    assert(preview.text == original.text);
    near(preview.paint.font.size, original.paint.font.size);
    p.finish_handle_drag(true);
    assert(content(p) == before && !p.modified() && !p.can_undo());
    p.begin_shape_transform(0, index, NativeShapeHandle::SouthEast, start);
    // Crossing the fixed corner reflects both axes while retaining positive
    // ODF dimensions and the original frame's source identifiers/content.
    auto crossing = original.transform.map({-20, -30});
    p.transform_shape_to(0, index, crossing);
    auto reflected = p.scene(0).shapes.at(index);
    near(reflected.width, 20);
    near(reflected.height, 30);
    near(reflected.transform.map({}), original.transform.map({}));
    near(reflected.transform.map({20, 30}), crossing);
    assert(reflected.text == original.text);
    p.finish_handle_drag();
    changed = content(p);
    assert(p.can_undo());
    p.save(output);
    assert(!p.modified());
    auto at = p.undo();
    assert(at && at->shape == index && content(p) == before && p.modified() && !p.can_undo());
    // A no-op gesture on the undo branch must retain the redo command and
    // restore the exact old attribute spelling/absence.
    p.begin_shape_transform(0, index, NativeShapeHandle::SouthEast, start);
    p.transform_shape_to(0, index, {start.x + 10, start.y + 10});
    p.transform_shape_to(0, index, start);
    p.finish_handle_drag();
    assert(content(p) == before && p.can_redo());
    assert(p.redo() && content(p) == changed && !p.modified());
    Presentation reopened;
    reopened.load(output);
    near(reopened.scene(0).shapes.at(index).width, 20);
    auto a = read_document_archive(input), b = read_document_archive(output);
    for (auto i = a.cbegin(); i != a.cend(); ++i)
      if (i.key() != "content.xml")
        assert(b.value(i.key()) == i.value());
    p.undo();
    p.begin_shape_transform(0, index, NativeShapeHandle::SouthEast, start);
    rejects([&] { p.transform_shape_to(0, index, {NAN, 2}); });
    p.finish_handle_drag(true);
    assert(content(p) == before && p.can_redo());
  }
  p.load(input);
  auto index = shape(p, "Text");
  auto original = p.scene(0).shapes.at(index);
  auto center = original.transform.map({50, 40});
  p.begin_shape_transform(0, index, NativeShapeHandle::Rotate,
                          {center.x + 80, center.y});
  p.transform_shape_to(0, index, {center.x, center.y + 80});
  auto rotated = p.scene(0).shapes.at(index);
  near(rotated.transform.map({50, 40}), center);
  near(rotated.transform.a, -original.transform.b);
  near(rotated.transform.b, original.transform.a);
  assert(rotated.text == original.text);
  p.finish_handle_drag();
  assert(p.undo());
  index = shape(p, "Line");
  original = p.scene(0).shapes.at(index);
  auto a = original.transform.map(original.contours[0].points[0]),
       b = original.transform.map(original.contours[0].points[1]);
  p.begin_shape_transform(0, index, NativeShapeHandle::LineStart, a);
  p.transform_shape_to(0, index, {a.x + 30, a.y + 20});
  const auto &line = p.scene(0).shapes.at(index);
  near(line.transform.map(line.contours[0].points[0]), {a.x + 30, a.y + 20});
  near(line.transform.map(line.contours[0].points[1]), b);
  p.finish_handle_drag();
  assert(p.undo());
  auto baseline = content(p);
  rejects([&] { p.transform_shape_to(0, index, {}); });
  assert(content(p) == baseline);
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  rejects([&] {
    p.begin_shape_transform(0, shape(p, "Text"), NativeShapeHandle::East, {});
  });
  assert(!p.modified());
  std::cout
      << "PASS native candidate gesture previews, mirrored frame/image/circle "
         "resize, center rotation, line endpoints, one-command history, "
         "cancel/redo/save/reopen, text/asset preservation and guards\n";
}
