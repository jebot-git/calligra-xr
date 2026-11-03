// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
#include "native_path.h"
#include <cmath>
#include <numbers>
namespace DomeOffice {
namespace {
constexpr double degrees = 180 / std::numbers::pi;
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void finite(double value, double limit) {
  if (!std::isfinite(value) || std::abs(value) > limit)
    fail("Object geometry exceeds the native coordinate limit");
}
Text number(double n) {
  finite(n, 1e9);
  return Text::number(n, 'g', 17);
}
Text pt(double n) { return number(n) + "pt"; }
SceneRect local_bounds(const SceneShape &s) {
  if (s.path)
    return path_bounds(*s.path, s.path_map);
  SceneRect b;
  bool first = true;
  for (const auto &contour : s.contours)
    for (auto p : contour.points) {
      if (first) {
        b = {p.x, p.y, 0, 0};
        first = false;
        continue;
      }
      double right = std::max(b.x + b.width, p.x),
             bottom = std::max(b.y + b.height, p.y);
      b.x = std::min(b.x, p.x);
      b.y = std::min(b.y, p.y);
      b.width = right - b.x;
      b.height = bottom - b.y;
    }
  return b;
}
bool mapped_shape(const SceneShape &s) {
  return s.kind == SceneShape::Path || s.kind == SceneShape::Polygon ||
         s.kind == SceneShape::Polyline;
}
std::pair<ScenePoint, ScenePoint> endpoints(const SceneShape &s) {
  if (s.contours.empty() || s.contours[0].points.size() != 2)
    fail("Line endpoints are unavailable");
  return {s.transform.map(s.contours[0].points[0]),
          s.transform.map(s.contours[0].points[1])};
}
Text scaled_path(const ScenePath &path, long double sx, long double sy) {
  auto point = [&](ScenePoint p) {
    return number(double(p.x * sx)) + " " + number(double(p.y * sy));
  };
  TextList out;
  for (const auto &sub : path.subpaths) {
    out << "M " + point(sub.start);
    for (const auto &segment : sub.segments) {
      switch (segment.kind) {
      case PathSegment::Line:
        out << "L " + point(segment.end);
        break;
      case PathSegment::Cubic:
        out << "C " + point(segment.control1) + " " + point(segment.control2) +
                   " " + point(segment.end);
        break;
      case PathSegment::Quadratic:
        out << "Q " + point(segment.control1) + " " + point(segment.end);
        break;
      case PathSegment::Arc: {
        // Positive axis scaling transforms the ellipse covariance. Use the
        // determinant for the small radius to avoid subtracting close roots.
        long double rx = segment.rx, ry = segment.ry,
                    phi = std::remainder(segment.rotation, 360.) / degrees;
        long double c = std::cos(phi), s = std::sin(phi);
        long double a = sx * sx * (rx * rx * c * c + ry * ry * s * s),
                    d = sy * sy * (rx * rx * s * s + ry * ry * c * c),
                    b = sx * sy * (rx * rx - ry * ry) * c * s;
        long double large = std::sqrt(
                        std::max(0.L, (a + d + std::hypot(a - d, 2 * b)) / 2)),
                    small = large == 0 ? 0
                                       : std::abs(static_cast<long double>(sx) *
                                                  sy * rx * ry) /
                                             large;
        if (rx == 0 || ry == 0) {
          out << "L " + point(segment.end);
          break;
        }
        out << "A " + number(double(large)) + " " + number(double(small)) +
                   " " +
                   number(double(.5L * std::atan2(2 * b, a - d) * degrees)) +
                   " " + (segment.large_arc ? "1" : "0") + " " +
                   (segment.sweep ? "1" : "0") + " " + point(segment.end);
        break;
      }
      }
    }
    if (sub.closed)
      out << "Z";
  }
  auto value = out.join(' ');
  if (value.size() > 1048576)
    fail("Resized path exceeds the native data limit");
  return value;
}
void resize(XmlElement source, const SceneShape &s,
            const NativeShapeGeometry &old, double width, double height) {
  bool x = width != old.width, y = height != old.height;
  if (!x && !y)
    return;
  if ((x && !old.width_enabled) || (y && !old.height_enabled))
    fail("A zero-extent path axis cannot be resized");
  for (auto [changed, value] : {std::pair{x, width}, std::pair{y, height}})
    if (changed && (!(value > 0) || value > 1e7))
      fail("Enter positive width and height within the native limit");
  if (mapped_shape(s) && !source.hasAttributeNS(SVG, "viewBox")) {
    long double sx = x ? static_cast<long double>(width) / old.width : 1,
                sy = y ? static_cast<long double>(height) / old.height : 1;
    if (old.width > 0 && old.height > 0 && old.width <= 1e7 &&
        old.height <= 1e7 && std::isfinite(double(sx)) &&
        std::isfinite(double(sy))) {
      // A viewport scales geometry only, retaining exact curves and font sizes.
      source.setAttributeNS(SVG, "svg:viewBox",
                            "0 0 " + number(old.width) + " " +
                                number(old.height));
      if (x || s.width <= 0)
        source.setAttributeNS(SVG, "svg:width", pt(width));
      if (y || s.height <= 0)
        source.setAttributeNS(SVG, "svg:height", pt(height));
    } else if (s.kind == SceneShape::Path) {
      source.setAttributeNS(SVG, "svg:d", scaled_path(*s.path, sx, sy));
    } else {
      TextList points;
      for (auto p : s.contours.at(0).points)
        points << number(double(p.x * sx)) + "," + number(double(p.y * sy));
      source.setAttributeNS(DRAW, "draw:points", points.join(' '));
    }
  }
  bool radius_form =
      source.localName() == "circle" || source.hasAttributeNS(SVG, "r") ||
      (source.hasAttributeNS(SVG, "rx") && source.hasAttributeNS(SVG, "ry")) ||
      (source.hasAttributeNS(SVG, "cx") && source.hasAttributeNS(SVG, "cy"));
  if (s.kind == SceneShape::Ellipse && radius_form) {
    // Normalize radius/center variants only during a size change. Preserve
    // their old local origin and every unrelated attribute/child/identifier.
    if (source.hasAttributeNS(SVG, "cx") && source.hasAttributeNS(SVG, "cy")) {
      source.setAttributeNS(
          SVG, "svg:x",
          pt(parse_scene_length(source.attributeNS(SVG, "cx")) - s.width / 2));
      source.setAttributeNS(
          SVG, "svg:y",
          pt(parse_scene_length(source.attributeNS(SVG, "cy")) - s.height / 2));
    }
    for (auto key : {"r", "rx", "ry", "cx", "cy"})
      source.removeAttributeNS(SVG, key);
    if (source.localName() == "circle")
      source.renameLocal("ellipse");
    // Radius-form ellipses did not necessarily have either dimension stored.
    source.setAttributeNS(SVG, "svg:width", pt(width));
    source.setAttributeNS(SVG, "svg:height", pt(height));
  } else {
    if (x)
      source.setAttributeNS(SVG, "svg:width", pt(width));
    if (y)
      source.setAttributeNS(SVG, "svg:height", pt(height));
  }
}
} // namespace
NativeShapeGeometry native_shape_geometry(const SceneShape &s) {
  if (s.master || s.source.isNull())
    fail("Master objects are not editable");
  NativeShapeGeometry out;
  out.line = s.kind == SceneShape::Line;
  if (out.line) {
    auto [a, b] = endpoints(s);
    out.origin = a;
    out.width = std::hypot(b.x - a.x, b.y - a.y);
    if (out.width > 0)
      out.rotation = std::atan2(b.y - a.y, b.x - a.x) * degrees;
    out.position_enabled = out.width_enabled = out.rotation_enabled =
        s.transform.inverse().has_value();
    out.height_enabled = false;
    return out;
  }
  out.origin = s.transform.map({});
  out.width = s.width;
  out.height = s.height;
  if (mapped_shape(s) && !s.source.hasAttributeNS(SVG, "viewBox")) {
    auto b = local_bounds(s);
    if (out.width <= 0)
      out.width = b.width;
    if (out.height <= 0)
      out.height = b.height;
    out.width_enabled = out.width > 0;
    out.height_enabled = out.height > 0;
  }
  const auto &m = s.transform;
  if (m.a != 0 || m.b != 0)
    out.rotation = std::atan2(m.b, m.a) * degrees;
  else if (m.c != 0 || m.d != 0)
    out.rotation = std::atan2(-m.c, m.d) * degrees;
  out.position_enabled = s.parent_transform.inverse().has_value();
  out.rotation_enabled = out.position_enabled && out.rotation.has_value();
  return out;
}
NativeShapeGeometry Presentation::shape_geometry(int slide, int index) const {
  return native_shape_geometry(scene(slide).shapes.at(index));
}
void Presentation::set_shape_geometry(int slide, int index,
                                      const NativeShapeGeometryEdit &edit) {
  if (!edit.x && !edit.y && !edit.width && !edit.height && !edit.rotation)
    return;
  for (const auto &v : {edit.x, edit.y, edit.width, edit.height, edit.rotation})
    if (v)
      finite(*v, 1e9);
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    fail("This presentation is read-only");
  const auto &shape = scene(slide).shapes.at(index);
  auto old = native_shape_geometry(shape);
  double x = edit.x.value_or(old.origin.x), y = edit.y.value_or(old.origin.y),
         w = edit.width.value_or(old.width),
         h = edit.height.value_or(old.height), delta = 0;
  if (edit.rotation) {
    if (!old.rotation && !old.line)
      fail("This shape has no defined rotation axis");
    if (old.rotation)
      delta = std::remainder(*edit.rotation - *old.rotation, 360.);
  }
  bool moved = x != old.origin.x || y != old.origin.y, rotated = delta != 0;
  if (!moved && !rotated && w == old.width && h == old.height)
    return;
  auto before = package_.xml.toByteArray();
  auto candidate = read_document_xml(before);
  auto source = stage_candidate_source(shape.source, candidate);
  if (old.line) {
    if (h != 0)
      fail("Lines use length and rotation, rather than height");
    if (w < 0 || w > 1e9 || (w == 0 && old.width > 0))
      fail("Enter a positive line length within the native limit");
    auto inverse = shape.transform.inverse();
    if (!inverse)
      fail("The line transform is singular");
    auto [a, b] = endpoints(shape);
    ScenePoint vector{b.x - a.x, b.y - a.y};
    if (old.width == 0 && w > 0) {
      if (!edit.rotation)
        fail("Choose a direction for the zero-length line");
      auto radians = std::remainder(*edit.rotation, 360.) / degrees;
      vector = {w * std::cos(radians), w * std::sin(radians)};
    } else if (rotated) {
      auto radians = std::remainder(*edit.rotation, 360.) / degrees;
      vector = {w * std::cos(radians), w * std::sin(radians)};
    } else if (w != old.width) {
      vector.x *= w / old.width;
      vector.y *= w / old.width;
    }
    auto first = inverse->map({x, y}),
         last = inverse->map({x + vector.x, y + vector.y});
    for (auto v : {first.x, first.y, last.x, last.y})
      finite(v, 1e7);
    if (w > 0 && first.x == last.x && first.y == last.y)
      fail("A line needs two different endpoints");
    if (moved) {
      source.setAttributeNS(SVG, "svg:x1", pt(first.x));
      source.setAttributeNS(SVG, "svg:y1", pt(first.y));
    }
    source.setAttributeNS(SVG, "svg:x2", pt(last.x));
    source.setAttributeNS(SVG, "svg:y2", pt(last.y));
  } else {
    resize(source, shape, old, w, h);
    if (moved || rotated) {
      auto inverse = shape.parent_transform.inverse();
      if (!inverse)
        fail("The containing group transform is singular");
      auto radians = delta / degrees;
      double c = std::cos(radians), s = std::sin(radians);
      SceneTransform action{c,
                            s,
                            -s,
                            c,
                            x - c * old.origin.x + s * old.origin.y,
                            y - s * old.origin.x - c * old.origin.y};
      auto own =
          parse_scene_transform(shape.source.attributeNS(DRAW, "transform"));
      source.setAttributeNS(
          DRAW, "draw:transform",
          format_scene_transform(*inverse * action * shape.parent_transform *
                                 own));
    }
    if (shape.kind == SceneShape::Frame)
      source.setAttributeNS(PRESENTATION, "presentation:user-transformed",
                            "true");
  }
  PresentationPosition at{slide, shape.editor_index, index};
  restore_content(candidate.toByteArray());
  remember(before, at, at);
}
} // namespace DomeOffice
