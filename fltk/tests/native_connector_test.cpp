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
void resolution() {
  auto shape = document("rect");
  SceneConnectorObject a{
      {0, 0, 100, 60},
      {1, 0, 0, 1, 10, 20},
      read_scene_glue_points(shape.documentElement(), {0, 0, 100, 60})};
  SceneConnectorObject b{
      {0, 0, 80, 40},
      {1, 0, 0, 1, 200, 30},
      read_scene_glue_points(shape.documentElement(), {0, 0, 80, 40})};
  std::map<Text, SceneConnectorObject> objects{{"a", a}, {"b", b}};
  auto lookup = [&](const Text &id) -> const SceneConnectorObject * {
    if (id == "alias-a")
      return &objects.at("a");
    auto it = objects.find(id);
    return it == objects.end() ? nullptr : &it->second;
  };
  SceneConnectorSpec spec;
  spec.start.shape = "a";
  spec.end.shape = "b";
  auto before = objects.at("a").points;
  auto resolved = resolve_scene_connector_ends(spec, {}, lookup);
  assert(!resolved.same_object && resolved.start.glue == 1 &&
         resolved.end.glue == 3);
  near(resolved.start.point, {110, 50});
  near(resolved.end.point, {200, 50});
  near(resolved.start.slide_point, {110, 50});
  assert(resolved.start.directions.size() == 1);
  near(resolved.start.directions[0], {1, 0});
  assert(resolved.start.obstacle && resolved.start.obstacle->x == 10 &&
         resolved.start.obstacle->width == 100);
  // Connector transforms change local coordinates, not physical nearest ports.
  const SceneTransform connector{0, 2, -.5, 0, 50, -10};
  auto transformed = resolve_scene_connector_ends(spec, connector, lookup);
  near(connector.map(transformed.start.point), {110, 50});
  near(connector.map(transformed.end.point), {200, 50});
  assert(transformed.start.glue == 1 && transformed.end.glue == 3);
  near(transformed.start.directions[0], {0, -1});
  // A nonuniform connector transform must not distort automatic choices.
  // Slide distances are 3 versus 2; local distances would be .3 versus 2.
  objects["metric"] = {
      {0, 0, 10, 10}, {}, {{4, {3, 0}, 15, false}, {5, {0, 2}, 15, false}}};
  SceneConnectorSpec metric;
  metric.start.x = metric.start.y = 0;
  metric.end.shape = "metric";
  auto physical = resolve_scene_connector_ends(metric, {10, 0, 0, 1}, lookup);
  assert(physical.end.glue == 5);
  near(physical.end.point, {0, 2});
  spec.start.glue = 0;
  spec.end.glue = 2;
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  near(resolved.start.point, {60, 20});
  near(resolved.end.point, {240, 70});
  spec.start.glue = 999;
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup); });
  spec.start.glue.reset();
  spec.end.glue.reset();
  spec.end.shape = "alias-a";
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  assert(resolved.same_object && resolved.start.glue != resolved.end.glue);
  spec.start.glue = 1;
  spec.end.glue = 1;
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  assert(resolved.same_object && resolved.start.glue == 1 &&
         resolved.end.glue == 1);
  near(resolved.start.point, resolved.end.point);
  spec.start = {};
  spec.start.x = 0;
  spec.start.y = 50;
  spec.end = {};
  spec.end.shape = "a";
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  assert(!resolved.start.glue && !resolved.start.obstacle &&
         resolved.end.glue == 3);
  near(resolved.end.point, {10, 50});
  spec.end.shape = "missing";
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup); });
  spec.end.shape = "a";
  rejects([&] { resolve_scene_connector_ends(spec, {0, 0, 0, 0}, lookup); });
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup, 2); });
  // Stable numeric ID ties, not source point order or int truncation.
  objects["ports"] = {
      {0, 0, 10, 10},
      {},
      {{UINT64_MAX, {1, 0}, 15, false}, {4, {-1, 0}, 15, false}}};
  spec.start.x = 0;
  spec.start.y = 0;
  spec.end.shape = "ports";
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  assert(resolved.end.glue == 4);
  objects["ports"].transform = {0, 2, -3, 0, 100, 200};
  spec.end.glue = UINT64_MAX;
  resolved = resolve_scene_connector_ends(spec, {}, lookup);
  near(resolved.end.point, {100, 202});
  near(resolved.end.directions[0], {1, 0});
  near(resolved.end.directions[1], {0, 1});
  near(resolved.end.directions[2], {-1, 0});
  near(resolved.end.directions[3], {0, -1});
  auto broken = objects["ports"];
  objects["ports"].points.push_back(objects["ports"].points[0]);
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup); });
  objects["ports"] = broken;
  objects["ports"].points[0].escape = 32;
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup); });
  objects["ports"] = broken;
  objects["ports"].points.clear();
  rejects([&] { resolve_scene_connector_ends(spec, {}, lookup); });
  assert(objects.at("a").points.size() == before.size());
  for (size_t i = 0; i < before.size(); ++i)
    near(objects.at("a").points[i].position, before[i].position);
  std::cout << "PASS explicit/automatic/aliased/self attachment resolution, "
               "physical nearest choices, affine directions, stable wide IDs "
               "and nonmutating bounded failures\n";
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
  resolution();
  if (argc == 3 && std::string(argv[1]) == "--documents")
    documents(argv[2]);
}
