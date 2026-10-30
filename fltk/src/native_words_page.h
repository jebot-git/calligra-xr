// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
struct WordsPageInfo {
  Text name, label;
  TextPageStyle geometry;
};
struct WordsPageFormat {
  // Dimensions and margins are points. Missing fields preserve source values.
  std::optional<double> width, height, left, right, top, bottom;
  bool empty() const;
};
std::vector<WordsPageInfo> words_page_styles(const OdfPackage &);
WordsPageInfo words_page_style(const OdfPackage &, const Text &name);
// Edits one master; a shared layout is copied before changing it. Empty name
// addresses the implicit default when no master exists. The update is atomic.
bool format_words_page(OdfPackage &, const Text &name, const WordsPageFormat &);
} // namespace DomeOffice
