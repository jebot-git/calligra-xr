// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
// Missing fields leave source formatting unchanged (or indicate mixed values
// in a selection query). Lengths are points; line spacing is a multiplier.
struct WordsCharacterFormat {
  // A highlight of "transparent" explicitly clears an inherited background.
  std::optional<Text> family, color, highlight;
  std::optional<double> size;
  std::optional<int> weight;
  std::optional<bool> italic, underline, strike;
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
  void overlay(const WordsParagraphFormat &);
};
struct WordsFormatInfo {
  WordsCharacterFormat character;
  std::optional<bool> bold;
  WordsParagraphFormat paragraph;
};
struct WordsParagraphStyle {
  Text name, label, parent, next;
  std::optional<Text> outline; // Missing inherits; empty explicitly means body.
  bool hidden = false;
};
struct WordsParagraphStyleChange {
  Text name; // Empty creates a new, globally unique internal style name.
  std::optional<Text> label, parent, next, outline;
  // next=":self" means this style; outline="inherit" removes that attribute.
  WordsCharacterFormat character;
  WordsParagraphFormat paragraph;
};
std::vector<WordsParagraphStyle> words_paragraph_styles(const OdfPackage &);
std::optional<Text> words_current_paragraph_style(const OdfPackage &,
                                                  size_t begin, size_t end);
Text words_paragraph_style_reference(OdfPackage &, const Text &name);
std::optional<int> words_paragraph_style_outline(const OdfPackage &,
                                                 const Text &name);
WordsFormatInfo words_paragraph_style_format(const OdfPackage &,
                                             const Text &name);
Text define_words_paragraph_style(OdfPackage &,
                                  const WordsParagraphStyleChange &);
bool delete_words_paragraph_style(OdfPackage &, const Text &name);
bool apply_words_paragraph_style(OdfPackage &, size_t begin, size_t end,
                                 const Text &name);
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
