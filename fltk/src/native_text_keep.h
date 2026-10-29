// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_table_layout.h"
namespace DomeOffice {
struct TextBlockTraits {
  double before = 0, after = 0;
  bool visible = true, keep_next = false, break_before = false,
       break_after = false;
  Text master;
};
TextBlockTraits text_block_traits(const TextFlow &, const TextBlock &);
bool text_blocks_force_break(const TextFlow &, std::span<const TextBlock>,
                             size_t depth = 0);
struct TextKeepLink {
  bool linked = false, whole_fits = false;
  double whole = 0, extra = 0;
};
// Memoized source-block measurements. Whole chains stay together when possible;
// oversized chains retain a satisfiable leading/trailing boundary instead.
class NativeKeepPlanner {
public:
  NativeKeepPlanner(const TextFlow &, NativeBlockLayouter &);
  TextKeepLink following(std::span<const TextBlock>, size_t index, double width,
                         double capacity);
  bool preceding(std::span<const TextBlock>, size_t index) const;
  double prefix(std::span<const TextBlock>, double width, double capacity);

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
// Reduce a paragraph's final fit only when its tail can share a fresh page with
// the following prefix. Explicit breaks are filtered by the keep planner.
size_t fit_kept_tail(std::span<const double>, size_t fit, double available,
                     double capacity, double extra, int widows,
                     std::vector<Text> &diagnostics);
} // namespace DomeOffice
