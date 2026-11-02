// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include "native_words_selection.h"
namespace DomeOffice {
enum class WordsListAction { Indent, Outdent, ExitEmpty };
void replace_words_text(OdfPackage &, size_t begin, size_t count,
                        const Text &inserted, bool paragraph_breaks,
                        bool retain_empty_format = false);
struct WordsRangeClear {
  OdfPackage document;
  WordsTextPoint caret;
  bool changed = false;
};
// Clear text while preserving table/cell boundaries and unselected structures.
// Retain the first insertion context for the following event's input.
WordsRangeClear clear_words_selection(const OdfPackage &,
                                      const WordsTextPoint &,
                                      const WordsTextPoint &);
bool change_words_list(OdfPackage &, size_t begin, size_t end, WordsListAction,
                       bool at_start_only = false);
} // namespace DomeOffice
