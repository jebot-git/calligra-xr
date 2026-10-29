// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu_image.h"
#include "gpu_text.h"
#include "native_text_shaper.h"
#include "native_text_editing.h"
#include "text_documents.h"
#include <FL/Fl_Widget.H>
namespace DomeOffice {
class TextPageCanvas : public Fl_Widget {
public:
  TextPagination pagination;
  std::function<TextSelection()> selection;
  std::function<void(TextSelection)> select;
  std::function<int(int)> input;
  std::function<void(size_t, size_t)> erase;
  std::function<void()> cancel_composition;
  TextPageCanvas(int x, int y, int w, int h);
  ~TextPageCanvas() override;
  void set_flow(const TextFlow &);
  void set_pagination(TextPagination);
  void reset_editing();
  void selection_changed(bool reveal = true);
  bool editing() const { return editing_; }
  double scroll_position() const { return scroll_; }
  std::optional<TextRect> caret_rect() const;
  std::optional<size_t> caret_page() const;
  size_t current_page() const;
  uint64_t rasterizations() const;
  GpuImageCache images;
  const std::string &render_error() const { return render_error_; }
  int handle(int) override;

private:
  std::unique_ptr<GpuTextCache> glyphs_;
  std::unique_ptr<NativeTextShaper> shaper_;
  uint64_t shaping_revision_ = 0;
  double scroll_ = 0;
  double page_width_ = 1;
  std::vector<double> page_offsets_;
  TextEditMap edit_map_;
  struct Stop {
    TextCaret caret;
    size_t page, line, global;
  };
  struct Line {
    size_t page, index;
    std::vector<Stop> stops;
  };
  std::vector<Line> lines_;
  std::vector<std::array<size_t, 3>> stop_index_;
  bool editing_ = false, dragging_ = false;
  int drag_x_ = 0, drag_y_ = 0;
  size_t preferred_page_ = 0, preferred_line_ = 0;
  TextCaret::Affinity affinity_ = TextCaret::Following;
  std::optional<double> preferred_x_;
  std::optional<Stop> cursor() const;
  std::optional<Stop> hit(double x, double y, bool closest = false) const;
  TextRect screen_rect(const Stop &) const;
  void choose(Stop, bool extend);
  bool navigate(int key, bool control, bool extend);
  void reveal_caret();
  void stop_drag();
  double drag_scroll_amount() const;
  static void drag_scroll(void *);
  std::string render_error_;
  double scale() const;
  double extent() const;
  void scroll(double);
  void draw() override;
};
} // namespace DomeOffice
