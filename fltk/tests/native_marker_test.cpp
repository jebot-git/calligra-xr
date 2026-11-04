// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_marker.h"
#include "native_stage_paint.h"
#include <filesystem>
#include "native_stroke.h"
#include "native_stage_fragment.h"
#include "native_stage_paste.h"
#include <cairo.h>
#include <cstring>
#include <limits>
namespace {
void near(double a, double b) { assert(std::abs(a - b) < 1e-9); }
void near(ScenePoint a, ScenePoint b) {
  near(a.x, b.x);
  near(a.y, b.y);
}
std::shared_ptr<const SceneMarkerDefinition>
marker(const Text &data = "M15 7L5 27H25Z",
       const Text &box = "100 -50 1000 500") {
  auto doc = read_document_xml(
      R"(<draw:marker xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"/>)");
  doc.documentElement().setAttributeNS(SVG, "svg:d", data);
  doc.documentElement().setAttributeNS(SVG, "svg:viewBox", box);
  return read_scene_marker(doc.documentElement());
}
void endpoints() {
  for (auto matrix :
       {SceneTransform{}, SceneTransform{-2, .2, .3, 3, 100, 40}}) {
    auto unit = [&](ScenePoint d) {
      d = {matrix.a * d.x + matrix.c * d.y, matrix.b * d.x + matrix.d * d.y};
      auto n = std::hypot(d.x, d.y);
      return ScenePoint{d.x / n, d.y / n};
    };
    for (auto [data, first, last] :
         {std::tuple<Text, ScenePoint, ScenePoint>{
              "M10 20L80 50", {70, 30}, {-70, -30}},
          {"M10 20C10 20 10 50 80 50", {0, 30}, {-70, 0}},
          {"M10 20C10 20 80 50 80 50", {70, 30}, {-70, -30}},
          {"M10 20Q10 20 80 50", {70, 30}, {-70, -30}},
          {"M10 20l0 0C10 20 10 50 80 50l0 0", {0, 30}, {-70, 0}}}) {
      auto value = scene_path_ends(parse_svg_path(data), matrix);
      assert(value.size() == 1 && value[0].open);
      near(value[0].first.point, matrix.map({10, 20}));
      near(value[0].last.point, matrix.map({80, 50}));
      near(value[0].first.inward, unit(first));
      near(value[0].last.inward, unit(last));
    }
  }
  auto arc = scene_path_ends(parse_svg_path("M10 20A40 30 0 0 1 90 20"));
  near(arc[0].first.inward, {0, -1});
  near(arc[0].last.inward, {0, -1});
  auto projected =
      scene_path_ends(parse_svg_path("M10 20A40 30 0 0 1 90 20"), {1, 0, 0, 0});
  near(projected[0].first.inward, {1, 0});
  near(projected[0].last.inward, {-1, 0});
  auto zero = scene_path_ends(parse_svg_path("M10 20l0 0"));
  assert(zero[0].open);
  near(zero[0].first.inward, {1, 0});
  near(zero[0].last.inward, {-1, 0});
  assert(!scene_path_ends(parse_svg_path("M10 20"))[0].open);
  assert(!scene_path_ends(parse_svg_path("M10 20L80 50Z"))[0].open);
  std::cout << "PASS exact line/cubic/quadratic/arc endpoint tangents, affine "
               "maps, singular projections and degenerate subpaths\n";
}
SceneShape shape(bool centered = false, double end = 130) {
  SceneShape s;
  s.kind = SceneShape::Line;
  s.contours = {{{{30, 60}, {end, 60}}, false}};
  s.paint.stroke = true;
  s.paint.stroke_width = 4;
  s.paint.stroke_cap = ScenePaint::Cap::Round;
  s.paint.marker_start = {"Arrow", 10, centered, marker()};
  s.paint.marker_end = s.paint.marker_start;
  return s;
}
void polygon(cairo_t *cr, const std::vector<SceneContour> &curves) {
  for (const auto &c : curves) {
    if (c.points.empty())
      continue;
    cairo_move_to(cr, c.points[0].x, c.points[0].y);
    for (size_t i = 1; i < c.points.size(); ++i)
      cairo_line_to(cr, c.points[i].x, c.points[i].y);
    cairo_close_path(cr);
  }
}
std::vector<uint8_t> raster(const SceneShape &s, bool reference,
                            bool dashed = false) {
  auto surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 400, 220);
  auto cr = cairo_create(surface);
  cairo_scale(cr, 2, 2);
  cairo_set_source_rgba(cr, 1, 1, 1, .5);
  if (reference) {
    const double delta = s.paint.marker_start.centered ? -5 : 0;
    const double end = s.contours[0].points.back().x;
    auto triangle = [&](double tip, double base) {
      cairo_move_to(cr, tip, 60);
      cairo_line_to(cr, base, 55);
      cairo_line_to(cr, base, 65);
      cairo_close_path(cr);
    };
    // All oracle polygons have positive winding, for a single alpha fill.
    triangle(30 + delta, 40 + delta);
    cairo_move_to(cr, end - delta, 60);
    cairo_line_to(cr, end - 10 - delta, 65);
    cairo_line_to(cr, end - 10 - delta, 55);
    cairo_close_path(cr);
    const double left = 40 + delta, right = end - 10 - delta;
    if (right > left) {
      if (!dashed)
        cairo_rectangle(cr, left, 58, right - left, 4);
      else
        for (int k = 0; k < 10; ++k) {
          double a = std::max(left, 30. + 21 * k),
                 b = std::min(right, 43. + 21 * k);
          if (a >= b)
            continue;
          cairo_rectangle(cr, a, 58, b - a, 4);
          for (auto x : {a, b})
            if (x > left && x < right) {
              cairo_new_sub_path(cr);
              cairo_arc(cr, x, 60, 2, 0, 2 * std::acos(-1.));
              cairo_close_path(cr);
            }
        }
    }
    cairo_fill(cr);
  } else {
    auto geometry = marker_stroke_geometry(s, s.contours);
    auto paint = s.paint;
    if (dashed) {
      SceneDashPattern pattern;
      pattern.first = 1;
      pattern.first_length = SceneDashLength{13, false};
      pattern.distance = SceneDashLength{8, false};
      paint.stroke_patterns = {pattern};
    }
    auto passes = stroke_pass_outlines(geometry.shafts, paint, {}, .01, 200000,
                                       geometry.markers);
    for (const auto &pass : passes) {
      polygon(cr, pass);
      cairo_fill(cr);
    }
  }
  assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
  cairo_surface_flush(surface);
  std::vector<uint8_t> result(400 * 220);
  for (int y = 0; y < 220; ++y)
    std::memcpy(result.data() + 400 * y,
                cairo_image_surface_get_data(surface) +
                    y * cairo_image_surface_get_stride(surface),
                400);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return result;
}
void geometry() {
  auto m = marker();
  for (bool centered : {false, true}) {
    auto start = marker_transform(*m, 10, centered, {{30, 60}, {1, 0}});
    near(start.map({15, 7}), {centered ? 25. : 30., 60});
    near(start.map({5, 27}), {centered ? 35. : 40., 65});
    auto finish = marker_transform(*m, 10, centered, {{130, 60}, {-1, 0}});
    near(finish.map({15, 7}), {centered ? 135. : 130., 60});
    near(finish.map({25, 27}), {centered ? 125. : 120., 65});
    for (bool dashed : {false, true}) {
      auto s = shape(centered);
      auto a = raster(s, false, dashed), b = raster(s, true, dashed);
      size_t checked = 0;
      for (int y = 1; y < 219; ++y)
        for (int x = 1; x < 399; ++x) {
          int i = 400 * y + x, v = b[i];
          bool stable = v <= 2 || v >= 126;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              if (std::abs(int(b[i + 400 * dy + dx]) - v) > 2)
                stable = false;
          if (stable) {
            ++checked;
            assert(std::abs(int(a[i]) - v) <= 6);
          }
        }
      assert(checked > 80000);
    }
  }
  auto short_line = shape(false, 37);
  assert(raster(short_line, false) == raster(short_line, true));
  auto s = shape();
  auto prepared = marker_stroke_geometry(s, s.contours);
  near(prepared.shafts[0].points.front(), {40, 60});
  near(prepared.shafts[0].points.back(), {120, 60});
  near(prepared.shafts[0].stroke_dash_offset, 10);
  assert(prepared.shafts[0].stroke_butt_start &&
         prepared.shafts[0].stroke_butt_end);
  s.marker_contours = prepared.markers;
  assert(s.hit({38, 63}) && !s.hit({38, 70}));
  auto split = stroke_dashes(prepared.shafts, {13, 8});
  near(split[0].points[0], {40, 60});
  near(split[0].points.back(), {43, 60});
  assert(split[0].stroke_butt_start && !split[0].stroke_butt_end);
  assert(split.back().stroke_butt_end);
  auto no_shaft = shape();
  no_shaft.paint.stroke_width = 0;
  auto marker_only = raster(no_shaft, false);
  assert(marker_only[120 * 400 + 160] == 0 &&
         marker_only[120 * 400 + 70] == 128);
  auto hollow = shape();
  hollow.paint.stroke_width = 20;
  hollow.paint.marker_end.definition =
      marker("M0 0H20V20H0Z M5 5V15H15V5Z", "0 0 20 20");
  auto hollow_pixels = raster(hollow, false);
  assert(hollow_pixels[120 * 400 + 250] == 0 &&
         hollow_pixels[120 * 400 + 258] == 128 &&
         hollow_pixels[120 * 400 + 230] == 128);
  auto curve = shape();
  curve.path =
      std::make_shared<ScenePath>(parse_svg_path("M30 60C30 0 130 0 130 60"));
  auto coarse =
      marker_stroke_geometry(curve, flatten_path(*curve.path, {}, {}, 2));
  auto fine =
      marker_stroke_geometry(curve, flatten_path(*curve.path, {}, {}, .01));
  assert(coarse.markers.size() == fine.markers.size());
  for (size_t i = 0; i < coarse.markers.size(); ++i) {
    assert(coarse.markers[i].points.size() == fine.markers[i].points.size());
    for (size_t j = 0; j < coarse.markers[i].points.size(); ++j)
      near(coarse.markers[i].points[j], fine.markers[i].points[j]);
  }
  assert(marker_contours(*marker("M0 0H10V10H0Z", "0 0 0 10"), 10, false,
                         {{0, 0}, {1, 0}})
             .empty());
  assert(marker_contours(*m, 0, false, {{0, 0}, {1, 0}}).empty());
  rejects([&] { marker_transform(*m, -1, false, {{0, 0}, {1, 0}}); });
  rejects([&] { marker("M0 0H10V10H0Z", "0 0 -1 10"); });
  rejects([&] { marker_contours(*m, 10, false, {{0, 0}, {0, 0}}); });
  rejects(
      [&] { marker_contours(*m, 10, false, {{0, 0}, {1, 0}}, {}, .01, 2); });
  std::cout << "PASS independent marker width/centering, stroke union alpha, "
               "preserved dash phase, overlapping markers, exact "
               "zoom-independent placement, hit testing and geometry limits\n";
}
void reader() {
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto p = fixture();
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto common = child(doc.documentElement(), OFFICE, "styles");
  if (common.isNull()) {
    common = doc.createElementNS(OFFICE, "office:styles");
    doc.documentElement().insertBefore(common,
                                       doc.documentElement().firstChild());
  }
  auto definition = doc.createElementNS(DRAW, "draw:marker");
  definition.setAttributeNS(DRAW, "draw:name", "Arrow");
  definition.setAttributeNS(SVG, "svg:viewBox", "100 -50 1000 500");
  definition.setAttributeNS(SVG, "svg:d", "M15 7L5 27H25Z");
  common.appendChild(definition);
  auto style = doc.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", "Marked");
  style.setAttributeNS(STYLE, "style:family", "graphic");
  auto props = doc.createElementNS(STYLE, "style:graphic-properties");
  for (auto [uri, key, value] :
       {std::tuple<Text, Text, Text>{DRAW, "draw:stroke", "solid"},
        {DRAW, "draw:marker-start", "Arrow"},
        {DRAW, "draw:marker-end", "Arrow"},
        {DRAW, "draw:marker-start-width", "10pt"},
        {DRAW, "draw:marker-end-width", "20pt"},
        {DRAW, "draw:marker-end-center", "true"},
        {SVG, "svg:stroke-width", "4pt"}})
    props.setAttributeNS(uri, key, value);
  style.appendChild(props);
  common.appendChild(style);
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "line"))
    if (node.attributeNS(DRAW, "name") == "N1")
      node.setAttributeNS(DRAW, "draw:style-name", "Marked");
  auto scenes = read_presentation_scenes(p);
  auto at = std::find_if(scenes[0].shapes.begin(), scenes[0].shapes.end(),
                         [](const auto &s) { return s.name == "N1"; });
  assert(at != scenes[0].shapes.end());
  assert(at->paint.marker_start.definition && at->paint.marker_end.definition &&
         at->paint.marker_start.width == 10 &&
         at->paint.marker_end.width == 20 && at->paint.marker_end.centered);
  assert(!at->marker_contours.empty());
  auto fragment = native_stage_fragment(
      p, scenes[0], {{int(at - scenes[0].shapes.begin()), 0}});
  TempDir dir;
  auto path = dir.path() + "/receiving.odp";
  p.save(path);
  Presentation receiving;
  receiving.load(path);
  auto before = receiving.image("styles.xml");
  auto position = receiving.commit_paste_objects(
      receiving.prepare_paste_objects(0, encode_stage_fragment(fragment)));
  const auto &copied = receiving.scene(0).shapes.at(position.objects[0].shape);
  assert(copied.paint.marker_start.name != "Arrow" &&
         copied.paint.marker_start.definition->data == "M15 7L5 27H25Z");
  assert(copied.paint.marker_start.width == 10 &&
         copied.paint.marker_end.width == 20 &&
         copied.paint.marker_end.centered);
  assert(receiving.undo() && receiving.image("styles.xml") == before);
  assert(receiving.redo());
  std::cout
      << "PASS native marker definitions, inherited widths/centering, cached "
         "geometry and actual clipboard dependency/context/history\n";
}
void authoring(const Text &output) {
  TempDir dir;
  auto source = fixture();
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto path = dir.path() + "/source.odp";
  source.save(path);
  Presentation p;
  p.load(path);
  const auto original = content(p), styles = p.image("styles.xml"),
             manifest = p.image("META-INF/manifest.xml");
  const NativeMarkerDefinition arrow{"M10 0L0 20H20Z", "0 0 20 20"},
      diamond{"M10 0L0 10L10 20L20 10Z", "0 0 20 20"};
  NativePaintFormat patch;
  patch.stroke = "solid";
  patch.stroke_width = 3;
  patch.marker_start.definition = arrow;
  patch.marker_start.width = 12;
  patch.marker_start.centered = false;
  patch.marker_end.definition = diamond;
  patch.marker_end.width = 18;
  patch.marker_end.centered = true;
  assert(p.set_object_paint(0, {ref(p, "N1")}, patch));
  auto paint = [&]() -> const ScenePaint & {
    return p.scene(0).shapes.at(find(p, "N1")).paint;
  };
  assert(paint().marker_start.definition->data == arrow.data &&
         paint().marker_end.definition->data == diamond.data);
  assert(paint().marker_start.width == 12 && paint().marker_end.width == 18 &&
         !paint().marker_start.centered && paint().marker_end.centered);
  auto authored = content(p), authored_styles = p.image("styles.xml");
  assert(authored_styles != styles &&
         p.image("META-INF/manifest.xml") == manifest);
  assert(p.image("opaque/source") == source.entry("opaque/source"));
  auto xml = read_document_xml(authored_styles);
  assert(xml.elementsByTagNameNS(DRAW, "marker").size() == 2);
  auto first_name = paint().marker_start.name,
       last_name = paint().marker_end.name;
  auto info = p.object_paint(0, {ref(p, "N1")});
  assert(info.values.marker_start.definition == arrow &&
         info.values.marker_end.definition == diamond &&
         info.available_markers.contains(first_name));
  auto revision = p.revision();
  assert(!p.set_object_paint(0, {ref(p, "N1")}, info.values));
  assert(p.revision() == revision);
  assert(p.undo() && content(p) == original &&
         p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest);
  for (int kind = 0; kind < 5; ++kind) {
    auto bad = patch;
    if (kind == 0)
      bad.marker_start.width = -1;
    if (kind == 1)
      bad.marker_start.definition->data = "Mnan 0";
    if (kind == 2)
      bad.marker_end.definition->view_box = "0 0 -1 20";
    if (kind == 3) {
      bad.marker_start.definition.reset();
      bad.marker_start.name = "Missing";
    }
    if (kind == 4)
      bad.marker_start.name = "Conflicting";
    rejects([&] { p.set_object_paint(0, {ref(p, "N1")}, bad); });
    assert(content(p) == original && p.image("styles.xml") == styles &&
           p.can_redo());
  }
  assert(p.redo() && content(p) == authored &&
         p.image("styles.xml") == authored_styles);
  // Reassign an existing common resource and edit only one endpoint's
  // placement.
  NativePaintFormat use;
  use.marker_start.name = last_name;
  use.marker_start.centered = true;
  assert(p.set_object_paint(0, {ref(p, "N1")}, use));
  assert(paint().marker_start.name == last_name &&
         paint().marker_start.centered && paint().marker_end.width == 18 &&
         p.image("styles.xml") == authored_styles);
  assert(p.undo() && paint().marker_start.name == first_name);
  // Same effective geometry remains a no-op with redo intact, even through a
  // complete info snapshot that includes both the common name and definition.
  auto snapshot = p.object_paint(0, {ref(p, "N1")}).values;
  revision = p.revision();
  assert(!p.set_object_paint(0, {ref(p, "N1")}, snapshot) && p.can_redo() &&
         p.revision() == revision);
  snapshot.marker_start.width = 15;
  assert(p.set_object_paint(0, {ref(p, "N1")}, snapshot));
  assert(p.image("styles.xml") == authored_styles &&
         paint().marker_start.width == 15);
  assert(p.undo());
  NativePaintFormat remove;
  remove.marker_start.name = "";
  assert(p.set_object_paint(0, {ref(p, "N1")}, remove));
  assert(paint().marker_start.name.empty() &&
         !paint().marker_start.definition && paint().marker_end.definition &&
         p.image("styles.xml") == authored_styles);
  assert(p.undo() && content(p) == authored);
  // Same fresh definition is shared between endpoints and selected group
  // members; widths/centers remain independent and existing common resources
  // are retained.
  NativePaintFormat group;
  group.marker_start.definition =
      NativeMarkerDefinition{"M0 0H10V10H0Z", "0 0 10 10"};
  group.marker_end.definition = group.marker_start.definition;
  group.marker_start.width = 8;
  group.marker_end.width = 9;
  assert(p.set_object_paint(0, {ref(p, "N1", 1)}, group));
  auto group_info = p.object_paint(0, {ref(p, "N1", 1)});
  assert(group_info.objects == 2 &&
         group_info.values.marker_start.centered == false &&
         !group_info.values.marker_end.centered &&
         group_info.values.marker_start.width == 8 &&
         group_info.values.marker_end.width == 9);
  auto changed_xml = read_document_xml(p.image("styles.xml"));
  assert(changed_xml.elementsByTagNameNS(DRAW, "marker").size() == 3);
  auto changed_nodes = changed_xml.elementsByTagNameNS(DRAW, "marker");
  for (auto n : xml.elementsByTagNameNS(DRAW, "marker")) {
    auto id = n.attributeNS(DRAW, "name");
    auto found =
        std::find_if(changed_nodes.begin(), changed_nodes.end(),
                     [&](auto v) { return v.attributeNS(DRAW, "name") == id; });
    assert(found != changed_nodes.end() &&
           found->attributeNS(SVG, "d") == n.attributeNS(SVG, "d") &&
           found->attributeNS(SVG, "viewBox") == n.attributeNS(SVG, "viewBox"));
  }
  assert(p.undo() && content(p) == authored &&
         p.image("styles.xml") == authored_styles);
  p.save(dir.path() + "/authored.odp");
  Presentation reopened;
  reopened.load(dir.path() + "/authored.odp");
  assert(reopened.scene(0)
             .shapes.at(find(reopened, "N1"))
             .paint.marker_end.definition->data == diamond.data);
  if (!output.empty()) {
    source.save(output + "/marker-source.odp");
    p.save(output + "/marker-authored.odp");
  }
  std::cout << "PASS independent endpoint authoring, existing/custom "
               "resources, group scope, mixed fields, no-ops, archive history, "
               "rejection and reopen\n";
}
void first_marker_styles() {
  TempDir dir;
  auto source = fixture();
  const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
  auto old_manifest = source.entry("META-INF/manifest.xml");
  auto manifest = read_document_xml(old_manifest);
  for (auto n : children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (n.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest.documentElement().removeChild(n);
  auto bytes = source.entry("styles.xml"),
       next_manifest = manifest.toByteArray();
  source.replay_part_changes(
      {{"styles.xml", true, false, 0, bytes.size(), 0, bytes, {}},
       {"META-INF/manifest.xml", true, true, 0, old_manifest.size(),
        next_manifest.size(), old_manifest, next_manifest}},
      true);
  auto path = dir.path() + "/first.odp";
  source.save(path);
  Presentation p;
  p.load(path);
  auto original = content(p),
       original_manifest = p.image("META-INF/manifest.xml");
  assert(p.image("styles.xml").empty());
  NativePaintFormat patch;
  patch.marker_start.definition =
      NativeMarkerDefinition{"M0 0H10V10H0Z", "0 0 10 10"};
  patch.marker_end.definition = patch.marker_start.definition;
  patch.marker_start.width = 10;
  patch.marker_end.width = 12;
  patch.stroke = "solid";
  patch.stroke_width = 2;
  assert(p.set_object_paint(0, {ref(p, "A"), ref(p, "B")}, patch));
  auto created = p.image("styles.xml"), changed = content(p);
  const auto &one = p.scene(0).shapes.at(find(p, "A")).paint;
  const auto &two = p.scene(0).shapes.at(find(p, "B")).paint;
  assert(one.marker_start.name == two.marker_start.name &&
         one.marker_start.name == one.marker_end.name &&
         one.marker_start.width == 10 && one.marker_end.width == 12);
  auto document = read_document_xml(created);
  assert(document.elementsByTagNameNS(DRAW, "marker").size() == 1);
  assert(p.undo() && content(p) == original && p.image("styles.xml").empty() &&
         p.image("META-INF/manifest.xml") == original_manifest);
  assert(p.redo() && content(p) == changed && p.image("styles.xml") == created);
  std::cout
      << "PASS first styles.xml and automatic-styles creation, multi-object "
         "mapping, one shared fresh marker and exact archive removal/replay\n";
}
} // namespace
int main(int argc, char **argv) {
  Text output;
  if (argc == 3 && std::string(argv[1]) == "--output") {
    output = argv[2];
    std::filesystem::create_directories(output.toStdString());
  }
  endpoints();
  geometry();
  reader();
  authoring(output);
  first_marker_styles();
}
