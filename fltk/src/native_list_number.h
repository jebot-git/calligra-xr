// SPDX-License-Identifier: LGPL-2.0-or-later
#pragma once
#include "native_types.h"
namespace DomeOffice {
bool supported_list_number_format(const Text &);
Text format_list_number(int64_t, const Text &, bool synchronized = false);
} // namespace DomeOffice
