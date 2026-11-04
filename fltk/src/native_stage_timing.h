// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
#include <functional>
namespace DomeOffice {
struct NativeStageTimingReference {
  size_t begin, size;
  Text id;
};
struct NativeStageTimingBudget {
  size_t examined = 0, terms = 0;
};
// Locate selected syncbase/event identifiers, retaining exact surrounding
// clock, event, offset and whitespace syntax. This is not an animation
// evaluator.
std::vector<NativeStageTimingReference>
native_stage_timing_references(const Text &,
                               const std::function<bool(const Text &)> &,
                               NativeStageTimingBudget * = nullptr);
} // namespace DomeOffice
