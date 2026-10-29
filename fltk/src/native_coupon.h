// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_calendar.h"
namespace DomeOffice {
// Maturity-anchored schedule shared by coupon queries and the bond model.
// The retained Calligra extensions include frequencies dividing 12, basis 5
// (30E+/360), and explicit end-of-month control.
struct CouponPeriod {
  std::chrono::sys_days previous, next;
  int remaining = 0;
  long double days = 0, before = 0, after = 0;
};
CouponPeriod coupon_period(std::chrono::sys_days settlement,
                           std::chrono::sys_days maturity, int frequency,
                           int basis = 0, bool end_of_month = true);
bool coupon_function_name(const Text &);
NativeValue coupon_function(const Text &, const std::vector<NativeValue> &,
                            const NativeCalculationSettings & = {});
} // namespace DomeOffice
