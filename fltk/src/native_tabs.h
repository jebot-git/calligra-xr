// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_shaper.h"
namespace DomeOffice {
struct TabAdvance {
  TextTabStop stop;
  double position = 0, advance = 0, offset = 0;
  bool clamped = false;
};
TabAdvance next_tab(const TextParagraph &, double current, double origin,
                    double field_width, double delimiter, bool rtl);
void prepare_tab_leaders(std::vector<TextTabPlacement> &, NativeTextShaper *);
} // namespace DomeOffice
