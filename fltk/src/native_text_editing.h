// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"

namespace DomeOffice {
// Scalar positions in the same paragraph-separated text used by TextDocument.
class TextEditMap {
public:
  void reset(const TextFlow &);
  size_t global(size_t paragraph, size_t offset) const;
  size_t paragraph_size(size_t paragraph) const;
  size_t paragraph_count() const { return starts_.size(); }
  size_t previous_grapheme(size_t) const;
  size_t next_grapheme(size_t) const;
  size_t previous_word(size_t) const;
  size_t next_word(size_t) const;
  std::pair<size_t, size_t> word_at(size_t) const;
  const Text &text() const { return text_; }

private:
  Text text_;
  std::vector<size_t> starts_, sizes_, graphemes_, word_starts_;
  std::vector<std::pair<size_t, size_t>> words_;
};
// Source-relative range, with optional selection of the paragraph separator.
// Rectangles use page coordinates and are intersected with the source clip.
std::vector<TextRect> text_selection_rects(const TextLine &, size_t begin,
                                           size_t end,
                                           bool paragraph_break = false);
} // namespace DomeOffice
