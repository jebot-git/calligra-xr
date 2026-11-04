// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_geometry_source.h"
#include "native_stage_source.h"
namespace DomeOffice {
void Presentation::begin_shape_transform(int slide, int index,
                                         NativeShapeHandle handle,
                                         ScenePoint pointer) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &shape = scene(slide).shapes.at(index);
  scene(slide).require_editable(shape.source);
  NativeShapeDrag gesture(shape, handle, pointer);
  // Keep only the immutable geometry/source needed for preview. Retaining
  // entire rich text or decoded image buffers here would duplicate large data.
  SceneShape original;
  original.kind = shape.kind;
  original.source = shape.source;
  original.transform = shape.transform;
  original.parent_transform = shape.parent_transform;
  original.width = shape.width;
  original.height = shape.height;
  original.contours = shape.contours;
  original.path = shape.path;
  original.path_map = shape.path_map;
  drag_ = HandleDrag{package_.xml.toByteArray(),
                     {slide, shape.editor_index, index},
                     -2 - int(handle),
                     state_};
  drag_->transform_gesture = std::move(gesture);
  drag_->original_shape = std::move(original);
}
void Presentation::transform_shape_to(int slide, int index, ScenePoint pointer,
                                      bool constrain) {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  if (!drag_ || !drag_->transform_gesture || !drag_->original_shape ||
      drag_->position.slide != slide || drag_->position.shape != index)
    throw std::runtime_error("This shape has no active transform gesture");
  auto proposal = drag_->transform_gesture->update(pointer, constrain);
  auto candidate = read_document_xml(drag_->before);
  const auto &original = *drag_->original_shape;
  std::vector<XmlElement> changed;
  if (proposal) {
    auto source = stage_candidate_source(original.source, candidate);
    apply_native_shape_geometry(original, source, proposal->geometry,
                                proposal->action);
    changed.push_back(source);
  }
  const auto bytes = geometry_content(candidate, changed);
  if (bytes != package_.xml.toByteArray())
    restore_content(bytes);
  drag_->changed = bytes != drag_->before;
  state_ = drag_->changed ? next_state_++ : drag_->state;
}
} // namespace DomeOffice
