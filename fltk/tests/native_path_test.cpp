// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_path.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
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
void near(double a, double b, double tolerance = 1e-7) {
  assert(std::abs(a - b) < tolerance);
}
void point(ScenePoint p, double x, double y) {
  near(p.x, x);
  near(p.y, y);
}
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
XmlElement element(XmlElement parent, const Text &ns, const Text &name) {
  auto e = parent.ownerDocument().createElementNS(ns, name);
  parent.appendChild(e);
  return e;
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles>
<style:style style:name="Label" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans" fo:font-size="12pt" fo:color="#24466e"/></style:style>
<style:style style:name="Blue" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#4488ee"/></style:style>
<style:style style:name="Green" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#44aa66"/></style:style>
<style:style style:name="Red" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#bb3344"/></style:style>
<style:style style:name="Orange" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#ff9000"/></style:style>
<style:style style:name="EvenOdd" style:family="graphic" style:parent-style-name="Blue"><style:graphic-properties xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" svg:fill-rule="evenodd"/></style:style>
</office:styles><office:automatic-styles><style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="480pt" fo:page-height="300pt"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Wide"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  auto page = element(p.body(), DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Native curve paths");
  page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
  auto shape = [&](const Text &type, const Text &name, const Text &paint,
                   double x, double y, double w, double h) {
    auto node = element(page, DRAW, "draw:" + type);
    node.setAttributeNS(DRAW, "draw:name", name);
    node.setAttributeNS(DRAW, "draw:style-name", paint);
    for (auto [key, n] : std::vector<std::pair<Text, double>>{
             {"x", x}, {"y", y}, {"width", w}, {"height", h}})
      node.setAttributeNS(SVG, "svg:" + key, Text::number(n) + "pt");
    return node;
  };
  auto title = shape("frame", "Title", "", 12, 5, 450, 30);
  auto text = element(element(title, DRAW, "draw:text-box"), TEXT, "text:p");
  text.setAttributeNS(TEXT, "text:style-name", "Label");
  set_plain_text(text, "Native curves — compound paths and rounded shapes");
  auto path = [&](const Text &name, const Text &paint, double x, double y,
                  double w, double h, const Text &data) {
    auto node = shape("path", name, paint, x, y, w, h);
    node.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
    node.setAttributeNS(SVG, "svg:d", data);
    return node;
  };
  path("Cubic", "Blue", 20, 45, 100, 100, "M0 100 C0 0 100 0 100 100 Z");
  path("Donut", "EvenOdd", 145, 45, 100, 100,
       "M0 0 H100 V100 H0 Z M25 25 H75 V75 H25 Z");
  auto rounded = shape("rect", "Rounded", "Green", 275, 45, 100, 100);
  rounded.setAttributeNS(DRAW, "draw:corner-radius", "25pt");
  auto pie = shape("ellipse", "Pie", "Orange", 20, 175, 100, 90);
  pie.setAttributeNS(DRAW, "draw:kind", "section");
  pie.setAttributeNS(DRAW, "draw:start-angle", "0");
  pie.setAttributeNS(DRAW, "draw:end-angle", "270");
  auto chord = shape("ellipse", "Chord", "Green", 145, 175, 100, 90);
  chord.setAttributeNS(DRAW, "draw:kind", "cut");
  chord.setAttributeNS(DRAW, "draw:start-angle", "0");
  chord.setAttributeNS(DRAW, "draw:end-angle", "180");
  path("Open fill", "Red", 275, 175, 100, 90, "M0 100 L50 0 100 100");
  path("Nonzero winding", "Blue", 395, 175, 65, 90,
       "M0 0 H100 V100 H0 Z M25 25 H75 V75 H25 Z");
  auto transformed = path("Transformed quadratic", "Orange", 0, 0, 60, 60,
                          "M0 100 Q50 -20 100 100 T200 100 Z");
  transformed.setAttributeNS(DRAW, "draw:transform",
                             "rotate(0.2) translate(395pt 50pt)");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--dump") {
    auto path = parse_svg_path(Text::fromUtf8(argv[2]));
    auto m = argc > 3 ? parse_scene_transform(Text::fromUtf8(argv[3]))
                      : SceneTransform{};
    auto flat = flatten_path(path, m, {}, .02);
    auto bounds = path_bounds(path, m);
    nlohmann::json out = {
        {"bounds", {bounds.x, bounds.y, bounds.width, bounds.height}},
        {"contours", nlohmann::json::array()}};
    for (const auto &c : flat) {
      nlohmann::json points = nlohmann::json::array();
      for (auto p : c.points)
        points.push_back({p.x, p.y});
      out["contours"].push_back({{"closed", c.closed}, {"points", points}});
    }
    std::cout << out.dump() << '\n';
    return 0;
  }
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation d;
    d.load(Text::fromUtf8(argv[2]));
    const auto &scene = d.scene(0);
    assert(scene.shapes.size() == 2 && scene.shapes[0].path &&
           scene.shapes[1].path);
    auto b = scene.shapes[0].bounds();
    near(b.x, 20);
    near(b.y, 70);
    near(b.width, 100);
    near(b.height, 75);
    assert(scene.shapes[0].hit({70, 100}) && !scene.shapes[0].hit({25, 50}));
    assert(!scene.shapes[1].hit({151, 46}));
    TempDir t;
    auto saved = t.path() + "/saved.odp";
    d.save(saved);
    Presentation again;
    again.load(saved);
    assert(again.scene(0).shapes.size() == 2);
    assert(read_document_archive(saved).value("styles.xml") ==
           read_document_archive(Text::fromUtf8(argv[2])).value("styles.xml"));
    std::cout << "PASS independent odfpy curves, viewBox, rounded rectangle, "
                 "hit tests and round trip\n";
    return 0;
  }
  auto p = parse_svg_path("m10 20 10 0 v10 h-10 z l5 5");
  assert(p.subpaths.size() == 2 && p.subpaths[0].closed);
  point(p.subpaths[1].start, 10, 20);
  point(p.subpaths[1].segments[0].end, 15, 25);
  p = parse_svg_path("M.5.6L1e2-2e1");
  point(p.subpaths[0].start, .5, .6);
  point(p.subpaths[0].segments[0].end, 100, -20);
  p = parse_svg_path(
      "M0 0 C10 20 30 40 50 60 s10 20 30 40 L5 6 S10 20 30 40 Q1 2 3 4 t5 6");
  auto &segments = p.subpaths[0].segments;
  point(segments[1].control1, 70, 80);
  point(segments[3].control1, 5, 6);
  point(segments[5].control1, 5, 6);
  point(segments[5].end, 8, 10);
  p = parse_svg_path("M0 0 A10 20 30 0110 20");
  assert(p.subpaths[0].segments[0].sweep &&
         !p.subpaths[0].segments[0].large_arc);
  point(p.subpaths[0].segments[0].end, 10, 20);
  auto cubic = parse_svg_path("M0 0 C0 100 100 100 100 0");
  auto box = path_bounds(cubic);
  near(box.x, 0);
  near(box.y, 0);
  near(box.width, 100);
  near(box.height, 75);
  box = path_bounds(cubic, {2, 0, 0, 3, 5, -7});
  near(box.x, 5);
  near(box.y, -7);
  near(box.width, 200);
  near(box.height, 225);
  box = path_bounds(parse_svg_path("M0 0Q50 100 100 0"));
  near(box.height, 50);
  box = path_bounds(parse_svg_path("M1000 1000 M0 0L10 10"));
  near(box.width, 10);
  near(box.height, 10);
  box = path_bounds(parse_svg_path("M0 0C100 0 -100 0 0 0"));
  near(box.x, -100 / std::sqrt(12));
  near(box.width, 200 / std::sqrt(12));
  box = path_bounds(parse_svg_path("M0 0 A10 10 0 0 1 100 0"));
  near(box.width, 100);
  near(box.y, -50);
  near(box.height, 50);
  box = path_bounds(parse_svg_path("M0 0 A0 30 0 0 1 100 50"));
  near(box.width, 100);
  near(box.height, 50);
  auto small = flatten_path(cubic),
       large = flatten_path(cubic, {}, SceneTransform{20, 0, 0, 20});
  assert(large[0].points.size() > small[0].points.size());
  auto round = rounded_rectangle_path(100, 60, 1000, 1000);
  box = path_bounds(round);
  near(box.width, 100);
  near(box.height, 60);
  auto ellipse = ellipse_path(100, 60);
  auto rotation = parse_scene_transform("rotate(0.7853981633974483)");
  box = path_bounds(ellipse, rotation);
  double radius = std::sqrt(1700.);
  near(box.x, 20 / std::sqrt(2.) - radius);
  near(box.y, 80 / std::sqrt(2.) - radius);
  near(box.width, 2 * radius);
  near(box.height, 2 * radius);
  SceneShape shape;
  shape.kind = SceneShape::Rectangle;
  shape.path = std::make_shared<ScenePath>(round);
  shape.paint.fill = true;
  shape.contours = flatten_path(round);
  assert(!shape.hit({1, 1}) && shape.hit({50, 30}));
  TempDir directory;
  auto file = directory.path() + "/paths.odp";
  auto package = fixture();
  package.save(file);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
    assert(File::copy(file, Text::fromUtf8(argv[2])));
  Presentation document;
  document.load(file);
  const auto &scene = document.scene(0);
  assert(scene.shapes.size() == 9 && scene.editable_sources.size() == 1);
  auto find = [&](const Text &name) -> const SceneShape & {
    for (const auto &s : scene.shapes)
      if (s.name == name)
        return s;
    throw std::runtime_error("Missing shape");
  };
  assert(find("Cubic").hit({70, 100}) && !find("Cubic").hit({25, 50}));
  near(find("Cubic").bounds().y, 70);
  assert(find("Donut").hit({150, 50}) && !find("Donut").hit({195, 95}));
  auto winding = find("Donut");
  winding.paint.even_odd = false;
  assert(winding.hit({195, 95}));
  assert(!find("Rounded").hit({276, 46}) && find("Rounded").hit({325, 95}));
  assert(find("Open fill").hit({325, 240}));
  assert(find("Pie").hit({50, 210}) && !find("Pie").hit({95, 245}));
  assert(find("Chord").hit({195, 195}) && !find("Chord").hit({195, 245}));
  auto saved = directory.path() + "/saved.odp";
  document.set_box_text(0, 0, "Edited path gallery");
  document.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(reopened.scene(0).shapes.size() == 9 &&
         reopened.slide(0).boxes[0].text == "Edited path gallery");
  assert(read_document_archive(saved).value("styles.xml") ==
         package.entry("styles.xml"));
  for (auto data : {"L0 0", "M0", "M0 0L", "M,0 0", "M0 0,", "M0 0,,1 1",
                    "M0 0Z1 2", "M0 0 A1 2 0 2 0 3 4", "M0 0 C1 2 3 4 5",
                    "M0 0R1 2", "Mnan 0", "M+-1 0", "M1e300 0"})
    rejects([&] { parse_svg_path(data); });
  rejects([&] { flatten_path(cubic, {}, {}, 0); });
  rejects([&] { flatten_path(cubic, {}, {}, .00001, 4); });
  auto broken = fixture();
  broken.xml.elementsByTagNameNS(DRAW, "path")[0].toElement().setAttributeNS(
      SVG, "svg:d", "M0 0 LNaN 5");
  broken.save(file);
  rejects([&] { reopened.load(file); });
  assert(reopened.slide(0).boxes[0].text == "Edited path gallery");
  broken = fixture();
  broken.xml.elementsByTagNameNS(DRAW, "path")[0].toElement().setAttributeNS(
      SVG, "svg:viewBox", "0 0 0 10");
  broken.save(file);
  rejects([&] { reopened.load(file); });
  std::cout << "PASS native SVG commands, reflected controls, corrected arcs, "
               "analytic bounds, adaptive curves, compound fill/hit tests, "
               "rounded/partial ellipses and ODP preservation\n";
}
