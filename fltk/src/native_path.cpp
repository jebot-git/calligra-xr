// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_path.h"
#include <charconv>
#include <limits>
#include <numbers>
namespace DomeOffice {
namespace {
constexpr double pi = std::numbers::pi, tau = 2 * pi;
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
ScenePoint add(ScenePoint a, ScenePoint b) { return {a.x + b.x, a.y + b.y}; }
ScenePoint sub(ScenePoint a, ScenePoint b) { return {a.x - b.x, a.y - b.y}; }
ScenePoint mul(ScenePoint a, double s) { return {a.x * s, a.y * s}; }
ScenePoint mid(ScenePoint a, ScenePoint b) { return mul(add(a, b), .5); }
bool same(ScenePoint a, ScenePoint b) { return a.x == b.x && a.y == b.y; }
void check(ScenePoint p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    fail("Path coordinates exceed the geometry limit");
}
void matrix(const SceneTransform &m) {
  for (auto n : {m.a, m.b, m.c, m.d, m.e, m.f})
    if (!std::isfinite(n))
      fail("Invalid path transform");
}
struct Input {
  std::string data;
  size_t at = 0;
  bool comma = false;
  explicit Input(const Text &text) : data(text.toStdString()) {
    if (data.size() > 1048576)
      fail("Path data exceeds the input limit");
  }
  void space() {
    while (at < data.size() && (data[at] == ' ' || data[at] == '\t' ||
                                data[at] == '\r' || data[at] == '\n'))
      ++at;
  }
  bool done() {
    space();
    return at == data.size();
  }
  void argument() {
    space();
    if (at < data.size() && data[at] == ',') {
      if (!comma)
        fail("Unexpected comma in path data");
      ++at;
      space();
    }
    if (at == data.size())
      fail("Incomplete path data");
  }
  double number() {
    argument();
    if (data[at] == '+') {
      ++at;
      if (at == data.size() || data[at] == '+' || data[at] == '-')
        fail("Invalid path number");
    }
    double n;
    auto p = std::from_chars(data.data() + at, data.data() + data.size(), n);
    if (p.ec != std::errc() || p.ptr == data.data() + at || !std::isfinite(n) ||
        std::abs(n) > 1e9)
      fail("Invalid path number");
    at = p.ptr - data.data();
    comma = true;
    return n;
  }
  bool flag() {
    argument();
    char f = data[at++];
    if (f != '0' && f != '1')
      fail("Invalid path arc flag");
    comma = true;
    return f == '1';
  }
};
struct Arc {
  ScenePoint center, u, v;
  double start = 0, delta = 0;
  ScenePoint point(double t) const {
    return add(center, add(mul(u, std::cos(t)), mul(v, std::sin(t))));
  }
};
std::optional<Arc> arc(ScenePoint from, const PathSegment &s) {
  if (s.rx == 0 || s.ry == 0 || same(from, s.end))
    return {};
  long double rx = std::abs(s.rx), ry = std::abs(s.ry),
              phi = std::fmod(s.rotation, 360) * pi / 180;
  long double c = std::cos(phi), sn = std::sin(phi),
              dx = (static_cast<long double>(from.x) - s.end.x) / 2,
              dy = (static_cast<long double>(from.y) - s.end.y) / 2;
  long double x = c * dx + sn * dy, y = -sn * dx + c * dy;
  long double scale = std::hypot(x / rx, y / ry);
  if (scale > 1) {
    rx *= scale;
    ry *= scale;
  }
  long double nx = x / rx, ny = y / ry, squared = nx * nx + ny * ny;
  if (squared == 0)
    return {};
  long double factor = std::sqrt(std::max(0.L, (1 - squared) / squared));
  if (s.large_arc == s.sweep)
    factor = -factor;
  long double cx = factor * rx * ny, cy = -factor * ry * nx;
  Arc a;
  a.center = {double(c * cx - sn * cy +
                     (static_cast<long double>(from.x) + s.end.x) / 2),
              double(sn * cx + c * cy +
                     (static_cast<long double>(from.y) + s.end.y) / 2)};
  a.u = {double(rx * c), double(rx * sn)};
  a.v = {double(-ry * sn), double(ry * c)};
  long double ux = (x - cx) / rx, uy = (y - cy) / ry, vx = (-x - cx) / rx,
              vy = (-y - cy) / ry;
  a.start = std::atan2(uy, ux);
  a.delta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  if (s.sweep && a.delta < 0)
    a.delta += tau;
  if (!s.sweep && a.delta > 0)
    a.delta -= tau;
  for (auto n :
       {a.center.x, a.center.y, a.u.x, a.u.y, a.v.x, a.v.y, a.start, a.delta})
    if (!std::isfinite(n))
      fail("Path arc exceeds the geometry limit");
  return a;
}
ScenePoint vector_map(const SceneTransform &m, ScenePoint p) {
  return {m.a * p.x + m.c * p.y, m.b * p.x + m.d * p.y};
}
double distance(ScenePoint p, ScenePoint a, ScenePoint b) {
  auto v = sub(b, a), w = sub(p, a);
  double d = v.x * v.x + v.y * v.y;
  double t = d ? std::clamp((w.x * v.x + w.y * v.y) / d, 0., 1.) : 0;
  return std::hypot(w.x - v.x * t, w.y - v.y * t);
}
struct Flatten {
  SceneTransform coordinates, device;
  double tolerance;
  size_t limit, count = 0;
  std::vector<SceneContour> contours;
  void emit(ScenePoint p) {
    check(p);
    auto &out = contours.back().points;
    if (!out.empty() && same(out.back(), p))
      return;
    if (++count > limit)
      fail("Path flattening exceeds the point limit");
    out.push_back(p);
  }
  void cubic(ScenePoint a, ScenePoint b, ScenePoint c, ScenePoint d,
             int depth = 0) {
    auto x = device.map(a), y = device.map(d);
    if (std::max(distance(device.map(b), x, y),
                 distance(device.map(c), x, y)) <= tolerance) {
      emit(d);
      return;
    }
    if (depth >= 24)
      fail("Path curve exceeds the precision limit");
    auto ab = mid(a, b), bc = mid(b, c), cd = mid(c, d), abc = mid(ab, bc),
         bcd = mid(bc, cd), m = mid(abc, bcd);
    cubic(a, ab, abc, m, depth + 1);
    cubic(m, bcd, cd, d, depth + 1);
  }
  void elliptical(const Arc &a, double start, double end, ScenePoint last,
                  double radius, int depth = 0) {
    double angle = end - start;
    if (radius * angle * angle / 8 <= tolerance) {
      emit(last);
      return;
    }
    if (depth >= 24)
      fail("Path arc exceeds the precision limit");
    double middle = (start + end) / 2;
    auto p = coordinates.map(a.point(middle));
    elliptical(a, start, middle, p, radius, depth + 1);
    elliptical(a, middle, end, last, radius, depth + 1);
  }
};
struct Bounds {
  double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
  void point(ScenePoint p) {
    check(p);
    left = std::min(left, p.x);
    right = std::max(right, p.x);
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  }
  SceneRect rect() const {
    return std::isfinite(left)
               ? SceneRect{left, top, right - left, bottom - top}
               : SceneRect{};
  }
};
ScenePoint cubic_at(ScenePoint a, ScenePoint b, ScenePoint c, ScenePoint d,
                    double t) {
  double r = 1 - t;
  return add(add(mul(a, r * r * r), mul(b, 3 * r * r * t)),
             add(mul(c, 3 * r * t * t), mul(d, t * t * t)));
}
template <class F> void roots(double a, double b, double c, F apply) {
  if (a == 0) {
    if (b != 0)
      apply(-c / b);
    return;
  }
  double d = b * b - 4 * a * c;
  if (d < 0)
    return;
  double q = -.5 * (b + std::copysign(std::sqrt(d), b));
  if (q == 0) {
    apply(-b / (2 * a));
    return;
  }
  apply(q / a);
  apply(c / q);
}
bool angle_in(const Arc &a, double t) {
  double d = std::fmod(a.delta >= 0 ? t - a.start : a.start - t, tau);
  if (d < 0)
    d += tau;
  return d <= std::abs(a.delta) + 1e-12;
}
} // namespace
ScenePath parse_svg_path(const Text &text) {
  Input in(text);
  ScenePath path;
  ScenePoint current{}, cubic_control{}, quad_control{};
  char command = 0, previous = 0;
  size_t segments = 0;
  auto pair = [&](bool relative) {
    ScenePoint p{in.number(), in.number()};
    if (relative)
      p = add(current, p);
    check(p);
    return p;
  };
  auto append = [&](PathSegment s) {
    if (++segments > 16384)
      fail("Path exceeds the segment limit");
    check(s.end);
    if (s.kind == PathSegment::Cubic || s.kind == PathSegment::Quadratic)
      check(s.control1);
    if (s.kind == PathSegment::Cubic)
      check(s.control2);
    if (path.subpaths.back().closed) {
      if (path.subpaths.size() >= 16384)
        fail("Path exceeds the subpath limit");
      path.subpaths.push_back({current, {}, {}});
    }
    path.subpaths.back().segments.push_back(s);
    current = s.end;
  };
  while (!in.done()) {
    char next = in.data[in.at];
    if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
      command = next;
      ++in.at;
      in.comma = false;
    } else if (!command)
      fail("Path command expected");
    bool relative = command >= 'a' && command <= 'z';
    char op = relative ? command - 'a' + 'A' : command;
    if (path.subpaths.empty() && op != 'M')
      fail("A path must start with moveto");
    if (op == 'M') {
      current = pair(relative);
      if (path.subpaths.size() >= 16384)
        fail("Path exceeds the subpath limit");
      path.subpaths.push_back({current, {}, {}});
      previous = 'M';
      command = relative ? 'l' : 'L';
      continue;
    }
    if (op == 'Z') {
      path.subpaths.back().closed = true;
      current = path.subpaths.back().start;
      previous = 'Z';
      command = 0;
      continue;
    }
    PathSegment s;
    if (op == 'L') {
      s.end = pair(relative);
    } else if (op == 'H') {
      s.end = current;
      auto x = in.number();
      s.end.x = relative ? current.x + x : x;
    } else if (op == 'V') {
      s.end = current;
      auto y = in.number();
      s.end.y = relative ? current.y + y : y;
    } else if (op == 'C') {
      s.kind = PathSegment::Cubic;
      s.control1 = pair(relative);
      s.control2 = pair(relative);
      s.end = pair(relative);
      cubic_control = s.control2;
    } else if (op == 'S') {
      s.kind = PathSegment::Cubic;
      s.control1 = (previous == 'C' || previous == 'S')
                       ? sub(mul(current, 2), cubic_control)
                       : current;
      s.control2 = pair(relative);
      s.end = pair(relative);
      cubic_control = s.control2;
    } else if (op == 'Q') {
      s.kind = PathSegment::Quadratic;
      s.control1 = pair(relative);
      s.end = pair(relative);
      quad_control = s.control1;
    } else if (op == 'T') {
      s.kind = PathSegment::Quadratic;
      s.control1 = (previous == 'Q' || previous == 'T')
                       ? sub(mul(current, 2), quad_control)
                       : current;
      s.end = pair(relative);
      quad_control = s.control1;
    } else if (op == 'A') {
      s.kind = PathSegment::Arc;
      s.rx = std::abs(in.number());
      s.ry = std::abs(in.number());
      s.rotation = in.number();
      s.large_arc = in.flag();
      s.sweep = in.flag();
      s.end = pair(relative);
    } else
      fail("Unknown SVG path command");
    append(s);
    previous = op;
  }
  return path;
}
std::vector<SceneContour> flatten_path(const ScenePath &path,
                                       const SceneTransform &coordinates,
                                       const SceneTransform &device,
                                       double tolerance, size_t point_limit) {
  matrix(coordinates);
  matrix(device);
  if (!std::isfinite(tolerance) || tolerance <= 0 || point_limit < 1 ||
      point_limit > 1000000)
    fail("Invalid path flattening settings");
  Flatten f{coordinates, device, tolerance, point_limit};
  for (const auto &subpath : path.subpaths) {
    f.contours.push_back({{}, subpath.closed});
    f.emit(coordinates.map(subpath.start));
    auto current = subpath.start;
    for (const auto &s : subpath.segments) {
      auto a = coordinates.map(current), d = coordinates.map(s.end);
      if (s.kind == PathSegment::Line)
        f.emit(d);
      else if (s.kind == PathSegment::Cubic)
        f.cubic(a, coordinates.map(s.control1), coordinates.map(s.control2), d);
      else if (s.kind == PathSegment::Quadratic) {
        auto b = coordinates.map(s.control1);
        f.cubic(a, add(a, mul(sub(b, a), 2. / 3)),
                add(d, mul(sub(b, d), 2. / 3)), d);
      } else if (auto curve = arc(current, s)) {
        auto transform = device * coordinates;
        auto u = vector_map(transform, curve->u),
             v = vector_map(transform, curve->v);
        double radius = std::hypot(u.x, u.y) + std::hypot(v.x, v.y);
        f.elliptical(*curve, curve->start, curve->start + curve->delta, d,
                     radius);
      } else
        f.emit(d);
      current = s.end;
    }
    auto &points = f.contours.back().points;
    if (subpath.closed && points.size() > 1 &&
        same(points.front(), points.back()))
      points.pop_back();
  }
  return std::move(f.contours);
}
SceneRect path_bounds(const ScenePath &path, const SceneTransform &m) {
  matrix(m);
  Bounds bounds;
  for (const auto &subpath : path.subpaths) {
    if (subpath.segments.empty())
      continue;
    auto current = subpath.start;
    bounds.point(m.map(current));
    for (const auto &s : subpath.segments) {
      auto a = m.map(current), d = m.map(s.end);
      bounds.point(d);
      if (s.kind == PathSegment::Cubic || s.kind == PathSegment::Quadratic) {
        auto b = m.map(s.control1), c = s.kind == PathSegment::Cubic
                                            ? m.map(s.control2)
                                            : ScenePoint{};
        if (s.kind == PathSegment::Quadratic) {
          c = add(d, mul(sub(b, d), 2. / 3));
          b = add(a, mul(sub(b, a), 2. / 3));
        }
        auto candidate = [&](double t) {
          if (t > 0 && t < 1)
            bounds.point(cubic_at(a, b, c, d, t));
        };
        roots(-a.x + 3 * b.x - 3 * c.x + d.x, 2 * (a.x - 2 * b.x + c.x),
              b.x - a.x, candidate);
        roots(-a.y + 3 * b.y - 3 * c.y + d.y, 2 * (a.y - 2 * b.y + c.y),
              b.y - a.y, candidate);
      } else if (s.kind == PathSegment::Arc)
        if (auto curve = arc(current, s)) {
          auto u = vector_map(m, curve->u), v = vector_map(m, curve->v);
          for (double t : {std::atan2(v.x, u.x), std::atan2(v.x, u.x) + pi,
                           std::atan2(v.y, u.y), std::atan2(v.y, u.y) + pi})
            if (angle_in(*curve, t))
              bounds.point(m.map(curve->point(t)));
        }
      current = s.end;
    }
  }
  return bounds.rect();
}
ScenePath rounded_rectangle_path(double w, double h, double rx, double ry) {
  for (auto n : {w, h, rx, ry})
    if (!std::isfinite(n) || n < 0 || n > 1e9)
      fail("Invalid rounded rectangle geometry");
  rx = std::min(rx, w / 2);
  ry = std::min(ry, h / 2);
  if (rx == 0 || ry == 0) {
    rx = ry = 0;
  }
  ScenePath path;
  PathSubpath subpath;
  subpath.start = {rx, 0};
  subpath.closed = true;
  auto line = [&](ScenePoint p) {
    subpath.segments.push_back({PathSegment::Line, p});
  };
  auto corner = [&](ScenePoint p) {
    PathSegment s;
    s.kind = PathSegment::Arc;
    s.end = p;
    s.rx = rx;
    s.ry = ry;
    s.sweep = true;
    subpath.segments.push_back(s);
  };
  line({w - rx, 0});
  corner({w, ry});
  line({w, h - ry});
  corner({w - rx, h});
  line({rx, h});
  corner({0, h - ry});
  line({0, ry});
  corner({rx, 0});
  path.subpaths.push_back(std::move(subpath));
  return path;
}
ScenePath ellipse_path(double w, double h, double start, double sweep,
                       EllipseClosure closure) {
  for (auto n : {w, h, start, sweep})
    if (!std::isfinite(n) || std::abs(n) > 1e9)
      fail("Invalid ellipse path geometry");
  if (w < 0 || h < 0 || std::abs(sweep) > 360)
    fail("Invalid ellipse path extent");
  ScenePath path;
  PathSubpath p;
  start = std::fmod(start, 360);
  double a = -start * pi / 180, delta = -sweep * pi / 180;
  auto point = [&](double t) {
    return ScenePoint{w * (1 + std::cos(t)) / 2, h * (1 + std::sin(t)) / 2};
  };
  p.start = point(a);
  int count = std::max(1, int(std::ceil(std::abs(delta) / (pi / 2))));
  for (int i = 1; i <= count; ++i) {
    PathSegment s;
    s.kind = PathSegment::Arc;
    s.end = point(a + delta * i / count);
    if (i == count && std::abs(sweep) == 360)
      s.end = p.start;
    s.rx = w / 2;
    s.ry = h / 2;
    s.sweep = delta >= 0;
    p.segments.push_back(s);
  }
  if (closure == EllipseClosure::Pie && std::abs(sweep) < 360)
    p.segments.push_back({PathSegment::Line, {w / 2, h / 2}});
  p.closed = closure != EllipseClosure::Open;
  path.subpaths.push_back(std::move(p));
  return path;
}
} // namespace DomeOffice
