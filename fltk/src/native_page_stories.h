// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <array>
namespace DomeOffice {
struct TextStoryContent {
  TextFlow flow;
  std::optional<TextParagraph::Alignment> alignment;
};
struct TextPageFieldContext {
  size_t page = 1, total = 1, logical = 1;
  std::optional<size_t> previous, next;
};
struct TextPageStory {
  enum Kind { Header, Footer } kind = Header;
  enum Variant { Default, First, Left } variant = Default;
  Text master;
  Text number_format = "1";
  bool number_sync = false;
  XmlElement source;
  TextCellStyle box;
  std::array<double, 4> margins{};
  double minimum = 0;
  std::optional<double> height;
  bool displayed = true, dynamic_spacing = false;
  bool page_field = false, count_field = false;
  std::vector<Text> unsupported;
  std::function<std::vector<TextStoryContent>(const TextPageFieldContext &)>
      read;
};
struct TextPageStories {
  std::array<std::shared_ptr<const TextPageStory>, 3> headers, footers;
};
void read_page_stories(const OdfPackage &, TextFlow &);
class PageStoryLayouter {
public:
  PageStoryLayouter(const MeasureText &, NativeTextShaper *);
  void prepare(TextPage &, size_t page, size_t total, bool first,
               std::vector<Text> &unsupported,
               std::optional<size_t> previous = {},
               std::optional<size_t> next = {});

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
