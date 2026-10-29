// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
// Numeric dates are relative to this workbook, while ISO strings are civil
// dates.
std::chrono::sys_days calendar_date(const NativeValue &,
                                    const NativeCalculationSettings & = {});
int basis_days(std::chrono::sys_days, std::chrono::sys_days, int basis);
long double basis_year_length(std::chrono::sys_days, std::chrono::sys_days,
                              int basis);
long double year_fraction(std::chrono::sys_days, std::chrono::sys_days,
                          int basis);
const NativeSignatures &calendar_signatures();
Text calendar_name(const Text &);
NativeValue calendar_function(const Text &, const std::vector<NativeValue> &,
                              const NativeCalculationSettings & = {});
} // namespace DomeOffice
