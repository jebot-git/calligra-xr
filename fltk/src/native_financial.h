// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &financial_signatures();
Text financial_name(const Text &);
NativeValue financial_function(const Text &, const std::vector<NativeValue> &);
} // namespace DomeOffice
