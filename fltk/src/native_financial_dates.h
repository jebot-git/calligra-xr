// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
std::chrono::sys_days financial_date(const NativeValue &);
NativeValue financial_date_function(const Text &,
                                    const std::vector<NativeValue> &);
} // namespace DomeOffice
