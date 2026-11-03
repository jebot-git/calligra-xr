// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
namespace DomeOffice {
namespace {
struct FrameText {
  const SceneShape &shape;
  XmlElement container;
  int index;
};
FrameText frame_text(const Presentation &p, int slide, int box) {
  const auto &scene = p.scene(slide);
  const int index = int(scene.editor_shapes.at(box));
  const auto &shape = scene.shapes.at(index);
  if (shape.master || !shape.image.empty() || shape.source.isNull())
    throw std::runtime_error("Choose an editable own-slide text frame");
  auto container = child(shape.source, DRAW, "text-box");
  if (container.isNull() && (!children(shape.source, TEXT, "p").empty() ||
                             !children(shape.source, TEXT, "h").empty()))
    container = shape.source;
  if (container.isNull())
    throw std::runtime_error("This shape has no editable paragraph content");
  return {shape, container, index};
}
std::pair<size_t, size_t> range(const FrameText &frame,
                                const TextSelection &selection) {
  if (selection.anchor > frame.shape.text.size() ||
      selection.cursor > frame.shape.text.size() || !selection.cells.empty() ||
      selection.cell || selection.anchor_cells)
    throw std::runtime_error("Invalid slide text selection");
  return {std::min(selection.anchor, selection.cursor),
          std::max(selection.anchor, selection.cursor)};
}
TextParagraph base(const SceneShape &shape) {
  TextParagraph p;
  p.font = shape.paint.font;
  if (!shape.paint.use_window_text_color)
    p.color = shape.paint.text_color;
  return p;
}
} // namespace
WordsFormatInfo Presentation::box_format_info(int slide, int box,
                                              TextSelection selection) const {
  auto frame = frame_text(*this, slide, box);
  auto [begin, end] = range(frame, selection);
  auto info =
      read_scoped_text_format(package_, frame.container, begin, end,
                              base(frame.shape), frame.shape.paragraph_style);
  if (begin == end && typing_format_ && typing_format_->slide == slide &&
      typing_format_->box == box &&
      typing_format_->position == selection.cursor) {
    info.character.overlay(typing_format_->format);
    if (typing_format_->format.weight)
      info.bold = *typing_format_->format.weight >= 600;
  }
  return info;
}
bool Presentation::format_box_characters(int slide, int box,
                                         TextSelection selection,
                                         const WordsCharacterFormat &format) {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  validate_words_format(format);
  auto frame = frame_text(*this, slide, box);
  scene(slide).require_editable(frame.shape.source);
  auto [begin, end] = range(frame, selection);
  if (format.empty())
    return false;
  finish_handle_drag();
  merge_text_ = false;
  const auto before = package_.xml.toByteArray();
  auto candidate = package_;
  candidate.xml = read_document_xml(before);
  auto target = stage_candidate_source(frame.container, candidate.xml);
  const bool changed =
      format_scoped_characters(candidate, target, begin, end, format);
  PresentationPosition at{slide, box, frame.index, selection};
  if (changed) {
    candidate.validate_archive_limits();
    restore_package(std::move(candidate));
    remember(before, at, at);
  }
  if (begin == end) {
    if (!typing_format_ || typing_format_->slide != slide ||
        typing_format_->box != box ||
        typing_format_->position != selection.cursor)
      typing_format_ = TypingFormat{slide, box, selection.cursor, {}};
    typing_format_->format.overlay(format);
  } else
    typing_format_.reset();
  return changed;
}
bool Presentation::format_box_paragraphs(int slide, int box,
                                         TextSelection selection,
                                         const WordsParagraphFormat &format) {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  validate_words_format(format);
  if (format.orphans || format.widows || format.keep_together ||
      format.keep_next || format.break_before)
    throw std::runtime_error(
        "Page pagination controls do not apply to a slide text frame");
  auto frame = frame_text(*this, slide, box);
  scene(slide).require_editable(frame.shape.source);
  auto [begin, end] = range(frame, selection);
  if (format.empty())
    return false;
  finish_handle_drag();
  merge_text_ = false;
  const auto before = package_.xml.toByteArray();
  auto candidate = package_;
  candidate.xml = read_document_xml(before);
  auto target = stage_candidate_source(frame.container, candidate.xml);
  const bool changed =
      format_scoped_paragraphs(candidate, target, begin, end, format);
  PresentationPosition at{slide, box, frame.index, selection};
  if (changed) {
    candidate.validate_archive_limits();
    restore_package(std::move(candidate));
    remember(before, at, at);
  }
  if (!selection.collapsed() ||
      (typing_format_ &&
       (typing_format_->slide != slide || typing_format_->box != box ||
        typing_format_->position != selection.cursor)))
    typing_format_.reset();
  return changed;
}
} // namespace DomeOffice
