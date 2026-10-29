// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_table_layout.h"
namespace DomeOffice {
struct TextCursorChunk {
  TextBoxLayout display;
  bool finished = false, progressed = false;
};
// A source cursor, independent of wrapped line numbers and physical page
// widths. begin_page() distinguishes a growing merged-cell area on the same
// page from an actual continuation and resets explicit page-break/header state.
class NativeTextCursor {
public:
  NativeTextCursor(const TextFlow &, std::span<const TextBlock>,
                   NativeBlockLayouter &, size_t depth = 0);
  void begin_page();
  TextCursorChunk fill(double width, double height, double capacity,
                       bool force_first_line = false);
  bool finished() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
