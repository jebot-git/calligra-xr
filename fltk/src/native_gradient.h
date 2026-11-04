// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
struct SceneGradientDefinition {
  enum Type : unsigned {
    Linear,
    Axial,
    Radial,
    Ellipsoid,
    Square,
    Rectangular
  };
  Type type = Linear;
  SceneColor start{0, 0, 0}, end{255, 255, 255};
  double start_intensity = 1, end_intensity = 1;
  double angle = 0; // Radians; source unitless ODF1.2 angles are degrees.
  double border = 0, cx = .5, cy = .5;
  bool operator==(const SceneGradientDefinition &) const = default;
};
SceneGradientDefinition read_scene_gradient(XmlElement);
void validate_scene_gradient(const SceneGradientDefinition &);
uint64_t read_scene_gradient_steps(const Text &);
// Shared native policy for ambiguous ODF1.2 concentric geometry (OFFICE-4117):
// outer start / inner end, expanded circle/ellipse and square, equal physical
// insets for rectangular rings. Angles and source definitions stay untouched.
double scene_gradient_parameter(const SceneGradientDefinition &, SceneRect,
                                ScenePoint);
SceneColor scene_gradient_color(const SceneGradientDefinition &, SceneRect,
                                ScenePoint, uint64_t steps = 0);
SceneRect scene_gradient_bounds(const SceneShape &);
// Bounded RGB BMP; immutable data uses the existing GPU image cache. Raster
// colors are shared by GPU and PDF/CUPS, never serialized into the ODF source.
std::shared_ptr<const ImageAsset>
scene_gradient_image(const SceneGradientDefinition &, SceneRect,
                     const SceneTransform &device, uint64_t steps = 0,
                     size_t pixel_limit = 4000000);
} // namespace DomeOffice
