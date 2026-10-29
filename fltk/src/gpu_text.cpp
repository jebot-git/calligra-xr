// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_text.h"
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
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_TEXTURE_BIT);
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
struct GpuTextCache::Impl {
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
    auto first =
        std::find_if(entries.begin(), entries.end(), [](const auto &entry) {
          return entry.second.texture != 0;
        });
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
    while (!entries.empty() &&
           (entries.size() >= 4096 || bytes + size > 64 * 1024 * 1024)) {
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
      glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, raster.width, raster.height, 0,
                   GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.alpha.data());
      glPopClientAttrib();
    }
    ++rasterizations;
    bytes += size;
    order.push_back(key);
    return entries.emplace(key, std::move(raster)).first->second;
  }
  Fl_Gl_Window *window = nullptr;
  ~Impl() {
    if (window && context && window->context() == context) {
      window->make_current();
      for (const auto &[key, raster] : entries)
        if (raster.texture)
          glDeleteTextures(1, &raster.texture);
    }
  }
  void draw(const ShapedText &text, const SceneTransform &transform,
            SceneColor color, Fl_Gl_Window &host) {
    window = &host;
    prepare(host);
    DrawingState state(host.w(), host.h());
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4d(color.red / 255., color.green / 255., color.blue / 255.,
              color.alpha);
    double total = transform.a * transform.a + transform.b * transform.b +
                   transform.c * transform.c + transform.d * transform.d;
    double determinant = transform.a * transform.d - transform.b * transform.c;
    double scale = std::sqrt(
        (total + std::sqrt(std::max(0., total * total -
                                            4 * determinant * determinant))) /
        2);
    double dpi = double(host.pixel_w()) / host.w();
    for (const auto &glyph : text.glyphs) {
      unsigned pixels = std::clamp(
          unsigned(std::ceil(std::clamp(glyph.size * scale * dpi, 1., 4096.))),
          1u, 4096u);
      const auto &raster = get(glyph, pixels);
      if (!raster.texture)
        continue;
      const auto paint = glyph.color.value_or(color);
      glColor4d(paint.red / 255., paint.green / 255., paint.blue / 255.,
                paint.alpha);
      double unit = glyph.size / raster.em;
      double x = glyph.x + raster.left * unit, y = glyph.y + raster.top * unit;
      auto a = transform.map({x, y}),
           b = transform.map({x + raster.width * unit, y}),
           c = transform.map(
               {x + raster.width * unit, y + raster.height * unit}),
           d = transform.map({x, y + raster.height * unit});
      glBindTexture(GL_TEXTURE_2D, raster.texture);
      glBegin(GL_QUADS);
      glTexCoord2f(0, 0);
      glVertex2d(a.x, a.y);
      glTexCoord2f(1, 0);
      glVertex2d(b.x, b.y);
      glTexCoord2f(1, 1);
      glVertex2d(c.x, c.y);
      glTexCoord2f(0, 1);
      glVertex2d(d.x, d.y);
      glEnd();
    }
  }
};
GpuTextCache::GpuTextCache() : impl_(std::make_unique<Impl>()) {}
GpuTextCache::~GpuTextCache() = default;
void GpuTextCache::draw(const ShapedText &text, const SceneTransform &transform,
                        SceneColor color, Fl_Gl_Window &window) {
  impl_->draw(text, transform, color, window);
}
void GpuTextCache::draw_tabs(const std::vector<TextTabPlacement> &tabs,
                             const SceneTransform &transform,
                             SceneColor fallback, Fl_Gl_Window &host) {
  DrawingState state(host.w(), host.h());
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (const auto &tab : tabs) {
    auto color = tab.color.value_or(fallback);
    glColor4d(color.red / 255., color.green / 255., color.blue / 255.,
              color.alpha);
    glBegin(GL_QUADS);
    for (const auto &stroke : tab.strokes) {
      double dx = stroke.x2 - stroke.x1, dy = stroke.y2 - stroke.y1,
             n = std::hypot(dx, dy);
      if (n <= 0)
        continue;
      double nx = -dy / n, ny = dx / n;
      double dpi_x = double(host.pixel_w()) / host.w(),
             dpi_y = double(host.pixel_h()) / host.h();
      double tx = (transform.a * dx / n + transform.c * dy / n) * dpi_x,
             ty = (transform.b * dx / n + transform.d * dy / n) * dpi_y;
      double ax = (transform.a * nx + transform.c * ny) * dpi_x,
             ay = (transform.b * nx + transform.d * ny) * dpi_y;
      double density = std::abs(tx * ay - ty * ax) / std::hypot(tx, ty);
      if (!std::isfinite(density) || density <= 0)
        continue;
      double thickness = std::max(stroke.width, 1 / density);
      glColor4d(color.red / 255., color.green / 255., color.blue / 255.,
                color.alpha * std::min(1., stroke.width * density));
      double x = nx * thickness / 2, y = ny * thickness / 2;
      for (auto p : {ScenePoint{stroke.x1 + x, stroke.y1 + y},
                     ScenePoint{stroke.x2 + x, stroke.y2 + y},
                     ScenePoint{stroke.x2 - x, stroke.y2 - y},
                     ScenePoint{stroke.x1 - x, stroke.y1 - y}}) {
        auto screen = transform.map(p);
        glVertex2d(screen.x, screen.y);
      }
    }
    glEnd();
    if (tab.leader) {
      if (tab.stop.leader_font.color_set && !tab.stop.leader_font.color)
        color = fallback;
      impl_->draw(*tab.leader, transform, color, host);
      glDisable(GL_TEXTURE_2D);
    }
  }
}
uint64_t GpuTextCache::rasterizations() const { return impl_->rasterizations; }
} // namespace DomeOffice
