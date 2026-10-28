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
EnhancedGeometry read_enhanced_geometry(const XmlElement &geometry,
                                        double width, double height,
                                        const ScenePaint &paint) {
  EnhancedGeometry result;
  EnhancedContext context;
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
  EnhancedEvaluator evaluator(context);
  auto path = geometry.attributeNS(DRAW, "enhanced-path");
  result.path =
      std::make_shared<ScenePath>(parse_enhanced_path(path, evaluator));
  auto v = context.viewbox;
  result.mapping = {
      width / v.width,         0, 0, height / v.height, -v.x * width / v.width,
      -v.y * height / v.height};
  SceneTransform mirror;
  if (boolean(geometry, "mirror-horizontal")) {
    mirror.a = -1;
    mirror.e = 2 * v.x + v.width;
  }
  if (boolean(geometry, "mirror-vertical")) {
    mirror.d = -1;
    mirror.f = 2 * v.y + v.height;
  }
  result.mapping = result.mapping * mirror;
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
  if (!children(geometry, DRAW, "handle").empty())
    result.unsupported.push_back("Enhanced shape adjustment handles");
  if (geometry.hasAttributeNS(DRAW, "text-rotate-angle")) {
    auto angle = geometry.attributeNS(DRAW, "text-rotate-angle").trimmed();
    if (angle != "0" && angle != "0deg" && angle != "0rad" && angle != "0grad")
      result.unsupported.push_back("Enhanced text rotation");
  }
  return result;
}
} // namespace DomeOffice
