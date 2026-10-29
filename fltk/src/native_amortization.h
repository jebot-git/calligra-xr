// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
bool amortization_function_name(const Text &);
NativeValue amortization_function(const Text &,
                                  const std::vector<NativeValue> &);
} // namespace DomeOffice
