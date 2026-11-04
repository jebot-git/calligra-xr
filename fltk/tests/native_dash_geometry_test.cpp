// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stroke.h"
#include <cairo.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
using namespace DomeOffice;
namespace {
using Cap = ScenePaint::Cap;
using Join = ScenePaint::Join;
void path(cairo_t *cr, const std::vector<SceneContour> &curves) {
  for (const auto &c : curves) {
    if (!c.stroke || c.points.empty())
      continue;
    cairo_move_to(cr, c.points[0].x, c.points[0].y);
    for (size_t i = 1; i < c.points.size(); ++i)
      cairo_line_to(cr, c.points[i].x, c.points[i].y);
    if (c.closed)
      cairo_close_path(cr);
  }
}
void analytic_dots(cairo_t *cr, const std::vector<SceneContour> &curves,
                   Cap cap, double gap, double width) {
  if (cap == Cap::Butt)
    return;
  // Independent global-distance oracle for a zero-on pattern. Cairo's dashed
  // stroker paints spurious butt-cap corner pixels in this case; use the SVG
  // zero-length cap rule rather than copying that reference discrepancy.
  for (const auto &c : curves) {
    if (!c.stroke || c.points.size() < 2)
      continue;
    auto vertices = c.points;
    if (c.closed)
      vertices.push_back(vertices.front());
    std::vector<double> ends;
    double total = 0;
    for (size_t i = 1; i < vertices.size(); ++i) {
      total += std::hypot(vertices[i].x - vertices[i - 1].x,
                          vertices[i].y - vertices[i - 1].y);
      ends.push_back(total);
    }
    for (size_t k = 0; double(k) * gap < total; ++k) {
      auto distance = double(k) * gap;
      auto i = size_t(std::lower_bound(ends.begin(), ends.end(), distance) -
                      ends.begin());
      while (i < ends.size() && ends[i] == (i ? ends[i - 1] : 0))
        ++i;
      assert(i < ends.size());
      auto a = vertices[i], b = vertices[i + 1];
      auto length = ends[i] - (i ? ends[i - 1] : 0);
      auto along = distance - (i ? ends[i - 1] : 0);
      auto ratio = along / length;
      double x = a.x + (b.x - a.x) * ratio, y = a.y + (b.y - a.y) * ratio;
      if (cap == Cap::Round) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x, y, width / 2, 0, 2 * 3.14159265358979323846);
      } else {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_rotate(cr, std::atan2(b.y - a.y, b.x - a.x));
        cairo_rectangle(cr, -width / 2, -width / 2, width, width);
        cairo_restore(cr);
      }
    }
  }
  cairo_fill(cr);
}
void folded_line(cairo_t *cr, const std::vector<double> &pattern,
                 const ScenePaint &paint) {
  // Analytic one-dimensional oracle for a closed P0->P1->P0 path. Cairo's
  // stroked reversing cusps clip some endpoint cap disks, so construct the
  // union of strips, endpoint caps and (round-only) cusp joins in axis space.
  const double half = std::hypot(55., 30.), total = 2 * half;
  const double radius = paint.stroke_width / 2;
  double period = 0;
  for (auto v : pattern)
    period += v;
  std::vector<std::pair<double, double>> intervals;
  for (size_t cycle = 0; double(cycle) * period < total; ++cycle) {
    double offset = 0;
    for (size_t j = 0; j < pattern.size(); ++j) {
      double begin = double(cycle) * period + offset,
             end = std::min(total, begin + pattern[j]);
      if (!(j % 2) && begin < total && end > begin)
        intervals.push_back({begin, end});
      offset += pattern[j];
    }
  }
  cairo_save(cr);
  cairo_translate(cr, 20, 30);
  cairo_rotate(cr, std::atan2(30., 55.));
  auto project = [&](double t) { return t <= half ? t : total - t; };
  auto semicircle = [&](double x, bool forward) {
    cairo_move_to(cr, x, 0);
    double angle =
        forward ? -3.14159265358979323846 / 2 : 3.14159265358979323846 / 2;
    cairo_arc(cr, x, 0, radius, angle, angle + 3.14159265358979323846);
    cairo_close_path(cr);
  };
  auto cap = [&](double t, bool start) {
    const double x = project(t);
    const double direction = (t < half ? 1 : -1) * (start ? -1 : 1);
    if (paint.stroke_cap == Cap::Round)
      semicircle(x, direction > 0);
    else if (paint.stroke_cap == Cap::Square)
      cairo_rectangle(cr, x + (direction < 0 ? -radius : 0), -radius, radius,
                      2 * radius);
  };
  const bool closed = intervals.size() == 1 && intervals.front().first == 0 &&
                      intervals.front().second == total;
  const bool wrap = !closed && intervals.size() > 1 &&
                    intervals.front().first == 0 &&
                    intervals.back().second == total;
  for (size_t i = 0; i < intervals.size(); ++i) {
    auto [a, b] = intervals[i];
    double left = std::min(project(a), project(b));
    double right =
        (a < half && b > half) ? half : std::max(project(a), project(b));
    cairo_rectangle(cr, left, -radius, right - left, 2 * radius);
    if (paint.stroke_join == Join::Round && a < half && b > half)
      semicircle(half, true);
    if (!closed && !(wrap && i == 0))
      cap(a, true);
    if (!closed && !(wrap && i + 1 == intervals.size()))
      cap(b, false);
  }
  if ((closed || wrap) && paint.stroke_join == Join::Round)
    semicircle(0, false);
  cairo_restore(cr);
  cairo_fill(cr);
}
void zero_gap_reference(cairo_t *cr, const std::vector<SceneContour> &curves,
                        double on) {
  // Global arclength intervals, independently sampled into Cairo solid paths.
  // Unlike Cairo's dash stroker, this retains the caps at zero-length gaps.
  cairo_push_group(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  for (const auto &c : curves) {
    auto v = c.points;
    if (c.closed)
      v.push_back(v.front());
    std::vector<double> positions{0};
    for (size_t i = 1; i < v.size(); ++i)
      positions.push_back(positions.back() +
                          std::hypot(v[i].x - v[i - 1].x, v[i].y - v[i - 1].y));
    auto total = positions.back();
    auto locate = [&](double t) {
      auto it = std::lower_bound(positions.begin() + 1, positions.end(), t);
      auto i = size_t(it - positions.begin());
      while (i < positions.size() && positions[i] == positions[i - 1])
        ++i;
      assert(i < positions.size());
      auto f = (t - positions[i - 1]) / (positions[i] - positions[i - 1]);
      return ScenePoint{v[i - 1].x + (v[i].x - v[i - 1].x) * f,
                        v[i - 1].y + (v[i].y - v[i - 1].y) * f};
    };
    const size_t count = size_t(std::ceil(total / on));
    for (size_t k = c.closed && count > 1 ? 1 : 0; k < count; ++k) {
      double begin = double(k) * on, end = std::min(total, begin + on);
      auto p = locate(begin);
      // At an exact vertex the preceding segment supplies the cap tangent
      // and join. A positive subpixel segment avoids Cairo's zero-segment bug.
      auto vertex =
          std::find(positions.begin() + 1, positions.end() - 1, begin);
      if (vertex != positions.end() - 1) {
        auto before = locate(std::max(0., begin - 1e-7));
        cairo_move_to(cr, before.x, before.y);
        cairo_line_to(cr, p.x, p.y);
      } else
        cairo_move_to(cr, p.x, p.y);
      for (size_t i = 1; i < positions.size(); ++i)
        if (positions[i] > begin && positions[i] < end)
          cairo_line_to(cr, v[i].x, v[i].y);
      p = locate(end);
      cairo_line_to(cr, p.x, p.y);
      if (c.closed && count == 1)
        cairo_close_path(cr);
      else if (c.closed && k + 1 == count) {
        const double first_end = std::min(total, on);
        for (size_t i = 1; i < positions.size(); ++i)
          if (positions[i] > 0 && positions[i] < first_end)
            cairo_line_to(cr, v[i].x, v[i].y);
        p = locate(first_end);
        cairo_line_to(cr, p.x, p.y);
      }
      cairo_stroke(cr);
    }
  }
  cairo_pop_group_to_source(cr);
  cairo_paint_with_alpha(cr, .5);
}
std::vector<uint8_t> raster(const std::vector<SceneContour> &curves,
                            const std::vector<double> &pattern,
                            const ScenePaint &paint, bool reference) {
  auto surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 500, 500);
  auto cr = cairo_create(surface);
  cairo_scale(cr, 2, 2);
  cairo_translate(cr, 15, 15);
  cairo_set_source_rgba(cr, 1, 1, 1, .5);
  if (reference && pattern == std::vector<double>{0, 15}) {
    analytic_dots(cr, curves, paint.stroke_cap, 15, paint.stroke_width);
  } else if (reference) {
    bool folded = curves.size() == 1 && curves[0].closed &&
                  curves[0].points.size() == 3 && curves[0].points[0].x == 20 &&
                  curves[0].points[1].x == 75;
    if (folded)
      folded_line(cr, pattern, paint);
    else
      path(cr, curves);
    cairo_set_line_width(cr, paint.stroke_width);
    cairo_set_line_cap(cr, paint.stroke_cap == Cap::Butt ? CAIRO_LINE_CAP_BUTT
                           : paint.stroke_cap == Cap::Round
                               ? CAIRO_LINE_CAP_ROUND
                               : CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(
        cr, paint.stroke_join == Join::Miter   ? CAIRO_LINE_JOIN_MITER
            : paint.stroke_join == Join::Round ? CAIRO_LINE_JOIN_ROUND
                                               : CAIRO_LINE_JOIN_BEVEL);
    cairo_set_miter_limit(cr, 4);
    cairo_set_tolerance(cr, .01);
    if (!folded && pattern == std::vector<double>{12, 0})
      zero_gap_reference(cr, curves, 12);
    else {
      if (!folded)
        cairo_set_dash(cr, pattern.data(), int(pattern.size()), 0);
      cairo_stroke(cr);
    }
  } else {
    auto dashed = stroke_dashes(curves, pattern);
    auto outline =
        stroke_outlines(dashed, paint, SceneTransform{2, 0, 0, 2}, .01);
    path(cr, outline);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_fill(cr);
  }
  assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
  cairo_surface_flush(surface);
  std::vector<uint8_t> data(500 * 500);
  auto bytes = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  for (int y = 0; y < 500; ++y)
    std::memcpy(data.data() + 500 * y, bytes + stride * y, 500);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return data;
}
void compare() {
  std::vector<std::vector<SceneContour>> examples{
      {{{{20, 30}, {175, 30}}, false}},
      {{{{20, 30}, {100, 30}, {100, 135}}, false}},
      {{{{20, 30}, {170, 30}, {170, 150}, {20, 150}}, true}},
      {{{{20, 30}, {150, 150}, {20, 150}, {150, 30}}, false}},
      {{{{20, 30}, {75, 60}, {20, 30}}, true}},
      {{{{20, 30}, {20, 30}, {170, 130}}, false}},
      {{{{20, 30}, {170, 30}}, false}, {{{20, 80}, {170, 80}}, false}}};
  std::vector<std::vector<double>> patterns{{13, 8}, {20, 6, 3, 6}, {0, 15},
                                            {12, 0}, {300, 12},     {1, 300}};
  size_t count = 0;
  for (auto cap : {Cap::Butt, Cap::Round, Cap::Square})
    for (auto join : {Join::Miter, Join::Round, Join::Bevel})
      for (size_t p = 0; p < patterns.size(); ++p)
        for (size_t e = 0; e < examples.size(); ++e) {
          ScenePaint paint;
          paint.stroke = true;
          paint.stroke_width = 9;
          paint.stroke_cap = cap;
          paint.stroke_join = join;
          auto a = raster(examples[e], patterns[p], paint, false),
               b = raster(examples[e], patterns[p], paint, true);
          int maximum = 0;
          size_t checked = 0, bad = 0;
          for (int y = 1; y < 499; ++y)
            for (int x = 1; x < 499; ++x) {
              size_t i = y * 500 + x;
              int value = b[i];
              if (value > 2 && value < 126)
                continue;
              bool stable = true;
              for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                  if (std::abs(int(b[i + dy * 500 + dx]) - value) > 2)
                    stable = false;
              if (!stable)
                continue;
              ++checked;
              int error = std::abs(int(a[i]) - value);
              maximum = std::max(maximum, error);
              if (error > 6)
                ++bad;
            }
          if (maximum > 8 || bad) {
            std::cerr << "Mismatch cap=" << int(cap) << " join=" << int(join)
                      << " pattern=" << p << " example=" << e
                      << " max=" << maximum << " bad=" << bad
                      << " checked=" << checked << "\n";
            for (auto item :
                 {std::pair{"native", &a}, std::pair{"cairo", &b}}) {
              std::ofstream f(std::string("../dash-") + item.first +
                                  "-mismatch.pgm",
                              std::ios::binary);
              f << "P5\n500 500\n255\n";
              f.write(reinterpret_cast<const char *>(item.second->data()),
                      item.second->size());
            }
            std::cerr << "Stable difference pixels:";
            int samples = 0;
            for (int y = 1; y < 499 && samples < 20; ++y)
              for (int x = 1; x < 499 && samples < 20; ++x)
                if (std::abs(int(a[y * 500 + x]) - int(b[y * 500 + x])) > 100) {
                  std::cerr << " " << x << "," << y;
                  ++samples;
                }
            std::cerr << "\n";
            std::abort();
          }
          assert(checked > 100000);
          ++count;
        }
  std::cout << "PASS " << count
            << " independent Cairo and SVG analytic dashed stroke "
               "geometry/alpha comparisons\n";
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
void guards() {
  const std::vector<SceneContour> line{{{{0, 0}, {100, 0}}, false}};
  assert(stroke_dashes(line, {0, 0}).size() == 1);
  assert(stroke_dashes(line, {1}).size() == stroke_dashes(line, {1, 1}).size());
  rejects([&] { stroke_dashes(line, {-1, 2}); });
  rejects([&] {
    stroke_dashes(line, {std::numeric_limits<double>::infinity(), 2});
  });
  rejects([&] { stroke_dashes(line, {1e-100, 1e-100}, 50); });
  assert(stroke_dashes(line, {}).size() == 1);
  auto d = stroke_dashes(line, {10, 10});
  assert(d.size() == 5 && d[0].points.front().x == 0 &&
         d[0].points.back().x == 10 && d.back().points.back().x == 90);
  std::cout << "PASS bounded dash validation, work/point limits and analytic "
               "intervals\n";
}
} // namespace
int main() {
  compare();
  guards();
}
