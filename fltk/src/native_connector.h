// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_path.h"
#include <cstdint>
namespace DomeOffice {
struct SceneGluePoint {
  enum Direction : unsigned { Up = 1, Right = 2, Down = 4, Left = 8, All = 15 };
  uint64_t id = 0;
  ScenePoint position; // Shape-local, before the containing shape transform.
  unsigned escape = All;
  bool standard = false;
};
// Standard edge centers plus explicit user points. Percent coordinates are
// relative to the bounds center; aligned lengths are relative to an edge or
// corner. Positions are bounded but not silently clamped to the rectangle.
// Enhanced-geometry replacement/none rules belong to the object resolver.
std::vector<SceneGluePoint> read_scene_glue_points(XmlElement shape,
                                                   const SceneRect &bounds,
                                                   size_t limit = 10000);
struct SceneConnectorEnd {
  Text shape;
  std::optional<uint64_t> glue;
  std::optional<double> x, y; // Preserve independently omitted attached axes.
};
struct SceneConnectorSpec {
  enum Type { Standard, Line, Lines, Curve } type = Standard;
  SceneConnectorEnd start, end;
  SceneRect viewbox;
  bool enabled = true;
  std::shared_ptr<const ScenePath> path;
  std::vector<double> skew;
};
// Parse source semantics without resolving identities, choosing omitted glue
// points, replacing supplied geometry, or mutating the source document.
SceneConnectorSpec read_scene_connector(XmlElement);
} // namespace DomeOffice
