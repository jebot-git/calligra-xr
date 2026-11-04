// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_connector.h"
#include <limits>
#include <filesystem>
namespace {
void near(double a, double b) { assert(std::abs(a - b) < 1e-9); }
void near(ScenePoint a, ScenePoint b) {
  near(a.x, b.x);
  near(a.y, b.y);
}
XmlDocument document(const Text &tag) {
  return read_document_xml(("<draw:" + tag + " xmlns:draw=\"" + DRAW +
                            "\" xmlns:svg=\"" + SVG + "\"/>")
                               .toUtf8());
}
XmlElement glue(XmlElement shape, const Text &id, const Text &x, const Text &y,
                const Text &align = {}, const Text &escape = {}) {
  auto n = shape.ownerDocument().createElementNS(DRAW, "draw:glue-point");
  n.setAttributeNS(DRAW, "draw:id", id);
  n.setAttributeNS(SVG, "svg:x", x);
  n.setAttributeNS(SVG, "svg:y", y);
  if (!align.empty())
    n.setAttributeNS(DRAW, "draw:align", align);
  n.setAttributeNS(DRAW, "draw:escape-direction",
                   escape.empty() ? "auto" : escape);
  shape.appendChild(n);
  return n;
}
void attachments() {
  auto doc = document("rect");
  auto node = doc.documentElement();
  const SceneRect box{10, 20, 100, 60};
  auto points = read_scene_glue_points(node, box);
  const ScenePoint expected[] = {{60, 20}, {110, 50}, {60, 80}, {10, 50}};
  const unsigned masks[] = {1, 2, 4, 8};
  assert(points.size() == 4);
  for (size_t i = 0; i < 4; ++i) {
    assert(points[i].id == i && points[i].standard &&
           points[i].escape == masks[i]);
    near(points[i].position, expected[i]);
  }
  glue(node, "4", "25%", "-50%", {}, "horizontal");
  glue(node, "18446744073709551615", "100%", "-125%", {}, "vertical");
  auto original = doc.toByteArray();
  points = read_scene_glue_points(node, box);
  near(points[4].position, {85, 20});
  assert(points[4].escape == 10 && !points[4].standard);
  near(points[5].position, {160, -25});
  assert(points[5].escape == 5 &&
         points[5].id == std::numeric_limits<uint64_t>::max());
  assert(doc.toByteArray() == original);
  rejects([&] { read_scene_glue_points(node, box, 5); });
  auto bad = glue(node, "4", "0%", "0%");
  rejects([&] { read_scene_glue_points(node, box); });
  node.removeChild(bad);
  bad = glue(node, "3", "0%", "0%");
  rejects([&] { read_scene_glue_points(node, box); });
  node.removeChild(bad);
  for (auto id : TextList{"-1", "4.5", "18446744073709551616", "nan"}) {
    bad = glue(node, id, "0%", "0%");
    rejects([&] { read_scene_glue_points(node, box); });
    node.removeChild(bad);
  }
  for (auto [x, y, align, escape] :
       {std::tuple<Text, Text, Text, Text>{"0pt", "0%", "", "auto"},
        {"0%", "0%", "center", "auto"},
        {"0pt", "0pt", "diagonal", "auto"},
        {"0pt", "0pt", "bottom", "auto"},
        {"0pt", "0pt", "center", "diagonal"},
        {"nan%", "0%", "", "auto"},
        {"", "0pt", "center", "auto"}}) {
    bad = glue(node, "7", x, y, align, escape);
    rejects([&] { read_scene_glue_points(node, box); });
    node.removeChild(bad);
  }
  bad = glue(node, "8", "0%", "0%");
  bad.removeAttributeNS(DRAW, "escape-direction");
  rejects([&] { read_scene_glue_points(node, box); });
  node.removeChild(bad);
  rejects([&] { read_scene_glue_points(node, {0, 0, -1, 1}); });
  rejects([&] {
    read_scene_glue_points(node,
                           {0, 0, std::numeric_limits<double>::infinity(), 1});
  });
  const TextList alignments{"top-left", "top",   "top-right",   "left",
                            "center",   "right", "bottom-left", "bottom-right"};
  const ScenePoint aligned[] = {{12, 17}, {62, 17},  {112, 17}, {12, 47},
                                {62, 47}, {112, 47}, {12, 77},  {112, 77}};
  for (size_t i = 0; i < alignments.size(); ++i) {
    auto d = document("rect");
    glue(d.documentElement(), "+004", "2pt", "-3pt", alignments[i]);
    auto p = read_scene_glue_points(d.documentElement(), box);
    near(p[4].position, aligned[i]);
    assert(p[4].id == 4);
  }
  const TextList directions{"auto", "up",         "right",   "down",
                            "left", "horizontal", "vertical"};
  const unsigned direction_masks[] = {15, 1, 2, 4, 8, 10, 5};
  for (size_t i = 0; i < directions.size(); ++i) {
    auto d = document("rect");
    glue(d.documentElement(), "4", "0%", "0%", {}, directions[i]);
    assert(read_scene_glue_points(d.documentElement(), box)[4].escape ==
           direction_masks[i]);
  }
  std::cout << "PASS standard edge centers, all aligned/percentage anchors, "
               "escape masks, exact identities, no clamping/mutation and "
               "bounded rejection\n";
}
void sources() {
  auto doc = document("connector");
  auto node = doc.documentElement();
  node.setAttributeNS(SVG, "svg:viewBox", "10 -20 200 100");
  for (auto [key, value] : {std::pair{"x1", "2.54cm"},
                            {"y1", "-10pt"},
                            {"x2", "100mm"},
                            {"y2", "150pt"}})
    node.setAttributeNS(SVG, Text("svg:") + key, value);
  node.setAttributeNS(DRAW, "draw:line-skew", "2pt -2.54cm 1in");
  node.setAttributeNS(SVG, "svg:d", "M10 -20C20 40 100 80 210 80");
  const TextList types{"standard", "line", "lines", "curve"};
  for (size_t i = 0; i < types.size(); ++i) {
    node.setAttributeNS(DRAW, "draw:type", types[i]);
    auto original = doc.toByteArray();
    auto s = read_scene_connector(node);
    assert(int(s.type) == int(i) && s.enabled);
    near(*s.start.x, 72);
    near(*s.start.y, -10);
    near(*s.end.x, 100 * 72 / 25.4);
    near(*s.end.y, 150);
    assert(s.start.shape.empty() && !s.start.glue && s.path &&
           s.path->subpaths.size() == 1);
    near(s.viewbox.x, 10);
    near(s.viewbox.y, -20);
    near(s.viewbox.width, 200);
    near(s.viewbox.height, 100);
    assert(s.skew.size() == 3);
    near(s.skew[0], 2);
    near(s.skew[1], -72);
    near(s.skew[2], 72);
    assert(doc.toByteArray() == original);
  }
  auto attached = document("connector");
  auto n = attached.documentElement();
  n.setAttributeNS(SVG, "svg:viewBox", "0, 0, 0, 100");
  n.setAttributeNS(DRAW, "draw:start-shape", "  later-shape  ");
  n.setAttributeNS(DRAW, "draw:end-shape", "other-shape");
  n.setAttributeNS(DRAW, "draw:end-glue-point", "18446744073709551615");
  n.setAttributeNS(SVG, "svg:x1", "12pt");
  auto s = read_scene_connector(n);
  assert(!s.enabled && !s.path && s.type == SceneConnectorSpec::Standard);
  assert(s.start.shape == "later-shape" && !s.start.glue && s.start.x == 12 &&
         !s.start.y);
  assert(s.end.shape == "other-shape" &&
         s.end.glue == std::numeric_limits<uint64_t>::max() && !s.end.x &&
         !s.end.y);
  for (auto [uri, key, value] :
       {std::tuple<Text, Text, Text>{DRAW, "draw:type", "unknown"},
        {SVG, "svg:viewBox", "0 0 -1 100"},
        {SVG, "svg:viewBox", "0 0 1"},
        {SVG, "svg:viewBox", "0 0 nan 100"},
        {SVG, "svg:viewBox", "0 0 1 1 1"},
        {SVG, "svg:d", "Mnan 0"},
        {DRAW, "draw:line-skew", "1pt 2pt 3pt 4pt"},
        {DRAW, "draw:line-skew", ""},
        {DRAW, "draw:line-skew", "10%"},
        {SVG, "svg:x1", ""},
        {SVG, "svg:y2", "10%"}}) {
    auto copy = read_document_xml(doc.toByteArray());
    copy.documentElement().setAttributeNS(uri, key, value);
    rejects([&] { read_scene_connector(copy.documentElement()); });
  }
  rejects([&] { read_scene_connector(document("line").documentElement()); });
  auto free = document("connector");
  free.documentElement().setAttributeNS(SVG, "svg:viewBox", "0 0 10 10");
  rejects([&] { read_scene_connector(free.documentElement()); });
  std::cout << "PASS all four connector modes, unit/skew/path/viewBox parsing, "
               "forward attached references, independent axis omission, source "
               "retention and invalid-input rejection\n";
}
void documents(const Text &directory) {
  if (directory.empty())
    return;
  std::filesystem::create_directories(directory.toStdString());
  auto p = fixture();
  auto page = p.xml.elementsByTagNameNS(DRAW, "page").front();
  page.removeAttributeNS(DRAW, "nav-order");
  auto shape = p.xml.elementsByTagNameNS(DRAW, "rect").front();
  const TextList alignments{"top-left", "top",   "top-right",   "left",
                            "center",   "right", "bottom-left", "bottom-right"};
  for (size_t i = 0; i < alignments.size(); ++i) {
    auto n = glue(shape, Text::number(i + 4), "2pt", "-3pt", alignments[i]);
    shape.insertBefore(n, shape.firstChild());
  }
  const TextList modes{"standard", "line", "lines", "curve"};
  for (size_t i = 0; i < 12; ++i) {
    auto n = p.xml.createElementNS(DRAW, "draw:connector");
    n.setAttributeNS(DRAW, "draw:name", Text("Connector%1").arg(int(i)));
    n.setAttributeNS("http://www.w3.org/XML/1998/namespace", "xml:id",
                     Text("connector%1").arg(int(i)));
    n.setAttributeNS(DRAW, "draw:type", modes[i % 4]);
    n.setAttributeNS(SVG, "svg:viewBox", "0 0 1000 1000");
    n.setAttributeNS(SVG, "svg:d", "M0 0C0 500 1000 500 1000 1000");
    if (i < 4) {
      n.setAttributeNS(SVG, "svg:x1", "30pt");
      n.setAttributeNS(SVG, "svg:y1", "40pt");
      n.setAttributeNS(SVG, "svg:x2", "180pt");
      n.setAttributeNS(SVG, "svg:y2", "140pt");
    } else {
      n.setAttributeNS(DRAW, "draw:start-shape", "a");
      n.setAttributeNS(DRAW, "draw:start-glue-point", Text::number(i));
      n.setAttributeNS(DRAW, "draw:end-shape", "b");
      n.setAttributeNS(DRAW, "draw:end-glue-point", "0");
    }
    page.insertBefore(n, shape);
    auto model = read_scene_connector(n);
    assert(model.path && model.type == int(i % 4));
    if (i >= 4)
      assert(model.start.shape == "a" && model.start.glue == i &&
             model.end.shape == "b");
  }
  auto points = read_scene_glue_points(shape, {0, 0, 100, 60});
  assert(points.size() == 12);
  p.save(directory + "/connector-sources.odp");
  std::cout << "PASS native source fixture with twelve "
               "supplied-path/free/forward-attached connectors and eight "
               "custom glue alignments\n";
}
} // namespace
int main(int argc, char **argv) {
  attachments();
  sources();
  if (argc == 3 && std::string(argv[1]) == "--documents")
    documents(argv[2]);
}
