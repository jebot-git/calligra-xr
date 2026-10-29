// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include <chrono>
#include <stdexcept>

namespace DomeOffice {
// Workbook-owned calculation context. Further CalculationSettings behaviors
// must be ported here rather than using process-global evaluator state.
class NativeCalculationSettings {
  std::chrono::sys_days reference_date_ =
      std::chrono::sys_days(std::chrono::year(1899) / 12 / 30);

public:
  NativeCalculationSettings() = default;
  explicit NativeCalculationSettings(std::chrono::year_month_day date) {
    if (!date.ok() || int(date.year()) < 1 || int(date.year()) > 9999)
      throw std::invalid_argument("Invalid calculation reference date");
    reference_date_ = std::chrono::sys_days(date);
  }
  std::chrono::sys_days reference_date() const { return reference_date_; }
};
} // namespace DomeOffice
