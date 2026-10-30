// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_shaper.h"
namespace DomeOffice {
struct TabAdvance {
  TextTabStop stop;
  double position = 0, advance = 0, offset = 0;
  bool clamped = false;
};
TabAdvance next_tab(const TextParagraph &, double current, double origin,
                    double field_width, double delimiter, bool rtl);
std::vector<TextLeaderStroke> text_line_strokes(TextDecoration::LineStyle,
                                                bool double_line, double left,
                                                double right, double y,
                                                double width);
std::vector<TextTabPlacement> text_decoration_lines(const ShapedText &);
void prepare_tab_leaders(std::vector<TextTabPlacement> &, NativeTextShaper *);
} // namespace DomeOffice
