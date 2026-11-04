// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include "slide_canvas.h"
#include "gpu_clip.h"
#include "gpu_image.h"
#include "native_path.h"
#include "native_stroke.h"
#include "native_marker.h"
#include "native_table_layout.h"
#include "suite/fltk_appearance.h"
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
    glEnable(GL_MULTISAMPLE);
    // The page is opaque. Preserve its alpha while compositing translucent
    // shape ink, so the Wayland compositor cannot add the desktop behind it.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
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
      std::vector<std::vector<ScenePoint>> stroke_triangles;
    };
    std::vector<Geometry> groups;
    SceneRect text_area;
    TextBoxLayout text;
  };
  std::vector<Prepared> prepared;
  NativeTextShaper shaper;
  GpuTextCache glyphs;
  GpuImageCache pictures;
  Fl_Gl_Window *window = nullptr;
  std::string error;
  size_t rendered = 0;
  GLint samples = 0;
  size_t geometry_points = 0;
  double geometry_x = NAN, geometry_y = NAN;
  SceneTextStats text_stats;
  void clear_images() { pictures.clear(); }
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
      auto marker_geometry = marker_stroke_geometry(
          shape, contours,
          SceneTransform{scale_x, 0, 0, scale_y} * shape.transform);
      std::map<size_t, std::vector<SceneContour>> strokes, markers;
      for (auto &c : marker_geometry.shafts)
        strokes[c.paint_group].push_back(std::move(c));
      for (auto &c : marker_geometry.markers) {
        geometry_points += c.points.size();
        if (geometry_points > 1000000)
          throw std::runtime_error("Slide markers exceed the point limit");
        markers[c.paint_group].push_back(std::move(c));
      }
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
        if (shape.paint.stroke) {
          auto outlines = stroke_pass_outlines(
              strokes[id], shape.paint,
              SceneTransform{scale_x, 0, 0, scale_y} * shape.transform, .125,
              200000, markers[id]);
          for (const auto &outline : outlines)
            geometry.stroke_triangles.push_back(Tess::make(outline, false));
        }
        p.groups.push_back(std::move(geometry));
      }
      p.text_area =
          shape.text_area.value_or(SceneRect{0, 0, shape.width, shape.height});
      if ((!shape.text.empty() || !shape.text_flow.tables.empty()) &&
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
            if (line.label && line.label->image)
              ++text_stats.image_labels;
            if (line.label && line.label->shaped) {
              ++text_stats.labels;
              text_stats.glyphs += line.label->shaped->glyphs.size();
              text_stats.missing_glyphs += line.label->shaped->missing_glyphs;
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
        offset_text_layout(p.text, p.text_area.x + shape.paint.padding_left, y);
        text_stats.table_paints += p.text.paints.size();
        for (const auto &cell : p.text.cells) {
          const auto &r = cell.rect;
          text_stats.cells.push_back(
              {cell,
               {shape.transform.map({r.x, r.y}),
                shape.transform.map({r.x + r.width, r.y}),
                shape.transform.map({r.x + r.width, r.y + r.height}),
                shape.transform.map({r.x, r.y + r.height})}});
        }
        for (auto &line : p.text.lines) {
          for (const auto &tab : line.tabs)
            text_stats.tabs.push_back(
                {tab.source,
                 shape.transform.map({line.x + tab.x1, line.baseline}),
                 shape.transform.map({line.x + tab.x2, line.baseline}),
                 tab.position, tab.strokes.size(),
                 tab.leader ? tab.leader->glyphs.size() : 0});
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
  void draw(const SlideScene &model, const SceneTransform &screen,
            SlideCanvas &canvas) {
    window = dynamic_cast<Fl_Gl_Window *>(canvas.window());
    if (!window)
      return;
    pictures.begin_frame(*window);
    State state(*window);
    scene_color({255, 255, 255});
    quad(screen, model.width, model.height);
    scene_color(model.background);
    quad(screen, model.width, model.height);
    rendered = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    std::vector<bool> selected_flags(model.shapes.size());
    if (canvas.selection_visible)
      for (auto index : native_object_leaves(model, canvas.selected_objects))
        selected_flags.at(index) = true;
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
        if (shape.paint.stroke) {
          scene_color(shape.paint.stroke_color);
          for (const auto &pass : geometry.stroke_triangles) {
            glBegin(GL_TRIANGLES);
            for (auto p : pass)
              vertex(transform.map(p));
            glEnd();
          }
        }
      }
      if (!shape.image.empty())
        pictures.draw(shape.raster, transform, shape.width, shape.height,
                      *window, {}, shape.paint.image_opacity);
      if (!cached.text.lines.empty() || !cached.text.paints.empty()) {
        glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT);
        SceneTransform origin;
        origin.e = cached.text_area.x;
        origin.f = cached.text_area.y;
        clip_frame(transform * origin, cached.text_area.width,
                   cached.text_area.height);
        unsigned char r, g, b;
        Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
        glyphs.draw_paints(cached.text.paints, transform, *window);
        for (const auto &line : cached.text.lines)
          if (line.shaped) {
            SceneTransform baseline;
            baseline.e = line.x;
            baseline.f = line.baseline;
            glyphs.draw(*line.shaped, transform * baseline, {r, g, b}, *window,
                        relative_text_clip(line.clip, line.x, line.baseline));
            glyphs.draw_tabs(
                line.tabs, transform * baseline, {r, g, b}, *window,
                relative_text_clip(line.clip, line.x, line.baseline));
            if (line.label && line.label->shaped) {
              baseline.e = line.label->x;
              glyphs.draw(
                  *line.label->shaped, transform * baseline, {r, g, b}, *window,
                  relative_text_clip(line.clip, line.label->x, line.baseline));
            }
            if (line.label && line.label->image) {
              baseline.e = line.label->x;
              baseline.f = line.baseline + line.label->y;
              pictures.draw(line.label->image->asset, transform * baseline,
                            line.label->width, line.label->height, *window,
                            relative_text_clip(line.clip, line.label->x,
                                               line.baseline + line.label->y));
            }
          }
        glPopAttrib();
      }
      if (canvas.selection_visible && selected_flags.at(i) && !shape.master) {
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
      // Generic handles stay under custom modifier diamonds, matching their
      // hit-test priority. All handle sizes are logical screen pixels.
      for (const auto &handle : canvas.transform_handles()) {
        auto p = screen.map(handle.point);
        if (handle.stem) {
          glLineWidth(1);
          glBegin(GL_LINES);
          vertex(screen.map(*handle.stem));
          vertex(p);
          glEnd();
        }
        bool rotation = handle.kind == NativeShapeHandle::Rotate;
        auto outline = [&](GLenum mode) {
          glBegin(mode);
          if (rotation) {
            for (int j = 0; j < 24; ++j) {
              double angle = j * 2 * std::acos(-1.) / 24;
              vertex({p.x + 5 * std::cos(angle), p.y + 5 * std::sin(angle)});
            }
          } else {
            vertex({p.x - 3, p.y - 3});
            vertex({p.x + 3, p.y - 3});
            vertex({p.x + 3, p.y + 3});
            vertex({p.x - 3, p.y + 3});
          }
          glEnd();
        };
        glColor4ub(r, g, b, 255);
        outline(GL_POLYGON);
        Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
        glColor4ub(r, g, b, 255);
        glLineWidth(1);
        outline(GL_LINE_LOOP);
        Fl::get_color(FL_SELECTION_COLOR, r, g, b);
      }
      if (canvas.aggregate_selection()) {
        const auto frame = native_object_frame(model, canvas.selected_objects);
        const auto transform = screen * frame.transform;
        glColor4ub(r, g, b, 255);
        glBegin(GL_LINE_LOOP);
        vertex(transform.map({0, 0}));
        vertex(transform.map({frame.width, 0}));
        vertex(transform.map({frame.width, frame.height}));
        vertex(transform.map({0, frame.height}));
        glEnd();
      }
      for (const auto &handle : shape.handles)
        if (!canvas.aggregate_selection() && handle.editable) {
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
  selected_objects.clear();
  impl_->current = nullptr;
  impl_->clear_images();
  redraw();
}
void SlideCanvas::clear_images() { impl_->clear_images(); }
const GpuImageCache &SlideCanvas::images() const { return impl_->pictures; }
const std::string &SlideCanvas::render_error() const { return impl_->error; }
size_t SlideCanvas::rendered_shapes() const { return impl_->rendered; }
size_t SlideCanvas::geometry_points() const { return impl_->geometry_points; }
int SlideCanvas::multisample_samples() const { return impl_->samples; }
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
std::vector<SlideCanvas::TransformHandle>
SlideCanvas::transform_handles() const {
  std::vector<TransformHandle> out;
  if (!selection_visible || !adjustable || selected_shape < 0 || !scene)
    return out;
  const auto &model = scene();
  if (size_t(selected_shape) >= model.shapes.size())
    return out;
  std::optional<SceneShape> aggregate;
  if (aggregate_selection())
    aggregate = native_object_frame(model, selected_objects);
  const auto &shape = aggregate ? *aggregate : model.shapes[selected_shape];
  if (shape.master || shape.source.isNull())
    return out;
  auto geometry = native_shape_geometry(shape);
  if (aggregate) {
    geometry.width_enabled = geometry.width > 0;
    geometry.height_enabled = geometry.height > 0;
  }
  if (geometry.line) {
    if (geometry.position_enabled) {
      out.push_back({NativeShapeHandle::LineStart,
                     shape.transform.map(shape.contours.at(0).points.at(0)),
                     {}});
      out.push_back({NativeShapeHandle::LineEnd,
                     shape.transform.map(shape.contours.at(0).points.at(1)),
                     {}});
    }
    return out;
  }
  if (geometry.position_enabled && shape.transform.inverse()) {
    const std::array<ScenePoint, 8> fractions{
        {{0, 0}, {.5, 0}, {1, 0}, {1, .5}, {1, 1}, {.5, 1}, {0, 1}, {0, .5}}};
    for (size_t i = 0; i < fractions.size(); ++i) {
      const auto f = fractions[i];
      if ((f.x != .5 && !geometry.width_enabled) ||
          (f.y != .5 && !geometry.height_enabled))
        continue;
      out.push_back(
          {NativeShapeHandle(i),
           shape.transform.map({f.x * geometry.width, f.y * geometry.height}),
           {}});
    }
  }
  if (geometry.rotation_enabled && geometry.rotation) {
    const auto screen = screen_transform(model);
    auto inverse = screen.inverse();
    if (!inverse)
      return out;
    const auto transform = screen * shape.transform;
    auto top = transform.map({geometry.width / 2, 0});
    auto center = transform.map({geometry.width / 2, geometry.height / 2});
    ScenePoint outward{top.x - center.x, top.y - center.y};
    if (std::hypot(outward.x, outward.y) < 1e-12)
      outward = {-transform.c, -transform.d};
    if (std::hypot(outward.x, outward.y) < 1e-12)
      outward = {transform.b, -transform.a};
    auto length = std::hypot(outward.x, outward.y);
    if (length > 0)
      out.push_back({NativeShapeHandle::Rotate,
                     inverse->map({top.x + 24 * outward.x / length,
                                   top.y + 24 * outward.y / length}),
                     shape.transform.map({geometry.width / 2, 0})});
  }
  return out;
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
  if (event == FL_KEYDOWN && Fl::event_key() == FL_Delete && adjustable &&
      selected_shape >= 0 && remove_selected &&
      !(Fl::event_state() & (FL_CTRL | FL_ALT | FL_META | FL_SHIFT))) {
    finish_drag();
    remove_selected();
    return 1;
  }
  if (event == FL_UNFOCUS && dragging())
    finish_drag(true);
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_KEYDOWN && dragging() && Fl::event_key() == FL_Escape) {
    finish_drag(true);
    return 1;
  }
  if (event == FL_DRAG && dragging()) {
    if (drag_handle_ < 0 && !drag_moved_) {
      if (std::hypot(Fl::event_x() - drag_screen_start_.x,
                     Fl::event_y() - drag_screen_start_.y) < 4)
        return 1;
      drag_moved_ = true;
    }
    if (adjustment &&
        !adjustment(Move, drag_shape_, drag_handle_,
                    drag_screen_inverse_.map(
                        {double(Fl::event_x()), double(Fl::event_y())})))
      drag_shape_ = drag_handle_ = -1;
    redraw();
    return 1;
  }
  if (event == FL_RELEASE && dragging()) {
    if (drag_handle_ < 0 && !drag_moved_) {
      finish_drag(true);
      return 1;
    }
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
        selected(-1, false, false);
      return 1;
    }
    const auto &model = scene();
    auto inverse = screen_transform(model).inverse();
    if (inverse) {
      if (adjustable && adjustment && !aggregate_selection() &&
          !Fl::event_state(FL_CTRL) && selected_shape >= 0 &&
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
      if (adjustable && adjustment && !Fl::event_state(FL_CTRL) &&
          selected_shape >= 0) {
        const auto screen = screen_transform(model);
        for (const auto &handle : transform_handles()) {
          auto p = screen.map(handle.point);
          if (std::hypot(p.x - Fl::event_x(), p.y - Fl::event_y()) <= 8) {
            drag_shape_ = selected_shape;
            drag_handle_ = -2 - int(handle.kind);
            drag_screen_inverse_ = *inverse;
            drag_screen_start_ = {double(Fl::event_x()), double(Fl::event_y())};
            drag_moved_ = false;
            if (!adjustment(Begin, drag_shape_, drag_handle_,
                            inverse->map(drag_screen_start_)))
              drag_shape_ = drag_handle_ = -1;
            return 1;
          }
        }
      }
      int i = model.hit_test(
          inverse->map({double(Fl::event_x()), double(Fl::event_y())}));
      if (i >= 0) {
        const bool toggle = Fl::event_state(FL_SHIFT),
                   leaf = Fl::event_state(FL_CTRL);
        if (selected)
          selected(i, toggle, leaf);
        if (toggle)
          return 1;
        if (adjustable && adjustment && selected_shape >= 0) {
          drag_shape_ = selected_shape;
          drag_handle_ = -1;
          drag_screen_inverse_ = *inverse;
          drag_screen_start_ = {double(Fl::event_x()), double(Fl::event_y())};
          drag_moved_ = false;
          if (!adjustment(Begin, drag_shape_, -1,
                          inverse->map(drag_screen_start_)))
            drag_shape_ = -1;
        }
      } else if (selected)
        selected(-1, false, false);
    }
    return 1;
  }
  if (event == FL_KEYDOWN && selection_visible &&
      Fl::event_key() == FL_Escape && selected) {
    selected(-1, false, false);
    return 1;
  }
  if (event == FL_KEYDOWN && !dragging() && adjustable && adjustment &&
      selected_shape >= 0 &&
      !(Fl::event_state() & (FL_CTRL | FL_ALT | FL_META))) {
    ScenePoint delta;
    double step = Fl::event_state(FL_SHIFT) ? 10 : 1;
    switch (Fl::event_key()) {
    case FL_Left:
      delta.x = -step;
      break;
    case FL_Right:
      delta.x = step;
      break;
    case FL_Up:
      delta.y = -step;
      break;
    case FL_Down:
      delta.y = step;
      break;
    default:
      return Fl_Widget::handle(event);
    }
    if (adjustment(Begin, selected_shape, -1, {})) {
      if (adjustment(Move, selected_shape, -1, delta))
        adjustment(End, selected_shape, -1, {});
      else
        adjustment(Cancel, selected_shape, -1, {});
    }
    redraw();
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
