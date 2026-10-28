// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
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
};
struct SceneContour {
  std::vector<ScenePoint> points;
  bool closed = false;
};
struct ScenePath;
struct SceneShape {
  enum Kind { Frame, Rectangle, Ellipse, Polygon, Polyline, Line, Path } kind = Frame;
  Text name, text, image;
  SceneTransform transform;
  ScenePaint paint;
  TextFlow text_flow;
  Text paragraph_style;
  double width = 0, height = 0;
  std::vector<SceneContour> contours;
  std::shared_ptr<const ScenePath> path;
  SceneTransform path_map;
  int editor_index = -1;
  bool master = false;
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
  int hit_test(ScenePoint) const; // Shape index; masters are not editable.
};
std::vector<SlideScene>
read_presentation_scenes(const OdfPackage &,
                         std::shared_ptr<OdfTextReader> = {});
} // namespace DomeOffice
