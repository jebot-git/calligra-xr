// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
struct WordsTableInsertion {
  size_t rows = 2, columns = 2;
  bool header = false;
};
struct WordsTableEdit {
  OdfPackage document;
  size_t cursor = 0;
};
// Source offsets count Unicode scalars and paragraph separators. A table can
// be inserted inside a body, section or existing cell. Deletion
// resolves the nearest source table, retaining its outer containers.
WordsTableEdit insert_words_table(const OdfPackage &, size_t position,
                                  const WordsTableInsertion &);
WordsTableEdit delete_words_table(const OdfPackage &, size_t position);
bool words_in_table(const OdfPackage &, size_t position);
} // namespace DomeOffice
