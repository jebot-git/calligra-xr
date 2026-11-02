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
bool words_in_table(const OdfPackage &, size_t position);
} // namespace DomeOffice
