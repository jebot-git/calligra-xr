// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu_image.h"
#include "gpu_text.h"
#include <FL/Fl_Widget.H>
namespace DomeOffice {
struct SceneTextStats {
  size_t lines = 0, glyphs = 0, missing_glyphs = 0, overflow_frames = 0,
         labels = 0, image_labels = 0;
  struct Tab {
    size_t source = 0;
    ScenePoint start, end;
    double position = 0;
    size_t strokes = 0, glyphs = 0;
  };
  std::vector<Tab> tabs;
  std::vector<Text> unsupported;
};
class SlideCanvas : public Fl_Widget {
public:
  std::function<const SlideScene &()> scene;
  std::function<uint64_t()> revision;
  std::function<void(int)>
      selected; // Scene shape index (including shapes without text).
  enum Adjustment { Begin, Move, End, Cancel };
  // False aborts the gesture; the controller restores its native transaction.
  std::function<bool(Adjustment, int, int, ScenePoint)> adjustment;
  int box = -1;
  int selected_shape = -1;
  bool selection_visible = true, adjustable = true;
  SlideCanvas(int, int, int, int);
  ~SlideCanvas() override;
  void reset();
  void clear_images();
  const GpuImageCache &images() const;
  const std::string &render_error() const;
  size_t rendered_shapes() const;
  size_t geometry_points() const;
  const SceneTextStats &text_stats() const;
  int handle(int) override;
  bool dragging() const { return drag_shape_ >= 0; }
  void finish_drag(bool cancel = false);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  SceneTransform screen_transform(const SlideScene &) const;
  void draw() override;
  int drag_shape_ = -1, drag_handle_ = -1;
  SceneTransform drag_screen_inverse_;
};
} // namespace DomeOffice
