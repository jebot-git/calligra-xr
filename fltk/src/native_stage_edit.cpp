// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
#include <cmath>
#include <stdexcept>
namespace DomeOffice {
namespace {
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
void point(ScenePoint p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    throw std::runtime_error("Shape position exceeds the coordinate limit");
}
} // namespace
void Presentation::begin_shape_move(int slide, int shape_index,
                                    ScenePoint pointer) {
  point(pointer);
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &shape = scene(slide).shapes.at(shape_index);
  scene(slide).require_editable(shape.source);
  if (shape.master || shape.source.isNull())
    throw std::runtime_error("This shape is not editable");
  auto inverse = shape.parent_transform.inverse();
  if (!inverse)
    throw std::runtime_error("The containing group transform is singular");
  drag_ = HandleDrag{package_.xml.toByteArray(),
                     {slide, shape.editor_index, shape_index},
                     -1,
                     state_};
  drag_->pointer = pointer;
  drag_->parent_inverse = *inverse;
  drag_->original_transform =
      parse_scene_transform(shape.source.attributeNS(DRAW, "transform"));
}
void Presentation::move_shape_to(int slide, int shape_index,
                                 ScenePoint pointer) {
  point(pointer);
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  if (!drag_ || drag_->handle != -1 || drag_->position.slide != slide ||
      drag_->position.shape != shape_index)
    throw std::runtime_error("This shape is not being moved");
  const auto &shape = scene(slide).shapes.at(shape_index);
  scene(slide).require_editable(shape.source);
  auto delta =
      ScenePoint{pointer.x - drag_->pointer.x, pointer.y - drag_->pointer.y};
  if (delta.x == 0 && delta.y == 0) {
    // Restore the original spelling/absence of transform too, so moving back
    // to the starting point leaves no document or undo change.
    if (drag_->changed)
      restore_content(drag_->before);
    drag_->changed = false;
    state_ = drag_->state;
    return;
  }
  const auto &p = drag_->parent_inverse;
  SceneTransform translation;
  translation.e = p.a * delta.x + p.c * delta.y;
  translation.f = p.b * delta.x + p.d * delta.y;
  auto candidate = read_document_xml(drag_->before);
  auto source = stage_candidate_source(shape.source, candidate);
  source.setAttributeNS(
      DRAW, "draw:transform",
      format_scene_transform(translation * drag_->original_transform));
  if (source.localName() == "frame")
    source.setAttributeNS(PRESENTATION, "presentation:user-transformed",
                          "true");
  auto bytes = candidate.toByteArray();
  if (bytes == package_.xml.toByteArray())
    return;
  // Rebuild and validate the complete candidate before changing live state.
  // Geometry, text runs, paths, image entries and surrounding groups are kept.
  restore_content(bytes);
  drag_->changed = true;
  state_ = next_state_++;
}
void Presentation::move_shape(int slide, int shape_index, ScenePoint delta) {
  begin_shape_move(slide, shape_index, {});
  try {
    move_shape_to(slide, shape_index, delta);
    finish_handle_drag();
  } catch (...) {
    finish_handle_drag(true);
    throw;
  }
}
} // namespace DomeOffice
