// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <span>
namespace DomeOffice {
using LayoutParagraph = std::function<std::shared_ptr<const TextBoxLayout>(
    const TextBlock &, double)>;
const TextParagraph &block_paragraph(const TextFlow &, const TextBlock &);
const TextTable &block_table(const TextFlow &, const TextBlock &);
TextRect intersect_text_rect(TextRect, TextRect);
void offset_text_layout(TextBoxLayout &, double x, double y);
void clip_text_layout(TextBoxLayout &, TextRect);
struct NativeCellLayout {
  TextCellBox box;
  TextRect content_clip;
  std::shared_ptr<const TextBoxLayout> text;
  double text_x = 0, text_y = 0;
};
struct NativeTableLayout {
  double x = 0, width = 0, height = 0;
  std::vector<double> column_edges, row_edges;
  size_t header_rows = 0;
  std::vector<NativeCellLayout> cells;
  TextBoxLayout display;
};
// One layout context caches paragraph/table geometry and bounds expanded work.
// Models and source paragraphs are immutable and must outlive this context.
class NativeBlockLayouter {
public:
  NativeBlockLayouter(const TextFlow &, LayoutParagraph);
  TextBoxLayout blocks(std::span<const TextBlock>, double width);
  std::shared_ptr<const NativeTableLayout> table(const TextBlock &,
                                                 double width);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
