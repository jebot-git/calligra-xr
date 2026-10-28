// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
// Import cell data into a new ODS package. Never writes the source archive.
OdfPackage import_xlsx(const Text& path);
} // namespace DomeOffice
