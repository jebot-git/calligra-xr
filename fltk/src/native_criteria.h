// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
#include <cstdint>
namespace DomeOffice {
struct CriteriaBudget {
  uint64_t remaining = 50'000'000;
  void charge(uint64_t amount);
};
class NativeCriterion {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  NativeCriterion(const NativeValue &, const NativeCriteriaSettings &,
                  CriteriaBudget &,
                  std::chrono::sys_days epoch =
                      NativeCalculationSettings().reference_date());
  ~NativeCriterion();
  NativeCriterion(NativeCriterion &&) noexcept;
  NativeCriterion &operator=(NativeCriterion &&) noexcept;
  bool matches(const NativeValue &, bool numeric_text = false) const;
};
bool conditional_function(const Text &name);
Text conditional_name(const Text &name);
const NativeSignatures &conditional_signatures();
} // namespace DomeOffice
