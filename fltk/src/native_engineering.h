// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &engineering_signatures();
Text engineering_name(const Text &);
NativeValue engineering_function(const Text &,
                                 const std::vector<NativeValue> &);
} // namespace DomeOffice
