// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_gradient.h"
#include "native_path.h"
#include <charconv>
#include <cmath>
#include <numbers>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
double number(Text s) {
  bool ok = false;
  double n = s.toDouble(&ok);
  if (!ok || !std::isfinite(n) || std::abs(n) > 1e9)
    fail("Invalid gradient number");
  return n;
}
double percent(XmlElement n, const Text &name, double fallback) {
  if (!n.hasAttributeNS(DRAW, name))
    return fallback;
  auto s = n.attributeNS(DRAW, name).trimmed();
  if (!s.endsWith('%'))
    fail("Gradient percentages require a percent sign");
  return number(s.left(s.size() - 1)) / 100;
}
SceneColor rgb(const Text &s) {
  auto text = s.toStdString();
  unsigned n = 0;
  if (text.size() != 7 || text[0] != '#')
    fail("Invalid gradient color");
  auto r = std::from_chars(text.data() + 1, text.data() + 7, n, 16);
  if (r.ec != std::errc() || r.ptr != text.data() + 7)
    fail("Invalid gradient color");
  return {static_cast<unsigned char>(n >> 16),
          static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)};
}
void bounds(SceneRect b) {
  for (auto n : {b.x, b.y, b.width, b.height})
    if (!std::isfinite(n) || std::abs(n) > 1e9)
      fail("Gradient bounds exceed the native limit");
  if (b.width < 0 || b.height < 0)
    fail("Negative gradient bounds");
}
} // namespace
void validate_scene_gradient(const SceneGradientDefinition &g) {
  if (g.type > SceneGradientDefinition::Rectangular)
    fail("Unknown drawing gradient type");
  for (auto n : {g.border, g.start_intensity, g.end_intensity})
    if (!std::isfinite(n) || n < 0 || n > 1)
      fail("Gradient border and intensities must be between 0 and 100 percent");
  for (auto n : {g.cx, g.cy, g.angle})
    if (!std::isfinite(n) || std::abs(n) > 1e7)
      fail("Gradient position or angle exceeds the native limit");
  if (g.start.alpha != 1 || g.end.alpha != 1)
    fail("ODF color gradients use opaque endpoint colors");
}
SceneGradientDefinition read_scene_gradient(XmlElement n) {
  if (n.namespaceURI() != DRAW || n.localName() != "gradient")
    fail("Expected a drawing gradient definition");
  SceneGradientDefinition g;
  const TextList names{"linear",    "axial",  "radial",
                       "ellipsoid", "square", "rectangular"};
  auto found =
      std::find(names.begin(), names.end(), n.attributeNS(DRAW, "style"));
  if (found == names.end())
    fail("Unknown drawing gradient type");
  g.type = SceneGradientDefinition::Type(found - names.begin());
  g.start = rgb(n.attributeNS(DRAW, "start-color", "#000000"));
  g.end = rgb(n.attributeNS(DRAW, "end-color", "#ffffff"));
  g.start_intensity = percent(n, "start-intensity", 1);
  g.end_intensity = percent(n, "end-intensity", 1);
  g.border = percent(n, "border", 0);
  g.cx = percent(n, "cx", .5);
  g.cy = percent(n, "cy", .5);
  auto angle = n.attributeNS(DRAW, "angle", "0").trimmed();
  double factor = std::numbers::pi / 180;
  for (const auto &[unit, scale] :
       {std::pair{"deg", std::numbers::pi / 180},
        std::pair{"grad", std::numbers::pi / 200}, std::pair{"rad", 1.}})
    if (angle.endsWith(unit)) {
      angle = angle.left(angle.size() - Text(unit).size());
      if (angle != angle.trimmed())
        fail("Gradient angle units must follow the number immediately");
      factor = scale;
      break;
    }
  g.angle = std::remainder(number(angle) * factor, 2 * std::numbers::pi);
  validate_scene_gradient(g);
  return g;
}
uint64_t read_scene_gradient_steps(const Text &text) {
  auto s = text.trimmed().toStdString();
  if (!s.empty() && s.front() == '+')
    s.erase(s.begin()); // XML nonNegativeInteger permits an explicit plus.
  uint64_t n;
  auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (r.ec != std::errc() || r.ptr != s.data() + s.size() || (n > 0 && n < 3))
    fail("Gradient steps must be zero (automatic) or at least three");
  return n;
}
namespace {
class Field {
  const SceneGradientDefinition &g;
  SceneRect b;
  double c, s, span, extent, rx = 0, ry = 0;

public:
  Field(const SceneGradientDefinition &definition, SceneRect area)
      : g(definition), b(area), c(std::cos(g.angle)), s(std::sin(g.angle)),
        span(1 - g.border),
        extent(std::abs(s) * b.width + std::abs(c) * b.height) {
    validate_scene_gradient(g);
    bounds(b);
    if (g.type == SceneGradientDefinition::Radial)
      rx = ry = std::hypot(b.width, b.height) / 2;
    else if (g.type == SceneGradientDefinition::Ellipsoid) {
      rx = b.width / std::sqrt(2.);
      ry = b.height / std::sqrt(2.);
    } else if (g.type >= SceneGradientDefinition::Square) {
      double w = b.width, h = b.height;
      if (g.type == SceneGradientDefinition::Square)
        w = h = std::max(w, h);
      rx = (std::abs(c) * w + std::abs(s) * h) / 2;
      ry = (std::abs(s) * w + std::abs(c) * h) / 2;
    }
    rx *= span;
    ry *= span;
  }
  double parameter(ScenePoint p) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
      fail("Invalid gradient sample point");
    if (b.width == 0 || b.height == 0 || span == 0)
      return g.type == SceneGradientDefinition::Axial ? 1 : 0;
    if ((g.type <= SceneGradientDefinition::Axial && extent == 0) ||
        (g.type >= SceneGradientDefinition::Radial && (rx == 0 || ry == 0)))
      return 0; // Underflowed, invisible axes must not produce NaN colors.
    double t;
    if (g.type <= SceneGradientDefinition::Axial) {
      const double v =
          (s * (p.x - b.x - b.width / 2) + c * (p.y - b.y - b.height / 2)) /
          extent;
      t = g.type == SceneGradientDefinition::Axial ? std::abs(2 * v) / span
                                                   : (v + .5 - g.border) / span;
    } else {
      const double dx = p.x - b.x - g.cx * b.width,
                   dy = p.y - b.y - g.cy * b.height;
      double x = c * dx - s * dy, y = s * dx + c * dy;
      if (g.type == SceneGradientDefinition::Radial) {
        x = dx;
        y = dy;
      }
      if (g.type == SceneGradientDefinition::Rectangular)
        t = std::min(rx - std::abs(x), ry - std::abs(y)) / std::min(rx, ry);
      else
        t = 1 - (g.type == SceneGradientDefinition::Square
                     ? std::max(std::abs(x) / rx, std::abs(y) / ry)
                     : std::hypot(x / rx, y / ry));
    }
    return std::clamp(t, 0., 1.);
  }
  SceneColor color(ScenePoint p, uint64_t steps) const {
    double t = parameter(p);
    if (steps) {
      if (steps < 3)
        fail("Gradient steps must be at least three");
      t = std::min(std::floor(t * double(steps)), double(steps) - 1) /
          (double(steps) - 1);
    }
    auto component = [&](unsigned a, unsigned z) {
      return static_cast<unsigned char>(
          std::clamp(std::lround(a * g.start_intensity * (1 - t) +
                                 z * g.end_intensity * t),
                     0L, 255L));
    };
    return {component(g.start.red, g.end.red),
            component(g.start.green, g.end.green),
            component(g.start.blue, g.end.blue)};
  }
};
} // namespace
double scene_gradient_parameter(const SceneGradientDefinition &g, SceneRect b,
                                ScenePoint p) {
  return Field(g, b).parameter(p);
}
SceneColor scene_gradient_color(const SceneGradientDefinition &g, SceneRect b,
                                ScenePoint p, uint64_t steps) {
  return Field(g, b).color(p, steps);
}
SceneRect scene_gradient_bounds(const SceneShape &shape) {
  if (shape.width > 0 && shape.height > 0)
    return {0, 0, shape.width, shape.height};
  if (shape.path)
    return path_bounds(*shape.path, shape.path_map);
  SceneShape local = shape;
  local.transform = {};
  return local.bounds();
}
std::shared_ptr<const ImageAsset>
scene_gradient_image(const SceneGradientDefinition &g, SceneRect b,
                     const SceneTransform &device, uint64_t steps,
                     size_t pixel_limit) {
  const Field field(g, b);
  if (!b.width || !b.height)
    return {};
  if (!pixel_limit || pixel_limit > 4000000)
    fail("Invalid gradient pixel budget");
  double w = std::ceil(b.width * std::hypot(device.a, device.b)),
         h = std::ceil(b.height * std::hypot(device.c, device.d));
  if (!std::isfinite(w) || !std::isfinite(h))
    fail("Invalid gradient device transform");
  w = std::clamp(w, 1., 2048.);
  h = std::clamp(h, 1., 2048.);
  double reduction = std::min(1., std::sqrt(pixel_limit / (w * h)));
  const int width = std::min(std::max(1, int(w * reduction)), int(pixel_limit)),
            height = std::min(std::max(1, int(h * reduction)),
                              int(pixel_limit / width));
  const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3),
               size = 54 + stride * height;
  Bytes data(size, 0);
  auto put = [&](size_t at, uint32_t value, size_t count = 4) {
    for (size_t i = 0; i < count; ++i)
      data[at + i] = char(value >> (8 * i));
  };
  data[0] = 'B';
  data[1] = 'M';
  put(2, size);
  put(10, 54);
  put(14, 40);
  put(18, width);
  put(22, height);
  put(26, 1, 2);
  put(28, 24, 2);
  put(34, stride * height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      auto color = field.color({b.x + (x + .5) * b.width / width,
                                b.y + (y + .5) * b.height / height},
                               steps);
      size_t at = 54 + size_t(height - 1 - y) * stride + x * 3;
      data[at] = char(color.blue);
      data[at + 1] = char(color.green);
      data[at + 2] = char(color.red);
    }
  return make_image_asset("native-gradient.bmp", std::move(data));
}
} // namespace DomeOffice
