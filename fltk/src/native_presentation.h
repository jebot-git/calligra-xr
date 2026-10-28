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
struct SceneColor {
  unsigned char red = 0, green = 0, blue = 0;
  double alpha = 1;
};
struct ScenePaint {
  bool fill = false, stroke = false;
  bool even_odd = false;
  SceneColor fill_color{255, 255, 255}, stroke_color, text_color;
  double stroke_width = 1;
  double padding = 3.5;
  TextFont font;
};
struct SceneContour {
  std::vector<ScenePoint> points;
  bool closed = false;
};
struct SceneShape {
  enum Kind { Frame, Rectangle, Ellipse, Polygon, Polyline, Line } kind = Frame;
  Text name, text, image;
  SceneTransform transform;
  ScenePaint paint;
  double width = 0, height = 0;
  std::vector<SceneContour> contours;
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
std::vector<SlideScene> read_presentation_scenes(const OdfPackage &);
} // namespace DomeOffice
