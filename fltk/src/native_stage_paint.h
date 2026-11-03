// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_selection.h"
namespace DomeOffice {
// Missing fields leave the corresponding source property unchanged. Values in
// info describe the effective selection; missing values are mixed.
struct NativePaintFormat {
  std::optional<Text> fill, fill_color, stroke, stroke_color;
  std::optional<double> fill_opacity, stroke_opacity, stroke_width;
  std::optional<bool> even_odd;
  bool empty() const;
};
struct NativePaintInfo {
  NativePaintFormat values;
  size_t objects = 0;
  bool editable = true;
};
} // namespace DomeOffice
