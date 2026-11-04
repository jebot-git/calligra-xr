// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_xml.h"
#include <optional>
namespace DomeOffice {
struct SceneDashLength {
  double value = 0;
  bool relative = false;
  bool operator==(const SceneDashLength &) const = default;
};
// Percentages resolve independently against the final cascaded stroke width.
struct SceneDashPattern {
  size_t first = 0, second = 0;
  std::optional<SceneDashLength> first_length, second_length, distance;
  bool round = false;
  std::vector<double> lengths(double stroke_width) const;
  bool operator==(const SceneDashPattern &) const = default;
};
SceneDashPattern read_scene_dash_pattern(XmlElement);
} // namespace DomeOffice
