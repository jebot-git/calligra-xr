// SPDX-License-Identifier: GPL-2.0-or-later
#include "slide_canvas.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/fl_draw.H>
#include <FL/gl.h>
#include <GL/glu.h>
#include <array>
#include <cmath>
#include <deque>
namespace DomeOffice {
namespace {
struct State {
  GLint mode;
  State(Fl_Gl_Window &window) {
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_TEXTURE_BIT | GL_TRANSFORM_BIT | GL_SCISSOR_BIT |
                 GL_LINE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, window.w(), window.h(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  ~State() {
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glMatrixMode(mode);
  }
};
void scene_color(SceneColor c) {
  glColor4d(c.red / 255., c.green / 255., c.blue / 255., c.alpha);
}
void vertex(ScenePoint p) { glVertex2d(p.x, p.y); }
void quad(const SceneTransform &m, double width, double height) {
  glBegin(GL_QUADS);
  vertex(m.map({0, 0}));
  vertex(m.map({width, 0}));
  vertex(m.map({width, height}));
  vertex(m.map({0, height}));
  glEnd();
}
struct Tess {
  std::vector<ScenePoint> triangles, batch;
  std::deque<std::array<GLdouble, 3>> vertices;
  GLenum mode = 0, error = 0;
  static void begin(GLenum mode, void *data) {
    auto &t = *static_cast<Tess *>(data);
    t.mode = mode;
    t.batch.clear();
  }
  static void point(void *point, void *data) {
    auto &t = *static_cast<Tess *>(data);
    auto p = static_cast<GLdouble *>(point);
    if (t.triangles.size() > 300000) {
      t.error = GLU_TESS_ERROR8;
      return;
    }
    t.batch.push_back({p[0], p[1]});
    auto n = t.batch.size();
    if (t.mode == GL_TRIANGLES && n % 3 == 0)
      t.triangles.insert(t.triangles.end(), t.batch.end() - 3, t.batch.end());
    else if (t.mode == GL_TRIANGLE_FAN && n >= 3)
      t.triangles.insert(t.triangles.end(),
                         {t.batch[0], t.batch[n - 2], t.batch[n - 1]});
    else if (t.mode == GL_TRIANGLE_STRIP && n >= 3)
      t.triangles.insert(t.triangles.end(),
                         {t.batch[n - 3], t.batch[n - 2], t.batch[n - 1]});
  }
  static void combine(GLdouble coords[3], void *[4], GLfloat[4], void **out,
                      void *data) {
    auto &t = *static_cast<Tess *>(data);
    t.vertices.push_back({coords[0], coords[1], 0});
    *out = t.vertices.back().data();
  }
  static void failed(GLenum code, void *data) {
    static_cast<Tess *>(data)->error = code;
  }
  static std::vector<ScenePoint> make(const SceneShape &shape) {
    Tess data;
    std::unique_ptr<GLUtesselator, decltype(&gluDeleteTess)> tess(
        gluNewTess(), gluDeleteTess);
    if (!tess)
      throw std::runtime_error("Cannot initialize slide geometry");
    gluTessCallback(tess.get(), GLU_TESS_BEGIN_DATA,
                    reinterpret_cast<_GLUfuncptr>(begin));
    gluTessCallback(tess.get(), GLU_TESS_VERTEX_DATA,
                    reinterpret_cast<_GLUfuncptr>(point));
    gluTessCallback(tess.get(), GLU_TESS_COMBINE_DATA,
                    reinterpret_cast<_GLUfuncptr>(combine));
    gluTessCallback(tess.get(), GLU_TESS_ERROR_DATA,
                    reinterpret_cast<_GLUfuncptr>(failed));
    gluTessProperty(tess.get(), GLU_TESS_WINDING_RULE,
                    shape.paint.even_odd ? GLU_TESS_WINDING_ODD
                                         : GLU_TESS_WINDING_NONZERO);
    gluTessNormal(tess.get(), 0, 0, 1);
    gluTessBeginPolygon(tess.get(), &data);
    for (const auto &contour : shape.contours)
      if (contour.closed) {
        gluTessBeginContour(tess.get());
        for (auto p : contour.points) {
          data.vertices.push_back({p.x, p.y, 0});
          auto *v = data.vertices.back().data();
          gluTessVertex(tess.get(), v, v);
        }
        gluTessEndContour(tess.get());
      }
    gluTessEndPolygon(tess.get());
    if (data.error)
      throw std::runtime_error(
          "Slide polygon cannot be tessellated within the geometry limit");
    return std::move(data.triangles);
  }
};
void clip_frame(const SceneTransform &m, double width, double height) {
  std::array<ScenePoint, 4> points = {m.map({0, 0}), m.map({width, 0}),
                                      m.map({width, height}),
                                      m.map({0, height})};
  auto center = m.map({width / 2, height / 2});
  for (size_t i = 0; i < 4; ++i) {
    auto a = points[i], b = points[(i + 1) % 4];
    double x = -(b.y - a.y), y = b.x - a.x, c = -x * a.x - y * a.y;
    if (x * center.x + y * center.y + c < 0) {
      x = -x;
      y = -y;
      c = -c;
    }
    GLdouble plane[] = {x, y, 0, c};
    glClipPlane(GL_CLIP_PLANE0 + i, plane);
    glEnable(GL_CLIP_PLANE0 + i);
  }
}
} // namespace
struct SlideCanvas::Impl {
  const SlideScene *current = nullptr;
  uint64_t revision = UINT64_MAX, font_revision = UINT64_MAX;
  struct Prepared {
    std::vector<ScenePoint> triangles;
    TextBoxLayout text;
  };
  std::vector<Prepared> prepared;
  NativeTextShaper shaper;
  GpuTextCache glyphs;
  struct Picture {
    GLuint texture = 0;
    size_t bytes = 0;
  };
  std::map<Text, Picture> pictures;
  size_t picture_bytes = 0;
  GLContext context = nullptr;
  Fl_Gl_Window *window = nullptr;
  std::string error;
  size_t rendered = 0;
  SceneTextStats text_stats;
  void clear_images() {
    if (window && context && window->context() == context) {
      window->make_current();
      for (auto &[key, image] : pictures)
        if (image.texture)
          glDeleteTextures(1, &image.texture);
    }
    pictures.clear();
    picture_bytes = 0;
  }
  ~Impl() { clear_images(); }
  void prepare(const SlideScene &model, uint64_t version) {
    if (current == &model && revision == version &&
        font_revision == dx::font_revision)
      return;
    if (font_revision != dx::font_revision)
      shaper = NativeTextShaper(
          Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
    current = nullptr;
    prepared.clear();
    text_stats = {};
    error.clear();
    for (const auto &shape : model.shapes) {
      Prepared p;
      if (shape.paint.fill)
        p.triangles = Tess::make(shape);
      if (!shape.text.empty() &&
          shape.width > shape.paint.padding_left + shape.paint.padding_right &&
          shape.height > shape.paint.padding_top + shape.paint.padding_bottom) {
        p.text = layout_text_box(shape.text_flow,
                                 shape.width - shape.paint.padding_left -
                                     shape.paint.padding_right,
                                 shaper);
        if (p.text.height >
            shape.height - shape.paint.padding_top - shape.paint.padding_bottom)
          ++text_stats.overflow_frames;
        text_stats.lines += p.text.lines.size();
        for (const auto &feature : p.text.unsupported)
          if (std::find(text_stats.unsupported.begin(),
                        text_stats.unsupported.end(),
                        feature) == text_stats.unsupported.end())
            text_stats.unsupported.push_back(feature);
        for (const auto &line : p.text.lines)
          if (line.shaped) {
            text_stats.glyphs += line.shaped->glyphs.size();
            text_stats.missing_glyphs += line.shaped->missing_glyphs;
          }
        double extra =
            std::max(0., shape.height - shape.paint.padding_top -
                             shape.paint.padding_bottom - p.text.height);
        double y =
            shape.paint.padding_top +
            (shape.paint.vertical_alignment == ScenePaint::Middle   ? extra / 2
             : shape.paint.vertical_alignment == ScenePaint::Bottom ? extra
                                                                    : 0);
        for (auto &line : p.text.lines) {
          line.x += shape.paint.padding_left;
          line.y += y;
          line.baseline += y;
        }
      }
      prepared.push_back(std::move(p));
    }
    current = &model;
    revision = version;
    font_revision = dx::font_revision;
  }
  GLuint picture(const Text &name, const Bytes &bytes) {
    if (auto found = pictures.find(name); found != pictures.end())
      return found->second.texture;
    if (pictures.size() >= 4096) {
      auto old = pictures.begin();
      if (old->second.texture)
        glDeleteTextures(1, &old->second.texture);
      picture_bytes -= old->second.bytes;
      pictures.erase(old);
    }
    pictures[name] = {};
    if (bytes.empty())
      return 0;
    std::unique_ptr<Fl_RGB_Image> decoded;
    auto label = name.toUtf8();
    if (bytes.startsWith("\x89PNG"))
      decoded = std::make_unique<Fl_PNG_Image>(
          label.c_str(), reinterpret_cast<const unsigned char *>(bytes.data()),
          bytes.size());
    else if (bytes.startsWith("\xff\xd8"))
      decoded = std::make_unique<Fl_JPEG_Image>(
          label.c_str(), reinterpret_cast<const unsigned char *>(bytes.data()),
          bytes.size());
    else if (bytes.contains("<svg")) {
      auto svg = std::make_unique<Fl_SVG_Image>(label.c_str(), bytes.c_str());
      svg->normalize();
      decoded = std::move(svg);
    }
    if (!decoded || decoded->fail())
      return 0;
    int w = decoded->w(), h = decoded->h(), depth = decoded->d();
    GLint texture_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    if (w < 1 || h < 1 || depth < 1 || depth > 4 || size_t(w) * h > 16000000 ||
        w > texture_limit || h > texture_limit)
      throw std::runtime_error("Slide image exceeds the raster limit");
    auto *source = reinterpret_cast<const unsigned char *>(decoded->data()[0]);
    int stride = decoded->ld() ? decoded->ld() : w * depth;
    std::vector<unsigned char> rgba(size_t(w) * h * 4);
    while (picture_bytes + rgba.size() > 128 * 1024 * 1024 &&
           pictures.size() > 1) {
      auto old = pictures.begin();
      if (old->first == name)
        ++old;
      if (old->second.texture)
        glDeleteTextures(1, &old->second.texture);
      picture_bytes -= old->second.bytes;
      pictures.erase(old);
    }
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        auto s = source + size_t(y) * stride + x * depth;
        auto d = rgba.data() + (size_t(y) * w + x) * 4;
        d[0] = s[0];
        d[1] = depth < 3 ? s[0] : s[1];
        d[2] = depth < 3 ? s[0] : s[2];
        d[3] = depth == 2 ? s[1] : depth == 4 ? s[3] : 255;
      }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    glPopClientAttrib();
    pictures[name] = {texture, rgba.size()};
    picture_bytes += rgba.size();
    return texture;
  }
  void draw(const SlideScene &model, const SceneTransform &screen,
            SlideCanvas &canvas) {
    window = dynamic_cast<Fl_Gl_Window *>(canvas.window());
    if (!window)
      return;
    if (context != window->context()) {
      pictures.clear();
      picture_bytes = 0;
      context = window->context();
    }
    State state(*window);
    scene_color({255, 255, 255});
    quad(screen, model.width, model.height);
    scene_color(model.background);
    quad(screen, model.width, model.height);
    rendered = 0;
    // Clip both page geometry and transformed frame contents in screen space.
    double dpi_x = double(window->pixel_w()) / window->w(),
           dpi_y = double(window->pixel_h()) / window->h();
    GLint old_clip[4];
    glGetIntegerv(GL_SCISSOR_BOX, old_clip);
    int x = int(std::ceil(screen.e * dpi_x)),
        y = int(std::ceil((window->h() - screen.f - model.height * screen.d) *
                          dpi_y));
    int right = int(std::floor((screen.e + model.width * screen.a) * dpi_x)),
        top = int(std::floor((window->h() - screen.f) * dpi_y));
    if (glIsEnabled(GL_SCISSOR_TEST)) {
      x = std::max(x, old_clip[0]);
      y = std::max(y, old_clip[1]);
      right = std::min(right, old_clip[0] + old_clip[2]);
      top = std::min(top, old_clip[1] + old_clip[3]);
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, std::max(0, right - x), std::max(0, top - y));
    for (size_t i = 0; i < model.shapes.size(); ++i) {
      const auto &shape = model.shapes[i];
      const auto &cached = prepared.at(i);
      auto transform = screen * shape.transform;
      if (shape.paint.fill) {
        scene_color(shape.paint.fill_color);
        glBegin(GL_TRIANGLES);
        for (auto p : cached.triangles)
          vertex(transform.map(p));
        glEnd();
      }
      if (shape.paint.stroke && shape.paint.stroke_width > 0) {
        scene_color(shape.paint.stroke_color);
        double r = shape.paint.stroke_width / 2;
        glBegin(GL_QUADS);
        for (const auto &c : shape.contours)
          for (size_t j = 1; j < c.points.size() + (c.closed ? 1 : 0); ++j) {
            auto a = c.points[j - 1], b = c.points[j % c.points.size()];
            double dx = b.x - a.x, dy = b.y - a.y, n = std::hypot(dx, dy);
            if (n == 0)
              continue;
            double nx = -dy / n * r, ny = dx / n * r;
            vertex(transform.map({a.x + nx, a.y + ny}));
            vertex(transform.map({b.x + nx, b.y + ny}));
            vertex(transform.map({b.x - nx, b.y - ny}));
            vertex(transform.map({a.x - nx, a.y - ny}));
          }
        glEnd();
      }
      if (!shape.image.empty()) {
        auto texture = picture(
            shape.image,
            canvas.image(Url::fromPercentEncoding(shape.image.toUtf8())));
        if (texture) {
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, texture);
          glColor4d(1, 1, 1, 1);
          glBegin(GL_QUADS);
          glTexCoord2f(0, 0);
          vertex(transform.map({0, 0}));
          glTexCoord2f(1, 0);
          vertex(transform.map({shape.width, 0}));
          glTexCoord2f(1, 1);
          vertex(transform.map({shape.width, shape.height}));
          glTexCoord2f(0, 1);
          vertex(transform.map({0, shape.height}));
          glEnd();
          glDisable(GL_TEXTURE_2D);
        }
      }
      if (!cached.text.lines.empty()) {
        glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT);
        clip_frame(transform, shape.width, shape.height);
        unsigned char r, g, b;
        Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
        for (const auto &line : cached.text.lines)
          if (line.shaped) {
            SceneTransform baseline;
            baseline.e = line.x;
            baseline.f = line.baseline;
            glyphs.draw(*line.shaped, transform * baseline, {r, g, b}, *window);
          }
        glPopAttrib();
      }
      if (shape.editor_index == canvas.box && !shape.master) {
        unsigned char r, g, b;
        Fl::get_color(FL_SELECTION_COLOR, r, g, b);
        glColor4ub(r, g, b, 255);
        glLineWidth(1);
        glBegin(GL_LINE_LOOP);
        vertex(transform.map({0, 0}));
        vertex(transform.map({shape.width, 0}));
        vertex(transform.map({shape.width, shape.height}));
        vertex(transform.map({0, shape.height}));
        glEnd();
      }
      ++rendered;
    }
  }
};
SlideCanvas::SlideCanvas(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), impl_(std::make_unique<Impl>()) {}
SlideCanvas::~SlideCanvas() = default;
void SlideCanvas::reset() {
  impl_->current = nullptr;
  impl_->clear_images();
  redraw();
}
const std::string &SlideCanvas::render_error() const { return impl_->error; }
size_t SlideCanvas::rendered_shapes() const { return impl_->rendered; }
const SceneTextStats &SlideCanvas::text_stats() const {
  return impl_->text_stats;
}
SceneTransform SlideCanvas::screen_transform(const SlideScene &model) const {
  double scale = std::max(
      .001, std::min((w() - 32) / model.width, (h() - 32) / model.height));
  return {scale,
          0,
          0,
          scale,
          x() + (w() - model.width * scale) / 2,
          y() + (h() - model.height * scale) / 2};
}
void SlideCanvas::draw() {
  fl_push_clip(x(), y(), w(), h());
  fl_color(FL_BACKGROUND_COLOR);
  fl_rectf(x(), y(), w(), h());
  try {
    const auto &model = scene();
    impl_->prepare(model, revision());
    impl_->draw(model, screen_transform(model), *this);
  } catch (const std::exception &e) {
    impl_->error = e.what();
    fl_font(dx::font, dx::size);
    fl_color(FL_FOREGROUND_COLOR);
    fl_draw("This slide cannot be displayed yet.", x() + 16, y() + 24);
  }
  fl_pop_clip();
}
int SlideCanvas::handle(int event) {
  if (event == FL_PUSH) {
    const auto &model = scene();
    auto inverse = screen_transform(model).inverse();
    if (inverse) {
      int i = model.hit_test(
          inverse->map({double(Fl::event_x()), double(Fl::event_y())}));
      if (i >= 0 && model.shapes[i].editor_index >= 0 && selected)
        selected(model.shapes[i].editor_index);
    }
    return 1;
  }
  return Fl_Widget::handle(event);
}
} // namespace DomeOffice
