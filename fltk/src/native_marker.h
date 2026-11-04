// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_path.h"
namespace DomeOffice {
// Source spelling stays available for scoped authoring and round trips. The
// geometry is not clipped to viewBox: physical width refers to the geometry.
struct SceneMarkerDefinition {
  Text data, view_box;
  std::shared_ptr<const ScenePath> path;
  SceneRect bounds;
  bool enabled = true;
};
std::shared_ptr<const SceneMarkerDefinition> read_scene_marker(XmlElement);
struct ScenePathEnd {
  ScenePoint point, inward;
};
struct ScenePathEnds {
  ScenePathEnd first, last;
  bool open = false, stroke = true;
  size_t paint_group = 0;
};
// Exact endpoint directions in shape-local space, independent of flattening
// tolerance. Directionless segment subpaths use the SVG positive-x fallback.
std::vector<ScenePathEnds> scene_path_ends(const ScenePath &,
                                           const SceneTransform & = {});
std::vector<ScenePathEnds>
scene_contour_ends(const std::vector<SceneContour> &);
SceneTransform marker_transform(const SceneMarkerDefinition &, double width,
                                bool centered, const ScenePathEnd &);
std::vector<SceneContour>
marker_contours(const SceneMarkerDefinition &, double width, bool centered,
                const ScenePathEnd &, const SceneTransform &device = {},
                double tolerance = .125, size_t point_limit = 200000);
struct SceneMarkerStroke {
  std::vector<SceneContour> shafts, markers;
};
// One pair per open original subpath, before dash splitting. Marker geometry
// and the shaft share stroke ink/opacity; trim retains the original dash phase.
SceneMarkerStroke marker_stroke_geometry(const SceneShape &,
                                         const std::vector<SceneContour> &,
                                         const SceneTransform &device = {},
                                         double tolerance = .125,
                                         size_t point_limit = 200000);
} // namespace DomeOffice
