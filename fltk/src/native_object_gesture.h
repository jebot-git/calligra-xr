// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_selection.h"
#include "native_shape_drag.h"
namespace DomeOffice {
SceneShape native_object_frame(const SlideScene &,
                               const std::vector<NativeObjectRef> &);
// Immutable original source/transforms for one object-selection gesture.
// Group actions are baked into leaves: strict ODF groups need no transform.
class NativeObjectGesture {
public:
  NativeObjectGesture(const SlideScene &, const std::vector<NativeObjectRef> &,
                      ScenePoint, std::optional<NativeShapeHandle> = {});
  std::optional<SceneTransform> update(ScenePoint, bool constrain) const;
  std::vector<XmlElement> apply(const SceneTransform &,
                                const XmlDocument &) const;

private:
  struct Source {
    XmlElement node;
    SceneTransform parent, inverse, own;
  };
  std::vector<Source> sources_;
  ScenePoint pointer_;
  SceneShape frame_;
  std::optional<NativeShapeDrag> shape_drag_;
};
} // namespace DomeOffice
