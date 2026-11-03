// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &information_signatures();
Text information_name(const Text &);
NativeValue information_function(const Text &, const NativeValue &);
} // namespace DomeOffice
