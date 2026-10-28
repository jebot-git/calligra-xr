// SPDX-License-Identifier: GPL-2.0-or-later
#include "slide_canvas.h"
#include "native_path.h"
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
  static std::vector<ScenePoint> make(const std::vector<SceneContour> &contours,
                                      bool even_odd) {
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
                    even_odd ? GLU_TESS_WINDING_ODD : GLU_TESS_WINDING_NONZERO);
    gluTessNormal(tess.get(), 0, 0, 1);
    gluTessBeginPolygon(tess.get(), &data);
    for (const auto &contour : contours)
      if (contour.points.size() >= 3) {
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
    struct Geometry {
      std::vector<SceneContour> contours;
      std::vector<ScenePoint> triangles;
    };
    std::vector<Geometry> groups;
    SceneRect text_area;
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
  size_t geometry_points = 0;
  double geometry_x = NAN, geometry_y = NAN;
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
  void prepare(const SlideScene &model, uint64_t version, double scale_x,
               double scale_y) {
    if (current == &model && revision == version &&
        font_revision == dx::font_revision && geometry_x == scale_x &&
        geometry_y == scale_y)
      return;
    if (font_revision != dx::font_revision)
      shaper = NativeTextShaper(
          Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
    current = nullptr;
    prepared.clear();
    text_stats = {};
    geometry_points = 0;
    error.clear();
    for (const auto &shape : model.shapes) {
      Prepared p;
      auto contours =
          shape.path ? flatten_path(*shape.path, shape.path_map,
                                    SceneTransform{scale_x, 0, 0, scale_y} *
                                        shape.transform)
                     : shape.contours;
      for (const auto &contour : contours)
        geometry_points += contour.points.size();
      if (geometry_points > 1000000)
        throw std::runtime_error(
            "Slide geometry exceeds the display point limit");
      std::map<size_t, std::vector<SceneContour>> groups;
      for (auto &contour : contours)
        groups[contour.paint_group].push_back(std::move(contour));
      for (auto &[id, items] : groups) {
        Prepared::Geometry geometry;
        geometry.contours = std::move(items);
        if (shape.paint.fill) {
          std::vector<SceneContour> filled;
          for (const auto &c : geometry.contours)
            if (c.fill)
              filled.push_back(c);
          bool even_odd = shape.path && shape.path->even_odd
                              ? *shape.path->even_odd
                              : shape.paint.even_odd;
          geometry.triangles = Tess::make(filled, even_odd);
        }
        p.groups.push_back(std::move(geometry));
      }
      p.text_area =
          shape.text_area.value_or(SceneRect{0, 0, shape.width, shape.height});
      if (!shape.text.empty() &&
          p.text_area.width >
              shape.paint.padding_left + shape.paint.padding_right &&
          p.text_area.height >
              shape.paint.padding_top + shape.paint.padding_bottom) {
        p.text = layout_text_box(shape.text_flow,
                                 p.text_area.width - shape.paint.padding_left -
                                     shape.paint.padding_right,
                                 shaper);
        if (p.text.height > p.text_area.height - shape.paint.padding_top -
                                shape.paint.padding_bottom)
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
            if(line.label&&line.label->shaped) {
              ++text_stats.labels;
              text_stats.glyphs+=line.label->shaped->glyphs.size();
              text_stats.missing_glyphs+=line.label->shaped->missing_glyphs;
            }
          }
        double extra =
            std::max(0., p.text_area.height - shape.paint.padding_top -
                             shape.paint.padding_bottom - p.text.height);
        double y =
            p.text_area.y + shape.paint.padding_top +
            (shape.paint.vertical_alignment == ScenePaint::Middle   ? extra / 2
             : shape.paint.vertical_alignment == ScenePaint::Bottom ? extra
                                                                    : 0);
        for (auto &line : p.text.lines) {
          line.x += p.text_area.x + shape.paint.padding_left;
          if(line.label)line.label->x+=p.text_area.x+shape.paint.padding_left;
          line.y += y;
          line.baseline += y;
        }
      }
      prepared.push_back(std::move(p));
    }
    current = &model;
    revision = version;
    font_revision = dx::font_revision;
    geometry_x = scale_x;
    geometry_y = scale_y;
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
      for (const auto &geometry : cached.groups) {
        if (shape.paint.fill) {
          scene_color(shape.paint.fill_color);
          glBegin(GL_TRIANGLES);
          for (auto p : geometry.triangles)
            vertex(transform.map(p));
          glEnd();
        }
        if (shape.paint.stroke && shape.paint.stroke_width > 0) {
          scene_color(shape.paint.stroke_color);
          double r = shape.paint.stroke_width / 2;
          glBegin(GL_QUADS);
          for (const auto &c : geometry.contours)
            if (c.stroke)
              for (size_t j = 1; j < c.points.size() + (c.closed ? 1 : 0);
                   ++j) {
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
        SceneTransform origin;
        origin.e = cached.text_area.x;
        origin.f = cached.text_area.y;
        clip_frame(transform * origin, cached.text_area.width,
                   cached.text_area.height);
        unsigned char r, g, b;
        Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
        for (const auto &line : cached.text.lines)
          if (line.shaped) {
            SceneTransform baseline;
            baseline.e = line.x;
            baseline.f = line.baseline;
            glyphs.draw(*line.shaped, transform * baseline, {r, g, b}, *window);
            if(line.label&&line.label->shaped) {
              baseline.e=line.label->x;
              glyphs.draw(*line.label->shaped,transform*baseline,{r,g,b},*window);
            }
          }
        glPopAttrib();
      }
      if (canvas.selection_visible && int(i) == canvas.selected_shape &&
          !shape.master) {
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
    if (canvas.selection_visible && canvas.adjustable &&
        canvas.selected_shape >= 0 &&
        size_t(canvas.selected_shape) < model.shapes.size()) {
      const auto &shape = model.shapes[canvas.selected_shape];
      unsigned char r, g, b;
      Fl::get_color(FL_SELECTION_COLOR, r, g, b);
      for (const auto &handle : shape.handles)
        if (handle.editable) {
          auto p = (screen * shape.transform).map(handle.position);
          glColor4ub(r, g, b, 255);
          glBegin(GL_QUADS);
          vertex({p.x, p.y - 5});
          vertex({p.x + 5, p.y});
          vertex({p.x, p.y + 5});
          vertex({p.x - 5, p.y});
          glEnd();
          Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
          glColor4ub(r, g, b, 255);
          glLineWidth(1);
          glBegin(GL_LINE_LOOP);
          vertex({p.x, p.y - 5});
          vertex({p.x + 5, p.y});
          vertex({p.x, p.y + 5});
          vertex({p.x - 5, p.y});
          glEnd();
          Fl::get_color(FL_SELECTION_COLOR, r, g, b);
        }
    }
  }
};
SlideCanvas::SlideCanvas(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), impl_(std::make_unique<Impl>()) {}
SlideCanvas::~SlideCanvas() = default;
void SlideCanvas::reset() {
  finish_drag();
  selected_shape = -1;
  impl_->current = nullptr;
  impl_->clear_images();
  redraw();
}
const std::string &SlideCanvas::render_error() const { return impl_->error; }
size_t SlideCanvas::rendered_shapes() const { return impl_->rendered; }
size_t SlideCanvas::geometry_points() const { return impl_->geometry_points; }
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
    auto transform = screen_transform(model);
    auto host = dynamic_cast<Fl_Gl_Window *>(window());
    if (!host)
      throw std::runtime_error("Slide rendering requires an OpenGL window");
    impl_->prepare(model, revision(), transform.a * host->pixel_w() / host->w(),
                   transform.d * host->pixel_h() / host->h());
    impl_->draw(model, transform, *this);
  } catch (const std::exception &e) {
    impl_->error = e.what();
    fl_font(dx::font, dx::size);
    fl_color(FL_FOREGROUND_COLOR);
    fl_draw("This slide cannot be displayed yet.", x() + 16, y() + 24);
  }
  fl_pop_clip();
}
int SlideCanvas::handle(int event) {
  if (event == FL_UNFOCUS && dragging())
    finish_drag(true);
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_KEYDOWN && dragging() && Fl::event_key() == FL_Escape) {
    finish_drag(true);
    return 1;
  }
  if (event == FL_DRAG && dragging()) {
    if (adjustment &&
        !adjustment(Move, drag_shape_, drag_handle_,
                    drag_screen_inverse_.map(
                        {double(Fl::event_x()), double(Fl::event_y())})))
      drag_shape_ = drag_handle_ = -1;
    redraw();
    return 1;
  }
  if (event == FL_RELEASE && dragging()) {
    if (adjustment &&
        !adjustment(Move, drag_shape_, drag_handle_,
                    drag_screen_inverse_.map(
                        {double(Fl::event_x()), double(Fl::event_y())})))
      drag_shape_ = drag_handle_ = -1;
    else
      finish_drag();
    return 1;
  }
  if (event == FL_PUSH) {
    if (Fl::event_button() != FL_LEFT_MOUSE)
      return 0;
    take_focus();
    if (!selection_visible) {
      if (selected)
        selected(-1);
      return 1;
    }
    const auto &model = scene();
    auto inverse = screen_transform(model).inverse();
    if (inverse) {
      if (adjustable && adjustment && selected_shape >= 0 &&
          size_t(selected_shape) < model.shapes.size()) {
        const auto &shape = model.shapes[selected_shape];
        auto transform = screen_transform(model) * shape.transform;
        for (size_t index = 0; index < shape.handles.size(); ++index)
          if (shape.handles[index].editable) {
            auto p = transform.map(shape.handles[index].position);
            if (std::hypot(p.x - Fl::event_x(), p.y - Fl::event_y()) <= 8) {
              drag_shape_ = selected_shape;
              drag_handle_ = int(index);
              drag_screen_inverse_ = *inverse;
              if (!adjustment(Begin, drag_shape_, drag_handle_,
                              inverse->map({double(Fl::event_x()),
                                            double(Fl::event_y())})))
                drag_shape_ = drag_handle_ = -1;
              return 1;
            }
          }
      }
      int i = model.hit_test(
          inverse->map({double(Fl::event_x()), double(Fl::event_y())}));
      if (i >= 0 && selected)
        selected(i);
    }
    return 1;
  }
  return Fl_Widget::handle(event);
}
void SlideCanvas::finish_drag(bool cancel) {
  if (!dragging())
    return;
  int shape = drag_shape_, handle = drag_handle_;
  drag_shape_ = drag_handle_ = -1;
  if (adjustment)
    adjustment(cancel ? Cancel : End, shape, handle, {});
  redraw();
}
} // namespace DomeOffice
