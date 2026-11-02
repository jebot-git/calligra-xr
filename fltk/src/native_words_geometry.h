// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_table_layout.h"
#include "native_text_shaper.h"
namespace DomeOffice {
struct RefinedAxis {
  std::vector<size_t> edges, parts;
  std::vector<double> lengths;
  size_t factor = 1;
  RefinedAxis(size_t count, size_t begin, size_t span, size_t number);
  RefinedAxis(const std::vector<double> &, size_t begin, size_t span,
              size_t number);
};
struct WordsTableGeometry {
  std::vector<double> columns, rows;
  double body_width = 0;
  double available_width = 0; // The selected table's enclosing text area.
};
using InspectTableGeometry = std::function<void(
    NativeBlockLayouter &, const NativeTableLayout &, const TextTable &)>;
void inspect_words_table_geometry(const TextFlow &,
                                  const std::vector<TextCellAddress> &,
                                  double body_width, NativeTextShaper &,
                                  const InspectTableGeometry &);
WordsTableGeometry measure_words_table_geometry(
    const TextFlow &, const std::vector<TextCellAddress> &,
    std::optional<double> body_width, NativeTextShaper &);
} // namespace DomeOffice
