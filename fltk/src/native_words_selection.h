// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <map>
namespace DomeOffice {
struct WordsTextPoint {
  size_t position = 0;
  std::vector<TextCellAddress> cells;
  bool operator==(const WordsTextPoint &) const = default;
};
struct WordsDisplayParagraph {
  size_t paragraph, begin, length;
  std::vector<TextCellAddress> cells;
};
struct WordsDisplaySpan {
  size_t first, last, begin, end;
  bool reverse = false;
};
struct WordsRangeSlice {
  size_t paragraph, begin, end;
  std::vector<TextCellAddress> cells;
  bool active = true;
};
// Document order, independent of page fragments, bidi visual order and shared
// repeated source offsets. Covered metadata is not displayed selected content.
class WordsDisplayMap {
public:
  WordsDisplayMap() = default;
  explicit WordsDisplayMap(const TextFlow &);
  WordsDisplaySpan span(const WordsTextPoint &, const WordsTextPoint &) const;
  std::vector<WordsRangeSlice> slices(const WordsDisplaySpan &) const;
  std::optional<std::pair<size_t, size_t>>
  portion(size_t paragraph, const std::vector<TextCellAddress> &,
          const WordsDisplaySpan &) const;
  const std::vector<WordsDisplayParagraph> &paragraphs() const {
    return paragraphs_;
  }

private:
  using Key = std::pair<size_t, std::vector<std::array<size_t, 5>>>;
  static Key key(size_t, const std::vector<TextCellAddress> &);
  size_t locate(const WordsTextPoint &) const;
  std::vector<WordsDisplayParagraph> paragraphs_;
  std::map<Key, size_t> indices_;
};
struct WordsSelectionEdit {
  OdfPackage document;
  WordsTextPoint anchor, cursor;
  std::vector<std::pair<size_t, size_t>> ranges;
};
// Make the selected displayed paragraph instances independent before querying
// or formatting them. Discard this candidate when an operation is a no-op.
WordsSelectionEdit materialize_words_selection(const OdfPackage &,
                                               const WordsTextPoint &anchor,
                                               const WordsTextPoint &cursor);
} // namespace DomeOffice
