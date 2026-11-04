// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_image.h"
#include "native_presentation.h"
#include <FL/Fl_Gl_Window.H>
#include <span>
namespace DomeOffice {
class GpuImageCache {
public:
  GpuImageCache();
  ~GpuImageCache();
  void begin_frame(Fl_Gl_Window &);
  bool draw(const std::shared_ptr<const ImageAsset> &, const SceneTransform &,
            double width, double height, Fl_Gl_Window &,
            std::optional<TextRect> clip = {}, double opacity = 1);
  void draw_fill(const std::shared_ptr<const ImageAsset> &,
                 const SceneTransform &, SceneRect,
                 std::span<const ScenePoint> triangles, Fl_Gl_Window &,
                 double opacity = 1, bool discrete = false);
  void clear();
  uint64_t decodes() const;
  uint64_t uploads() const;
  size_t texture_bytes() const;
  const std::vector<std::string> &errors() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace DomeOffice
