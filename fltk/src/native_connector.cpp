// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_connector.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
double bounded(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e9)
    fail("Connector coordinates exceed the limit");
  return value;
}
double number(Text text) {
  auto raw = text.trimmed().toStdString();
  if (raw.empty() || raw.size() > 128)
    fail("Invalid connector number");
  auto first = raw.data();
  if (*first == '+')
    ++first;
  double value = 0;
  auto result = std::from_chars(first, raw.data() + raw.size(), value);
  if (result.ec != std::errc() || result.ptr != raw.data() + raw.size())
    fail("Invalid connector number");
  return bounded(value);
}
double absolute(Text text) {
  text = text.trimmed();
  if (text.empty() || text.endsWith('%'))
    fail("Expected an absolute connector distance");
  return bounded(parse_scene_length(text));
}
uint64_t identifier(Text text) {
  auto raw = text.trimmed().toStdString();
  if (raw.empty() || raw.size() > 64)
    fail("Invalid glue-point identifier");
  auto first = raw.data();
  if (*first == '+')
    ++first;
  uint64_t value = 0;
  auto result = std::from_chars(first, raw.data() + raw.size(), value);
  if (result.ec != std::errc() || result.ptr != raw.data() + raw.size())
    fail("Invalid glue-point identifier");
  return value;
}
unsigned escape(const Text &name) {
  if (name == "auto")
    return SceneGluePoint::All;
  if (name == "up")
    return SceneGluePoint::Up;
  if (name == "right")
    return SceneGluePoint::Right;
  if (name == "down")
    return SceneGluePoint::Down;
  if (name == "left")
    return SceneGluePoint::Left;
  if (name == "horizontal")
    return SceneGluePoint::Left | SceneGluePoint::Right;
  if (name == "vertical")
    return SceneGluePoint::Up | SceneGluePoint::Down;
  fail("Invalid glue-point escape direction");
}
SceneConnectorEnd endpoint(XmlElement node, bool start) {
  SceneConnectorEnd out;
  const Text end = start ? "start" : "end", suffix = start ? "1" : "2";
  out.shape = node.attributeNS(DRAW, end + "-shape").trimmed();
  if (node.hasAttributeNS(DRAW, end + "-glue-point"))
    out.glue = identifier(node.attributeNS(DRAW, end + "-glue-point"));
  for (auto [name, field] :
       {std::pair{Text("x"), &out.x}, std::pair{Text("y"), &out.y}})
    if (node.hasAttributeNS(SVG, name + suffix))
      *field = absolute(node.attributeNS(SVG, name + suffix));
  if (out.shape.empty() && (!out.x || !out.y))
    fail("A free connector endpoint needs both coordinates");
  return out;
}
} // namespace
std::vector<SceneGluePoint> read_scene_glue_points(XmlElement shape,
                                                   const SceneRect &bounds,
                                                   size_t limit) {
  if (limit < 4 || limit > 100000 || bounds.width < 0 || bounds.height < 0)
    fail("Invalid glue-point bounds or limit");
  for (double v : {bounds.x, bounds.y, bounds.width, bounds.height,
                   bounds.x + bounds.width, bounds.y + bounds.height})
    bounded(v);
  const double cx = bounded(bounds.x + bounds.width / 2),
               cy = bounded(bounds.y + bounds.height / 2);
  std::vector<SceneGluePoint> out{
      {0, {cx, bounds.y}, SceneGluePoint::Up, true},
      {1, {bounds.x + bounds.width, cy}, SceneGluePoint::Right, true},
      {2, {cx, bounds.y + bounds.height}, SceneGluePoint::Down, true},
      {3, {bounds.x, cy}, SceneGluePoint::Left, true}};
  std::set<uint64_t> ids{0, 1, 2, 3};
  for (auto node : children(shape, DRAW, "glue-point")) {
    if (out.size() >= limit)
      fail("Too many glue points");
    if (!node.hasAttributeNS(DRAW, "id") || !node.hasAttributeNS(SVG, "x") ||
        !node.hasAttributeNS(SVG, "y") ||
        !node.hasAttributeNS(DRAW, "escape-direction"))
      fail("Incomplete glue point");
    auto id = identifier(node.attributeNS(DRAW, "id"));
    if (!ids.insert(id).second)
      fail("Duplicate or reserved glue-point identifier");
    auto alignment = node.attributeNS(DRAW, "align");
    ScenePoint origin{cx, cy};
    if (!alignment.empty()) {
      static const std::map<Text, ScenePoint> anchors{
          {"top-left", {0, 0}},    {"top", {.5, 0}},
          {"top-right", {1, 0}},   {"left", {0, .5}},
          {"center", {.5, .5}},    {"right", {1, .5}},
          {"bottom-left", {0, 1}}, {"bottom-right", {1, 1}}};
      auto at = anchors.find(alignment);
      if (at == anchors.end())
        fail("Invalid glue-point alignment");
      origin = {bounds.x + bounds.width * at->second.x,
                bounds.y + bounds.height * at->second.y};
    }
    auto coordinate = [&](const Text &name, double origin, double extent) {
      auto text = node.attributeNS(SVG, name).trimmed();
      if (text.endsWith('%')) {
        if (!alignment.empty())
          fail("Aligned glue points require absolute distances");
        text.chop(1);
        return bounded(origin + number(text) * extent / 100);
      }
      if (alignment.empty())
        fail("Unaligned glue points require percentages");
      return bounded(origin + absolute(text));
    };
    out.push_back({id,
                   {coordinate("x", origin.x, bounds.width),
                    coordinate("y", origin.y, bounds.height)},
                   escape(node.attributeNS(DRAW, "escape-direction")),
                   false});
  }
  return out;
}
SceneConnectorSpec read_scene_connector(XmlElement node) {
  if (node.namespaceURI() != DRAW || node.localName() != "connector")
    fail("Expected a connector source element");
  SceneConnectorSpec out;
  auto type = node.attributeNS(DRAW, "type", "standard");
  if (type == "line")
    out.type = SceneConnectorSpec::Line;
  else if (type == "lines")
    out.type = SceneConnectorSpec::Lines;
  else if (type == "curve")
    out.type = SceneConnectorSpec::Curve;
  else if (type != "standard")
    fail("Invalid connector type");
  out.start = endpoint(node, true);
  out.end = endpoint(node, false);
  if (!node.hasAttributeNS(SVG, "viewBox"))
    fail("Connector viewBox is missing");
  auto raw = node.attributeNS(SVG, "viewBox").toStdString();
  if (raw.size() > 1024)
    fail("Connector viewBox exceeds the limit");
  std::replace(raw.begin(), raw.end(), ',', ' ');
  std::istringstream values(raw);
  double box[4];
  for (double &v : box) {
    std::string token;
    if (!(values >> token))
      fail("Invalid connector viewBox");
    v = number(Text(token));
  }
  std::string extra;
  if (values >> extra || box[2] < 0 || box[3] < 0)
    fail("Invalid connector viewBox");
  out.viewbox = {box[0], box[1], box[2], box[3]};
  out.enabled = box[2] > 0 && box[3] > 0;
  if (node.hasAttributeNS(SVG, "d"))
    out.path = std::make_shared<const ScenePath>(
        parse_svg_path(node.attributeNS(SVG, "d")));
  if (node.hasAttributeNS(DRAW, "line-skew")) {
    auto raw = node.attributeNS(DRAW, "line-skew").toStdString();
    if (raw.size() > 1024)
      fail("Connector skew exceeds the limit");
    std::istringstream input(raw);
    for (std::string token; input >> token;) {
      if (out.skew.size() == 3)
        fail("A connector has at most three skew offsets");
      out.skew.push_back(absolute(Text(token)));
    }
    if (out.skew.empty())
      fail("Connector skew is empty");
  }
  return out;
}
namespace {
void matrix(const SceneTransform &m) {
  for (double v : {m.a, m.b, m.c, m.d, m.e, m.f})
    bounded(v);
}
ScenePoint checked(ScenePoint p) {
  bounded(p.x);
  bounded(p.y);
  return p;
}
SceneRect mapped_bounds(const SceneRect &r, const SceneTransform &m) {
  if (r.width < 0 || r.height < 0)
    fail("Invalid connector target bounds");
  for (double v : {r.x, r.y, r.width, r.height})
    bounded(v);
  double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
  for (auto p : {ScenePoint{r.x, r.y},
                 {r.x + r.width, r.y},
                 {r.x, r.y + r.height},
                 {r.x + r.width, r.y + r.height}}) {
    p = checked(m.map(checked(p)));
    left = std::min(left, p.x);
    right = std::max(right, p.x);
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  }
  return {left, top, right - left, bottom - top};
}
struct Candidates {
  const SceneConnectorObject *object = nullptr;
  std::vector<SceneConnectorAnchor> values;
};
Candidates candidates(const SceneConnectorEnd &end,
                      const SceneTransform &to_slide,
                      const SceneConnectorLookup &lookup) {
  Candidates result;
  if (end.shape.empty()) {
    if (!end.x || !end.y)
      fail("Missing free connector endpoint coordinates");
    SceneConnectorAnchor a;
    a.point = checked({*end.x, *end.y});
    a.slide_point = checked(to_slide.map(a.point));
    a.directions = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    result.values.push_back(std::move(a));
    return result;
  }
  if (!lookup || !(result.object = lookup(end.shape)))
    fail("Unresolved connector shape identity");
  auto inverse = to_slide.inverse();
  if (!inverse)
    fail("Attached connector transform is singular");
  matrix(result.object->transform);
  auto mapping = *inverse * result.object->transform;
  matrix(mapping);
  auto obstacle = mapped_bounds(result.object->bounds, mapping);
  if (result.object->points.size() > 10000)
    fail("Too many connector target glue points");
  std::set<uint64_t> ids;
  for (const auto &point : result.object->points) {
    if (!ids.insert(point.id).second)
      fail("Duplicate connector target glue ID");
    if (!point.escape || (point.escape & ~SceneGluePoint::All))
      fail("Invalid connector target escape mask");
    checked(point.position);
    if (end.glue && point.id != *end.glue)
      continue;
    SceneConnectorAnchor a;
    a.slide_point = checked(result.object->transform.map(point.position));
    a.point = checked(inverse->map(a.slide_point));
    a.obstacle = obstacle;
    a.glue = point.id;
    a.shape = end.shape;
    std::vector<ScenePoint> vectors;
    if (point.exit_vector)
      vectors.push_back(checked(*point.exit_vector));
    else
      for (auto [bit, vector] :
           {std::pair<unsigned, ScenePoint>{SceneGluePoint::Up, {0, -1}},
            {SceneGluePoint::Right, {1, 0}},
            {SceneGluePoint::Down, {0, 1}},
            {SceneGluePoint::Left, {-1, 0}}})
        if (point.escape & bit)
          vectors.push_back(vector);
    for (auto vector : vectors) {
      ScenePoint v{mapping.a * vector.x + mapping.c * vector.y,
                   mapping.b * vector.x + mapping.d * vector.y};
      auto n = std::hypot(v.x, v.y);
      if (!std::isfinite(n))
        fail("Invalid connector escape vector");
      if (n > 0)
        a.directions.push_back({v.x / n, v.y / n});
    }
    result.values.push_back(std::move(a));
  }
  if (result.values.empty())
    fail("Connector target has no matching glue point");
  std::sort(result.values.begin(), result.values.end(),
            [](const auto &a, const auto &b) { return a.glue < b.glue; });
  return result;
}
} // namespace
SceneConnectorEnds resolve_scene_connector_ends(
    const SceneConnectorSpec &spec, const SceneTransform &to_slide,
    const SceneConnectorLookup &lookup, size_t work_limit) {
  if (work_limit < 1 || work_limit > 100000000)
    fail("Invalid connector resolution work limit");
  matrix(to_slide);
  auto from = candidates(spec.start, to_slide, lookup),
       to = candidates(spec.end, to_slide, lookup);
  const bool same = from.object && from.object == to.object;
  const bool distinct = same && (!spec.start.glue || !spec.end.glue) &&
                        (from.values.size() > 1 || to.values.size() > 1);
  long double best = INFINITY;
  size_t start = 0, end = 0, work = 0;
  bool found = false;
  for (size_t i = 0; i < from.values.size(); ++i)
    for (size_t j = 0; j < to.values.size(); ++j) {
      if (++work > work_limit)
        fail("Automatic connector glue resolution exceeds the work limit");
      const auto &a = from.values[i], &b = to.values[j];
      if (distinct && a.glue == b.glue)
        continue;
      long double dx = static_cast<long double>(a.slide_point.x) -
                       b.slide_point.x,
                  dy = static_cast<long double>(a.slide_point.y) -
                       b.slide_point.y;
      auto cost = dx * dx + dy * dy;
      if (!found || cost < best) {
        best = cost;
        start = i;
        end = j;
        found = true;
      }
    }
  if (!found)
    fail("No connector attachment pair is available");
  return {std::move(from.values[start]), std::move(to.values[end]), same};
}
} // namespace DomeOffice
