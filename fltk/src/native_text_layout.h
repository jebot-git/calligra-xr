// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <functional>

namespace DomeOffice {
// All geometry is in typographic points. Text offsets count Unicode scalars,
// independent of the frontend's UTF-8 buffer representation.
struct TextFont {
  Text family;
  double size = 12;
  bool bold = false, italic = false;
  bool operator==(const TextFont &) const = default;
};
struct TextSpan {
  size_t begin = 0, end = 0;
  TextFont font;
};
struct TextParagraph {
  Text text, master;
  TextFont font;
  std::vector<TextSpan> spans;
  double left = 0, right = 0, indent = 0, before = 0, after = 0;
  double line_factor = 1.2, line_height = 0;
  enum Alignment { Left, Center, Right, Justify } alignment = Left;
  bool break_before = false, break_after = false;
  bool keep_together = false, keep_next = false;
  int orphans = 2, widows = 2;
};
struct TextPageStyle {
  Text name, next;
  double width = 595.275590551, height = 841.88976378;
  double left = 56.692913386, right = 56.692913386;
  double top = 56.692913386, bottom = 56.692913386;
};
struct TextFlow {
  std::vector<TextParagraph> paragraphs;
  std::map<Text, TextPageStyle> masters;
  Text first_master;
  std::vector<Text> unsupported;
};
struct TextMetrics {
  double width = 0, ascent = 0, descent = 0;
};
using MeasureText = std::function<TextMetrics(const Text &, const TextFont &)>;
struct TextFragment {
  Text text;
  TextFont font;
  double x = 0, width = 0;
};
struct TextLine {
  size_t paragraph = 0, begin = 0, end = 0;
  double x = 0, y = 0, baseline = 0, height = 0, width = 0;
  std::vector<TextFragment> fragments;
};
struct TextPage {
  TextPageStyle style;
  std::vector<TextLine> lines;
};
struct TextPagination {
  std::vector<TextPage> pages;
  std::vector<Text> unsupported;
};
TextFlow read_text_flow(const OdfPackage &);
TextPagination paginate_text(const TextFlow &, const MeasureText &);
} // namespace DomeOffice
