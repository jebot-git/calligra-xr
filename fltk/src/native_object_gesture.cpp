// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_object_gesture.h"
#include "native_stage_source.h"
#include "native_stage_order.h"
#include <cmath>
namespace DomeOffice {
namespace {
void point(ScenePoint p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    throw std::runtime_error("Object gesture exceeds the coordinate limit");
}
SceneTransform parent_transform(XmlElement node) {
  std::vector<XmlElement> parents;
  for (auto parent = node.parentNode().toElement();
       parent.namespaceURI() == DRAW && parent.localName() == "g";
       parent = parent.parentNode().toElement()) {
    if (parents.size() >= 256)
      throw std::runtime_error("Presentation group nesting exceeds the limit");
    parents.push_back(parent);
  }
  SceneTransform transform;
  for (auto it = parents.rbegin(); it != parents.rend(); ++it)
    transform =
        transform * parse_scene_transform(it->attributeNS(DRAW, "transform"));
  return transform;
}
} // namespace
SceneShape native_object_frame(const SlideScene &scene,
                               const std::vector<NativeObjectRef> &refs) {
  auto leaves = native_object_leaves(scene, refs);
  if (leaves.empty())
    throw std::runtime_error("Select at least one object");
  auto bounds = scene.shapes.at(leaves.front()).bounds();
  for (auto index : leaves) {
    auto box = scene.shapes.at(index).bounds();
    double right = std::max(bounds.x + bounds.width, box.x + box.width),
           bottom = std::max(bounds.y + bounds.height, box.y + box.height);
    bounds.x = std::min(bounds.x, box.x);
    bounds.y = std::min(bounds.y, box.y);
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
  }
  point({bounds.x, bounds.y});
  point({bounds.x + bounds.width, bounds.y + bounds.height});
  SceneShape frame;
  frame.kind = SceneShape::Rectangle;
  frame.width = bounds.width;
  frame.height = bounds.height;
  frame.transform.e = bounds.x;
  frame.transform.f = bounds.y;
  XmlDocument source;
  frame.source = source.createElementNS(DRAW, "draw:rect");
  return frame;
}
NativeObjectGesture::NativeObjectGesture(
    const SlideScene &scene, const std::vector<NativeObjectRef> &selection,
    ScenePoint pointer, std::optional<NativeShapeHandle> handle)
    : pointer_(pointer), frame_(native_object_frame(scene, selection)) {
  point(pointer);
  if (handle) {
    if (*handle == NativeShapeHandle::LineStart ||
        *handle == NativeShapeHandle::LineEnd)
      throw std::runtime_error("Object selections use frame handles");
    const bool x = *handle != NativeShapeHandle::North &&
                   *handle != NativeShapeHandle::South;
    const bool y = *handle != NativeShapeHandle::East &&
                   *handle != NativeShapeHandle::West;
    if (*handle != NativeShapeHandle::Rotate &&
        ((x && frame_.width <= 0) || (y && frame_.height <= 0)))
      throw std::runtime_error("A zero-size selection axis cannot be scaled");
    shape_drag_.emplace(frame_, *handle, pointer);
  }
  auto collect = [&](auto &&self, XmlElement node, SceneTransform parent,
                     int depth) -> void {
    if (depth > 256 || sources_.size() >= 100000)
      throw std::runtime_error("Object gesture exceeds the shape limit");
    if (node.namespaceURI() == DRAW && node.localName() == "g") {
      auto group =
          parent * parse_scene_transform(node.attributeNS(DRAW, "transform"));
      for (const auto &item : native_stacking_children(node)) {
        if (item.node.namespaceURI() == DRAW &&
            item.node.localName() == "glue-point")
          continue;
        self(self, item.node, group, depth + 1);
      }
      return;
    }
    if (node.namespaceURI() != DRAW ||
        !TextList{"frame", "rect", "ellipse", "circle", "path", "polygon",
                  "polyline", "line", "custom-shape", "connector", "caption",
                  "control", "measure", "regular-polygon", "page-thumbnail"}
             .contains(node.localName()))
      throw std::runtime_error("This selection contains an object whose "
                               "transform is not yet supported");
    auto inverse = parent.inverse();
    if (!inverse)
      throw std::runtime_error("A containing group transform is singular");
    sources_.push_back(
        {node, parent, *inverse,
         parse_scene_transform(node.attributeNS(DRAW, "transform"))});
  };
  for (auto ref : native_object_selection(scene, selection)) {
    auto source = native_object_source(scene, ref);
    collect(collect, source, parent_transform(source), 0);
  }
}
std::optional<SceneTransform>
NativeObjectGesture::update(ScenePoint pointer, bool constrain) const {
  point(pointer);
  if (!shape_drag_) {
    if (pointer.x == pointer_.x && pointer.y == pointer_.y)
      return {};
    return SceneTransform{
        1, 0, 0, 1, pointer.x - pointer_.x, pointer.y - pointer_.y};
  }
  auto proposal = shape_drag_->update(pointer, constrain);
  if (!proposal)
    return {};
  SceneTransform scale;
  scale.a = frame_.width > 0
                ? proposal->geometry.width.value_or(frame_.width) / frame_.width
                : 1;
  scale.d =
      frame_.height > 0
          ? proposal->geometry.height.value_or(frame_.height) / frame_.height
          : 1;
  return proposal->action.value_or(SceneTransform{}) * frame_.transform *
         scale * *frame_.transform.inverse();
}
void NativeObjectGesture::apply(const SceneTransform &action,
                                const XmlDocument &candidate) const {
  const Text PRESENTATION =
      "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
  for (double value :
       {action.a, action.b, action.c, action.d, action.e, action.f})
    if (!std::isfinite(value))
      throw std::runtime_error("Invalid object-selection transform");
  for (const auto &source : sources_) {
    auto node = stage_candidate_source(source.node, candidate);
    node.setAttributeNS(DRAW, "draw:transform",
                        format_scene_transform(source.inverse * action *
                                               source.parent * source.own));
    if (node.localName() == "frame")
      node.setAttributeNS(PRESENTATION, "presentation:user-transformed",
                          "true");
  }
}
} // namespace DomeOffice
