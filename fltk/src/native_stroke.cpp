// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stroke.h"
#include <GL/glu.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
namespace DomeOffice {
namespace {
[[noreturn]] void fail() {
  throw std::runtime_error(
      "Stroke geometry exceeds the native precision or point limit");
}
ScenePoint add(ScenePoint a, ScenePoint b) { return {a.x + b.x, a.y + b.y}; }
ScenePoint sub(ScenePoint a, ScenePoint b) { return {a.x - b.x, a.y - b.y}; }
ScenePoint mul(ScenePoint a, double v) { return {a.x * v, a.y * v}; }
double cross(ScenePoint a, ScenePoint b) { return a.x * b.y - a.y * b.x; }
bool same(ScenePoint a, ScenePoint b) { return a.x == b.x && a.y == b.y; }
// Canonicalize the union before painting. Independent overlapping polygons can
// leave numerical cracks in rasterizers even with a nonzero fill rule. GLU's
// CPU boundary tessellation needs no GL context and retains holes and
// crossings.
struct Union {
  std::deque<std::array<GLdouble, 3>> vertices;
  std::array<GLdouble, 3> overflow{};
  std::vector<SceneContour> contours;
  size_t points = 0, combined = 0, limit;
  bool error = false;
  static void begin(GLenum mode, void *data) {
    auto &u = *static_cast<Union *>(data);
    if (mode != GL_LINE_LOOP)
      u.error = true;
    u.contours.push_back({{}, true});
  }
  static void point(void *value, void *data) {
    auto &u = *static_cast<Union *>(data);
    if (u.error)
      return;
    if (++u.points > u.limit || u.contours.empty()) {
      u.error = true;
      return;
    }
    const auto *p = static_cast<GLdouble *>(value);
    u.contours.back().points.push_back({p[0], p[1]});
  }
  static void combine(GLdouble coords[3], void *[4], GLfloat[4], void **out,
                      void *data) {
    auto &u = *static_cast<Union *>(data);
    if (++u.combined > u.limit) {
      u.error = true;
      // GLU owns geometric coordinates separately from this callback payload.
      // Once failed, vertex callbacks discard every payload without reading it.
      *out = u.overflow.data();
      return;
    }
    u.vertices.push_back({coords[0], coords[1], 0});
    *out = u.vertices.back().data();
  }
  static void failed(GLenum, void *data) {
    static_cast<Union *>(data)->error = true;
  }
  static std::vector<SceneContour> make(const std::vector<SceneContour> &input,
                                        size_t limit) {
    Union u{{}, {}, {}, 0, 0, limit};
    std::unique_ptr<GLUtesselator, decltype(&gluDeleteTess)> tess(
        gluNewTess(), gluDeleteTess);
    if (!tess)
      fail();
    gluTessCallback(tess.get(), GLU_TESS_BEGIN_DATA,
                    reinterpret_cast<_GLUfuncptr>(begin));
    gluTessCallback(tess.get(), GLU_TESS_VERTEX_DATA,
                    reinterpret_cast<_GLUfuncptr>(point));
    gluTessCallback(tess.get(), GLU_TESS_COMBINE_DATA,
                    reinterpret_cast<_GLUfuncptr>(combine));
    gluTessCallback(tess.get(), GLU_TESS_ERROR_DATA,
                    reinterpret_cast<_GLUfuncptr>(failed));
    gluTessProperty(tess.get(), GLU_TESS_BOUNDARY_ONLY, GL_TRUE);
    gluTessProperty(tess.get(), GLU_TESS_WINDING_RULE,
                    GLU_TESS_WINDING_NONZERO);
    gluTessNormal(tess.get(), 0, 0, 1);
    gluTessBeginPolygon(tess.get(), &u);
    for (const auto &c : input) {
      gluTessBeginContour(tess.get());
      for (auto p : c.points) {
        u.vertices.push_back({p.x, p.y, 0});
        auto *v = u.vertices.back().data();
        gluTessVertex(tess.get(), v, v);
      }
      gluTessEndContour(tess.get());
    }
    gluTessEndPolygon(tess.get());
    if (u.error)
      fail();
    return std::move(u.contours);
  }
};
struct Outline {
  std::vector<SceneContour> contours;
  size_t count = 0, limit;
  double radius, device_radius, tolerance;
  void polygon(std::vector<ScenePoint> points) {
    if (points.size() < 3)
      return;
    if (points.size() > limit - count)
      fail();
    double area = 0;
    const auto origin = points.front();
    for (size_t i = 0; i < points.size(); ++i) {
      auto p = points[i];
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e10 ||
          std::abs(p.y) > 1e10)
        fail();
      area +=
          cross(sub(p, origin), sub(points[(i + 1) % points.size()], origin));
    }
    if (area == 0)
      return;
    if (area < 0)
      std::reverse(points.begin(), points.end());
    count += points.size();
    contours.push_back({std::move(points), true});
  }
  void disk(ScenePoint center) {
    // For a chord, device-space sagitta <= R * angle^2 / 8. The Frobenius
    // bound used for R remains conservative under shear/nonuniform scaling.
    double estimate = std::ceil(std::numbers::pi *
                                std::sqrt(device_radius / (2 * tolerance)));
    if (!std::isfinite(estimate) || estimate > double(limit - count))
      fail();
    size_t n = std::max(size_t(12), size_t(estimate));
    // Keep the four extrema exact, including small caps and degenerate dots.
    n = (n + 3) / 4 * 4;
    if (n > limit - count)
      fail();
    std::vector<ScenePoint> points;
    points.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      double a = 2 * std::numbers::pi * double(i) / double(n);
      points.push_back(
          add(center, {radius * std::cos(a), radius * std::sin(a)}));
    }
    polygon(std::move(points));
  }
  void cap(ScenePoint center, ScenePoint direction, bool start,
           ScenePaint::Cap mode) {
    if (mode == ScenePaint::Cap::Butt)
      return;
    auto outward = mul(direction, start ? -radius : radius);
    auto normal = ScenePoint{-direction.y * radius, direction.x * radius};
    if (mode == ScenePaint::Cap::Square) {
      polygon({add(center, normal), add(add(center, normal), outward),
               add(sub(center, normal), outward), sub(center, normal)});
      return;
    }
    // Caps are semicircles, not disks. A full disk adds incorrect ink when a
    // short segment immediately reverses or a dash starts at a corner.
    double estimate = std::ceil(std::numbers::pi *
                                std::sqrt(device_radius / (8 * tolerance)));
    if (!std::isfinite(estimate) || estimate + 2 > double(limit - count))
      fail();
    size_t steps = std::max(size_t(6), size_t(estimate));
    steps = (steps + 1) / 2 * 2;
    std::vector<ScenePoint> arc{center};
    double angle = std::atan2(direction.y, direction.x) +
                   (start ? std::numbers::pi / 2 : -std::numbers::pi / 2);
    for (size_t i = 0; i <= steps; ++i) {
      double a = angle + std::numbers::pi * double(i) / double(steps);
      arc.push_back(add(center, {radius * std::cos(a), radius * std::sin(a)}));
    }
    polygon(std::move(arc));
  }
  void join(ScenePoint vertex, ScenePoint before, ScenePoint after,
            ScenePaint::Join mode) {
    if (mode == ScenePaint::Join::None)
      return;
    double turn = cross(before, after);
    double dot = before.x * after.x + before.y * after.y;
    if (mode == ScenePaint::Join::Round && std::abs(turn) <= 1e-14 && dot < 0) {
      cap(vertex, before, false, ScenePaint::Cap::Round);
      return;
    }
    if (turn == 0)
      return;
    double sign = turn > 0 ? -1 : 1;
    auto a = ScenePoint{-before.y * sign * radius, before.x * sign * radius};
    auto b = ScenePoint{-after.y * sign * radius, after.x * sign * radius};
    auto first = add(vertex, a), last = add(vertex, b);
    if (mode == ScenePaint::Join::Round) {
      const double sweep = std::atan2(turn, dot);
      double estimate = std::ceil(std::abs(sweep) *
                                  std::sqrt(device_radius / (8 * tolerance)));
      if (!std::isfinite(estimate) || estimate + 2 > double(limit - count))
        fail();
      size_t steps = std::max(size_t(1), size_t(estimate));
      std::vector<ScenePoint> arc{vertex, first};
      const double start = std::atan2(a.y, a.x);
      for (size_t i = 1; i < steps; ++i) {
        double angle = start + sweep * double(i) / double(steps);
        arc.push_back(
            add(vertex, {radius * std::cos(angle), radius * std::sin(angle)}));
      }
      arc.push_back(last);
      polygon(std::move(arc));
      return;
    }
    if (mode == ScenePaint::Join::Miter) {
      double denominator = 1 + before.x * after.x + before.y * after.y;
      if (denominator > 1e-14) {
        auto miter = mul(add(a, b), 1 / denominator);
        if (std::hypot(miter.x, miter.y) <=
            radius * ScenePaint::stroke_miter_limit) {
          polygon({vertex, first, add(vertex, miter), last});
          return;
        }
      }
    }
    polygon({vertex, first, last});
  }
};
} // namespace
std::vector<SceneContour>
stroke_outlines(const std::vector<SceneContour> &contours,
                const ScenePaint &paint, const SceneTransform &device,
                double tolerance, size_t point_limit) {
  if (!std::isfinite(paint.stroke_width) || paint.stroke_width < 0 ||
      !std::isfinite(tolerance) || tolerance <= 0 || point_limit < 1 ||
      point_limit > 1000000)
    fail();
  if (!paint.stroke || paint.stroke_width == 0)
    return {};
  double scale = std::hypot(std::hypot(device.a, device.b),
                            std::hypot(device.c, device.d));
  if (!std::isfinite(scale))
    fail();
  Outline out{{},
              0,
              point_limit,
              paint.stroke_width / 2,
              paint.stroke_width / 2 * scale,
              tolerance};
  for (const auto &contour : contours) {
    if (!contour.stroke || contour.points.empty())
      continue;
    if (contour.points.size() > point_limit)
      fail();
    std::vector<ScenePoint> points;
    for (auto p : contour.points) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y))
        fail();
      if (points.empty() || !same(points.back(), p))
        points.push_back(p);
    }
    if (contour.closed && points.size() > 1 &&
        same(points.front(), points.back()))
      points.pop_back();
    if (points.size() == 1) {
      // SVG strokes a zero-length segment/close with round/square caps, but
      // a moveto-only subpath has no ink. A directionless square is
      // axis-aligned.
      if (contour.stroke_zero_length || contour.points.size() > 1) {
        if (paint.stroke_cap == ScenePaint::Cap::Round)
          out.disk(points.front());
        else if (paint.stroke_cap == ScenePaint::Cap::Square) {
          auto p = points.front();
          ScenePoint along{out.radius, 0};
          if (contour.stroke_start_tangent) {
            auto d = *contour.stroke_start_tangent;
            double size = std::hypot(d.x, d.y);
            if (!std::isfinite(size) || size <= 0)
              fail();
            along = mul(d, out.radius / size);
          }
          auto normal = ScenePoint{-along.y, along.x};
          out.polygon({add(add(p, along), normal), add(sub(p, along), normal),
                       sub(sub(p, along), normal), sub(add(p, along), normal)});
        }
      }
      continue;
    }
    const size_t segments = points.size() - (contour.closed ? 0 : 1);
    std::vector<ScenePoint> directions;
    for (size_t i = 0; i < segments; ++i) {
      auto a = points[i], b = points[(i + 1) % points.size()];
      auto delta = sub(b, a);
      double length = std::hypot(delta.x, delta.y);
      if (!std::isfinite(length) || length == 0)
        fail();
      auto d = mul(delta, 1 / length);
      directions.push_back(d);
      auto normal = ScenePoint{-d.y * out.radius, d.x * out.radius};
      out.polygon(
          {add(a, normal), add(b, normal), sub(b, normal), sub(a, normal)});
    }
    if (!contour.closed && contour.stroke_start_tangent) {
      auto d = *contour.stroke_start_tangent;
      double size = std::hypot(d.x, d.y);
      if (!std::isfinite(size) || size <= 0)
        fail();
      out.join(points.front(), mul(d, 1 / size), directions.front(),
               paint.stroke_join);
    }
    for (size_t i = contour.closed ? 0 : 1;
         i < points.size() - (contour.closed ? 0 : 1); ++i)
      out.join(points[i], directions[(i + segments - 1) % segments],
               directions[i % segments], paint.stroke_join);
    if (!contour.closed) {
      auto first = directions.front();
      if (contour.stroke_start_tangent) {
        const auto d = *contour.stroke_start_tangent;
        first = mul(d, 1 / std::hypot(d.x, d.y));
      }
      out.cap(points.front(), first, true, paint.stroke_cap);
      out.cap(points.back(), directions.back(), false, paint.stroke_cap);
    }
  }
  return Union::make(out.contours, point_limit);
}
} // namespace DomeOffice
