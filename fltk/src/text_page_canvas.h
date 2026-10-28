// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <FL/Fl_Widget.H>
namespace DomeOffice {
class TextPageCanvas : public Fl_Widget {
public:
  TextPagination pagination;
  std::function<void(size_t)> edit_paragraph;
  TextPageCanvas(int x, int y, int w, int h);
  ~TextPageCanvas() override;
  static TextMetrics measure(const Text &, const TextFont &);
  void set_pagination(TextPagination);
  size_t current_page() const;
  uint64_t rasterizations() const;
  const std::string &render_error() const { return render_error_; }
  int handle(int) override;

private:
  struct GlyphCache;
  std::unique_ptr<GlyphCache> glyphs_;
  double scroll_ = 0;
  std::string render_error_;
  double scale() const;
  double extent() const;
  void scroll(double);
  void draw() override;
};
} // namespace DomeOffice
