// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &distribution_signatures();
Text distribution_name(const Text &);
NativeValue distribution_function(const Text &,
                                  const std::vector<NativeValue> &);
} // namespace DomeOffice
