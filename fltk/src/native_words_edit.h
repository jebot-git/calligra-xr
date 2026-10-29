// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
void replace_words_text(OdfPackage &, size_t begin, size_t count,
                        const Text &inserted, bool paragraph_breaks);
} // namespace DomeOffice
