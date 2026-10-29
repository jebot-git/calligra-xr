// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_table_layout.h"
namespace DomeOffice {
struct TablePageFragment {
  TextBoxLayout display;
  bool advance = false, finished = false;
};
// Row boundaries remain source based when the following master changes width.
// Whole row groups use cached geometry; fragments resume independent cell
// source cursors, including merged and nested continuations.
class NativeTablePaginator {
public:
  NativeTablePaginator(const TextFlow &, const TextBlock &,
                       NativeBlockLayouter &, size_t depth = 0);
  TablePageFragment next(double width, double available, double capacity,
                         bool page_empty, bool repeat_header = true);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
