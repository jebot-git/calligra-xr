// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_geometry.h"
namespace DomeOffice {
// Internal candidate-only mutation. The controller validates/publishes complete
// scenes and manages permission, gesture rollback and history boundaries.
bool apply_native_shape_geometry(const SceneShape &, XmlElement candidate,
                                 const NativeShapeGeometryEdit &,
                                 std::optional<SceneTransform> action = {});
} // namespace DomeOffice
