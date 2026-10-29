// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_table_layout.h"
namespace DomeOffice {
struct TablePageFragment {
  TextBoxLayout display;
  bool advance = false, finished = false;
};
// Row boundaries remain source based when the following master changes width.
// Cells joined by vertical spans move as a group until cell-flow splitting is
// implemented; an oversized group is retained and explicitly diagnosed.
class NativeTablePaginator {
public:
  NativeTablePaginator(const TextFlow &, const TextBlock &,
                       NativeBlockLayouter &);
  TablePageFragment next(double width, double available, double capacity,
                         bool page_empty);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
