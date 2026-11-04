// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stroke.h"
#include <algorithm>
#include <cmath>

namespace DomeOffice {
namespace {
[[noreturn]] void fail() {
  throw std::runtime_error(
      "Dash pattern exceeds the native precision or point limit");
}
bool equal(ScenePoint a, ScenePoint b) { return a.x == b.x && a.y == b.y; }
struct Splitter {
  const std::vector<double> &pattern;
  size_t limit;
  double period;
  size_t points = 0, work = 0;
  std::vector<SceneContour> result;
  void budget() {
    if (++work > limit * 8)
      fail();
  }
  void point(SceneContour &out, ScenePoint p) {
    if (out.points.empty() || !equal(out.points.back(), p)) {
      if (++points > limit)
        fail();
      out.points.push_back(p);
    }
  }
  void run(const SceneContour &input) {
    if (!input.stroke || input.points.empty())
      return;
    if (input.points.size() > limit)
      fail();
    std::vector<ScenePoint> vertices;
    for (auto p : input.points) {
      budget();
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e10 ||
          std::abs(p.y) > 1e10)
        fail();
      if (vertices.empty() || !equal(vertices.back(), p))
        vertices.push_back(p);
    }
    if (input.closed && vertices.size() > 1 &&
        equal(vertices.front(), vertices.back()))
      vertices.pop_back();
    const auto start = result.size();
    SceneContour current;
    current.fill = false;
    current.paint_group = input.paint_group;
    auto finish = [&] {
      if (!current.points.empty()) {
        current.stroke_zero_length = current.points.size() == 1;
        result.push_back(std::move(current));
        current = {};
        current.fill = false;
        current.paint_group = input.paint_group;
      }
    };
    size_t at = 0;
    double left = pattern[0];
    auto next = [&] {
      budget();
      at = (at + 1) % pattern.size();
      left = pattern[at];
    };
    if (!std::isfinite(input.stroke_dash_offset) ||
        input.stroke_dash_offset < 0 || input.stroke_dash_offset > 1e10)
      fail();
    double offset = std::fmod(input.stroke_dash_offset, period);
    while (offset > left) {
      offset -= left;
      next();
    }
    left -= offset;
    bool origin = true;
    auto zeros = [&](ScenePoint position, bool dots,
                     std::optional<ScenePoint> direction) {
      size_t traversed = 0;
      while (left == 0) {
        if (++traversed > pattern.size())
          fail();
        if (!(at & 1) && dots) {
          point(current, position);
          current.stroke_butt_start = origin && input.stroke_butt_start;
          current.stroke_butt_end =
              vertices.size() == 1 && input.stroke_butt_end;
          current.stroke_start_tangent = direction;
          finish();
        }
        next();
      }
    };
    if (vertices.size() == 1) {
      if (!input.stroke_zero_length && input.points.size() == 1)
        return;
      zeros(vertices.front(), true, input.stroke_start_tangent);
      if (!(at & 1)) {
        point(current, vertices.front());
        current.stroke_butt_start = input.stroke_butt_start;
        current.stroke_butt_end = input.stroke_butt_end;
        finish();
      }
      return;
    }
    bool first_on = false, end_on = false, gap = false;
    const size_t edges = vertices.size() - (input.closed ? 0 : 1);
    for (size_t i = 0; i < edges; ++i) {
      auto a = vertices[i], b = vertices[(i + 1) % vertices.size()];
      const double dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
      if (!std::isfinite(length) || length <= 0)
        fail();
      double used = 0;
      while (used < length) {
        budget();
        auto position = used == 0 ? a
                                  : ScenePoint{a.x + dx * (used / length),
                                               a.y + dy * (used / length)};
        auto incoming = ScenePoint{dx / length, dy / length};
        if (used == 0 && i > 0)
          incoming = {a.x - vertices[i - 1].x, a.y - vertices[i - 1].y};
        zeros(position, true, incoming);
        bool on = !(at & 1);
        if (on && used == 0 && i > 0 && current.points.empty()) {
          point(current, position);
          current.stroke_start_tangent = incoming;
        }
        double remaining = length - used, amount = std::min(left, remaining);
        double final = used + amount;
        if (amount <= 0 || final <= used || !std::isfinite(final))
          fail();
        auto to = amount == remaining ? b
                                      : ScenePoint{a.x + dx * (final / length),
                                                   a.y + dy * (final / length)};
        if (i == 0 && used == 0)
          first_on = on;
        if (on) {
          if (current.points.empty() && origin)
            current.stroke_butt_start = input.stroke_butt_start;
          point(current, position);
          point(current, to);
          if (i + 1 == edges && amount == remaining)
            current.stroke_butt_end = input.stroke_butt_end;
        } else {
          finish();
          gap = true;
        }
        used = final;
        origin = false;
        if (amount == left) {
          if (on)
            finish();
          next();
        } else
          left -= amount;
        end_on = on;
      }
      // Starting an on interval exactly at a vertex includes the preceding
      // zero-length segment/tangent in the next dash's join (SVG/Cairo).
      if (!(at & 1) && left > 0 && current.points.empty() && i + 1 < edges) {
        point(current, b);
        current.stroke_start_tangent = ScenePoint{dx / length, dy / length};
      }
    }
    finish();
    if (input.closed && first_on && end_on && result.size() > start) {
      auto &first = result[start];
      auto &last = result.back();
      if (!gap && result.size() == start + 1 && first.points.size() > 1) {
        if (equal(first.points.front(), first.points.back()))
          first.points.pop_back();
        first.closed = true;
      } else if (&first != &last && !first.points.empty() &&
                 !last.points.empty() &&
                 equal(first.points.front(), vertices.front()) &&
                 equal(last.points.back(), vertices.front())) {
        last.points.insert(last.points.end(), first.points.begin() + 1,
                           first.points.end());
        result.erase(result.begin() + start);
      }
    }
  }
};
} // namespace
std::vector<SceneContour> stroke_dashes(const std::vector<SceneContour> &input,
                                        const std::vector<double> &pattern,
                                        size_t point_limit) {
  if (point_limit < 1 || point_limit > 1000000 || pattern.size() > 100000)
    fail();
  if (pattern.empty())
    return input;
  auto normalized = pattern;
  if (normalized.size() % 2) {
    if (normalized.size() > 50000)
      fail();
    normalized.insert(normalized.end(), pattern.begin(), pattern.end());
  }
  double total = 0;
  for (auto value : normalized) {
    if (!std::isfinite(value) || value < 0 || !std::isfinite(total + value))
      fail();
    total += value;
  }
  // SVG's all-zero pattern is an undashed stroke.
  if (total == 0)
    return input;
  Splitter splitter{normalized, point_limit, total};
  for (const auto &c : input)
    splitter.run(c);
  return std::move(splitter.result);
}
std::vector<std::vector<SceneContour>>
stroke_pass_outlines(const std::vector<SceneContour> &contours,
                     const ScenePaint &paint, const SceneTransform &device,
                     double tolerance, size_t point_limit,
                     const std::vector<SceneContour> &extra_fills) {
  if (point_limit < 1 || point_limit > 1000000 ||
      paint.stroke_patterns.size() > 64)
    fail();
  std::vector<std::vector<SceneContour>> result;
  if (!paint.stroke || (paint.stroke_width == 0 && extra_fills.empty()))
    return result;
  const auto fallback = std::vector<SceneDashPattern>{SceneDashPattern{}};
  size_t used = 0;
  for (const auto &pattern :
       paint.stroke_patterns.empty() ? fallback : paint.stroke_patterns) {
    auto pass = paint;
    // The named round dash shape requests round ends. Rectangular dash shapes
    // use the cascaded SVG cap (butt by default); solid passes keep that cap.
    if (pattern.round)
      pass.stroke_cap = ScenePaint::Cap::Round;
    std::vector<SceneContour> outlines;
    if (paint.stroke_width != 0) {
      auto split = stroke_dashes(contours, pattern.lengths(paint.stroke_width),
                                 point_limit);
      outlines = stroke_outlines(split, pass, device, tolerance, point_limit);
    }
    if (!extra_fills.empty()) {
      outlines.insert(outlines.end(), extra_fills.begin(), extra_fills.end());
      outlines = filled_contour_union(outlines, point_limit);
    }
    for (const auto &c : outlines) {
      if (c.points.size() > point_limit - used)
        fail();
      used += c.points.size();
    }
    result.push_back(std::move(outlines));
  }
  return result;
}
} // namespace DomeOffice
