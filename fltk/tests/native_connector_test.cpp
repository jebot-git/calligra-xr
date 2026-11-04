// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_connector.h"
#include "native_enhanced_geometry.h"
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
void enhanced_sites() {
  auto doc = document("enhanced-geometry");
  auto n = doc.documentElement();
  n.setAttributeNS(SVG, "svg:viewBox", "10 20 100 60");
  auto read = [&] {
    return read_enhanced_glue_points(n, 200, 120, {}, {0, 0, 200, 120});
  };
  assert(read().empty()); // The default none excludes implicit sites too.
  n.setAttributeNS(DRAW, "draw:glue-point-type", "rectangle");
  auto points = read();
  assert(points.size() == 8);
  const ScenePoint corners[] = {{100, 0}, {200, 60}, {100, 120}, {0, 60}};
  for (size_t i = 0; i < 4; ++i) {
    near(points[i + 4].position, corners[i]);
    assert(points[i + 4].id == i + 4 && !points[i + 4].standard);
  }
  n.setAttributeNS(DRAW, "draw:glue-point-type", "segments");
  n.setAttributeNS(DRAW, "draw:enhanced-path",
                   "M 10 20 C 10 80 110 20 110 80 N");
  points = read();
  assert(points.size() == 6); // Vertices, not the Bezier control points.
  near(points[4].position, {0, 0});
  near(points[5].position, {200, 120});
  n.setAttributeNS(DRAW, "draw:glue-points", "10 20 110 20 110 80 10 80");
  n.setAttributeNS(DRAW, "draw:glue-point-type", "none");
  n.setAttributeNS(DRAW, "draw:mirror-horizontal", "true");
  n.setAttributeNS(DRAW, "draw:glue-point-leaving-directions",
                   "0,100grad,3.141592653589793rad,270deg");
  auto before = doc.toByteArray();
  points = read();
  assert(points.size() == 8 && doc.toByteArray() == before);
  near(points[4].position, {200, 0});
  near(points[5].position, {0, 0});
  near(points[6].position, {0, 120});
  near(points[7].position, {200, 120});
  const ScenePoint vectors[] = {{-2, 0}, {0, 2}, {2, 0}, {0, -2}};
  for (size_t i = 0; i < 4; ++i) {
    assert(points[i + 4].exit_vector);
    near(*points[i + 4].exit_vector, vectors[i]);
  }
  for (auto directions :
       TextList{"0", "0,0,0,0,0", "0,0,0,0bogus", "0,0,0,nan"}) {
    n.setAttributeNS(DRAW, "draw:glue-point-leaving-directions", directions);
    rejects(read);
  }
  n.removeAttributeNS(DRAW, "glue-point-leaving-directions");
  n.setAttributeNS(DRAW, "draw:glue-points", "10 20 30");
  rejects(read);
  n.removeAttributeNS(DRAW, "glue-points");
  n.setAttributeNS(DRAW, "draw:glue-point-type", "unknown");
  rejects(read);
  std::cout << "PASS enhanced none/rectangle/segment sites, explicit override, "
               "mirror mapping, SVG angle units and bounded invalid input\n";
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
void routes() {
  SceneConnectorSpec spec;
  SceneConnectorEnds ends;
  ends.start.point = {10, 20};
  ends.end.point = {130, 80};
  ends.start.directions = {{1, 0}};
  ends.end.directions = {{-1, 0}};
  for (auto type : {SceneConnectorSpec::Line, SceneConnectorSpec::Lines,
                    SceneConnectorSpec::Curve, SceneConnectorSpec::Standard}) {
    spec.type = type;
    auto geometry = scene_connector_geometry(spec, ends);
    const auto &path = *geometry.path;
    assert(path.subpaths.size() == 1);
    near(path.subpaths[0].start, {10, 20});
    near(path.subpaths[0].segments.back().end, {130, 80});
    if (type == SceneConnectorSpec::Line)
      assert(path.subpaths[0].segments.size() == 1);
    if (type == SceneConnectorSpec::Lines)
      assert(path.subpaths[0].segments.size() == 3);
    if (type == SceneConnectorSpec::Curve) {
      const auto &segment = path.subpaths[0].segments[0];
      assert(segment.kind == PathSegment::Cubic);
      near(segment.control1.y, 20);
      near(segment.control2.y, 80);
      assert(segment.control1.x > 10 && segment.control2.x < 130);
    }
    if (type == SceneConnectorSpec::Standard) {
      auto previous = path.subpaths[0].start;
      for (auto segment : path.subpaths[0].segments) {
        assert(previous.x == segment.end.x || previous.y == segment.end.y);
        previous = segment.end;
      }
    }
  }
  // Supplied paths retain their curve data; only the physical endpoint map
  // changes.
  spec.path =
      std::make_shared<ScenePath>(parse_svg_path("M10 20C10 70 110 70 110 20"));
  auto original = *spec.path;
  ends.start.point = {30, 40};
  ends.end.point = {230, 40};
  auto supplied = scene_connector_geometry(spec, ends);
  assert(supplied.path == spec.path);
  near(supplied.mapping.map({10, 20}), {30, 40});
  near(supplied.mapping.map({110, 20}), {230, 40});
  near(supplied.mapping.map({10, 70}), {30, 140});
  ends.end.point = {30, 240};
  supplied = scene_connector_geometry(spec, ends);
  near(supplied.mapping.map({10, 20}), {30, 40});
  near(supplied.mapping.map({110, 20}), {30, 240});
  near(supplied.mapping.map({10, 70}), {-70, 40});
  assert(spec.path->subpaths[0].segments[0].control1.y ==
         original.subpaths[0].segments[0].control1.y);
  auto computed = scene_connector_geometry(spec, ends, 12, 100000, true);
  assert(computed.path != spec.path);
  near(computed.path->subpaths[0].start, {30, 40});
  spec.path.reset();
  spec.type = SceneConnectorSpec::Standard;
  // A right-facing start on the wrong side of its box must route around both
  // boxes rather than cut back through either one.
  ends.start.point = {0, 50};
  ends.start.directions = {{-1, 0}};
  ends.start.obstacle = SceneRect{0, 0, 100, 100};
  ends.end.point = {200, 50};
  ends.end.directions = {{1, 0}};
  ends.end.obstacle = SceneRect{100, 0, 100, 100};
  auto around = scene_connector_geometry(spec, ends);
  auto points = flatten_path(*around.path);
  assert(points.size() == 1 && points[0].points.size() >= 6);
  auto previous = points[0].points.front();
  for (size_t i = 1; i < points[0].points.size(); ++i) {
    auto next = points[0].points[i];
    assert(previous.x == next.x || previous.y == next.y);
    if (i > 1 && i + 1 < points[0].points.size())
      for (auto r : {*ends.start.obstacle, *ends.end.obstacle}) {
        if (previous.x == next.x && previous.x > r.x &&
            previous.x < r.x + r.width)
          assert(std::max(previous.y, next.y) <= r.y ||
                 std::min(previous.y, next.y) >= r.y + r.height);
        if (previous.y == next.y && previous.y > r.y &&
            previous.y < r.y + r.height)
          assert(std::max(previous.x, next.x) <= r.x ||
                 std::min(previous.x, next.x) >= r.x + r.width);
      }
    previous = next;
  }
  rejects([&] { scene_connector_geometry(spec, ends, 12, 1); });
  // A small positive gap remains routable despite the default clearance.
  ends.start.point = {100, 50};
  ends.start.directions = {{1, 0}};
  ends.end.point = {120, 50};
  ends.end.directions = {{-1, 0}};
  ends.end.obstacle = SceneRect{120, 0, 100, 100};
  auto narrow = scene_connector_geometry(spec, ends);
  assert(!narrow.path->subpaths.empty());
  // Skew a free horizontal inner line: preserve endpoint escape lines and
  // insert perpendicular bridges rather than creating diagonal corners.
  ends = {};
  ends.start.point = {0, 0};
  ends.end.point = {120, 0};
  ends.start.directions = {{1, 0}};
  ends.end.directions = {{-1, 0}};
  spec.skew = {10};
  auto shifted = scene_connector_geometry(spec, ends);
  auto contour = flatten_path(*shifted.path)[0];
  bool shifted_line = false;
  previous = contour.points.front();
  for (size_t i = 1; i < contour.points.size(); ++i) {
    auto next = contour.points[i];
    assert(previous.x == next.x || previous.y == next.y);
    if (previous.y == 10 && next.y == 10)
      shifted_line = true;
    previous = next;
  }
  assert(shifted_line);
  near(contour.points.front(), {0, 0});
  near(contour.points.back(), {120, 0});
  spec.skew = {-10};
  auto negative = flatten_path(*scene_connector_geometry(spec, ends).path)[0];
  assert(std::any_of(negative.points.begin(), negative.points.end(),
                     [](auto p) { return p.y == -10; }));
  ends.start.point = {0, 0};
  ends.end.point = {0, 120};
  ends.start.directions = {{0, 1}};
  ends.end.directions = {{0, -1}};
  spec.skew = {10};
  auto vertical = flatten_path(*scene_connector_geometry(spec, ends).path)[0];
  assert(std::any_of(vertical.points.begin(), vertical.points.end(),
                     [](auto p) { return p.x == -10; }));
  spec.skew.clear();
  ends.same_object = true;
  ends.start.point = ends.end.point = {50, 0};
  ends.start.obstacle = ends.end.obstacle = SceneRect{0, 0, 100, 100};
  ends.start.directions = ends.end.directions = {{0, -1}};
  for (auto type : {SceneConnectorSpec::Standard, SceneConnectorSpec::Curve}) {
    spec.type = type;
    auto loop = scene_connector_geometry(spec, ends);
    auto bounds = path_bounds(*loop.path);
    assert(bounds.width > 1 && bounds.height > 1);
    near(loop.path->subpaths[0].start, {50, 0});
    near(loop.path->subpaths[0].segments.back().end, {50, 0});
  }
  spec.enabled = false;
  assert(scene_connector_geometry(spec, ends).path->subpaths.empty());
  rejects([&] { scene_connector_geometry(spec, ends, -1); });
  std::cout << "PASS all four computed routes, preserved SVG curves and affine "
               "fits, obstacle avoidance, narrow gaps, signed skew, self loops "
               "and limits\n";
}
void scene_integration() {
  auto p = fixture();
  const Text XML = "http://www.w3.org/XML/1998/namespace";
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto page = p.xml.elementsByTagNameNS(DRAW, "page").front();
  page.removeAttributeNS(DRAW, "nav-order");
  auto a = p.xml.elementsByTagNameNS(DRAW, "rect").front();
  a.setAttributeNS(DRAW, "draw:layer", "ConnectorHidden");
  a.setAttributeNS(DRAW, "draw:id", "ignored-legacy-id");
  auto outer = child(page, DRAW, "g");
  outer.setAttributeNS(DRAW, "draw:transform", "translate(30pt 5pt)");
  auto group_glue = glue(outer, "4", "50%", "0%", {}, "right");
  outer.insertBefore(group_glue, outer.firstChild());
  auto c = [&](const Text &name, const Text &from, const Text &start,
               const Text &to, const Text &end) {
    auto node = p.xml.createElementNS(DRAW, "draw:connector");
    node.setAttributeNS(DRAW, "draw:name", name);
    node.setAttributeNS(DRAW, "draw:type", "line");
    node.setAttributeNS(SVG, "svg:viewBox", "0 0 1000 1000");
    node.setAttributeNS(DRAW, "draw:start-shape", from);
    node.setAttributeNS(DRAW, "draw:start-glue-point", start);
    node.setAttributeNS(DRAW, "draw:end-shape", to);
    node.setAttributeNS(DRAW, "draw:end-glue-point", end);
    page.insertBefore(node, a);
    return node;
  };
  auto forward = c("Forward", "a", "0", "b", "3");
  auto label = p.xml.createElementNS(TEXT, "text:p");
  label.appendChild(p.xml.createTextNode("Link"));
  forward.appendChild(label);
  c("GroupTarget", "outer", "4", "b", "0");
  auto custom = p.xml.createElementNS(DRAW, "draw:custom-shape");
  custom.setAttributeNS(XML, "xml:id", "enhanced");
  custom.setAttributeNS(DRAW, "draw:name", "Enhanced");
  custom.setAttributeNS(DRAW, "draw:layer", "ConnectorHidden");
  custom.setAttributeNS(SVG, "svg:x", "300pt");
  custom.setAttributeNS(SVG, "svg:y", "20pt");
  custom.setAttributeNS(SVG, "svg:width", "100pt");
  custom.setAttributeNS(SVG, "svg:height", "60pt");
  auto enhanced = p.xml.createElementNS(DRAW, "draw:enhanced-geometry");
  enhanced.setAttributeNS(SVG, "svg:viewBox", "10 20 100 60");
  enhanced.setAttributeNS(DRAW, "draw:enhanced-path",
                          "M 10 20 L 110 20 110 80 10 80 Z N");
  enhanced.setAttributeNS(DRAW, "draw:modifiers", "60");
  enhanced.setAttributeNS(DRAW, "draw:glue-points", "$0 ?g");
  enhanced.setAttributeNS(DRAW, "draw:glue-point-leaving-directions", "90deg");
  auto equation = p.xml.createElementNS(DRAW, "draw:equation");
  equation.setAttributeNS(DRAW, "draw:name", "g");
  equation.setAttributeNS(DRAW, "draw:formula", "50");
  enhanced.appendChild(equation);
  custom.appendChild(enhanced);
  page.appendChild(custom);
  c("EnhancedTarget", "enhanced", "4", "b", "0");
  auto sites =
      read_enhanced_glue_points(enhanced, 100, 60, {}, {0, 0, 100, 60});
  assert(sites.size() == 5 && sites[4].id == 4 && sites[4].exit_vector);
  near(sites[4].position, {50, 30});
  near(*sites[4].exit_vector, {0, 1});
  // Same XML ID in styles.xml is legal and must not shadow content.xml.
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto master = styles.elementsByTagNameNS(STYLE, "master-page").front();
  auto layers = styles.createElementNS(DRAW, "draw:layer-set");
  auto hidden = styles.createElementNS(DRAW, "draw:layer");
  hidden.setAttributeNS(DRAW, "draw:name", "ConnectorHidden");
  hidden.setAttributeNS(DRAW, "draw:display", "none");
  layers.appendChild(hidden);
  master.parentNode().insertBefore(layers, master);
  auto mr = styles.createElementNS(DRAW, "draw:rect");
  mr.setAttributeNS(XML, "xml:id", "a");
  mr.setAttributeNS(SVG, "svg:x", "500pt");
  mr.setAttributeNS(SVG, "svg:y", "500pt");
  mr.setAttributeNS(SVG, "svg:width", "40pt");
  mr.setAttributeNS(SVG, "svg:height", "40pt");
  master.appendChild(mr);
  auto mc = styles.importNode(forward, true).toElement();
  mc.setAttributeNS(DRAW, "draw:name", "StyleScope");
  mc.setAttributeNS(DRAW, "draw:start-shape", "a");
  mc.setAttributeNS(DRAW, "draw:start-glue-point", "0");
  mc.setAttributeNS(DRAW, "draw:end-shape", "a");
  mc.setAttributeNS(DRAW, "draw:end-glue-point", "1");
  for (auto child = mc.firstChild(); !child.isNull();) {
    auto next = child.nextSibling();
    mc.removeChild(child);
    child = next;
  }
  master.insertBefore(mc, mr);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  auto before = p.xml.toByteArray(), before_styles = p.entry("styles.xml");
  auto scenes = read_presentation_scenes(p);
  auto named = [&](const SlideScene &s,
                   const Text &name) -> const SceneShape & {
    auto at = std::find_if(s.shapes.begin(), s.shapes.end(),
                           [&](const auto &n) { return n.name == name; });
    assert(at != s.shapes.end());
    return *at;
  };
  const auto &f = named(scenes[0], "Forward");
  assert(std::none_of(
      scenes[0].shapes.begin(), scenes[0].shapes.end(),
      [&](const auto &s) { return s.source == a || s.source == custom; }));
  assert(f.path && f.editor_index >= 0 && f.text == "Link");
  near(f.transform.map(f.path_map.map(f.path->subpaths[0].start)), {60, 20});
  near(f.transform.map(f.path_map.map(f.path->subpaths[0].segments.back().end)),
       {40, 80});
  assert(f.text_area && f.text_area->width > 0 && f.text_area->height > 0);
  const auto &group = named(scenes[0], "GroupTarget");
  near(group.path->subpaths[0].start, {270, 120});
  const auto &e = named(scenes[0], "EnhancedTarget");
  near(e.path->subpaths[0].start, {350, 50});
  const auto &style = named(scenes[0], "StyleScope");
  assert(style.master);
  near(style.path->subpaths[0].start, {520, 500});
  near(style.path->subpaths[0].segments.back().end, {540, 520});
  assert(scenes[0].unsupported.empty());
  assert(p.xml.toByteArray() == before &&
         p.entry("styles.xml") == before_styles);
  auto printed = read_presentation_scenes(p, {}, SceneTarget::Print);
  near(named(printed[0], "Forward").path->subpaths[0].start, {60, 20});
  near(named(printed[0], "GroupTarget").path->subpaths[0].start, {270, 120});
  // xml:id takes precedence; a conflicting legacy draw:id must not bind.
  c("LegacyMustNotBind", "ignored-legacy-id", "0", "b", "0");
  auto bad = read_presentation_scenes(p);
  assert(!bad[0].unsupported.empty());
  assert(std::none_of(
      bad[0].shapes.begin(), bad[0].shapes.end(),
      [](const auto &s) { return s.name == "LegacyMustNotBind"; }));
  std::cout << "PASS actual scene forward/hidden/group/enhanced attachments, "
               "labels, per-file IDs, legacy precedence, screen/print parity "
               "and source retention\n";
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
  enhanced_sites();
  sources();
  resolution();
  routes();
  scene_integration();
  if (argc == 3 && std::string(argv[1]) == "--documents")
    documents(argv[2]);
}
