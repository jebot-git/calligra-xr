// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_path.h"
#include <cstdint>
#include <functional>
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
struct SceneConnectorObject {
  SceneRect bounds;
  SceneTransform transform; // Object-local to slide coordinates.
  std::vector<SceneGluePoint> points;
};
struct SceneConnectorAnchor {
  ScenePoint point;       // Connector-local coordinates.
  ScenePoint slide_point; // Source-space target for physical nearest choices.
  std::vector<ScenePoint> directions; // Normalized transformed escape vectors.
  std::optional<SceneRect> obstacle;
  std::optional<uint64_t> glue;
  Text shape;
};
struct SceneConnectorEnds {
  SceneConnectorAnchor start, end;
  bool same_object = false;
};
using SceneConnectorLookup =
    std::function<const SceneConnectorObject *(const Text &)>;
// Lookup is over the complete identity catalog, so forward references and
// aliases work independently of source order. Explicit glue IDs remain exact;
// omitted IDs choose the nearest pair with stable ID tie breaking. Self-links
// prefer distinct ports where available. No source or object is mutated.
SceneConnectorEnds resolve_scene_connector_ends(
    const SceneConnectorSpec &, const SceneTransform &connector_to_slide,
    const SceneConnectorLookup &, size_t work_limit = 1000000);
} // namespace DomeOffice
