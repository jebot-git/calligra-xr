// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_paint.h"
#include "native_path.h"
#include "native_stroke.h"
#include <cairo.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
namespace {
using Cap = ScenePaint::Cap;
using Join = ScenePaint::Join;
void path(cairo_t *cr, const std::vector<SceneContour> &contours) {
  for (const auto &c : contours) {
    if (!c.stroke || c.points.empty())
      continue;
    cairo_move_to(cr, c.points.front().x, c.points.front().y);
    for (size_t i = 1; i < c.points.size(); ++i)
      cairo_line_to(cr, c.points[i].x, c.points[i].y);
    if (c.closed)
      cairo_close_path(cr);
  }
}
std::vector<uint32_t> raster(const std::vector<SceneContour> &contours,
                             const ScenePaint &paint, SceneTransform device,
                             bool reference) {
  auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 320, 240);
  auto *cr = cairo_create(surface);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  cairo_matrix_t matrix{device.a, device.b, device.c,
                        device.d, device.e, device.f};
  cairo_transform(cr, &matrix);
  cairo_set_source_rgba(cr, 0, 0, 0, .5);
  if (reference) {
    // Cairo chooses its miter cutoff using transformed angles. Render the
    // independent stroke under uniform scale, then project that mask, so the
    // SVG user-space miter limit does not change under shear or anisotropy.
    auto *target = cr;
    auto *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, 1600, 1600);
    cr = cairo_create(mask);
    cairo_scale(cr, 8, 8);
    cairo_translate(cr, 40, 40);
    path(cr, contours);
    cairo_set_line_width(cr, paint.stroke_width);
    cairo_set_line_cap(cr, paint.stroke_cap == Cap::Butt ? CAIRO_LINE_CAP_BUTT
                           : paint.stroke_cap == Cap::Square
                               ? CAIRO_LINE_CAP_SQUARE
                               : CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(
        cr, paint.stroke_join == Join::Miter   ? CAIRO_LINE_JOIN_MITER
            : paint.stroke_join == Join::Bevel ? CAIRO_LINE_JOIN_BEVEL
                                               : CAIRO_LINE_JOIN_ROUND);
    cairo_set_miter_limit(cr, 4);
    cairo_set_tolerance(cr, .01);
    cairo_stroke(cr);
    assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cr = target;
    auto *pattern = cairo_pattern_create_for_surface(mask);
    cairo_matrix_t projection{8, 0, 0, 8, 320, 320};
    cairo_pattern_set_matrix(pattern, &projection);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_BEST);
    cairo_mask(cr, pattern);
    cairo_pattern_destroy(pattern);
    cairo_surface_destroy(mask);
  } else {
    path(cr, stroke_outlines(contours, paint, device, .015));
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_fill(cr);
  }
  assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
  cairo_surface_flush(surface);
  std::vector<uint32_t> out(320 * 240);
  const auto *bytes = cairo_image_surface_get_data(surface);
  for (int y = 0; y < 240; ++y)
    std::memcpy(out.data() + 320 * y,
                bytes + y * cairo_image_surface_get_stride(surface), 320 * 4);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return out;
}
void independent_cairo() {
  std::vector<std::vector<SceneContour>> examples{
      {{{{30, 35}, {115, 35}}, false}},
      {{{{25, 35}, {90, 35}, {90, 100}}, false}},
      {{{{25, 100}, {80, 25}, {85, 110}}, false}},
      {{{{20, 20}, {100, 100}, {20, 100}, {100, 20}}, false}},
      {{{{20, 20}, {100, 20}, {100, 100}, {20, 100}}, true}},
      {{{{20, 20}, {80, 20}, {20, 20}}, false}},
      {{{{20, 20}, {20, 20}, {80, 20}, {80, 100}}, false}},
      {{{{20, 20}, {110, 20}}, false}, {{{60, 5}, {60, 95}}, false}}};
  std::mt19937 random(20261104);
  std::uniform_real_distribution<double> coordinate(20, 110);
  for (int i = 0; i < 20; ++i) {
    SceneContour c;
    c.closed = i % 2;
    for (int j = 0; j < 7; ++j)
      c.points.push_back({coordinate(random), coordinate(random)});
    examples.push_back({c});
  }
  size_t cases = 0;
  for (auto cap : {Cap::Butt, Cap::Round, Cap::Square})
    for (auto join : {Join::Miter, Join::Round, Join::Bevel})
      for (auto device : {SceneTransform{1.7, 0, 0, 1.5, 15, 15},
                          SceneTransform{1.4, .2, -.3, 1.1, 50, 10},
                          SceneTransform{-1.5, .1, .2, 1.4, 230, 10}})
        for (const auto &contours : examples) {
          ScenePaint paint;
          paint.stroke = true;
          paint.stroke_width = 13;
          paint.stroke_cap = cap;
          paint.stroke_join = join;
          auto a = raster(contours, paint, device, false),
               b = raster(contours, paint, device, true);
          // Pixel coverage at edges differs between rasterizers, including
          // Cairo's image and PDF backends for the same valid vector region.
          // Compare stable interiors/exteriors with a one-pixel margin.
          // Analytic probes and actual GPU/CUPS cases also exercise corners,
          // endpoints, zero-length strokes and the retained zigzag case.
          int maximum = 0;
          size_t clear_ink = 0, clear_pixels = 0;
          for (int y = 1; y < 239; ++y)
            for (int x = 1; x < 319; ++x) {
              size_t index = size_t(y * 320 + x);
              int expected = int(b[index] & 255);
              if (expected > 128 && expected < 253)
                continue;
              bool clear = true;
              for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                  if (std::abs(int(b[size_t((y + dy) * 320 + x + dx)] & 255) -
                               expected) > 2)
                    clear = false;
              if (!clear)
                continue;
              ++clear_pixels;
              if (expected <= 128)
                ++clear_ink;
              maximum =
                  std::max(maximum, std::abs(int(a[index] & 255) - expected));
            }
          auto ink_pixels =
              std::count_if(b.begin(), b.end(),
                            [](uint32_t pixel) { return (pixel & 255) < 253; });
          // Even the shortest doubled-back segment must retain a substantial
          // interior, and the check must cover most of the reference ink.
          assert(clear_pixels > 60000 && clear_ink > 500 &&
                 clear_ink * 2 > size_t(ink_pixels));
          if (maximum > 24) {
            std::cerr << "Cairo silhouette mismatch case=" << cases
                      << " cap=" << int(cap) << " join=" << int(join)
                      << " difference=" << maximum << '\n';
            std::abort();
          }
          ++cases;
        }
  assert(cases == 756);
  std::cout << "PASS " << cases
            << " independent Cairo stroke silhouettes, alpha, closed/open "
               "paths and affine transforms\n";
}
bool inside(const std::vector<SceneContour> &contours, ScenePoint p) {
  int winding = 0;
  for (const auto &c : contours)
    for (size_t i = 0; i < c.points.size(); ++i) {
      auto a = c.points[i], b = c.points[(i + 1) % c.points.size()];
      double side = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
      if (a.y <= p.y && b.y > p.y && side > 0)
        ++winding;
      if (a.y > p.y && b.y <= p.y && side < 0)
        --winding;
    }
  return winding != 0;
}
void geometry_edges() {
  ScenePaint p;
  p.stroke = true;
  p.stroke_width = 20;
  auto line = std::vector<SceneContour>{{{{40, 40}, {100, 40}}, false}};
  assert(!inside(stroke_outlines(line, p), {35, 40}));
  p.stroke_cap = Cap::Round;
  assert(inside(stroke_outlines(line, p), {35, 40}));
  assert(!inside(stroke_outlines(line, p), {31, 31}));
  p.stroke_cap = Cap::Square;
  assert(inside(stroke_outlines(line, p), {31, 31}));
  auto corner =
      std::vector<SceneContour>{{{{20, 40}, {80, 40}, {80, 100}}, false}};
  p.stroke_cap = Cap::Butt;
  p.stroke_join = Join::Miter;
  assert(inside(stroke_outlines(corner, p), {89, 31}));
  p.stroke_join = Join::Round;
  assert(!inside(stroke_outlines(corner, p), {89, 31}));
  assert(inside(stroke_outlines(corner, p), {86, 34}));
  p.stroke_join = Join::Bevel;
  assert(!inside(stroke_outlines(corner, p), {86, 34}));
  assert(inside(stroke_outlines(corner, p), {82, 34}));
  p.stroke_join = Join::None;
  assert(!inside(stroke_outlines(corner, p), {82, 34}));
  assert(inside(stroke_outlines(corner, p), {79, 40}));
  for (auto data :
       {"M40 40 L40 40", "M40 40z", "M40 40h0", "M40 40C40 40 40 40 40 40"}) {
    auto contours = flatten_path(parse_svg_path(data));
    p.stroke_cap = Cap::Butt;
    assert(stroke_outlines(contours, p).empty());
    p.stroke_cap = Cap::Round;
    assert(inside(stroke_outlines(contours, p), {40, 40}));
    assert(!inside(stroke_outlines(contours, p), {31, 31}));
    p.stroke_cap = Cap::Square;
    assert(inside(stroke_outlines(contours, p), {31, 31}));
  }
  assert(stroke_outlines(flatten_path(parse_svg_path("M40 40")), p).empty());
  rejects([&] { stroke_outlines(line, p, {}, .1, 3); });
  rejects([&] { stroke_outlines(line, p, {}, 0); });
  rejects([&] { stroke_outlines(line, p, {}, .1, 1000001); });
  p.stroke_width = std::numeric_limits<double>::infinity();
  rejects([&] { stroke_outlines(line, p); });
}
OdfPackage styled() {
  auto p = fixture();
  auto xml = p.xml;
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto automatic = xml.createElementNS(OFFICE, "office:automatic-styles");
  xml.documentElement().insertBefore(
      automatic, child(xml.documentElement(), OFFICE, "body"));
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  if (common.isNull()) {
    common = styles.createElementNS(OFFICE, "office:styles");
    styles.documentElement().insertBefore(
        common, styles.documentElement().firstChild());
  }
  auto base = styles.createElementNS(STYLE, "style:style");
  base.setAttributeNS(STYLE, "style:name", "Stroke");
  base.setAttributeNS(STYLE, "style:family", "graphic");
  auto graphic = styles.createElementNS(STYLE, "style:graphic-properties");
  graphic.setAttributeNS(DRAW, "draw:stroke", "solid");
  graphic.setAttributeNS(SVG, "svg:stroke-width", "13pt");
  graphic.setAttributeNS(SVG, "svg:stroke-linecap", "round");
  graphic.setAttributeNS(DRAW, "draw:stroke-linejoin", "bevel");
  base.appendChild(graphic);
  common.appendChild(base);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  for (auto name : {"rect", "frame", "line"})
    for (auto node : xml.elementsByTagNameNS(DRAW, name))
      node.setAttributeNS(DRAW, "draw:style-name", "Stroke");
  auto direct = xml.importNode(base, true).toElement();
  direct.setAttributeNS(STYLE, "style:name", "Direct");
  direct.setAttributeNS(STYLE, "style:parent-style-name", "Stroke");
  direct.removeChild(direct.firstChild());
  auto props = xml.createElementNS(STYLE, "style:graphic-properties");
  props.setAttributeNS(SVG, "svg:stroke-linecap", "square");
  direct.appendChild(props);
  automatic.appendChild(direct);
  xml.elementsByTagNameNS(DRAW, "rect")
      .front()
      .setAttributeNS(DRAW, "draw:style-name", "Direct");
  return p;
}
void cascade_diagnostics() {
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto package = styled();
  auto styles = read_document_xml(package.entry("styles.xml"));
  auto legacy = styles.createElementNS(STYLE, "style:style");
  legacy.setAttributeNS(STYLE, "style:name", "Legacy");
  legacy.setAttributeNS(STYLE, "style:family", "graphic");
  legacy.setAttributeNS(STYLE, "style:parent-style-name", "Stroke");
  auto props = styles.createElementNS(STYLE, "style:graphic-properties");
  props.setAttributeNS(DRAW, "draw:stroke-linejoin", "middle");
  legacy.appendChild(props);
  child(styles.documentElement(), OFFICE, "styles").appendChild(legacy);
  package.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  XmlElement direct;
  for (auto n : package.xml.elementsByTagNameNS(STYLE, "style"))
    if (n.attributeNS(STYLE, "name") == "Direct")
      direct = n;
  direct.setAttributeNS(STYLE, "style:parent-style-name", "Legacy");
  auto scene = read_presentation_scenes(package).front();
  assert(std::find(scene.unsupported.begin(), scene.unsupported.end(),
                   Text("Deprecated middle stroke joins")) !=
         scene.unsupported.end());
  child(direct, STYLE, "graphic-properties")
      .setAttributeNS(DRAW, "draw:stroke-linejoin", "miter");
  scene = read_presentation_scenes(package).front();
  assert(std::find(scene.unsupported.begin(), scene.unsupported.end(),
                   Text("Deprecated middle stroke joins")) ==
         scene.unsupported.end());
}
void authoring() {
  TempDir dir;
  auto source = dir.path() + "/source.odp", saved = dir.path() + "/saved.odp";
  styled().save(source);
  Presentation p;
  p.load(source);
  auto original = p.scene(0).source.ownerDocument().toByteArray();
  auto styles = p.image("styles.xml"),
       manifest = p.image("META-INF/manifest.xml");
  auto info = p.object_paint(0, {ref(p, "A"), ref(p, "B")});
  assert(!info.values.stroke_cap && info.values.stroke_join == "bevel");
  assert(p.scene(0).shapes[find(p, "A")].paint.stroke_cap == Cap::Square);
  assert(p.scene(0).shapes[find(p, "A")].paint.stroke_join == Join::Bevel);
  p.move_shape(0, find(p, "B"), {1, 0});
  p.undo();
  auto revision = p.revision();
  NativePaintFormat patch;
  patch.stroke_cap = "square";
  assert(!p.set_object_paint(0, {ref(p, "A")}, patch));
  patch.stroke_join = "invalid";
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, patch); });
  assert(p.can_redo() && p.revision() == revision &&
         p.scene(0).source.ownerDocument().toByteArray() == original);
  patch.stroke_cap = "butt";
  patch.stroke_join = "none";
  assert(p.set_object_paint(0, {ref(p, "A")}, patch));
  auto after = p.scene(0).source.ownerDocument().toByteArray();
  assert(after != original && p.modified() && !p.can_redo());
  assert(p.scene(0).shapes[find(p, "A")].paint.stroke_join == Join::None);
  assert(p.scene(0).shapes[find(p, "B")].paint.stroke_cap == Cap::Round);
  assert(p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest);
  p.undo();
  assert(p.scene(0).source.ownerDocument().toByteArray() == original);
  p.redo();
  assert(p.scene(0).source.ownerDocument().toByteArray() == after);
  p.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(reopened.scene(0).source.ownerDocument().toByteArray() == after);
  assert(reopened.scene(0).shapes[find(reopened, "A")].paint.stroke_cap ==
         Cap::Butt);
  patch.stroke_cap = "round";
  patch.stroke_join = "round";
  p.set_object_paint(0, {ref(p, "X", 1)}, patch);
  for (auto name : {"X", "Y", "N1", "N2"})
    assert(p.scene(0).shapes[find(p, name)].paint.stroke_join == Join::Round);
  assert(p.scene(0).shapes[find(p, "B")].paint.stroke_join == Join::Bevel);
}
} // namespace
int main() {
  geometry_edges();
  independent_cairo();
  authoring();
  cascade_diagnostics();
  std::cout << "PASS native stroke geometry, ODF cascade, scoped styles, "
               "source/history and save/reopen\n";
}
