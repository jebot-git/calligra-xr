// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_geometry.h"
namespace DomeOffice {
enum class NativeShapeHandle {
  NorthWest,
  North,
  NorthEast,
  East,
  SouthEast,
  South,
  SouthWest,
  West,
  Rotate,
  LineStart,
  LineEnd
};
struct NativeShapeDragProposal {
  NativeShapeGeometryEdit geometry;
  // A slide-space action on the ORIGINAL shape, after changing frame size.
  // This also expresses reflections when a resize crosses its fixed anchor.
  std::optional<SceneTransform> action;
};
// Immutable gesture coordinates. Pointer updates always refer to the starting
// geometry, avoiding cumulative rounding and preserving the opposite anchor.
class NativeShapeDrag {
public:
  NativeShapeDrag(const SceneShape &, NativeShapeHandle, ScenePoint pointer);
  // Empty means the original spelling/source should be restored exactly.
  // Constrain preserves resize aspect ratio, or snaps rotation/lines to 15°.
  std::optional<NativeShapeDragProposal> update(ScenePoint pointer,
                                                bool constrain = false) const;

private:
  NativeShapeHandle handle_;
  NativeShapeGeometry geometry_;
  SceneTransform transform_;
  std::optional<SceneTransform> inverse_;
  ScenePoint pointer_, center_, first_, last_;
};
} // namespace DomeOffice
