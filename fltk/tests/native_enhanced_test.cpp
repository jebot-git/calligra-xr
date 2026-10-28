// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_enhanced_geometry.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-6); }
XmlElement element(XmlElement p, const Text &ns, const Text &name) {
  auto e = p.ownerDocument().createElementNS(ns, name);
  p.appendChild(e);
  return e;
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles>
<style:style style:name="Caption" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans" fo:font-size="12pt" fo:color="#24466e"/></style:style>
<style:style style:name="Label" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans" fo:font-size="12pt" fo:color="#ffffff"/><style:paragraph-properties fo:text-align="center"/></style:style>
<style:style style:name="Blue" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#4488ee" fo:padding="2pt" draw:textarea-vertical-align="middle"/></style:style>
<style:style style:name="Green" style:family="graphic" style:parent-style-name="Blue"><style:graphic-properties draw:fill-color="#44aa66"/></style:style>
<style:style style:name="Parts" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#ff9000" draw:stroke="solid" svg:stroke-color="#bb3344" svg:stroke-width="6pt"/></style:style>
<style:style style:name="Alpha" style:family="graphic" style:parent-style-name="Blue"><style:graphic-properties draw:opacity="50%"/></style:style>
</office:styles><office:automatic-styles><style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="480pt" fo:page-height="300pt"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Wide"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  auto page = element(p.body(), DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Native enhanced geometry");
  page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
  auto box = [&](const Text &type, const Text &name, const Text &style,
                 double x, double y, double w, double h) {
    auto n = element(page, DRAW, "draw:" + type);
    n.setAttributeNS(DRAW, "draw:name", name);
    n.setAttributeNS(DRAW, "draw:style-name", style);
    for (auto [key, value] : std::vector<std::pair<Text, double>>{
             {"x", x}, {"y", y}, {"width", w}, {"height", h}})
      n.setAttributeNS(SVG, "svg:" + key, Text::number(value) + "pt");
    return n;
  };
  auto title = box("frame", "Title", "", 12, 5, 450, 30);
  auto para = element(element(title, DRAW, "draw:text-box"), TEXT, "text:p");
  para.setAttributeNS(TEXT, "text:style-name", "Caption");
  set_plain_text(para, "Native equations, custom shapes and path paint sets");
  auto geometry = [&](XmlElement shape, const Text &path) {
    auto g = element(shape, DRAW, "draw:enhanced-geometry");
    g.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
    g.setAttributeNS(DRAW, "draw:enhanced-path", path);
    return g;
  };
  for (int i = 0; i < 2; ++i) {
    auto shape = box("custom-shape", i ? "Mirrored" : "Arrow",
                     i ? "Green" : "Blue", i ? 230 : 20, 55, 180, 85);
    auto text = element(shape, TEXT, "text:p");
    text.setAttributeNS(TEXT, "text:style-name", "Label");
    set_plain_text(text, i ? "Mirrored text" : "Editable arrow");
    auto g = geometry(shape, "M0 ?top L?base ?top ?base 0 ?right ?middle ?base "
                             "?height ?base ?bottom 0 ?bottom Z N");
    g.setAttributeNS(DRAW, "draw:modifiers", "65 25");
    g.setAttributeNS(DRAW, "draw:text-areas", "5 ?top ?base ?bottom");
    if (i)
      g.setAttributeNS(DRAW, "draw:mirror-horizontal", "true");
    for (auto [name, formula] :
         std::vector<std::pair<Text, Text>>{{"base", "$0"},
                                            {"top", "$1"},
                                            {"bottom", "height-$1"},
                                            {"right", "width"},
                                            {"height", "height"},
                                            {"middle", "height/2"}}) {
      auto e = element(g, DRAW, "draw:equation");
      e.setAttributeNS(DRAW, "draw:name", name);
      e.setAttributeNS(DRAW, "draw:formula", formula);
    }
  }
  geometry(box("custom-shape", "Parts", "Parts", 20, 180, 180, 85),
           "M0 0 L40 0 40 100 0 100 Z F N M60 0 L100 0 100 100 60 100 Z S N M0 "
           "50 L100 50 F N");
  geometry(box("custom-shape", "Compound", "Alpha", 230, 180, 85, 85),
           "M0 0 L100 0 100 100 0 100 Z M25 25 L75 25 75 75 25 75 Z N M50 50 "
           "L100 50 100 100 50 100 Z N");
  geometry(box("custom-shape", "Arc", "Green", 350, 180, 100, 85),
           "U50 50 45 45 0 270 L50 50 Z N");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation d;
    d.load(Text::fromUtf8(argv[2]));
    const auto &s = d.scene(0);
    assert(s.shapes.size() == 1 && s.editable_sources.size() == 1 &&
           s.unsupported.empty());
    const auto &shape = s.shapes[0];
    near(shape.text_area->x, 10);
    near(shape.text_area->y, 20);
    near(shape.text_area->width, 110);
    near(shape.text_area->height, 60);
    assert(shape.text == "Independent" && shape.hit({50, 80}) &&
           !shape.hit({215, 45}));
    d.set_box_text(0, 0, "Edited custom text");
    TempDir directory;
    auto saved = directory.path() + "/saved.odp";
    d.save(saved);
    Presentation again;
    again.load(saved);
    assert(again.slide(0).boxes[0].text == "Edited custom text" &&
           again.scene(0).shapes[0].text_flow.paragraphs[0].font.size == 12);
    assert(read_document_archive(saved).value("styles.xml") ==
           read_document_archive(Text::fromUtf8(argv[2])).value("styles.xml"));
    std::cout << "PASS independent odfpy enhanced equations, modifiers, text "
                 "areas and direct custom-shape editing\n";
    return 0;
  }
  EnhancedContext c;
  c.viewbox = {10, 20, 100, 80};
  c.width = 72;
  c.height = 144;
  c.has_fill = true;
  c.modifiers = {40, 60};
  c.equations = {{"a", "$0+width/2"},
                 {"b", "?a*2"},
                 {"name-curve", "sin(pi/2)*10"},
                 {"循環", "if(0,?循環,7)"}};
  EnhancedEvaluator e(c);
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--path") {
    EnhancedContext context;
    context.viewbox = {0, 0, 100, 100};
    context.width = context.height = 100;
    context.modern_angles = argc < 4 || Text::fromUtf8(argv[3]) != "legacy";
    EnhancedEvaluator evaluator(context);
    auto p = parse_enhanced_path(Text::fromUtf8(argv[2]), evaluator);
    auto b = path_bounds(p);
    auto contours = flatten_path(p, {}, {}, .02);
    nlohmann::json j = {{"bounds", {b.x, b.y, b.width, b.height}},
                        {"contours", nlohmann::json::array()}};
    for (const auto &contour : contours) {
      nlohmann::json points = nlohmann::json::array();
      for (auto point : contour.points)
        points.push_back({point.x, point.y});
      j["contours"].push_back({{"points", points},
                               {"closed", contour.closed},
                               {"group", contour.paint_group},
                               {"fill", contour.fill},
                               {"stroke", contour.stroke}});
    }
    std::cout << j.dump() << '\n';
    return 0;
  }
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--evaluate") {
    std::cout << std::setprecision(17) << e.evaluate(Text::fromUtf8(argv[2]))
              << '\n';
    return 0;
  }
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--corpus") {
    size_t shapes = 0;
    for (int i = 2; i < argc; ++i) {
      auto members = read_document_archive(Text::fromUtf8(argv[i]));
      auto doc = read_document_xml(members.value("content.xml"));
      for (auto n : doc.elementsByTagNameNS(DRAW, "enhanced-geometry")) {
        ScenePaint p;
        p.fill = true;
        p.stroke = true;
        auto geometry = read_enhanced_geometry(n.toElement(), 100, 100, p);
        auto contours = flatten_path(*geometry.path, geometry.mapping);
        assert(!contours.empty());
        path_bounds(*geometry.path, geometry.mapping);
        ++shapes;
      }
    }
    assert(shapes);
    std::cout << "PASS " << shapes << " upstream enhanced-geometry stencils\n";
    return 0;
  }
  near(e.evaluate("12/3*2+4-3"), 9);
  near(e.evaluate("?name-curve+?b"), 190);
  near(e.evaluate("?循環"), 7);
  near(e.evaluate("if(0,1/0,sqrt(81))"), 9);
  near(e.evaluate("atan2(1,0)"), std::numbers::pi / 2);
  near(e.evaluate("left+top+right+bottom+hasfill+hasstroke"), 241);
  near(e.evaluate("logwidth+logheight"), 7620);
  for (auto formula : {"1/0", "sqrt(-1)", "min(1)", "unknown(1)", "$99",
                       "?missing", "sin(1,2)", "1e999", "(1+2", "1 2"})
    rejects([&] { e.evaluate(formula); });
  auto cycle = c;
  cycle.equations = {{"a", "?b"}, {"b", "?a"}};
  EnhancedEvaluator bad(cycle);
  rejects([&] { bad.evaluate("?a"); });
  EnhancedContext simple;
  simple.viewbox = {0, 0, 100, 100};
  simple.width = 100;
  simple.height = 100;
  EnhancedEvaluator plain(simple);
  for (auto data :
       {"M0 0 L100 0 100 100 Z F N M20 20 L80 20 80 80 20 80 Z S N",
        "M0 0 C20 100 80 100 100 0 Q50 -100 0 0 Z N",
        "B0 0 100 100 100 50 50 0 N", "V0 0 100 100 100 50 50 0 N",
        "M0 0 A0 0 100 100 100 50 50 0 N", "M0 0 W0 0 100 100 100 50 50 0 N",
        "M0 0 X20 20 40 0 Y60 20 80 0 N", "U50 50 40 20 0 360 Z N",
        "M0 0 T50 50 40 20 45 235 N", "M90 50 G40 20 0 450 N"}) {
    auto p = parse_enhanced_path(data, plain);
    assert(!flatten_path(p).empty());
    path_bounds(p);
  }
  auto sets = parse_enhanced_path(
      "M0 0 L100 0 100 100 0 100 Z F N M0 0 L100 0 100 100 0 100 Z S N", plain);
  auto contours = flatten_path(sets);
  assert(!contours[0].fill && contours[0].stroke && contours[1].fill &&
         !contours[1].stroke && contours[1].paint_group == 1);
  auto modern = parse_enhanced_path("U50 50 40 20 0 90 N", plain);
  near(path_bounds(modern).y, 50);
  simple.modern_angles = false;
  EnhancedEvaluator legacy(simple);
  near(path_bounds(parse_enhanced_path("U50 50 40 20 0 90 N", legacy)).y, 30);
  for (auto data : {"L0 0", "M0", "M0 0 C1 2", "M0 0 N L1 2", "M0 0 J10 10",
                    "M0 ?missing", "M$99 0"})
    rejects([&] { parse_enhanced_path(data, plain); });
  TempDir directory;
  auto path = directory.path() + "/enhanced.odp";
  auto package = fixture();
  package.save(path);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
    assert(File::copy(path, Text::fromUtf8(argv[2])));
  Presentation document;
  document.load(path);
  const auto &scene = document.scene(0);
  assert(scene.shapes.size() == 6 && scene.editable_sources.size() == 3 &&
         scene.unsupported.empty());
  auto find = [&](const Text &name) -> const SceneShape & {
    for (const auto &s : scene.shapes)
      if (s.name == name)
        return s;
    throw std::runtime_error("Shape not found");
  };
  const auto &arrow = find("Arrow");
  assert(arrow.editor_index == 1 && arrow.text == "Editable arrow");
  near(arrow.text_area->x, 9);
  near(arrow.text_area->y, 21.25);
  near(arrow.text_area->width, 108);
  assert(arrow.hit({90, 100}) && !arrow.hit({190, 60}));
  near(find("Mirrored").text_area->x, 63);
  assert(find("Mirrored").hit({240, 97}));
  assert(!find("Parts").hit({50, 200}) && find("Parts").hit({150, 200}));
  assert(!find("Compound").hit({260, 210}) && find("Compound").hit({285, 235}));
  document.set_box_text(0, 1, "Edited arrow");
  auto saved = directory.path() + "/saved.odp";
  document.save(saved);
  Presentation again;
  again.load(saved);
  assert(again.slide(0).boxes[1].text == "Edited arrow");
  assert(read_document_archive(saved).value("styles.xml") ==
         package.entry("styles.xml"));
  auto changed = fixture();
  changed.xml.elementsByTagNameNS(DRAW, "enhanced-geometry")[0]
      .toElement()
      .setAttributeNS(DRAW, "draw:modifiers", "50 10");
  auto updated = read_presentation_scenes(changed);
  near(updated[0].shapes[1].text_area->width, 81);
  near(updated[0].shapes[1].text_area->y, 8.5);
  auto broken = fixture();
  auto reversed = fixture();
  reversed.xml.elementsByTagNameNS(DRAW, "enhanced-geometry")[0]
      .toElement()
      .setAttributeNS(DRAW, "draw:text-areas", "?base ?bottom 5 ?top");
  auto normalized = read_presentation_scenes(reversed);
  near(normalized[0].shapes[1].text_area->x, 9);
  near(normalized[0].shapes[1].text_area->width, 108);
  broken.xml.elementsByTagNameNS(DRAW, "equation")[0]
      .toElement()
      .setAttributeNS(DRAW, "draw:formula", "?base+1");
  broken.save(path);
  rejects([&] { again.load(path); });
  assert(again.slide(0).boxes[1].text == "Edited arrow");
  std::cout
      << "PASS native enhanced formulas/commands, paint sets, modifiers, "
         "mirrors, text areas, custom text edit/save and malformed input\n";
}
