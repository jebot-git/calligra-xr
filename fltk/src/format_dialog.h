// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_words_format.h"
#include "native_words_page.h"
#include <FL/Fl_Window.H>
namespace DomeOffice {
std::optional<WordsCharacterFormat>
character_format_dialog(Fl_Window &, const WordsCharacterFormat &,
                        std::optional<bool> bold);
std::optional<WordsParagraphFormat>
paragraph_format_dialog(Fl_Window &, const WordsParagraphFormat &);
struct WordsStylesDialogResult {
  OdfPackage document;
  std::optional<Text> assignment;
};
std::optional<WordsStylesDialogResult>
paragraph_styles_dialog(Fl_Window &, OdfPackage, std::optional<Text> selected);
std::optional<WordsStylesDialogResult>
character_styles_dialog(Fl_Window &, OdfPackage, std::optional<Text> selected);
std::optional<OdfPackage> page_setup_dialog(Fl_Window &, OdfPackage,
                                            const Text &page_style);
} // namespace DomeOffice
