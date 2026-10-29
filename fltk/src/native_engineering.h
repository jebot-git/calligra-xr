// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
using NativeSignatures = std::map<Text, std::pair<size_t, size_t>>;
const NativeSignatures &engineering_signatures();
Text engineering_name(const Text &);
NativeValue engineering_function(const Text &,
                                 const std::vector<NativeValue> &);
} // namespace DomeOffice
