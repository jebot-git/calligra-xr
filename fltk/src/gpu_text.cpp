// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include "gpu_text.h"
#include "gpu_clip.h"
#include "native_tabs.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Gl_Window.H>
#include <FL/fl_draw.H>
#include <FL/gl.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
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
            SceneColor color, Fl_Gl_Window &host,
            std::optional<TextRect> clip) {
    if (clip && (clip->width <= 0 || clip->height <= 0))
      return;
    window = &host;
    prepare(host);
    DrawingState state(host.w(), host.h());
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    // Compose coverage into alpha once; opaque paper must stay opaque.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
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
      double width = raster.width * unit, height = raster.height * unit;
      double left = x, top = y, right = x + width, bottom = y + height;
      if (clip) {
        left = std::max(left, clip->x);
        top = std::max(top, clip->y);
        right = std::min(right, clip->x + clip->width);
        bottom = std::min(bottom, clip->y + clip->height);
      }
      if (right <= left || bottom <= top)
        continue;
      auto a = transform.map({left, top}), b = transform.map({right, top}),
           c = transform.map({right, bottom}),
           d = transform.map({left, bottom});
      double u1 = (left - x) / width, v1 = (top - y) / height,
             u2 = (right - x) / width, v2 = (bottom - y) / height;
      glBindTexture(GL_TEXTURE_2D, raster.texture);
      glBegin(GL_QUADS);
      glTexCoord2d(u1, v1);
      glVertex2d(a.x, a.y);
      glTexCoord2d(u2, v1);
      glVertex2d(b.x, b.y);
      glTexCoord2d(u2, v2);
      glVertex2d(c.x, c.y);
      glTexCoord2d(u1, v2);
      glVertex2d(d.x, d.y);
      glEnd();
    }
  }
};
GpuTextCache::GpuTextCache() : impl_(std::make_unique<Impl>()) {}
GpuTextCache::~GpuTextCache() = default;
void GpuTextCache::draw(const ShapedText &text, const SceneTransform &transform,
                        SceneColor color, Fl_Gl_Window &window,
                        std::optional<TextRect> clip, bool backgrounds) {
  if (backgrounds)
    draw_paints(text_backgrounds(text, clip), transform, window);
  impl_->draw(text, transform, color, window, clip);
  draw_tabs(text_decoration_lines(text), transform, color, window, clip);
}
void GpuTextCache::draw_tabs(const std::vector<TextTabPlacement> &tabs,
                             const SceneTransform &transform,
                             SceneColor fallback, Fl_Gl_Window &host,
                             std::optional<TextRect> clip) {
  DrawingState state(host.w(), host.h());
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  for (const auto &tab : tabs) {
    auto color = tab.color.value_or(fallback);
    glColor4d(color.red / 255., color.green / 255., color.blue / 255.,
              color.alpha);
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
      std::array<ClipPoint, 4> points = {
          ClipPoint{stroke.x1 + x, stroke.y1 + y},
          ClipPoint{stroke.x2 + x, stroke.y2 + y},
          ClipPoint{stroke.x2 - x, stroke.y2 - y},
          ClipPoint{stroke.x1 - x, stroke.y1 - y}};
      auto polygon = clip_polygon(points, clip);
      if (polygon.size >= 3) {
        glBegin(GL_TRIANGLE_FAN);
        for (size_t i = 0; i < polygon.size; ++i) {
          auto p = transform.map({polygon.points[i].x, polygon.points[i].y});
          glVertex2d(p.x, p.y);
        }
        glEnd();
      }
    }
    if (tab.leader) {
      if (tab.stop.leader_font.color_set && !tab.stop.leader_font.color)
        color = fallback;
      impl_->draw(*tab.leader, transform, color, host, clip);
      glDisable(GL_TEXTURE_2D);
    }
  }
}
void GpuTextCache::draw_paints(const std::vector<TextPaint> &paints,
                               const SceneTransform &transform,
                               Fl_Gl_Window &host) {
  DrawingState state(host.w(), host.h());
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_TEXTURE_RECTANGLE_ARB);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  double dpi_x = double(host.pixel_w()) / host.w(),
         dpi_y = double(host.pixel_h()) / host.h();
  auto density = [&](bool horizontal) {
    double tx = (horizontal ? transform.a : transform.c) * dpi_x,
           ty = (horizontal ? transform.b : transform.d) * dpi_y,
           nx = (horizontal ? transform.c : transform.a) * dpi_x,
           ny = (horizontal ? transform.d : transform.b) * dpi_y;
    double length = std::hypot(tx, ty);
    return length > 0 ? std::abs(tx * ny - ty * nx) / length : 0.;
  };
  double horizontal = density(true), vertical = density(false);
  static const auto circle = [] {
    std::array<ClipPoint, 24> points;
    for (size_t i = 0; i < points.size(); ++i) {
      double angle = 2 * std::numbers::pi * i / points.size();
      points[i] = {std::cos(angle), std::sin(angle)};
    }
    return points;
  }();
  for (const auto &paint : paints) {
    auto r = paint.rect;
    double alpha = paint.color.alpha;
    if (r.width <= 0 || r.height <= 0 || alpha <= 0)
      continue;
    if (paint.ellipse) {
      double width = vertical > 0 ? std::max(r.width, 1 / vertical) : r.width,
             height =
                 horizontal > 0 ? std::max(r.height, 1 / horizontal) : r.height;
      alpha *= r.width * r.height / (width * height);
      r.x -= (width - r.width) / 2;
      r.y -= (height - r.height) / 2;
      r.width = width;
      r.height = height;
    } else if (paint.coverage == TextPaint::Horizontal && horizontal > 0) {
      double h = std::max(r.height, 1 / horizontal);
      alpha *= r.height / h;
      r.y -= (h - r.height) / 2;
      r.height = h;
    } else if (paint.coverage == TextPaint::Vertical && vertical > 0) {
      double w = std::max(r.width, 1 / vertical);
      alpha *= r.width / w;
      r.x -= (w - r.width) / 2;
      r.width = w;
    }
    std::array<ClipPoint, 24> points;
    size_t n = paint.ellipse ? 24 : 4;
    if (paint.ellipse)
      for (size_t i = 0; i < n; ++i)
        points[i] = {r.x + r.width * (circle[i].x + 1) / 2,
                     r.y + r.height * (circle[i].y + 1) / 2};
    else {
      points[0] = {r.x, r.y};
      points[1] = {r.x + r.width, r.y};
      points[2] = {r.x + r.width, r.y + r.height};
      points[3] = {r.x, r.y + r.height};
    }
    auto polygon =
        clip_polygon(std::span<const ClipPoint>(points.data(), n), paint.clip);
    if (polygon.size < 3)
      continue;
    glColor4d(paint.color.red / 255., paint.color.green / 255.,
              paint.color.blue / 255., alpha);
    glBegin(GL_TRIANGLE_FAN);
    for (size_t i = 0; i < polygon.size; ++i) {
      auto p = transform.map({polygon.points[i].x, polygon.points[i].y});
      glVertex2d(p.x, p.y);
    }
    glEnd();
  }
}
uint64_t GpuTextCache::rasterizations() const { return impl_->rasterizations; }
} // namespace DomeOffice
