// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_page_canvas.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>
#include <FL/gl.h>
#include <algorithm>
#include <cmath>
#include <deque>
namespace DomeOffice {
namespace {
Fl_Font face(const TextFont &font) {
  if (font.family.empty())
    return dx::font;
  std::string family = font.family.toStdString();
  // Unstyled document text needs a scalable face for physical page zoom.
  // Controls keep the session font; page content defaults to system monospace.
  if (family.empty())
    family = "monospace";
  if (family.size() >= 2 && (family.front() == '\'' || family.front() == '"') &&
      family.back() == family.front())
    family = family.substr(1, family.size() - 2);
  // Use a declared face only when it is available in the session font config.
  // Fontconfig's generic fallback can otherwise mix a fixed bitmap advance
  // with a scalable face's ink in the desktop's bitmap adapters.
  static std::map<std::string, bool> availability;
  static uint64_t revision = 0;
  if (revision != dx::font_revision) {
    availability.clear();
    revision = dx::font_revision;
  }
  if (!availability.contains(family) && availability.size() < 1024) {
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet *available = FcFontList(nullptr, pattern, objects);
    availability[family] = available && available->nfont > 0;
    if (available)
      FcFontSetDestroy(available);
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
  }
  bool present = availability.contains(family) && availability.at(family);
  if (!present)
    return dx::font;
  std::string name =
      family + " / " + (font.bold ? "Bold" : "Regular") + (font.italic ? " Italic" : "");
  // FLTK retains the name pointer; map keys remain alive for the process.
  static std::map<std::string, Fl_Font> fonts;
  if (auto found = fonts.find(name); found != fonts.end())
    return found->second;
  if (fonts.size() >= 1024)
    return dx::font;
  auto [it, inserted] = fonts.emplace(name, FL_FREE_FONT + 64 + fonts.size());
  Fl::set_font(it->second, it->first.c_str());
  return it->second;
}
int raster_size() { return dx::size; }
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
TextMetrics face_metrics() {
  // The session's bitmap adapters can retain requested outline metrics while
  // drawing a fixed-size strike. Measure actual ink once per face, using the
  // same FLTK surface as the cached glyph masks; never read back the GL frame.
  static std::map<Fl_Font, TextMetrics> metrics;
  static uint64_t revision = 0;
  if (revision != dx::font_revision) {
    metrics.clear();
    revision = dx::font_revision;
  }
  Fl_Font font = fl_font();
  if (auto found = metrics.find(font); found != metrics.end())
    return found->second;
  constexpr int baseline = 128;
  Fl_Image_Surface surface(512, 256);
  Fl_Surface_Device::push_current(&surface);
  fl_color(FL_WHITE);
  fl_rectf(0, 0, 512, 256);
  fl_font(font, raster_size());
  fl_color(FL_BLACK);
  fl_draw("MgÅ", 64, baseline);
  std::unique_ptr<Fl_RGB_Image> image(surface.image());
  Fl_Surface_Device::pop_current();
  fl_font(font, raster_size());
  if (!image || image->fail() || image->d() < 3)
    throw std::runtime_error("Cannot measure native page font");
  auto *data = reinterpret_cast<const unsigned char *>(image->data()[0]);
  int stride = image->ld() ? image->ld() : image->w() * image->d();
  int top = baseline, bottom = baseline;
  for (int y = 0; y < image->h(); ++y)
    for (int x = 0; x < image->w(); ++x)
      if (data[size_t(y) * stride + x * image->d()] < 250) {
        top = std::min(top, y);
        bottom = std::max(bottom, y + 1);
      }
  TextMetrics value{0, double(std::max(1, baseline - top)), double(std::max(0, bottom - baseline))};
  metrics[font] = value;
  return value;
}
} // namespace
struct TextPageCanvas::GlyphCache {
  struct Raster {
    GLuint texture = 0;
    int width = 0, height = 0, left = 0, top = 0;
    double em = 1;
  };
  std::map<std::pair<Fl_Font, std::string>, Raster> entries;
  std::deque<std::pair<Fl_Font, std::string>> order;
  GLContext context = nullptr;
  uint64_t rasterizations = 0;
  uint64_t font_revision = 0;
  void prepare(Fl_Gl_Window &window) {
    if (context != window.context() ||
        (!entries.empty() && !glIsTexture(entries.begin()->second.texture))) {
      // The old context owns its textures; destroying it already releases them.
      entries.clear();
      order.clear();
      context = window.context();
    }
    if (font_revision != dx::font_revision) {
      for (const auto &[key, raster] : entries)
        glDeleteTextures(1, &raster.texture);
      entries.clear();
      order.clear();
      font_revision = dx::font_revision;
    }
  }
  const Raster &get(const TextFragment &fragment) {
    auto key = std::make_pair(face(fragment.font), fragment.text.toStdString());
    if (auto found = entries.find(key); found != entries.end())
      return found->second;
    if (entries.size() >= 512) {
      auto old = entries.find(order.front());
      glDeleteTextures(1, &old->second.texture);
      entries.erase(old);
      order.pop_front();
    }
    fl_font(key.first, raster_size());
    int dx, dy, iw, ih;
    fl_text_extents(key.second.c_str(), dx, dy, iw, ih);
    Raster raster;
    auto metrics = face_metrics();
    raster.em = metrics.ascent + metrics.descent;
    raster.left = std::min(0, dx);
    raster.top = std::min(-int(metrics.ascent), dy);
    raster.width =
        std::max(1, std::max(int(std::ceil(fl_width(key.second.c_str()))), dx + iw) - raster.left);
    raster.height = std::max(1, std::max(int(metrics.descent), dy + ih) - raster.top);
    if (raster.width > 16384 || raster.height > 4096 ||
        size_t(raster.width) * raster.height > 16'000'000)
      throw std::runtime_error("Text fragment exceeds the page glyph-cache limit");
    Fl_Image_Surface surface(raster.width, raster.height);
    Fl_Surface_Device::push_current(&surface);
    fl_color(FL_WHITE);
    fl_rectf(0, 0, raster.width, raster.height);
    fl_font(key.first, raster_size());
    fl_color(FL_BLACK);
    fl_draw(key.second.c_str(), -raster.left, -raster.top);
    std::unique_ptr<Fl_RGB_Image> image(surface.image());
    Fl_Surface_Device::pop_current();
    if (!image || image->fail() || image->d() < 3)
      throw std::runtime_error("Cannot cache native page glyphs");
    std::vector<unsigned char> alpha(size_t(raster.width) * raster.height);
    int stride = image->ld() ? image->ld() : image->w() * image->d();
    auto *data = reinterpret_cast<const unsigned char *>(image->data()[0]);
    for (int y = 0; y < raster.height; ++y)
      for (int x = 0; x < raster.width; ++x)
        alpha[size_t(y) * raster.width + x] = 255 - data[size_t(y) * stride + x * image->d()];
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
                 GL_UNSIGNED_BYTE, alpha.data());
    glPopClientAttrib();
    ++rasterizations;
    order.push_back(key);
    return entries.emplace(std::move(key), raster).first->second;
  }
  void draw(const TextPage &page, double left, double top, double zoom, TextPageCanvas &widget) {
    auto *window = dynamic_cast<Fl_Gl_Window *>(widget.window());
    if (!window)
      return;
    prepare(*window);
    DrawingState state(window->w(), window->h());
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    gl_color(FL_FOREGROUND_COLOR);
    for (const auto &line : page.lines) {
      if (top + (line.y + line.height) * zoom < widget.y() ||
          top + line.y * zoom > widget.y() + widget.h())
        continue;
      for (const auto &fragment : line.fragments) {
        const auto &raster = get(fragment);
        double unit = fragment.font.size / raster.em * zoom;
        double x = left + (line.x + fragment.x) * zoom + raster.left * unit;
        double y = top + line.baseline * zoom + raster.top * unit;
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
      glDeleteTextures(1, &raster.texture);
  }
}
uint64_t TextPageCanvas::rasterizations() const { return glyphs_->rasterizations; }
TextMetrics TextPageCanvas::measure(const Text &text, const TextFont &font) {
  // Normalize actual face metrics, including fixed-size session bitmap faces,
  // to document units. Drawing uses the same normalization at every zoom.
  fl_font(face(font), raster_size());
  auto metrics = face_metrics();
  double unit = font.size / (metrics.ascent + metrics.descent);
  return {fl_width(text.toUtf8().c_str()) * unit, metrics.ascent * unit, metrics.descent * unit};
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
