// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
// Process/OS facts only. Document identity, active worksheets, view origin and
// calculation policy are supplied by SheetEngine's actual application context.
NativeValue native_host_information(const Text &category);
} // namespace DomeOffice
