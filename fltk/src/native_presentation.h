// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include "native_layers.h"
#include "native_dash_pattern.h"
#include <optional>
namespace DomeOffice {
struct ScenePoint {
  double x = 0, y = 0;
};
struct SceneRect {
  double x = 0, y = 0, width = 0, height = 0;
};
struct SceneTransform {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  ScenePoint map(ScenePoint) const;
  SceneTransform
  operator*(const SceneTransform &) const; // Column vectors: this after rhs.
  std::optional<SceneTransform> inverse() const;
};
SceneTransform parse_scene_transform(const Text &);
double parse_scene_length(const Text &, double fallback = 0);
Text format_scene_transform(const SceneTransform &);
using SceneColor = TextColor;
struct ScenePaint {
  bool fill = false, stroke = false;
  bool even_odd = false;
  SceneColor fill_color{255, 255, 255}, stroke_color, text_color;
  bool use_window_text_color = false;
  double stroke_width = 1;
  double padding = 3.5;
  double padding_left = 3.5, padding_right = 3.5, padding_top = 3.5,
         padding_bottom = 3.5;
  enum VerticalAlignment { Top, Middle, Bottom } vertical_alignment = Top;
  TextFont font;
  double image_opacity =
      1; // ODF draw:image-opacity; independent of fill alpha.
  enum class Cap { Butt, Round, Square } stroke_cap = Cap::Butt;
  enum class Join { Miter, Round, Bevel, None } stroke_join = Join::Miter;
  // SVG's initial miter limit. ODF1.2 has no standard miter-limit attribute.
  static constexpr double stroke_miter_limit = 4;
  bool dashed = false;
  Text dash_name;
  TextList dash_names;
  // One base pass followed by draw:stroke-dash-names in source order. An empty
  // pattern is solid. Paint passes separately to preserve ODF opacity.
  std::vector<SceneDashPattern> stroke_patterns;
};
struct SceneContour {
  std::vector<ScenePoint> points;
  bool closed = false;
  size_t paint_group = 0;
  bool fill = true, stroke = true;
  bool stroke_zero_length =
      false; // Distinguish a segment/close from moveto only.
  // A dash beginning exactly at a vertex retains the incoming tangent, even
  // when that initial segment has zero length. Joins and square caps use it.
  // Genuinely directionless zero-length source subpaths stay axis-aligned.
  std::optional<ScenePoint> stroke_start_tangent;
};
struct ScenePath;
struct SceneHandle {
  ScenePoint
      position; // Shape-local coordinates, including geometry/handle mirrors.
  bool editable = false;
};
struct SceneShape {
  enum Kind {
    Frame,
    Rectangle,
    Ellipse,
    Polygon,
    Polyline,
    Line,
    Path,
    Custom
  } kind = Frame;
  Text name, text, image;
  std::shared_ptr<const ImageAsset> raster;
  SceneTransform transform;
  SceneTransform parent_transform; // Accumulated group coordinates.
  ScenePaint paint;
  TextFlow text_flow;
  Text paragraph_style;
  double width = 0, height = 0;
  std::vector<SceneContour> contours;
  std::shared_ptr<const ScenePath> path;
  SceneTransform path_map;
  std::optional<SceneRect> text_area;
  std::vector<SceneHandle> handles;
  XmlElement source; // Own-slide source; masters cannot be edited.
  int editor_index = -1;
  bool master = false;
  bool layer_protected = false;
  SceneRect bounds() const;
  bool hit(ScenePoint) const;
};
struct SlideScene {
  Text name, master;
  double width = 28 * 72 / 2.54, height = 15.75 * 72 / 2.54;
  SceneColor background{255, 255, 255};
  std::vector<SceneShape>
      shapes; // Master first, then slide; stable group/z order.
  std::vector<XmlElement> editable_sources;
  std::vector<size_t> editor_shapes;
  std::vector<Text> unsupported;
  XmlElement source;
  SceneLayers layers;
  bool editable(XmlElement, bool descendants = true) const;
  void require_editable(XmlElement, bool descendants = true) const;
  int hit_test(ScenePoint) const; // Shape index; masters are not editable.
};
std::vector<SlideScene>
read_presentation_scenes(const OdfPackage &,
                         std::shared_ptr<OdfTextReader> = {},
                         SceneTarget = SceneTarget::Screen);
} // namespace DomeOffice
