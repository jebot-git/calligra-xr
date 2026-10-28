// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_page_canvas.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Gl_Window.H>
#include <FL/fl_draw.H>
#include <FL/gl.h>
#include <algorithm>
#include <cmath>
#include <deque>
namespace DomeOffice {
namespace {
struct DrawingState {
  GLint mode;
  DrawingState(int width, int height) {
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
  }
  ~DrawingState() {
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(mode);
    glPopAttrib();
  }
};
} // namespace
struct TextPageCanvas::GlyphCache {
  struct Raster {
    GLuint texture = 0;
    int width = 0, height = 0, left = 0, top = 0;
    double em = 1;
    std::shared_ptr<const NativeTypeface> face;
  };
  using Key = std::tuple<uint64_t, uint32_t, unsigned>;
  std::map<Key, Raster> entries;
  std::deque<Key> order;
  GLContext context = nullptr;
  uint64_t rasterizations = 0;
  size_t bytes = 0;
  void prepare(Fl_Gl_Window &window) {
    auto first = std::find_if(entries.begin(), entries.end(),
                              [](const auto &entry) { return entry.second.texture != 0; });
    if (context != window.context() ||
        (first != entries.end() && !glIsTexture(first->second.texture))) {
      entries.clear();
      order.clear();
      bytes = 0;
      context = window.context();
    }
  }
  const Raster &get(const ShapedGlyph &glyph, unsigned pixels) {
    Key key{glyph.face->id, glyph.glyph, pixels};
    if (auto found = entries.find(key); found != entries.end())
      return found->second;
    auto bitmap = NativeTextShaper::rasterize(*glyph.face, glyph.glyph, pixels);
    size_t size = bitmap.alpha.size();
    while (!entries.empty() && (entries.size() >= 4096 || bytes + size > 64 * 1024 * 1024)) {
      auto old = entries.find(order.front());
      if (old->second.texture)
        glDeleteTextures(1, &old->second.texture);
      bytes -= size_t(old->second.width) * old->second.height;
      entries.erase(old);
      order.pop_front();
    }
    Raster raster;
    raster.width = bitmap.width;
    raster.height = bitmap.height;
    raster.left = bitmap.left;
    raster.top = bitmap.top;
    raster.em = bitmap.pixels_per_em;
    raster.face = glyph.face;
    if (size) {
      glGenTextures(1, &raster.texture);
      glBindTexture(GL_TEXTURE_2D, raster.texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, raster.width, raster.height, 0, GL_ALPHA,
                   GL_UNSIGNED_BYTE, bitmap.alpha.data());
      glPopClientAttrib();
    }
    ++rasterizations;
    bytes += size;
    order.push_back(key);
    return entries.emplace(key, std::move(raster)).first->second;
  }
  void draw(const TextPage &page, double left, double top, double zoom, TextPageCanvas &widget) {
    auto *window = dynamic_cast<Fl_Gl_Window *>(widget.window());
    if (!window)
      return;
    prepare(*window);
    DrawingState state(window->w(), window->h());
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    gl_color(FL_FOREGROUND_COLOR);
    double dpi = double(window->pixel_w()) / window->w();
    for (const auto &line : page.lines) {
      if (top + (line.y + line.height) * zoom < widget.y() ||
          top + line.y * zoom > widget.y() + widget.h())
        continue;
      if (!line.shaped)
        throw std::runtime_error("Native page glyph positions are missing");
      for (const auto &glyph : line.shaped->glyphs) {
        unsigned pixels = std::clamp(unsigned(std::ceil(glyph.size * zoom * dpi)), 1u, 4096u);
        const auto &raster = get(glyph, pixels);
        if (!raster.texture)
          continue;
        double unit = glyph.size / raster.em * zoom;
        double x = left + (line.x + glyph.x) * zoom + raster.left * unit;
        double y = top + (line.baseline + glyph.y) * zoom + raster.top * unit;
        glBindTexture(GL_TEXTURE_2D, raster.texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2d(x, y);
        glTexCoord2f(1, 0);
        glVertex2d(x + raster.width * unit, y);
        glTexCoord2f(1, 1);
        glVertex2d(x + raster.width * unit, y + raster.height * unit);
        glTexCoord2f(0, 1);
        glVertex2d(x, y + raster.height * unit);
        glEnd();
      }
    }
  }
};
TextPageCanvas::TextPageCanvas(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), glyphs_(std::make_unique<GlyphCache>()) {}
TextPageCanvas::~TextPageCanvas() {
  if (auto *window = dynamic_cast<Fl_Gl_Window *>(this->window());
      window && glyphs_->context && window->context() == glyphs_->context) {
    window->make_current();
    for (const auto &[key, raster] : glyphs_->entries)
      if (raster.texture)
        glDeleteTextures(1, &raster.texture);
  }
}
uint64_t TextPageCanvas::rasterizations() const { return glyphs_->rasterizations; }
void TextPageCanvas::set_flow(const TextFlow &flow) {
  if (!shaper_ || shaping_revision_ != dx::font_revision) {
    shaper_ = std::make_unique<NativeTextShaper>(
        Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
    shaping_revision_ = dx::font_revision;
  }
  set_pagination(paginate_text(flow, *shaper_));
}
void TextPageCanvas::set_pagination(TextPagination value) {
  pagination = std::move(value);
  render_error_.clear();
  scroll_ = 0;
  redraw();
}
double TextPageCanvas::scale() const {
  double width = 1;
  for (const auto &page : pagination.pages)
    width = std::max(width, page.style.width);
  return std::clamp((w() - 48) / width, .05, 1.5);
}
double TextPageCanvas::extent() const {
  double height = 24, zoom = scale();
  for (const auto &page : pagination.pages)
    height += page.style.height * zoom + 24;
  return height;
}
void TextPageCanvas::scroll(double amount) {
  scroll_ = std::clamp(scroll_ + amount, 0., std::max(0., extent() - h()));
  redraw();
}
size_t TextPageCanvas::current_page() const {
  double top = 12 - scroll_, zoom = scale();
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    top += pagination.pages[i].style.height * zoom + 24;
    if (top > h() / 2.)
      return i;
  }
  return pagination.pages.empty() ? 0 : pagination.pages.size() - 1;
}
void TextPageCanvas::draw() {
  fl_push_clip(x(), y(), w(), h());
  fl_color(FL_BACKGROUND_COLOR);
  fl_rectf(x(), y(), w(), h());
  double top = y() + 12 - scroll_, zoom = scale();
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    const auto &page = pagination.pages[i];
    double left = x() + (w() - page.style.width * zoom) / 2;
    double height = page.style.height * zoom;
    if (top + height >= y() && top <= y() + h()) {
      // Session colors apply to page, text and border as well as controls.
      fl_color(FL_BACKGROUND2_COLOR);
      fl_rectf(int(left), int(top), int(page.style.width * zoom), int(height));
      fl_color(FL_INACTIVE_COLOR);
      fl_rect(int(left), int(top), int(page.style.width * zoom), int(height));
      try {
        glyphs_->draw(page, left, top, zoom, *this);
      } catch (const std::exception &error) {
        render_error_ = error.what();
        fl_font(dx::font, dx::size);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw("This page cannot be displayed yet.", x() + 24, y() + 24);
      }
      fl_font(dx::font, dx::size);
      fl_color(FL_FOREGROUND_COLOR);
      auto label = std::to_string(i + 1) + " / " + std::to_string(pagination.pages.size());
      fl_draw(label.c_str(), int(left), int(top + height + 17));
    }
    top += height + 24;
  }
  fl_pop_clip();
}
int TextPageCanvas::handle(int event) {
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_MOUSEWHEEL) {
    scroll(Fl::event_dy() * 48);
    return 1;
  }
  if (event == FL_KEYDOWN) {
    auto key = Fl::event_key();
    if (key == FL_Page_Down || key == ' ') {
      scroll(h() * .85);
      return 1;
    }
    if (key == FL_Page_Up) {
      scroll(-h() * .85);
      return 1;
    }
    if (key == FL_Down) {
      scroll(48);
      return 1;
    }
    if (key == FL_Up) {
      scroll(-48);
      return 1;
    }
    if (key == FL_Home) {
      scroll(-extent());
      return 1;
    }
    if (key == FL_End) {
      scroll(extent());
      return 1;
    }
  }
  if (event == FL_PUSH) {
    take_focus();
    if (Fl::event_clicks() && edit_paragraph) {
      double top = y() + 12 - scroll_, zoom = scale();
      for (const auto &page : pagination.pages) {
        for (const auto &line : page.lines)
          if (Fl::event_y() >= top + line.y * zoom &&
              Fl::event_y() <= top + (line.y + line.height) * zoom) {
            edit_paragraph(line.paragraph);
            return 1;
          }
        top += page.style.height * zoom + 24;
      }
    }
    return 1;
  }
  return Fl_Widget::handle(event);
}
} // namespace DomeOffice
