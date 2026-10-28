// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu_text.h"
#include <FL/Fl_Widget.H>
namespace DomeOffice {
class SlideCanvas : public Fl_Widget {
public:
  std::function<const SlideScene &()> scene;
  std::function<uint64_t()> revision;
  std::function<Bytes(const Text &)> image;
  std::function<void(int)> selected;
  int box = -1;
  SlideCanvas(int, int, int, int);
  ~SlideCanvas() override;
  void reset();
  const std::string &render_error() const;
  size_t rendered_shapes() const;
  int handle(int) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  SceneTransform screen_transform(const SlideScene &) const;
  void draw() override;
};
} // namespace DomeOffice
