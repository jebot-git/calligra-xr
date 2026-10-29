// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
enum class WordsListAction { Indent, Outdent, ExitEmpty };
void replace_words_text(OdfPackage &, size_t begin, size_t count,
                        const Text &inserted, bool paragraph_breaks);
bool change_words_list(OdfPackage &, size_t begin, size_t end, WordsListAction,
                       bool at_start_only = false);
} // namespace DomeOffice
