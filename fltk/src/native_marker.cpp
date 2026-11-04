// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_marker.h"
#include "native_stroke.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
namespace DomeOffice {
namespace {
[[noreturn]] void fail() {
  throw std::runtime_error(
      "Invalid marker definition, width or endpoint geometry");
}
ScenePoint unit(ScenePoint p) {
  auto length = std::hypot(p.x, p.y);
  if (!std::isfinite(length) || length == 0)
    fail();
  return {p.x / length, p.y / length};
}
} // namespace
std::shared_ptr<const SceneMarkerDefinition>
read_scene_marker(XmlElement node) {
  auto definition = std::make_shared<SceneMarkerDefinition>();
  if (!node.hasAttributeNS(SVG, "d") || !node.hasAttributeNS(SVG, "viewBox"))
    fail();
  definition->data = node.attributeNS(SVG, "d");
  definition->view_box = node.attributeNS(SVG, "viewBox");
  auto raw = definition->view_box.toStdString();
  std::replace(raw.begin(), raw.end(), ',', ' ');
  std::istringstream input(raw);
  double values[4];
  for (auto &value : values) {
    std::string token;
    if (!(input >> token))
      fail();
    auto first = token.data();
    if (!token.empty() && token[0] == '+')
      ++first;
    auto parsed = std::from_chars(first, token.data() + token.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != token.data() + token.size() ||
        !std::isfinite(value) || std::abs(value) > 1e9)
      fail();
  }
  std::string extra;
  if (input >> extra || values[2] < 0 || values[3] < 0)
    fail();
  definition->enabled = values[2] > 0 && values[3] > 0;
  definition->path =
      std::make_shared<const ScenePath>(parse_svg_path(definition->data));
  definition->bounds = path_bounds(*definition->path);
  return definition;
}
std::vector<ScenePathEnds>
scene_contour_ends(const std::vector<SceneContour> &contours) {
  if (contours.size() > 100000)
    fail();
  std::vector<ScenePathEnds> result;
  for (const auto &c : contours) {
    for (auto p : c.points)
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e10 ||
          std::abs(p.y) > 1e10)
        fail();
    ScenePathEnds ends;
    ends.open = !c.closed && (c.points.size() > 1 || c.stroke_zero_length);
    ends.stroke = c.stroke;
    ends.paint_group = c.paint_group;
    ends.first.inward = {1, 0};
    ends.last.inward = {-1, 0};
    if (!c.points.empty()) {
      ends.first.point = c.points.front();
      ends.last.point = c.points.back();
      for (size_t i = 1; i < c.points.size(); ++i) {
        auto a = c.points[i - 1], b = c.points[i];
        if (a.x != b.x || a.y != b.y) {
          ends.first.inward = unit({b.x - a.x, b.y - a.y});
          break;
        }
      }
      for (size_t i = c.points.size(); i > 1; --i) {
        auto a = c.points[i - 2], b = c.points[i - 1];
        if (a.x != b.x || a.y != b.y) {
          ends.last.inward = unit({a.x - b.x, a.y - b.y});
          break;
        }
      }
    }
    for (auto p : {ends.first.point, ends.last.point})
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e10 ||
          std::abs(p.y) > 1e10)
        fail();
    result.push_back(ends);
  }
  return result;
}
SceneTransform marker_transform(const SceneMarkerDefinition &marker,
                                double width, bool centered,
                                const ScenePathEnd &end) {
  const auto b = marker.bounds;
  if (!std::isfinite(width) || width < 0 || width > 1e9 || b.width <= 0 ||
      !std::isfinite(b.width) || !std::isfinite(b.height) || b.height < 0 ||
      !std::isfinite(b.x) || !std::isfinite(b.y) ||
      !std::isfinite(end.point.x) || !std::isfinite(end.point.y))
    fail();
  auto inward = unit(end.inward);
  const double scale = width / b.width;
  // Corrected OASIS placement (OFFICE-3685/3756): the local positive-y axis
  // points into the stroke. Center=true anchors the geometry center;
  // center=false anchors its top-edge center. Keep ODF1.2 source syntax.
  const double x = b.x + b.width / 2, y = b.y + (centered ? b.height / 2 : 0);
  SceneTransform result{scale * inward.y, -scale * inward.x, scale * inward.x,
                        scale * inward.y};
  result.e = end.point.x - result.a * x - result.c * y;
  result.f = end.point.y - result.b * x - result.d * y;
  for (auto v : {result.a, result.b, result.c, result.d, result.e, result.f})
    if (!std::isfinite(v) || std::abs(v) > 1e10)
      fail();
  return result;
}
std::vector<SceneContour> marker_contours(const SceneMarkerDefinition &marker,
                                          double width, bool centered,
                                          const ScenePathEnd &end,
                                          const SceneTransform &device,
                                          double tolerance,
                                          size_t point_limit) {
  if (!std::isfinite(width) || width < 0 || width > 1e9 || !marker.path ||
      !std::isfinite(tolerance) || tolerance <= 0 || point_limit < 1 ||
      point_limit > 1000000)
    fail();
  if (!marker.enabled || width == 0 || marker.bounds.width == 0 ||
      marker.bounds.height == 0)
    return {};
  auto contours =
      flatten_path(*marker.path, marker_transform(marker, width, centered, end),
                   device, tolerance, point_limit);
  for (auto &c : contours) {
    c.closed = true;
    c.stroke = false;
    c.fill = true;
  }
  return contours;
}
namespace {
SceneContour trim(const SceneContour &source, double first, double last,
                  size_t limit) {
  SceneContour result = source;
  result.points.clear();
  result.stroke_start_tangent.reset();
  result.stroke_butt_start = first > 0 || source.stroke_butt_start;
  result.stroke_butt_end = last > 0 || source.stroke_butt_end;
  if (source.points.size() > limit)
    fail();
  std::vector<double> distances{0};
  for (size_t i = 1; i < source.points.size(); ++i) {
    auto a = source.points[i - 1], b = source.points[i];
    auto value = distances.back() + std::hypot(b.x - a.x, b.y - a.y);
    if (!std::isfinite(value))
      fail();
    distances.push_back(value);
  }
  auto total = distances.back();
  if (first + last >= total)
    return result;
  auto interpolate = [&](double at) {
    auto next = std::upper_bound(distances.begin(), distances.end(), at);
    size_t i = std::min(size_t(next - distances.begin()), distances.size() - 1);
    if (!i)
      return source.points.front();
    auto a = source.points[i - 1], b = source.points[i];
    double length = distances[i] - distances[i - 1];
    double ratio = length ? (at - distances[i - 1]) / length : 0;
    return ScenePoint{a.x + (b.x - a.x) * ratio, a.y + (b.y - a.y) * ratio};
  };
  result.points.push_back(interpolate(first));
  for (size_t i = 1; i < source.points.size(); ++i)
    if (distances[i] > first && distances[i] < total - last)
      result.points.push_back(source.points[i]);
  result.points.push_back(interpolate(total - last));
  result.stroke_dash_offset += first;
  if (!std::isfinite(result.stroke_dash_offset) ||
      result.stroke_dash_offset > 1e10)
    fail();
  return result;
}
} // namespace
SceneMarkerStroke marker_stroke_geometry(
    const SceneShape &shape, const std::vector<SceneContour> &contours,
    const SceneTransform &device, double tolerance, size_t point_limit) {
  if (point_limit < 1 || point_limit > 1000000 || !std::isfinite(tolerance) ||
      tolerance <= 0)
    fail();
  SceneMarkerStroke result{contours, {}};
  if (!shape.paint.stroke || (!shape.paint.marker_start.definition &&
                              !shape.paint.marker_end.definition))
    return result;
  auto ends = shape.path ? scene_path_ends(*shape.path, shape.path_map)
                         : scene_contour_ends(contours);
  if (ends.size() != contours.size())
    fail();
  size_t points = 0;
  for (size_t i = 0; i < ends.size(); ++i) {
    if (!ends[i].open || !ends[i].stroke)
      continue;
    double shortening[2]{};
    for (size_t j = 0; j < 2; ++j) {
      auto use = j ? shape.paint.marker_end : shape.paint.marker_start;
      if (!use.definition)
        continue;
      auto placed = marker_contours(*use.definition, use.width, use.centered,
                                    j ? ends[i].last : ends[i].first, device,
                                    tolerance, point_limit);
      if (placed.empty())
        continue;
      // Canonicalize each marker before combining it with stroke outlines.
      // Source polygons may have either outer winding and nested holes.
      placed = filled_contour_union(placed, point_limit);
      if (placed.empty())
        continue;
      for (auto &c : placed) {
        if (c.points.size() > point_limit - points)
          fail();
        points += c.points.size();
        c.paint_group = ends[i].paint_group;
        c.fill = true;
        c.stroke = false;
        result.markers.push_back(std::move(c));
      }
      const auto b = use.definition->bounds;
      shortening[j] =
          b.height * (use.width / b.width) * (use.centered ? .5 : 1);
      if (!std::isfinite(shortening[j]) || shortening[j] > 1e10)
        fail();
    }
    if (shortening[0] || shortening[1])
      result.shafts[i] =
          trim(contours[i], shortening[0], shortening[1], point_limit);
  }
  for (const auto &c : result.shafts) {
    if (c.points.size() > point_limit - points)
      fail();
    points += c.points.size();
  }
  return result;
}
} // namespace DomeOffice
