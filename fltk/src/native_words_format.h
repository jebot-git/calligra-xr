// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
// Missing fields leave source formatting unchanged (or indicate mixed values
// in a selection query). Lengths are points; line spacing is a multiplier.
struct WordsCharacterFormat {
  std::optional<Text> family, color;
  std::optional<double> size;
  std::optional<int> weight;
  std::optional<bool> italic;
  bool empty() const;
  void overlay(const WordsCharacterFormat &);
  bool operator==(const WordsCharacterFormat &) const = default;
};
struct WordsParagraphFormat {
  std::optional<Text> alignment;
  std::optional<double> left, right, indent, before, after, line_factor;
  std::optional<bool> keep_next, keep_together, break_before;
  std::optional<int> orphans, widows;
  bool empty() const;
};
struct WordsFormatInfo {
  WordsCharacterFormat character;
  std::optional<bool> bold;
  WordsParagraphFormat paragraph;
};
void validate_words_format(const WordsCharacterFormat &);
void validate_words_format(const WordsParagraphFormat &);
WordsFormatInfo read_words_format(const OdfPackage &, size_t begin, size_t end);
// Apply to source paragraphs/runs, preserving unrelated styles and ZIP entries.
// A collapsed character range changes the default of an empty paragraph only;
// TextDocument holds the transient typing format for other caret positions.
bool format_words_characters(OdfPackage &, size_t begin, size_t end,
                             const WordsCharacterFormat &);
bool format_words_paragraphs(OdfPackage &, size_t begin, size_t end,
                             const WordsParagraphFormat &);
} // namespace DomeOffice
