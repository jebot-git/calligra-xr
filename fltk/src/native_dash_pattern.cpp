// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_dash_pattern.h"
#include "native_presentation.h"
#include <cmath>
namespace DomeOffice {
namespace {
[[noreturn]] void fail() {
  throw std::runtime_error("Invalid or excessive drawing dash pattern");
}
size_t count(XmlElement node, const Text &name) {
  if (!node.hasAttributeNS(DRAW, name))
    return 0;
  bool ok = false;
  auto n = node.attributeNS(DRAW, name).toInt(&ok);
  if (!ok || n < 0 || n > 50000)
    fail();
  return size_t(n);
}
std::optional<SceneDashLength> measure(XmlElement node, const Text &name) {
  if (!node.hasAttributeNS(DRAW, name))
    return {};
  auto text = node.attributeNS(DRAW, name).trimmed();
  bool relative = text.endsWith('%'), ok = true;
  double value = relative ? text.left(text.size() - 1).toDouble(&ok) / 100
                          : parse_scene_length(text);
  if (!ok || !std::isfinite(value) || value < 0 || value > 1e9)
    fail();
  return SceneDashLength{value, relative};
}
} // namespace
SceneDashPattern read_scene_dash_pattern(XmlElement node) {
  SceneDashPattern result;
  result.first = count(node, "dots1");
  result.second = count(node, "dots2");
  result.first_length = measure(node, "dots1-length");
  result.second_length = measure(node, "dots2-length");
  result.distance = measure(node, "distance");
  auto shape = node.attributeNS(DRAW, "style", "rect");
  if (shape != "rect" && shape != "round")
    fail();
  result.round = shape == "round";
  (void)result.lengths(1);
  return result;
}
std::vector<double> SceneDashPattern::lengths(double width) const {
  if (!std::isfinite(width) || width < 0 || first > 50000 || second > 50000 ||
      first + second > 50000)
    fail();
  auto resolve = [&](const std::optional<SceneDashLength> &measure) {
    // ODF1.2 does not specify values for these omitted optional attributes.
    // Omitted lengths/spacing use one stroke width, omitted counts use zero.
    // Explicit zero stays zero; source serialization preserves the spelling.
    auto value =
        measure ? measure->value * (measure->relative ? width : 1) : width;
    if (!std::isfinite(value) || value < 0 || value > 1e10)
      fail();
    return value;
  };
  auto a = resolve(first_length), b = resolve(second_length),
       gap = resolve(distance);
  std::vector<double> out;
  out.reserve(2 * (first + second));
  for (auto [number, length] : {std::pair{first, a}, std::pair{second, b}})
    for (size_t i = 0; i < number; ++i) {
      out.push_back(length);
      out.push_back(gap);
    }
  return out;
}
} // namespace DomeOffice
