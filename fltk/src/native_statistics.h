// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &statistics_signatures();
Text statistics_name(const Text &);
bool statistics_includes_text(const Text &name);
NativeValue statistics_function(const Text &name,
                                const std::vector<double> &values);
} // namespace DomeOffice
