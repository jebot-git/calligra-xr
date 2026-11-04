// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_connector.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
ScenePoint add(ScenePoint a, ScenePoint b) { return {a.x + b.x, a.y + b.y}; }
ScenePoint sub(ScenePoint a, ScenePoint b) { return {a.x - b.x, a.y - b.y}; }
ScenePoint mul(ScenePoint a, double v) { return {a.x * v, a.y * v}; }
double dot(ScenePoint a, ScenePoint b) { return a.x * b.x + a.y * b.y; }
double cross(ScenePoint a, ScenePoint b) { return a.x * b.y - a.y * b.x; }
bool equal(ScenePoint a, ScenePoint b) { return a.x == b.x && a.y == b.y; }
ScenePoint checked(ScenePoint p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    fail("Connector route coordinates exceed the limit");
  return p;
}
ScenePoint unit(ScenePoint p) {
  double n = std::hypot(p.x, p.y);
  return n > 0 ? mul(p, 1 / n) : ScenePoint{1, 0};
}
std::shared_ptr<const ScenePath>
polyline(const std::vector<ScenePoint> &points) {
  auto p = std::make_shared<ScenePath>();
  if (points.empty())
    return p;
  PathSubpath subpath;
  subpath.start = checked(points.front());
  for (size_t i = 1; i < points.size(); ++i) {
    PathSegment s;
    s.end = checked(points[i]);
    subpath.segments.push_back(s);
  }
  if (subpath.segments.empty()) {
    PathSegment s;
    s.end = subpath.start;
    subpath.segments.push_back(s);
  }
  p->subpaths.push_back(std::move(subpath));
  return p;
}
ScenePoint direction(const SceneConnectorAnchor &a, ScenePoint towards) {
  const auto delta = sub(towards, a.point);
  ScenePoint best = unit(delta);
  double cost = -INFINITY;
  for (auto v : a.directions) {
    v = unit(v);
    double next = dot(v, delta);
    if (next > cost) {
      best = v;
      cost = next;
    }
  }
  return best;
}
SceneRect expanded(SceneRect r, double margin) {
  return {r.x - margin, r.y - margin, r.width + 2 * margin,
          r.height + 2 * margin};
}
bool inside(ScenePoint p, SceneRect r) {
  return p.x > r.x && p.x < r.x + r.width && p.y > r.y && p.y < r.y + r.height;
}
ScenePoint stub(const SceneConnectorAnchor &a, ScenePoint d, double length,
                double clearance) {
  double distance = length;
  if (a.obstacle) {
    auto b = expanded(*a.obstacle, clearance);
    if (a.point.x >= b.x && a.point.x <= b.x + b.width && a.point.y >= b.y &&
        a.point.y <= b.y + b.height) {
      double exit = INFINITY;
      if (d.x > 0)
        exit = std::min(exit, (b.x + b.width - a.point.x) / d.x);
      if (d.x < 0)
        exit = std::min(exit, (b.x - a.point.x) / d.x);
      if (d.y > 0)
        exit = std::min(exit, (b.y + b.height - a.point.y) / d.y);
      if (d.y < 0)
        exit = std::min(exit, (b.y - a.point.y) / d.y);
      distance = std::max(distance, exit);
    }
  }
  return checked(add(a.point, mul(d, distance)));
}
bool clear(ScenePoint a, ScenePoint b, const std::vector<SceneRect> &boxes) {
  // Grid edges are axis aligned; touching the expanded boundary is allowed.
  for (auto r : boxes) {
    if (equal(a, b)) {
      if (inside(a, r))
        return false;
      continue;
    }
    if (a.x == b.x && a.x > r.x && a.x < r.x + r.width &&
        std::max(a.y, b.y) > r.y && std::min(a.y, b.y) < r.y + r.height)
      return false;
    if (a.y == b.y && a.y > r.y && a.y < r.y + r.height &&
        std::max(a.x, b.x) > r.x && std::min(a.x, b.x) < r.x + r.width)
      return false;
  }
  return true;
}
std::vector<ScenePoint> orthogonal(ScenePoint a, ScenePoint b,
                                   const std::vector<SceneRect> &boxes,
                                   size_t limit) {
  std::vector<double> xs{a.x, b.x}, ys{a.y, b.y};
  for (auto r : boxes) {
    xs.push_back(r.x);
    xs.push_back(r.x + r.width);
    ys.push_back(r.y);
    ys.push_back(r.y + r.height);
  }
  auto distinct = [](auto &values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
  };
  distinct(xs);
  distinct(ys);
  const size_t nx = xs.size(), ny = ys.size(), count = nx * ny;
  if (count > 10000)
    fail("Connector routing grid exceeds the limit");
  auto point = [&](size_t id) { return ScenePoint{xs[id % nx], ys[id / nx]}; };
  auto index = [&](ScenePoint p) {
    return size_t(std::lower_bound(xs.begin(), xs.end(), p.x) - xs.begin()) +
           nx *
               size_t(std::lower_bound(ys.begin(), ys.end(), p.y) - ys.begin());
  };
  auto start = index(a), end = index(b);
  std::vector<double> costs(count * 3, INFINITY);
  std::vector<size_t> previous(count * 3, SIZE_MAX);
  using Item = std::pair<double, size_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pending;
  const auto initial = start * 3 + 2;
  costs[initial] = 0;
  pending.push({0, initial});
  size_t work = 0, finish = SIZE_MAX;
  while (!pending.empty()) {
    auto [cost, state] = pending.top();
    pending.pop();
    if (++work > limit)
      fail("Connector route search exceeds the work limit");
    if (cost != costs[state])
      continue;
    auto id = state / 3, axis = state % 3;
    if (id == end) {
      finish = state;
      break;
    }
    const auto x = id % nx, y = id / nx;
    std::vector<std::pair<size_t, size_t>> neighbors;
    if (x)
      neighbors.push_back({id - 1, 0});
    if (x + 1 < nx)
      neighbors.push_back({id + 1, 0});
    if (y)
      neighbors.push_back({id - nx, 1});
    if (y + 1 < ny)
      neighbors.push_back({id + nx, 1});
    for (auto [next, next_axis] : neighbors) {
      auto p = point(id), q = point(next);
      if (!clear(p, q, boxes))
        continue;
      auto value = cost + std::abs(q.x - p.x) + std::abs(q.y - p.y) +
                   (axis != 2 && axis != next_axis ? 8 : 0);
      auto target = next * 3 + next_axis;
      if (value < costs[target]) {
        costs[target] = value;
        previous[target] = state;
        pending.push({value, target});
      }
    }
  }
  if (finish == SIZE_MAX)
    fail("No orthogonal connector route avoids the target bounds");
  std::vector<ScenePoint> result;
  for (auto state = finish; state != SIZE_MAX; state = previous[state])
    result.push_back(point(state / 3));
  std::reverse(result.begin(), result.end());
  // Retain the stub endpoints; remove only redundant intermediate grid points.
  for (size_t i = 1; i + 1 < result.size();) {
    if (cross(sub(result[i], result[i - 1]), sub(result[i + 1], result[i])) ==
        0)
      result.erase(result.begin() + i);
    else
      ++i;
  }
  return result;
}
std::vector<ScenePoint> skewed(const std::vector<ScenePoint> &points,
                               const std::vector<double> &skew) {
  if (skew.empty() || points.size() < 3)
    return points;
  struct Edge {
    ScenePoint origin, vector;
  };
  std::vector<Edge> edges;
  for (size_t i = 1; i < points.size(); ++i) {
    if (equal(points[i - 1], points[i]))
      continue;
    auto v = unit(sub(points[i], points[i - 1]));
    auto origin = points[i - 1];
    if (!edges.empty() && i + 1 < points.size() &&
        edges.size() - 1 < skew.size())
      origin = add(origin, mul(ScenePoint{-v.y, v.x}, skew[edges.size() - 1]));
    edges.push_back({origin, v});
  }
  if (edges.empty())
    return points;
  std::vector<ScenePoint> result{points.front()};
  for (size_t i = 1; i < edges.size(); ++i) {
    auto a = edges[i - 1], b = edges[i];
    double determinant = cross(a.vector, b.vector);
    if (std::abs(determinant) > 1e-12) {
      result.push_back(checked(
          add(a.origin, mul(a.vector, cross(sub(b.origin, a.origin), b.vector) /
                                          determinant))));
    } else {
      // Parallel offset lines acquire a perpendicular bridge at the old join.
      auto original = points[std::min(i, points.size() - 1)];
      auto p =
          add(a.origin, mul(a.vector, dot(sub(original, a.origin), a.vector)));
      auto q =
          add(b.origin, mul(b.vector, dot(sub(original, b.origin), b.vector)));
      if (!equal(result.back(), p))
        result.push_back(checked(p));
      if (!equal(result.back(), q))
        result.push_back(checked(q));
    }
  }
  if (!equal(result.back(), points.back()))
    result.push_back(points.back());
  return result;
}
SceneTransform fit(const ScenePath &path, ScenePoint a, ScenePoint b) {
  if (path.subpaths.empty())
    return {};
  auto from = path.subpaths.front().start;
  const auto &last = path.subpaths.back();
  auto to = last.closed || last.segments.empty() ? last.start
                                                 : last.segments.back().end;
  auto source = sub(to, from), target = sub(b, a);
  SceneTransform m;
  if ((source.x == 0 && target.x != 0) || (source.y == 0 && target.y != 0)) {
    double n = dot(source, source);
    if (n == 0)
      fail("A supplied connector loop cannot span distinct endpoints");
    m.a = m.d = dot(source, target) / n;
    m.b = cross(source, target) / n;
    m.c = -m.b;
  } else {
    // A degenerate axis retains the other axis' physical scale. For a loop
    // with no chord scale use the conventional hundredth-millimetre unit.
    double fallback = 72. / 2540;
    if (source.x != 0)
      fallback = std::abs(target.x / source.x);
    else if (source.y != 0)
      fallback = std::abs(target.y / source.y);
    m.a = source.x != 0 ? target.x / source.x : fallback;
    m.d = source.y != 0 ? target.y / source.y : fallback;
  }
  m.e = a.x - m.a * from.x - m.c * from.y;
  m.f = a.y - m.b * from.x - m.d * from.y;
  for (double v : {m.a, m.b, m.c, m.d, m.e, m.f})
    if (!std::isfinite(v) || std::abs(v) > 1e9)
      fail("Supplied connector path mapping exceeds the limit");
  return m;
}
} // namespace
SceneConnectorGeometry scene_connector_geometry(const SceneConnectorSpec &spec,
                                                const SceneConnectorEnds &ends,
                                                double clearance,
                                                size_t work_limit,
                                                bool recompute) {
  if (!std::isfinite(clearance) || clearance < 0 || clearance > 10000 ||
      work_limit < 1 || work_limit > 1000000)
    fail("Invalid connector routing options");
  auto a = checked(ends.start.point), b = checked(ends.end.point);
  if (!spec.enabled)
    return {std::make_shared<const ScenePath>(), {}};
  if (spec.path && !recompute)
    return {spec.path, fit(*spec.path, a, b)};
  if (spec.type == SceneConnectorSpec::Line)
    return {polyline({a, b}), {}};
  // Do not inflate two nearby targets until the gap becomes unroutable.
  if (ends.start.obstacle && ends.end.obstacle && !ends.same_object) {
    const auto x = *ends.start.obstacle, y = *ends.end.obstacle;
    double gap_x = std::max({0., x.x - y.x - y.width, y.x - x.x - x.width});
    double gap_y = std::max({0., x.y - y.y - y.height, y.y - x.y - x.height});
    auto gap = std::hypot(gap_x, gap_y);
    if (gap > 0)
      clearance = std::min(clearance, gap / 3);
  }
  if (spec.type == SceneConnectorSpec::Standard && equal(a, b) &&
      !ends.same_object)
    return {polyline({a, b}), {}};
  const auto first = direction(ends.start, b), last = direction(ends.end, a);
  double length = std::max(clearance, std::hypot(b.x - a.x, b.y - a.y) / 3);
  if (equal(a, b))
    length = std::max(24., clearance);
  auto from = stub(ends.start, first, length, clearance),
       to = stub(ends.end, last, length, clearance);
  if (spec.type == SceneConnectorSpec::Curve) {
    auto p = std::make_shared<ScenePath>();
    PathSubpath subpath;
    subpath.start = a;
    PathSegment s;
    s.kind = PathSegment::Cubic;
    if (equal(a, b)) {
      auto perpendicular = ScenePoint{-first.y, first.x};
      auto middle = checked(add(a, mul(first, 2 * length)));
      s.control1 = from;
      s.control2 = checked(sub(middle, mul(perpendicular, length)));
      s.end = middle;
      subpath.segments.push_back(s);
      s.control1 = checked(add(middle, mul(perpendicular, length)));
      s.control2 = to;
      s.end = b;
      subpath.segments.push_back(s);
    } else {
      s.control1 = from;
      s.control2 = to;
      s.end = b;
      subpath.segments.push_back(s);
    }
    p->subpaths.push_back(std::move(subpath));
    return {p, {}};
  }
  if (spec.type == SceneConnectorSpec::Lines)
    return {polyline({a, from, to, b}), {}};
  if (spec.type != SceneConnectorSpec::Standard)
    fail("Unknown computed connector route type");
  std::vector<SceneRect> boxes;
  for (auto end : {&ends.start, &ends.end})
    if (end->obstacle)
      boxes.push_back(expanded(*end->obstacle, clearance));
  std::vector<ScenePoint> middle;
  if (ends.same_object && equal(from, to)) {
    ScenePoint out{0, -1};
    if (ends.start.obstacle) {
      auto r = expanded(*ends.start.obstacle, clearance);
      if (from.y <= r.y)
        out = {0, -1};
      else if (from.x >= r.x + r.width)
        out = {1, 0};
      else if (from.y >= r.y + r.height)
        out = {0, 1};
      else if (from.x <= r.x)
        out = {-1, 0};
      else
        fail("Self connector escape remains inside its target");
    }
    auto perpendicular = ScenePoint{-out.y, out.x};
    auto size = std::max(length, 24.);
    middle = {from, checked(add(from, mul(perpendicular, size))),
              checked(add(add(from, mul(perpendicular, size)), mul(out, size))),
              checked(add(from, mul(out, size))), from};
  } else
    middle = orthogonal(from, to, boxes, work_limit);
  std::vector<ScenePoint> points{a};
  points.insert(points.end(), middle.begin(), middle.end());
  points.push_back(b);
  points = skewed(points, spec.skew);
  // Generated intermediate lines must remain outside the actual target boxes;
  // only the first/last escape segments may traverse a custom point's box.
  std::vector<SceneRect> actual;
  for (auto end : {&ends.start, &ends.end})
    if (end->obstacle)
      actual.push_back(*end->obstacle);
  for (size_t i = 2; i + 1 < points.size(); ++i)
    if (!clear(points[i - 1], points[i], actual))
      fail("Connector skew intersects a target's bounding box");
  return {polyline(points), {}};
}
} // namespace DomeOffice
