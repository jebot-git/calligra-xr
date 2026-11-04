// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
// Boundaries of the stroke union, with positive outer contours and negative
// holes. Fill/tessellate the entire set once to avoid doubled translucent ink.
// Geometry stays in local coordinates; device controls round-edge precision.
std::vector<SceneContour> stroke_outlines(const std::vector<SceneContour> &,
                                          const ScenePaint &,
                                          const SceneTransform &device = {},
                                          double tolerance = .125,
                                          size_t point_limit = 200000);
} // namespace DomeOffice
