// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
// Civil dates in the native workbook's common 1899-12-30 epoch. Importers
// normalize Excel epochs before these routines see a value.
std::chrono::sys_days calendar_date(const NativeValue &);
int basis_days(std::chrono::sys_days, std::chrono::sys_days, int basis);
long double basis_year_length(std::chrono::sys_days, std::chrono::sys_days,
                              int basis);
long double year_fraction(std::chrono::sys_days, std::chrono::sys_days,
                          int basis);
const NativeSignatures &calendar_signatures();
Text calendar_name(const Text &);
NativeValue calendar_function(const Text &, const std::vector<NativeValue> &);
} // namespace DomeOffice
