// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_words_format.h"
#include "native_words_page.h"
#include "native_words_tabs.h"
#include "native_words_tables.h"
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
std::optional<WordsTabsFormat>
tab_stops_dialog(Fl_Window &, OdfPackage, size_t begin, size_t end,
                 const std::vector<TextCellAddress> &cells = {},
                 const std::vector<std::pair<size_t, size_t>> &ranges = {});
std::optional<WordsTableEdit> insert_table_dialog(Fl_Window &, OdfPackage,
                                                  size_t position);
std::optional<WordsTableEdit>
cell_format_dialog(Fl_Window &, OdfPackage, const TextCellAddress &,
                   const std::vector<TextCellAddress> &, size_t position);
std::optional<WordsTableEdit>
table_sizing_dialog(Fl_Window &, OdfPackage, const TextCellAddress &,
                    const std::vector<TextCellAddress> &, size_t position,
                    double body_width);
std::optional<WordsTableEdit>
merge_cells_dialog(Fl_Window &, OdfPackage, const TextCellAddress &,
                   const std::vector<TextCellAddress> &);
std::optional<WordsTableEdit>
subdivide_cell_dialog(Fl_Window &, OdfPackage, const TextCellAddress &,
                      const std::vector<TextCellAddress> &, double body_width);
} // namespace DomeOffice
