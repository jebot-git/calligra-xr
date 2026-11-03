// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
struct NativeShapeGeometry {
  ScenePoint origin;
  // Local frame extents; for a line, width is its slide-space length.
  double width = 0, height = 0;
  std::optional<double> rotation;
  bool line = false, position_enabled = true, rotation_enabled = true;
  bool width_enabled = true, height_enabled = true;
};
struct NativeShapeGeometryEdit {
  std::optional<double> x, y, width, height, rotation;
};
NativeShapeGeometry native_shape_geometry(const SceneShape &);
} // namespace DomeOffice
