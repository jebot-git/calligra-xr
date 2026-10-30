// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
#include "native_text_shaper.h"
#include <FL/Fl_Gl_Window.H>
namespace DomeOffice {
class GpuTextCache {
public:
  GpuTextCache();
  ~GpuTextCache();
  void draw(const ShapedText &, const SceneTransform &, SceneColor,
            Fl_Gl_Window &, std::optional<TextRect> clip = {},
            bool backgrounds = true);
  void draw_tabs(const std::vector<TextTabPlacement> &, const SceneTransform &,
                 SceneColor, Fl_Gl_Window &, std::optional<TextRect> clip = {});
  void draw_paints(const std::vector<TextPaint> &, const SceneTransform &,
                   Fl_Gl_Window &);
  uint64_t rasterizations() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace DomeOffice
