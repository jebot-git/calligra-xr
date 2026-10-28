// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
namespace DomeOffice::Formula {
Text from_excel(const Text &);
Text decode(const Text &);
Text encode(const Text &);
Text save_region(const Text &);
Text load_region(const Text &);
Text shift(const Text &, int columns, int rows);
} // namespace DomeOffice::Formula
