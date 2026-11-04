// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
// Alternate local-space on/off lengths, starting with an on interval at each
// subpath origin. Closed seams join a wrapping dash; zero on intervals retain
// their point for round/square cap rendering. Limits bound work and allocation.
std::vector<SceneContour> stroke_dashes(const std::vector<SceneContour> &,
                                        const std::vector<double> &,
                                        size_t point_limit = 200000);
// Boundaries of the stroke union, with positive outer contours and negative
// holes. Fill/tessellate the entire set once to avoid doubled translucent ink.
// Geometry stays in local coordinates; device controls round-edge precision.
std::vector<SceneContour> stroke_outlines(const std::vector<SceneContour> &,
                                          const ScenePaint &,
                                          const SceneTransform &device = {},
                                          double tolerance = .125,
                                          size_t point_limit = 200000);
// Individual ODF stroke passes: union within each pass, alpha-over between
// passes. Aggregate point budget applies across all overlays.
std::vector<std::vector<SceneContour>>
stroke_pass_outlines(const std::vector<SceneContour> &, const ScenePaint &,
                     const SceneTransform &device = {}, double tolerance = .125,
                     size_t point_limit = 200000,
                     const std::vector<SceneContour> &extra_fills = {});
// Nonzero canonicalization, including holes and source winding. Union the
// shaft and its markers before applying stroke alpha once.
std::vector<SceneContour>
filled_contour_union(const std::vector<SceneContour> &,
                     size_t point_limit = 200000);
} // namespace DomeOffice
