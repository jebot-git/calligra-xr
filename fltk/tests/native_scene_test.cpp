// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
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
XmlElement element(XmlElement parent, const Text &ns, const Text &name) {
  auto e = parent.ownerDocument().createElementNS(ns, name);
  parent.appendChild(e);
  return e;
}
XmlElement box(XmlElement parent, const Text &text,
               const Text &transform = {}) {
  auto frame = element(parent, DRAW, "draw:frame");
  frame.setAttributeNS(SVG, "svg:width", "150pt");
  frame.setAttributeNS(SVG, "svg:height", "30pt");
  frame.setAttributeNS(DRAW, "draw:style-name", "Text");
  if (!transform.empty())
    frame.setAttributeNS(DRAW, "draw:transform", transform);
  auto p = element(element(frame, DRAW, "draw:text-box"), TEXT, "text:p");
  set_plain_text(p, text);
  return frame;
}
OdfPackage fixture() {
  OdfPackage package(Kind::Stage);
  package.set_entry(
      "styles.xml",
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles>
<style:default-style style:family="paragraph"><style:text-properties fo:font-family="Noto Sans" fo:font-size="14pt"/></style:default-style>
<style:style style:name="AlphaBase" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#ff0000" draw:opacity="50%"/></style:style>
<style:style style:name="Text" style:family="graphic"><style:text-properties fo:font-family="Noto Sans" fo:font-size="14pt"/></style:style>
<style:style style:name="MasterText" style:family="graphic" style:parent-style-name="Text"><style:text-properties fo:color="#ffffff" fo:font-size="20pt"/></style:style>
</office:styles><office:automatic-styles>
<style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="400pt" fo:page-height="240pt"/></style:page-layout>
<style:page-layout style:name="Tall"><style:page-layout-properties fo:page-width="240pt" fo:page-height="400pt"/></style:page-layout>
<style:style style:name="Scoped" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#24466e"/></style:style>
<style:style style:name="WideBG" style:family="drawing-page"><style:drawing-page-properties draw:fill="solid" draw:fill-color="#e8f2ff"/></style:style>
<style:style style:name="TallBG" style:family="drawing-page"><style:drawing-page-properties draw:fill="solid" draw:fill-color="#fff5e6"/></style:style>
</office:automatic-styles><office:master-styles>
<style:master-page style:name="WideMaster" style:page-layout-name="Wide" draw:style-name="WideBG"><draw:rect draw:name="Master bar" draw:style-name="Scoped" svg:x="0pt" svg:y="0pt" svg:width="400pt" svg:height="40pt"/><draw:frame draw:style-name="MasterText" svg:x="12pt" svg:y="5pt" svg:width="350pt" svg:height="30pt"><draw:text-box><text:p>Native Stage — master &amp; shapes</text:p></draw:text-box></draw:frame></style:master-page>
<style:master-page style:name="TallMaster" style:page-layout-name="Tall" draw:style-name="TallBG"><draw:rect draw:style-name="Scoped" svg:x="0pt" svg:y="0pt" svg:width="240pt" svg:height="40pt"/></style:master-page>
</office:master-styles></office:document-styles>)",
      "text/xml");
  auto root = package.xml.documentElement();
  auto automatic =
      package.xml.createElementNS(OFFICE, "office:automatic-styles");
  root.insertBefore(automatic, root.firstChild());
  auto style = [&](const Text &name, const Text &family,
                   const Text &properties) {
    auto s = element(automatic, STYLE, "style:style");
    s.setAttributeNS(STYLE, "style:name", name);
    s.setAttributeNS(STYLE, "style:family", family);
    return std::pair{s, element(s, STYLE, "style:" + properties)};
  };
  auto [scoped, fill] = style("Scoped", "graphic", "graphic-properties");
  fill.setAttributeNS(DRAW, "draw:fill", "solid");
  fill.setAttributeNS(DRAW, "draw:fill-color", "#ff9000");
  auto [alpha, a] = style("Alpha", "graphic", "graphic-properties");
  alpha.setAttributeNS(STYLE, "style:parent-style-name", "AlphaBase");
  a.setAttributeNS(DRAW, "draw:fill-color", "#4488ee");
  auto [green, g] = style("Green", "graphic", "graphic-properties");
  g.setAttributeNS(DRAW, "draw:fill", "solid");
  g.setAttributeNS(DRAW, "draw:fill-color", "#44aa66");
  auto [line, l] = style("Line", "graphic", "graphic-properties");
  l.setAttributeNS(DRAW, "draw:stroke", "solid");
  l.setAttributeNS(SVG, "svg:stroke-color", "#bb3344");
  l.setAttributeNS(SVG, "svg:stroke-width", "2pt");
  auto [hide, h] =
      style("NoObjects", "drawing-page", "drawing-page-properties");
  h.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:presentation:1.0",
                   "presentation:background-objects-visible", "false");
  auto page = element(package.body(), DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Gallery");
  page.setAttributeNS(DRAW, "draw:master-page-name", "WideMaster");
  auto rect = element(page, DRAW, "draw:rect");
  rect.setAttributeNS(DRAW, "draw:name", "Orange");
  rect.setAttributeNS(DRAW, "draw:style-name", "Scoped");
  for (auto [name, value] :
       std::vector<std::pair<Text, Text>>{{"x", "20pt"},
                                          {"y", "60pt"},
                                          {"width", "100pt"},
                                          {"height", "60pt"}})
    rect.setAttributeNS(SVG, "svg:" + name, value);
  auto oval = element(page, DRAW, "draw:ellipse");
  oval.setAttributeNS(DRAW, "draw:name", "Alpha ellipse");
  oval.setAttributeNS(DRAW, "draw:style-name", "Alpha");
  for (auto [name, value] : std::vector<std::pair<Text, Text>>{
           {"cx", "100pt"}, {"cy", "100pt"}, {"rx", "40pt"}, {"ry", "30pt"}})
    oval.setAttributeNS(SVG, "svg:" + name, value);
  auto polygon = element(page, DRAW, "draw:polygon");
  polygon.setAttributeNS(DRAW, "draw:name", "Concave");
  polygon.setAttributeNS(DRAW, "draw:style-name", "Green");
  polygon.setAttributeNS(DRAW, "draw:points",
                         "0,0 100,0 100,35 35,35 35,100 0,100");
  polygon.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
  for (auto [name, value] :
       std::vector<std::pair<Text, Text>>{{"x", "145pt"},
                                          {"y", "140pt"},
                                          {"width", "50pt"},
                                          {"height", "50pt"}})
    polygon.setAttributeNS(SVG, "svg:" + name, value);
  auto stroke = element(page, DRAW, "draw:line");
  stroke.setAttributeNS(DRAW, "draw:style-name", "Line");
  stroke.setAttributeNS(SVG, "svg:x1", "20pt");
  stroke.setAttributeNS(SVG, "svg:y1", "190pt");
  stroke.setAttributeNS(SVG, "svg:x2", "120pt");
  stroke.setAttributeNS(SVG, "svg:y2", "190pt");
  auto group = element(page, DRAW, "draw:g");
  group.setAttributeNS(DRAW, "draw:transform",
                       "rotate(0.2) translate(200pt 70pt)");
  group.setAttributeNS(DRAW, "draw:z-index", "10");
  auto nested = element(group, DRAW, "draw:g");
  nested.setAttributeNS(DRAW, "draw:transform", "translate(10pt 0pt)");
  box(nested, "Grouped editable");
  auto frame = element(group, DRAW, "draw:frame");
  frame.setAttributeNS(SVG, "svg:x", "30pt");
  frame.setAttributeNS(SVG, "svg:y", "40pt");
  frame.setAttributeNS(SVG, "svg:width", "70pt");
  frame.setAttributeNS(SVG, "svg:height", "50pt");
  element(frame, DRAW, "draw:image")
      .setAttributeNS(XLINK, "xlink:href", "Pictures/checker.png");
  box(page, "Plain editor", "translate(20pt 205pt)")
      .setAttributeNS(DRAW, "draw:z-index", "2");
  page = element(package.body(), DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Portrait");
  page.setAttributeNS(DRAW, "draw:master-page-name", "TallMaster");
  page.setAttributeNS(DRAW, "draw:style-name", "NoObjects");
  package.set_entry(
      "Pictures/checker.png",
      Bytes::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAEklEQV"
                        "R4nGNQaHig0PCAAUIBACcOBgEpwnGPAAAAAElFTkSuQmCC"),
      "image/png");
  return package;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation document;
    document.load(Text::fromUtf8(argv[2]));
    assert(document.count() == 1);
    const auto &scene = document.scene(0);
    near(scene.width, 480);
    near(scene.height, 270);
    assert(scene.shapes.size() == 3 && scene.shapes[0].master &&
           scene.editable_sources.size() == 1);
    const auto &frame = scene.shapes[scene.editor_shapes[0]];
    assert(frame.text == "Independent Český Stage");
    auto point = frame.transform.map({10, 10});
    near(point.x, 105);
    near(point.y, 80);
    assert(scene.hit_test(point) == int(scene.editor_shapes[0]));
    auto archive = read_document_archive(Text::fromUtf8(argv[2]));
    TempDir output;
    document.set_box_text(0, 0, "Edited independently");
    auto saved = output.path() + "/external.odp";
    document.save(saved);
    Presentation reopened;
    reopened.load(saved);
    assert(reopened.slide(0).boxes[0].text == "Edited independently");
    assert(read_document_archive(saved).value("styles.xml") ==
           archive.value("styles.xml"));
    std::cout << "PASS independent ODP master, geometry, grouped hit/edit and "
                 "unchanged style bytes\n";
    return 0;
  }
  TempDir directory;
  auto path = directory.path() + "/scene.odp",
       saved = directory.path() + "/saved.odp";
  auto package = fixture();
  package.save(path);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
    assert(File::copy(path, Text::fromUtf8(argv[2])));
  auto ordered = parse_scene_transform("translate(10pt,20pt) scale(2 3)");
  auto p = ordered.map({1, 2});
  near(p.x, 22);
  near(p.y, 66);
  p = parse_scene_transform("rotate(1.5707963267948966)").map({10, 0});
  near(p.x, 0);
  near(p.y, 10);
  auto inverse = ordered.inverse();
  assert(inverse);
  p = inverse->map(p);
  assert(std::isfinite(p.x));
  assert(!parse_scene_transform("scale(0)").inverse());
  Presentation document;
  document.load(path);
  assert(document.count() == 2);
  const auto &scene = document.scene(0);
  near(scene.width, 400);
  near(scene.height, 240);
  assert(scene.master == "WideMaster" && scene.shapes.size() == 9 &&
         scene.editable_sources.size() == 3);
  assert(scene.shapes[0].master &&
         scene.shapes[0].paint.fill_color.red == 0x24 &&
         scene.shapes[0].paint.fill_color.green == 0x46);
  auto find = [&](const Text &name) -> const SceneShape & {
    for (const auto &s : scene.shapes)
      if (s.name == name)
        return s;
    throw std::runtime_error("Shape not found");
  };
  assert(find("Orange").paint.fill_color.red == 0xff);
  near(find("Alpha ellipse").paint.fill_color.alpha, .5);
  near(find("Alpha ellipse").width, 80);
  near(find("Alpha ellipse").bounds().x, 60);
  assert(find("Concave").hit({150, 145}) && !find("Concave").hit({185, 180}));
  assert(scene.hit_test({25, 10}) == -1);
  int orange = scene.hit_test({25, 65});
  assert(orange >= 0 && scene.shapes[orange].name == "Orange");
  const auto &nested = scene.shapes[scene.editor_shapes[0]];
  assert(nested.text == "Grouped editable");
  p = nested.transform.map({75, 15});
  assert(scene.hit_test(p) == int(scene.editor_shapes[0]));
  const auto &portrait = document.scene(1);
  near(portrait.width, 240);
  near(portrait.height, 400);
  assert(portrait.shapes.empty() && portrait.background.red == 255 &&
         portrait.background.blue == 0xe6);
  document.set_box_text(0, 0, "Edited nested frame");
  assert(document.slide(0).boxes[0].text == "Edited nested frame" &&
         document.slide(0).boxes[2].text == "Plain editor");
  document.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(reopened.slide(0).boxes[0].text == "Edited nested frame");
  assert(read_document_archive(saved).value("styles.xml") ==
         package.entry("styles.xml"));
  std::cout
      << "PASS native master/page layout, scoped styles, alpha inheritance, "
         "vector geometry, stacking, inverse hit tests and grouped edits\n";
  auto broken = fixture();
  auto groups = broken.xml.elementsByTagNameNS(DRAW, "g");
  groups[0].setAttributeNS(DRAW, "draw:transform", "rotate(NaN)");
  broken.save(path);
  rejects([&] { reopened.load(path); });
  assert(reopened.slide(0).boxes[0].text == "Edited nested frame");
  for (auto text :
       {"translate()", "rotate(1 2)", "matrix(1 0 0 1 0)", "scale(inf)",
        "move(3pt)", "translate(1zz)", "scale(1e300)"})
    rejects([&] { parse_scene_transform(text); });
  broken = fixture();
  auto styles = broken.xml.elementsByTagNameNS(STYLE, "style");
  styles[0].setAttributeNS(STYLE, "style:parent-style-name", "Scoped");
  rejects([&] { read_presentation_scenes(broken); });
  std::cout
      << "PASS malformed transforms, cyclic styles and failed-load rollback\n";
}
