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
} // namespace DomeOffice
