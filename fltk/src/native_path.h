// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
struct PathSegment {
  enum Kind { Line, Cubic, Quadratic, Arc } kind = Line;
  ScenePoint end, control1, control2;
  double rx = 0, ry = 0, rotation = 0; // Arc rotation in degrees, as in SVG.
  bool large_arc = false, sweep = false;
};
struct PathSubpath {
  ScenePoint start;
  std::vector<PathSegment> segments;
  bool closed = false;
};
struct ScenePath {
  std::vector<PathSubpath> subpaths;
};
ScenePath parse_svg_path(const Text &);
// Coordinates map path units to local shape units; device is used only for
// the flatness test. Returned contours retain local coordinates and closure.
std::vector<SceneContour> flatten_path(const ScenePath &,
                                       const SceneTransform &coordinates = {},
                                       const SceneTransform &device = {},
                                       double tolerance = .25,
                                       size_t point_limit = 16384);
SceneRect path_bounds(const ScenePath &, const SceneTransform & = {});
ScenePath rounded_rectangle_path(double width, double height, double rx,
                                 double ry);
enum class EllipseClosure { Open, Chord, Pie };
ScenePath ellipse_path(double width, double height, double start_degrees = 0,
                       double sweep_degrees = 360,
                       EllipseClosure = EllipseClosure::Chord);
} // namespace DomeOffice
