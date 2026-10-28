// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <functional>
#include <optional>

namespace DomeOffice {
struct TextColor {
  unsigned char red = 0, green = 0, blue = 0;
  double alpha = 1;
  bool operator==(const TextColor &) const = default;
};
// All geometry is in typographic points. Text offsets count Unicode scalars,
// independent of the frontend's UTF-8 buffer representation.
struct TextFont {
  Text family, language;
  double size = 12;
  int weight = 400;
  bool bold = false, italic = false;
  bool operator==(const TextFont &) const = default;
};
struct TextSpan {
  size_t begin = 0, end = 0;
  TextFont font;
  std::optional<TextColor> color;
};
struct TextListLabel {
  Text text, style;
  int level = 1;
  TextFont font;
  std::optional<TextColor> color;
  enum Alignment { Left, Right, Center, Start, End } alignment = Start;
  enum Follow { Tab, Space, Nothing } follow = Tab;
  bool alignment_mode = false, visible = true;
  double space_before = 0, min_width = 18, min_distance = 0, indent = 0;
  std::optional<double> tab_stop;
  std::vector<double> paragraph_tabs;
  double default_tab = 72 * 1.25 / 2.54;
  bool relative_tabs = true;
};
struct TextParagraph {
  Text text, master;
  TextFont font;
  std::optional<TextColor> color;
  std::vector<TextSpan> spans;
  double left = 0, right = 0, indent = 0, before = 0, after = 0;
  double line_factor = 1.2, line_height = 0;
  enum Alignment { Left, Center, Right, Justify, Start, End } alignment = Start;
  enum Direction { Auto, LTR, RTL } direction = Auto;
  bool break_before = false, break_after = false;
  bool keep_together = false, keep_next = false;
  int orphans = 2, widows = 2;
  std::optional<TextListLabel> list;
};
struct TextPageStyle {
  Text name, next;
  double width = 595.275590551, height = 841.88976378;
  double left = 56.692913386, right = 56.692913386;
  double top = 56.692913386, bottom = 56.692913386;
  TextColor background{255, 255, 255};
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
  std::optional<TextColor> color;
};
struct TextLine {
  size_t paragraph = 0, begin = 0, end = 0;
  double x = 0, y = 0, baseline = 0, height = 0, width = 0;
  std::vector<TextFragment>
      fragments; // Logical source runs; shaped has visual glyph positions.
  std::shared_ptr<const struct ShapedText> shaped;
  struct Label {
    Text text;
    TextFont font;
    std::optional<TextColor> color;
    double x = 0, width = 0; // Page/frame coordinates, independent of body x.
    std::shared_ptr<const struct ShapedText> shaped;
  };
  std::optional<Label> label;
};
struct TextPage {
  TextPageStyle style;
  std::vector<TextLine> lines;
};
struct TextPagination {
  std::vector<TextPage> pages;
  std::vector<Text> unsupported;
};
// Reuse one scoped ODF style catalogue across a document's text frames.
// Like NativeTextShaper, each reader is used by one document thread.
class OdfTextReader {
public:
  explicit OdfTextReader(const OdfPackage &);
  TextFlow frame(const XmlElement &, const TextParagraph &base,
                 const Text &paragraph_style = {},
                 bool styles_source = false) const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
struct TextBoxLayout {
  std::vector<TextLine> lines;
  double height = 0;
  std::vector<Text> unsupported;
};
TextBoxLayout layout_text_box(const TextFlow &, double width,
                              const MeasureText &);
TextBoxLayout layout_text_box(const TextFlow &, double width,
                              class NativeTextShaper &);
TextFlow read_text_flow(const OdfPackage &);
TextPagination paginate_text(const TextFlow &, const MeasureText &);
TextPagination paginate_text(const TextFlow &, class NativeTextShaper &);
} // namespace DomeOffice
