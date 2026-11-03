// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_words_format.h"
#include "native_stage_geometry.h"
#include "native_stage_layers.h"
#include "native_words_page.h"
#include "native_words_tabs.h"
#include "native_words_tables.h"
#include "native_words_story.h"
#include <FL/Fl_Window.H>
#include <functional>
namespace DomeOffice {
std::optional<NativeStageLayerEdit> stage_layers_dialog(Fl_Window &, NativeStageLayerEdit);
bool shape_geometry_dialog(
    Fl_Window &, const NativeShapeGeometry &,
    const std::function<void(const NativeShapeGeometryEdit &)> &apply);
std::optional<WordsCharacterFormat>
character_format_dialog(Fl_Window &, const WordsCharacterFormat &,
                        std::optional<bool> bold);
std::optional<WordsParagraphFormat>
paragraph_format_dialog(Fl_Window &, const WordsParagraphFormat &,
                        bool paginated = true);
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
struct WordsStoryDialogResult {
  WordsStoryAddress address;
  bool displayed = true;
};
std::optional<WordsStoryDialogResult>
page_story_dialog(Fl_Window &, const OdfPackage &, WordsStoryAddress);
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
