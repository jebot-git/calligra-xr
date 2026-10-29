// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
NativeValue financial_date_function(const Text &,
                                    const std::vector<NativeValue> &,
                                    const NativeCalculationSettings & = {});
} // namespace DomeOffice
