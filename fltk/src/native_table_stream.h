// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_cursor.h"
namespace DomeOffice {
// A connected row group with independent cell source cursors. Non-ending
// merged cells are filled once their area on this page has been established.
class NativeTableStream {
public:
  NativeTableStream(const TextFlow &, const TextBlock &, size_t begin,
                    size_t end, std::vector<size_t> cells,
                    NativeBlockLayouter &, size_t depth);
  TextCursorChunk next(double width, double height, double capacity,
                       bool force = false);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
