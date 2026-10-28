// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_enhanced_geometry.h"
#include <charconv>
#include <numbers>
namespace DomeOffice {
namespace {
constexpr double pi = std::numbers::pi, tau = 2 * pi;
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
double finite(double n) {
  if (!std::isfinite(n))
    fail("Non-finite enhanced geometry parameter");
  return n;
}
struct Input {
  std::string text;
  size_t at = 0;
  explicit Input(const Text &s) : text(s.toStdString()) {
    if (text.size() > 1048576)
      fail("Enhanced path input exceeds the limit");
  }
  void space() {
    while (
        at < text.size() &&
        (std::isspace(static_cast<unsigned char>(text[at])) || text[at] == ','))
      ++at;
  }
  bool done() {
    space();
    return at == text.size();
  }
  double number() {
    space();
    if (at == text.size())
      fail("Incomplete enhanced geometry parameter");
    if (text[at] == '+') {
      ++at;
      if (at == text.size() || text[at] == '+' || text[at] == '-')
        fail("Invalid enhanced geometry number");
    }
    double n;
    auto p = std::from_chars(text.data() + at, text.data() + text.size(), n);
    if (p.ec != std::errc() || p.ptr == text.data() + at)
      fail("Invalid enhanced geometry number");
    at = p.ptr - text.data();
    return finite(n);
  }
  double parameter(const EnhancedEvaluator &e) {
    space();
    if (at == text.size())
      fail("Incomplete enhanced path");
    if (text[at] == '?') {
      ++at;
      auto n = e.reference_length(std::string_view(text).substr(at));
      if (!n)
        fail("Unknown enhanced path equation");
      auto name = Text::fromUtf8(text.substr(at, n));
      at += n;
      return e.reference(name);
    }
    if (text[at] == '$') {
      size_t start = at++;
      while (at < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[at])))
        ++at;
      return e.evaluate(Text::fromUtf8(text.substr(start, at - start)));
    }
    if (std::islower(static_cast<unsigned char>(text[at]))) {
      size_t start = at++;
      while (at < text.size() &&
             std::islower(static_cast<unsigned char>(text[at])))
        ++at;
      return e.evaluate(Text::fromUtf8(text.substr(start, at - start)));
    }
    return number();
  }
};
ScenePoint add(ScenePoint a, ScenePoint b) { return {a.x + b.x, a.y + b.y}; }
bool boolean(const XmlElement &e, const Text &name) {
  auto value = e.attributeNS(DRAW, name, "false");
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  fail("Invalid enhanced geometry boolean");
}
double positive(double a) {
  a = std::fmod(a, tau);
  return a < 0 ? a + tau : a;
}
struct Builder {
  ScenePath path;
  ScenePoint current{};
  bool has_current = false;
  size_t group = 0, segments = 0;
  Builder() {
    path.paints.push_back({});
    path.even_odd = true;
  }
  void check(ScenePoint p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
        std::abs(p.y) > 1e9)
      fail("Enhanced path coordinates exceed the limit");
  }
  void move(ScenePoint p) {
    check(p);
    if (path.subpaths.size() >= 16384)
      fail("Enhanced path subpaths exceed the limit");
    path.subpaths.push_back({p, {}, false, group});
    current = p;
    has_current = true;
  }
  void require() {
    if (!has_current)
      fail("Enhanced path command requires a current point");
  }
  void segment(PathSegment s) {
    require();
    check(s.end);
    if (++segments > 16384)
      fail("Enhanced path segments exceed the limit");
    if (path.subpaths.back().closed)
      move(current);
    path.subpaths.back().segments.push_back(s);
    current = s.end;
  }
  void line(ScenePoint p) { segment({PathSegment::Line, p}); }
  void close() {
    require();
    path.subpaths.back().closed = true;
    current = path.subpaths.back().start;
  }
  void end() {
    has_current = false;
    if (++group >= 4096)
      fail("Enhanced path paint sets exceed the limit");
    path.paints.push_back({});
  }
  void ellipse(ScenePoint center, double rx, double ry, double start,
               double delta, bool move_to) {
    rx = std::abs(rx);
    ry = std::abs(ry);
    finite(start);
    finite(delta);
    if (std::abs(delta) > tau * 1024)
      fail("Enhanced arc turns exceed the limit");
    auto point = [&](double t) {
      return add(center, {rx * std::cos(t), ry * std::sin(t)});
    };
    auto first = point(start);
    if (move_to)
      move(first);
    else {
      require();
      line(first);
    }
    int count = std::max(1, int(std::ceil(std::abs(delta) / (pi / 2))));
    for (int i = 1; i <= count; ++i) {
      PathSegment s;
      s.kind = PathSegment::Arc;
      s.rx = rx;
      s.ry = ry;
      s.sweep = delta > 0;
      s.end = point(start + delta * i / count);
      if (i == count && std::abs(std::remainder(delta, tau)) < 1e-13)
        s.end = first;
      segment(s);
    }
  }
};
double radial_parameter(double degrees, double rx, double ry) {
  double a = std::fmod(degrees, 360) * pi / 180;
  return std::atan2(rx * std::sin(a), ry * std::cos(a));
}
SceneRect mapped_rect(const SceneTransform &m, SceneRect r) {
  auto a = m.map({r.x, r.y}), b = m.map({r.x + r.width, r.y}),
       c = m.map({r.x + r.width, r.y + r.height}),
       d = m.map({r.x, r.y + r.height});
  for (auto p : {a, b, c, d})
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
        std::abs(p.y) > 1e9)
      fail("Enhanced text area exceeds the geometry limit");
  double left = std::min({a.x, b.x, c.x, d.x}),
         top = std::min({a.y, b.y, c.y, d.y});
  return {left, top, std::max({a.x, b.x, c.x, d.x}) - left,
          std::max({a.y, b.y, c.y, d.y}) - top};
}
} // namespace
ScenePath parse_enhanced_path(const Text &data, const EnhancedEvaluator &e) {
  Input input(data);
  Builder b;
  char command = 0;
  bool x_axis = true;
  auto number = [&] { return input.parameter(e); };
  auto point = [&] { return ScenePoint{number(), number()}; };
  while (!input.done()) {
    char c = input.text[input.at];
    bool fresh = c >= 'A' && c <= 'Z';
    if (fresh) {
      command = c;
      ++input.at;
      if (c == 'X' || c == 'Y')
        x_axis = c == 'X';
    } else if (!command)
      fail("Enhanced path command expected");
    if (command == 'M') {
      b.move(point());
      command = 'L';
      continue;
    }
    if (command == 'N') {
      b.end();
      command = 0;
      continue;
    }
    if (command == 'Z') {
      b.close();
      command = 0;
      continue;
    }
    if (command == 'F') {
      b.path.paints[b.group].fill = false;
      command = 0;
      continue;
    }
    if (command == 'S') {
      b.path.paints[b.group].stroke = false;
      command = 0;
      continue;
    }
    if (command == 'L') {
      b.line(point());
      continue;
    }
    if (command == 'C') {
      auto c1 = point(), c2 = point(), end = point();
      PathSegment s;
      s.kind = PathSegment::Cubic;
      s.control1 = c1;
      s.control2 = c2;
      s.end = end;
      b.segment(s);
      continue;
    }
    if (command == 'Q') {
      auto control = point(), end = point();
      PathSegment s;
      s.kind = PathSegment::Quadratic;
      s.control1 = control;
      s.end = end;
      b.segment(s);
      continue;
    }
    if (command == 'X' || command == 'Y') {
      b.require();
      auto end = point();
      double dx = end.x - b.current.x, dy = end.y - b.current.y;
      PathSegment s;
      s.kind = PathSegment::Arc;
      s.rx = std::abs(dx);
      s.ry = std::abs(dy);
      s.end = end;
      s.sweep = x_axis ? dx * dy > 0 : dx * dy < 0;
      b.segment(s);
      x_axis = !x_axis;
      continue;
    }
    if (command == 'A' || command == 'B' || command == 'V' || command == 'W') {
      auto p = point(), q = point(), start_vector = point(),
           end_vector = point();
      ScenePoint center{(p.x + q.x) / 2, (p.y + q.y) / 2};
      double rx = std::abs(q.x - p.x) / 2, ry = std::abs(q.y - p.y) / 2;
      auto angle = [&](ScenePoint p) {
        return std::atan2(ry ? (p.y - center.y) / ry : 0,
                          rx ? (p.x - center.x) / rx : 0);
      };
      double start = angle(start_vector), end = angle(end_vector);
      bool clockwise = command == 'V' || command == 'W';
      double delta = clockwise ? positive(end - start) : -positive(start - end);
      if (delta == 0)
        delta = clockwise ? tau : -tau;
      b.ellipse(center, rx, ry, start, delta, command == 'B' || command == 'V');
      continue;
    }
    if (command == 'T' || command == 'U') {
      auto center = point();
      double rx = std::abs(number()), ry = std::abs(number()), start = number(),
             end = number();
      double a, delta;
      if (e.context().modern_angles) {
        a = radial_parameter(start, rx, ry);
        delta = positive(radial_parameter(end, rx, ry) - a);
        if (std::abs(end - start) == 360)
          delta = tau;
      } else {
        a = -std::fmod(start, 360) * pi / 180;
        double sweep = std::fmod(end - start, 360);
        if (sweep <= 0)
          sweep += 360;
        delta = -sweep * pi / 180;
      }
      b.ellipse(center, rx, ry, a, delta, command == 'U');
      continue;
    }
    if (command == 'G') {
      b.require();
      double rx = std::abs(number()), ry = std::abs(number()), start = number(),
             sweep = number();
      double a = radial_parameter(start, rx, ry),
             end = radial_parameter(start + sweep, rx, ry);
      double turns = std::trunc(sweep / 360), remainder = std::fmod(sweep, 360);
      double delta = turns * tau +
                     (remainder >= 0 ? positive(end - a) : -positive(a - end));
      ScenePoint center{b.current.x - rx * std::cos(a),
                        b.current.y - ry * std::sin(a)};
      b.ellipse(center, rx, ry, a, delta, false);
      continue;
    }
    fail("Unknown enhanced path command");
  }
  return std::move(b.path);
}
namespace {
EnhancedContext read_context(const XmlElement &geometry, double width,
                             double height, const ScenePaint &paint,
                             bool radius_first) {
  EnhancedContext context;
  context.legacy_polar_radius_first = radius_first;
  context.width = width;
  context.height = height;
  context.has_fill = paint.fill;
  context.has_stroke = paint.stroke;
  auto version = geometry.ownerDocument()
                     .documentElement()
                     .attributeNS(OFFICE, "version", "1.4")
                     .split('.');
  if (version.size() < 2)
    fail("Invalid enhanced geometry ODF version");
  bool major_ok, minor_ok;
  int major = version[0].toInt(&major_ok), minor = version[1].toInt(&minor_ok);
  if (!major_ok || !minor_ok || major < 1 || minor < 0)
    fail("Invalid enhanced geometry ODF version");
  context.modern_angles = major > 1 || minor >= 4;
  auto view = geometry.attributeNS(SVG, "viewBox");
  if (!view.empty()) {
    Input in(view);
    context.viewbox = {in.number(), in.number(), in.number(), in.number()};
    if (!in.done())
      fail("Invalid enhanced viewBox");
  }
  auto modifiers = geometry.attributeNS(DRAW, "modifiers");
  Input values(modifiers);
  while (!values.done()) {
    if (context.modifiers.size() >= 4096)
      fail("Enhanced modifiers exceed the limit");
    context.modifiers.push_back(values.number());
  }
  for (auto eq : children(geometry, DRAW, "equation")) {
    auto name = eq.attributeNS(DRAW, "name");
    if (context.equations.contains(name))
      fail("Duplicate enhanced equation name");
    context.equations[name] = eq.attributeNS(DRAW, "formula");
  }
  for (auto [name, target] :
       std::initializer_list<std::pair<const char *, std::optional<double> *>>{
           {"path-stretchpoint-x", &context.xstretch},
           {"path-stretchpoint-y", &context.ystretch}})
    if (geometry.hasAttributeNS(DRAW, name)) {
      Input in(geometry.attributeNS(DRAW, name));
      *target = in.number();
      if (!in.done())
        fail("Invalid enhanced stretch point");
    }
  return context;
}
SceneTransform geometry_mapping(const XmlElement &geometry,
                                const EnhancedContext &context) {
  auto v = context.viewbox;
  SceneTransform mapping = {context.width / v.width,
                            0,
                            0,
                            context.height / v.height,
                            -v.x * context.width / v.width,
                            -v.y * context.height / v.height};
  SceneTransform mirror;
  if (boolean(geometry, "mirror-horizontal")) {
    mirror.a = -1;
    mirror.e = 2 * v.x + v.width;
  }
  if (boolean(geometry, "mirror-vertical")) {
    mirror.d = -1;
    mirror.f = 2 * v.y + v.height;
  }
  return mapping * mirror;
}
struct Handle {
  Text x, y, pole_x, pole_y;
  std::optional<Text> min_x, max_x, min_y, max_y;
  std::optional<size_t> modifier_x, modifier_y;
  bool polar = false;
  SceneTransform transform;
  bool editable() const {
    return (modifier_x || modifier_y) &&
           !(modifier_x && modifier_y && modifier_x == modifier_y);
  }
};
double parameter(const Text &value, const EnhancedEvaluator &e) {
  Input input(value);
  double result = input.parameter(e);
  if (!input.done())
    fail("Invalid adjustment handle parameter");
  return result;
}
std::optional<size_t> modifier(const Text &value) {
  auto bytes = value.trimmed().toUtf8();
  if (bytes.empty() || bytes.front() != '$')
    return {};
  size_t index;
  auto end = bytes.data() + bytes.size();
  auto parsed = std::from_chars(bytes.data() + 1, end, index);
  if (parsed.ec != std::errc() || parsed.ptr != end)
    fail("Invalid handle modifier");
  return index;
}
std::pair<Text, Text> pair(const Text &value) {
  std::istringstream input(value.toStdString());
  std::string x, y, extra;
  if (!(input >> x >> y) || (input >> extra))
    fail("Invalid adjustment handle pair");
  return {Text::fromUtf8(x), Text::fromUtf8(y)};
}
Handle read_handle(const XmlElement &node, const EnhancedEvaluator &e,
                   const SceneTransform &mapping) {
  Handle h;
  auto has = [&](const char *name) { return node.hasAttributeNS(DRAW, name); };
  auto get = [&](const char *name) { return node.attributeNS(DRAW, name); };
  const bool xy = has("handle-position-x") || has("handle-position-y");
  const bool polar = has("handle-polar-pole-x") || has("handle-polar-pole-y") ||
                     has("handle-polar-radius") || has("handle-polar-angle");
  if (xy && polar)
    fail("Adjustment handle mixes Cartesian and polar coordinates");
  if (xy) {
    if (!has("handle-position-x") || !has("handle-position-y"))
      fail("Incomplete XY handle");
    h.x = get("handle-position-x");
    h.y = get("handle-position-y");
  } else if (polar) {
    if (!has("handle-polar-pole-x") || !has("handle-polar-pole-y"))
      fail("Incomplete polar handle");
    h.polar = true;
    h.pole_x = get("handle-polar-pole-x");
    h.pole_y = get("handle-polar-pole-y");
    h.x = node.attributeNS(DRAW, "handle-polar-angle", "0");
    h.y = node.attributeNS(DRAW, "handle-polar-radius", "0");
  } else {
    // Compatibility with pre-1.4 files: angle precedes radius, as in Calligra.
    std::tie(h.x, h.y) = pair(get("handle-position"));
    if (has("handle-polar")) {
      h.polar = true;
      std::tie(h.pole_x, h.pole_y) = pair(get("handle-polar"));
      if (e.context().legacy_polar_radius_first)
        std::swap(h.x, h.y);
    }
  }
  parameter(h.x, e);
  parameter(h.y, e);
  h.modifier_x = modifier(h.x);
  h.modifier_y = modifier(h.y);
  if (h.polar) {
    parameter(h.pole_x, e);
    parameter(h.pole_y, e);
  }
  auto range = [&](const char *name) -> std::optional<Text> {
    if (!has(name))
      return {};
    auto value = get(name);
    parameter(value, e);
    return value;
  };
  if (h.polar) {
    h.min_y = range("handle-radius-range-minimum");
    h.max_y = range("handle-radius-range-maximum");
    if (h.max_y && parameter(*h.max_y, e) < 0)
      fail("Negative polar radius bound");
  } else {
    h.min_x = range("handle-range-x-minimum");
    h.max_x = range("handle-range-x-maximum");
    h.min_y = range("handle-range-y-minimum");
    h.max_y = range("handle-range-y-maximum");
  }
  SceneTransform extra;
  if (boolean(node, "handle-switched") &&
      e.context().height > e.context().width)
    extra = {0, 1, 1, 0, 0, 0};
  auto v = e.context().viewbox;
  SceneTransform mirror;
  // ODF names the mirror axis: horizontal changes y; vertical changes x.
  if (boolean(node, "handle-mirror-horizontal")) {
    mirror.d = -1;
    mirror.f = 2 * v.y + v.height;
  }
  if (boolean(node, "handle-mirror-vertical")) {
    mirror.a = -1;
    mirror.e = 2 * v.x + v.width;
  }
  h.transform = mapping * mirror * extra;
  return h;
}
double constrain(double value, const std::optional<Text> &min,
                 const std::optional<Text> &max, const EnhancedEvaluator &e) {
  double lo =
      min ? parameter(*min, e) : -std::numeric_limits<double>::infinity();
  double hi =
      max ? parameter(*max, e) : std::numeric_limits<double>::infinity();
  if (lo > hi)
    fail("Inverted adjustment handle range");
  return std::clamp(value, lo, hi);
}
SceneHandle handle_position(const Handle &h, const EnhancedEvaluator &e) {
  ScenePoint p{parameter(h.x, e), parameter(h.y, e)};
  constrain(p.x, h.min_x, h.max_x, e);
  constrain(p.y, h.min_y, h.max_y, e);
  if (h.polar) {
    if (p.y < 0)
      fail("Negative adjustment handle radius");
    double angle = std::remainder(p.x, 360) * pi / 180;
    p = {parameter(h.pole_x, e) + p.y * std::cos(angle),
         parameter(h.pole_y, e) + p.y * std::sin(angle)};
  }
  p = h.transform.map(p);
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    fail("Adjustment handle exceeds geometry limits");
  return {p, h.editable() && bool(h.transform.inverse())};
}
} // namespace
EnhancedGeometry read_enhanced_geometry(const XmlElement &geometry,
                                        double width, double height,
                                        const ScenePaint &paint,
                                        bool radius_first) {
  EnhancedGeometry result;
  auto context = read_context(geometry, width, height, paint, radius_first);
  EnhancedEvaluator evaluator(context);
  auto path = geometry.attributeNS(DRAW, "enhanced-path");
  result.path =
      std::make_shared<ScenePath>(parse_enhanced_path(path, evaluator));
  result.mapping = geometry_mapping(geometry, context);
  auto handles = children(geometry, DRAW, "handle");
  if (handles.size() > 4096)
    fail("Too many adjustment handles");
  for (const auto &node : handles) {
    auto h = read_handle(node, evaluator, result.mapping);
    result.handles.push_back(handle_position(h, evaluator));
    if (h.modifier_x && h.modifier_y && h.modifier_x == h.modifier_y)
      result.unsupported.push_back(
          "Handle aliases one modifier across two coordinates");
  }
  auto area = geometry.attributeNS(DRAW, "text-areas");
  if (!area.empty()) {
    Input in(area);
    std::vector<SceneRect> areas;
    while (!in.done()) {
      double x = in.parameter(evaluator), y = in.parameter(evaluator),
             right = in.parameter(evaluator), bottom = in.parameter(evaluator);
      if (right < x || bottom < y)
        result.unsupported.push_back("Reversed enhanced text area normalized");
      areas.push_back(
          mapped_rect(result.mapping, {x, y, right - x, bottom - y}));
      if (areas.size() > 2)
        fail("Enhanced geometry declares more than two text areas");
    }
    result.text_area = areas.front();
    if (areas.size() > 1)
      result.vertical_text_area = areas[1];
  }
  if (context.xstretch || context.ystretch)
    result.unsupported.push_back("Enhanced path stretch points");
  if (path.empty())
    result.unsupported.push_back(
        "Custom shape preset without explicit enhanced path");
  if (boolean(geometry, "extrusion"))
    result.unsupported.push_back("Custom shape extrusion");
  if (boolean(geometry, "text-path"))
    result.unsupported.push_back("Text on enhanced path");
  if (geometry.hasAttributeNS(DRAW, "text-rotate-angle")) {
    auto angle = geometry.attributeNS(DRAW, "text-rotate-angle").trimmed();
    if (angle != "0" && angle != "0deg" && angle != "0rad" && angle != "0grad")
      result.unsupported.push_back("Enhanced text rotation");
  }
  return result;
}
std::vector<double> adjusted_enhanced_modifiers(const XmlElement &geometry,
                                                double width, double height,
                                                const ScenePaint &paint,
                                                size_t index, ScenePoint target,
                                                bool radius_first) {
  if (!std::isfinite(target.x) || !std::isfinite(target.y))
    fail("Non-finite adjustment target");
  auto context = read_context(geometry, width, height, paint, radius_first);
  EnhancedEvaluator evaluator(context);
  auto handles = children(geometry, DRAW, "handle");
  if (index >= handles.size())
    fail("Invalid adjustment handle");
  auto h = read_handle(handles[index], evaluator,
                       geometry_mapping(geometry, context));
  auto inverse = h.transform.inverse();
  if (!h.editable() || !inverse)
    fail("This adjustment handle is not editable");
  target = inverse->map(target);
  if (h.polar) {
    double dx = target.x - parameter(h.pole_x, evaluator),
           dy = target.y - parameter(h.pole_y, evaluator);
    double radius = finite(std::hypot(dx, dy));
    double angle = radius == 0 ? parameter(h.x, evaluator)
                               : positive(std::atan2(dy, dx)) * 180 / pi;
    target = {angle, radius};
  }
  auto values = context.modifiers;
  // Project coupled modifier-dependent bounds to a fixed point. Undefined or
  // nonconverging constraints fail before any document XML is changed.
  for (int iteration = 0; iteration < 32; ++iteration) {
    context.modifiers = values;
    EnhancedEvaluator current(context);
    auto next = values;
    if (h.modifier_x)
      next.at(*h.modifier_x) = constrain(target.x, h.min_x, h.max_x, current);
    if (h.modifier_y)
      next.at(*h.modifier_y) = constrain(target.y, h.min_y, h.max_y, current);
    if (h.polar && h.modifier_y)
      next.at(*h.modifier_y) = std::max(0., next.at(*h.modifier_y));
    bool stable = true;
    for (size_t i = 0; i < next.size(); ++i) {
      finite(next[i]);
      if (std::abs(next[i] - values[i]) >
          1e-10 * std::max(1., std::abs(values[i])))
        stable = false;
    }
    if (stable)
      return next;
    values = std::move(next);
  }
  fail("Adjustment handle constraints do not converge");
}
bool enhanced_legacy_polar_order(const OdfPackage &package) {
  auto bytes = package.entry("meta.xml");
  if (bytes.empty())
    return false;
  XmlDocument metadata;
  if (!metadata.setContent(bytes))
    return false;
  auto nodes = metadata.elementsByTagNameNS(
      "urn:oasis:names:tc:opendocument:xmlns:meta:1.0", "generator");
  if (nodes.empty())
    return false;
  auto generator = nodes.front().text();
  for (const auto &prefix : {"OpenOffice.org", "NeoOffice", "LibreOffice",
                             "StarOffice", "Lotus Symphony"})
    if (generator.startsWith(prefix))
      return true;
  return false;
}
} // namespace DomeOffice
