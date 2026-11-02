// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
struct WordsTableInsertion {
  size_t rows = 2, columns = 2;
  bool header = false;
};
struct WordsTableEdit {
  OdfPackage document;
  size_t cursor = 0;
  std::optional<TextCellAddress> cell{};
  std::vector<TextCellAddress> cells{};
};
struct WordsTableRangeEdit {
  OdfPackage document;
  size_t begin = 0, end = 0;
  std::vector<TextCellAddress> cells;
};
// Materialize only the selected displayed instance before a text transaction.
// Offsets stay within the leaf cell; the complete outer-to-inner path is
// needed. No result means that all enclosing row/cell sources are already
// independent.
std::optional<WordsTableRangeEdit>
materialize_words_table_range(const OdfPackage &,
                              const std::vector<TextCellAddress> &,
                              size_t begin, size_t end);
// Source offsets count Unicode scalars and paragraph separators. A table can
// be inserted inside a body, section or existing cell. Deletion
// resolves the nearest source table, retaining its outer containers.
WordsTableEdit insert_words_table(const OdfPackage &, size_t position,
                                  const WordsTableInsertion &);
WordsTableEdit delete_words_table(const OdfPackage &, size_t position);
WordsTableEdit delete_words_table_index(const OdfPackage &, size_t table);
enum class WordsTableGridAction {
  RowBefore,
  RowAfter,
  DeleteRow,
  ColumnBefore,
  ColumnAfter,
  DeleteColumn
};
std::vector<TextCellAddress> words_table_cells_at(const OdfPackage &,
                                                  size_t position);
std::optional<TextCellAddress> words_table_cell_at(const OdfPackage &,
                                                   size_t position);
WordsTableEdit
edit_words_table_grid(const OdfPackage &, const TextCellAddress &,
                      WordsTableGridAction,
                      const std::vector<TextCellAddress> &path = {});
// Rectangle dimensions count logical grid slots from the addressed anchor.
// Existing spans must be contained completely; split restores the existing
// grid.
WordsTableEdit
merge_words_table_cells(const OdfPackage &, const TextCellAddress &,
                        size_t rows, size_t columns,
                        const std::vector<TextCellAddress> &path = {});
WordsTableEdit
split_words_table_cell(const OdfPackage &, const TextCellAddress &,
                       const std::vector<TextCellAddress> &path = {});
// Missing properties retain the effective cell style, including row/column
// defaults.
struct WordsCellFormat {
  std::optional<TextColor> background;
  std::optional<TextCellStyle::Vertical> vertical;
  std::array<std::optional<double>, 4> padding;
  std::array<std::optional<TextTableBorder>, 4> borders;
};
TextCellStyle
words_table_cell_format(const OdfPackage &, const TextCellAddress &,
                        const std::vector<TextCellAddress> &path = {});
WordsTableEdit
format_words_table_cell(const OdfPackage &, const TextCellAddress &,
                        const WordsCellFormat &,
                        const std::vector<TextCellAddress> &path = {},
                        std::optional<size_t> position = {});
// Split one displayed cell into an explicit count of logical row/column parts.
// Existing grid boundaries remain; neighboring cells span the refined slots.
WordsTableEdit subdivide_words_table_cell(
    const OdfPackage &, const TextCellAddress &, size_t rows, size_t columns,
    const std::vector<TextCellAddress> &path = {},
    std::optional<double> body_width = {}, bool equal_physical = false,
    NativeTextShaper *shaper = nullptr);
bool words_in_table(const OdfPackage &, size_t position);
} // namespace DomeOffice
