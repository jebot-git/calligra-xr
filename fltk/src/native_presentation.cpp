// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_presentation.h"
#include <charconv>
#include <cmath>
#include <numbers>

namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
constexpr double cm = 72 / 2.54;
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
void note(std::vector<Text> &items, const Text &text) {
  if (std::find(items.begin(), items.end(), text) == items.end())
    items.push_back(text);
}
struct Scanner {
  std::string text;
  size_t at = 0;
  explicit Scanner(const Text &value) : text(value.toStdString()) {}
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
  bool consume(char c) {
    space();
    if (at < text.size() && text[at] == c) {
      ++at;
      return true;
    }
    return false;
  }
  double number() {
    space();
    double n;
    if (at < text.size() && text[at] == '+')
      ++at;
    auto result =
        std::from_chars(text.data() + at, text.data() + text.size(), n);
    if (result.ec != std::errc() || result.ptr == text.data() + at ||
        !std::isfinite(n) || std::abs(n) > 1e9)
      fail("Invalid presentation number");
    at = result.ptr - text.data();
    return n;
  }
  double length() {
    double n = number();
    size_t start = at;
    while (at < text.size() &&
           std::isalpha(static_cast<unsigned char>(text[at])))
      ++at;
    auto unit = text.substr(start, at - start);
    if (unit.empty() || unit == "pt")
      return n;
    if (unit == "cm")
      return n * cm;
    if (unit == "mm")
      return n * cm / 10;
    if (unit == "in")
      return n * 72;
    if (unit == "pc")
      return n * 12;
    if (unit == "px")
      return n * .75;
    fail("Unsupported presentation length");
  }
  double angle() {
    double n = number();
    size_t start = at;
    while (at < text.size() &&
           std::isalpha(static_cast<unsigned char>(text[at])))
      ++at;
    auto unit = text.substr(start, at - start);
    if (unit.empty() || unit == "rad")
      return n;
    if (unit == "deg")
      return n * std::numbers::pi / 180;
    if (unit == "grad")
      return n * std::numbers::pi / 200;
    fail("Invalid presentation angle");
  }
};
double length(const Text &value, double fallback = 0) {
  if (value.empty())
    return fallback;
  Scanner input(value);
  auto n = input.length();
  if (!input.done() || std::abs(n) > 1e7)
    fail("Invalid presentation geometry");
  return n;
}
double opacity(const Text &value) {
  Scanner input(value);
  double n = input.number();
  if (input.consume('%'))
    n /= 100;
  if (!input.done() || n < 0 || n > 1)
    fail("Invalid presentation opacity");
  return n;
}
SceneColor color(const Text &value) {
  auto text = value.toStdString();
  if (text == "transparent")
    return {0, 0, 0, 0};
  if (text == "black")
    return {};
  if (text == "white")
    return {255, 255, 255};
  if (text.size() != 7 || text[0] != '#')
    fail("Invalid presentation color");
  unsigned number;
  auto result = std::from_chars(text.data() + 1, text.data() + 7, number, 16);
  if (result.ec != std::errc() || result.ptr != text.data() + 7)
    fail("Invalid presentation color");
  return {static_cast<unsigned char>(number >> 16),
          static_cast<unsigned char>(number >> 8),
          static_cast<unsigned char>(number)};
}
void valid(const SceneTransform &m) {
  for (double v : {m.a, m.b, m.c, m.d, m.e, m.f})
    if (!std::isfinite(v) || std::abs(v) > 1e9)
      fail("Presentation transform exceeds the limit");
}
Text joined(const XmlElement &parent) {
  TextList parts;
  auto walk = [&](auto &&self, const XmlElement &e) -> void {
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        auto p = n.toElement();
        if (p.namespaceURI() == TEXT &&
            (p.localName() == "p" || p.localName() == "h"))
          parts << plain_text(p);
        else
          self(self, p);
      }
  };
  walk(walk, parent);
  return parts.join('\n');
}
struct Source {
  std::map<std::pair<Text, Text>, XmlElement> automatic;
  std::map<Text, XmlElement> layouts;
};
struct Catalog {
  Source styles, content;
  std::map<std::pair<Text, Text>, XmlElement> common;
  std::map<Text, XmlElement> defaults, masters;
  std::map<Text, Text> fonts;
  std::vector<XmlDocument> documents;
  std::map<std::string, int> editor_ids;
  size_t nodes = 0, points = 0;
  Catalog(const OdfPackage &package) {
    auto bytes = package.entry("styles.xml");
    if (!bytes.empty())
      scan(read_document_xml(bytes), styles);
    scan(package.xml, content);
  }
  void scan(const XmlDocument &doc, Source &source) {
    documents.push_back(doc);
    for (auto e : doc.elementsByTagNameNS(STYLE, "font-face"))
      fonts[e.attributeNS(STYLE, "name")] = e.attributeNS(SVG, "font-family");
    for (auto group : children(doc.documentElement(), OFFICE)) {
      bool automatic = group.localName() == "automatic-styles";
      if (automatic || group.localName() == "styles") {
        for (auto e : children(group, STYLE)) {
          auto name = e.attributeNS(STYLE, "name"),
               family = e.attributeNS(STYLE, "family");
          if (e.localName() == "style")
            (automatic ? source.automatic : common)[{family, name}] = e;
          else if (e.localName() == "default-style")
            defaults[family] = e;
          else if (e.localName() == "page-layout")
            source.layouts[name] = e;
        }
      } else if (group.localName() == "master-styles")
        for (auto e : children(group, STYLE, "master-page"))
          masters[e.attributeNS(STYLE, "name")] = e;
    }
  }
  std::vector<XmlElement> chain(const Source &source, const Text &family,
                                Text name, bool include_default = true) const {
    std::vector<XmlElement> result;
    auto automatic = source.automatic.find({family, name});
    XmlElement current;
    if (automatic != source.automatic.end())
      current = automatic->second;
    else if (auto named = common.find({family, name}); named != common.end())
      current = named->second;
    while (!current.isNull()) {
      if (std::find(result.begin(), result.end(), current) != result.end() ||
          result.size() >= 256)
        fail("Cyclic presentation style inheritance");
      result.push_back(current);
      name = current.attributeNS(STYLE, "parent-style-name");
      current = {};
      if (!name.empty()) {
        if (auto named = common.find({family, name}); named != common.end())
          current = named->second;
        else if (auto automatic = source.automatic.find({family, name});
                 automatic != source.automatic.end())
          current = automatic->second;
      }
    }
    if (auto found = defaults.find(family);
        include_default && found != defaults.end())
      result.push_back(found->second);
    std::reverse(result.begin(), result.end());
    return result;
  }
  void paint_properties(ScenePaint &paint, const XmlElement &style,
                        SlideScene &slide, bool page = false) {
    auto p = child(style, STYLE,
                   page ? "drawing-page-properties" : "graphic-properties");
    if (p.hasAttributeNS(DRAW, "fill")) {
      auto value = p.attributeNS(DRAW, "fill");
      paint.fill = value == "solid";
      if (value != "solid" && value != "none")
        note(slide.unsupported, "Gradient, bitmap and hatch fills");
    }
    if (p.hasAttributeNS(DRAW, "fill-color")) {
      auto alpha = paint.fill_color.alpha;
      paint.fill_color = color(p.attributeNS(DRAW, "fill-color"));
      paint.fill_color.alpha *= alpha;
    }
    if (p.hasAttributeNS(DRAW, "opacity"))
      paint.fill_color.alpha = opacity(p.attributeNS(DRAW, "opacity"));
    if (p.hasAttributeNS(SVG, "fill-rule"))
      paint.even_odd = p.attributeNS(SVG, "fill-rule") == "evenodd";
    if (p.hasAttributeNS(DRAW, "stroke")) {
      auto value = p.attributeNS(DRAW, "stroke");
      paint.stroke = value == "solid" || value == "dash";
      if (value != "solid" && value != "none")
        note(slide.unsupported, "Dashed and advanced strokes");
    }
    if (p.hasAttributeNS(SVG, "stroke-color")) {
      auto alpha = paint.stroke_color.alpha;
      paint.stroke_color = color(p.attributeNS(SVG, "stroke-color"));
      paint.stroke_color.alpha *= alpha;
    }
    if (p.hasAttributeNS(SVG, "stroke-opacity"))
      paint.stroke_color.alpha = opacity(p.attributeNS(SVG, "stroke-opacity"));
    if (p.hasAttributeNS(SVG, "stroke-width")) {
      paint.stroke_width = length(p.attributeNS(SVG, "stroke-width"));
      if (paint.stroke_width < 0)
        fail("Negative presentation stroke width");
    }
    if (p.hasAttributeNS(FO, "padding")) {
      paint.padding = length(p.attributeNS(FO, "padding"));
      if (paint.padding < 0)
        fail("Negative text frame padding");
    }
    auto text = child(style, STYLE, "text-properties");
    if (text.hasAttributeNS(FO, "color"))
      paint.text_color = color(text.attributeNS(FO, "color"));
    if (text.hasAttributeNS(STYLE, "font-name")) {
      auto name = text.attributeNS(STYLE, "font-name");
      paint.font.family = fonts.contains(name) ? fonts.at(name) : name;
    }
    if (text.hasAttributeNS(FO, "font-family"))
      paint.font.family = text.attributeNS(FO, "font-family");
    if (text.hasAttributeNS(FO, "font-weight")) {
      auto value = text.attributeNS(FO, "font-weight");
      bool ok = true;
      paint.font.weight = value == "bold"     ? 700
                          : value == "normal" ? 400
                                              : value.toInt(&ok);
      if (!ok || paint.font.weight < 1 || paint.font.weight > 1000)
        fail("Invalid shape font weight");
      paint.font.bold = paint.font.weight >= 600;
    }
    if (text.hasAttributeNS(FO, "font-style"))
      paint.font.italic = text.attributeNS(FO, "font-style") != "normal";
    if (text.hasAttributeNS(FO, "language"))
      paint.font.language = text.attributeNS(FO, "language");
    if (text.hasAttributeNS(FO, "font-size")) {
      auto value = text.attributeNS(FO, "font-size");
      if (value.endsWith('%')) {
        bool ok;
        auto factor = value.left(value.size() - 1).toDouble(&ok);
        if (!ok || factor <= 0 || factor > 10000)
          fail("Invalid presentation text size");
        paint.font.size *= factor / 100;
      } else
        paint.font.size = length(value);
      if (paint.font.size <= 0 || paint.font.size > 1000)
        fail("Invalid presentation text size");
    }
    for (const auto &name : {"shadow", "clip", "fill-gradient-name",
                             "stroke-dash", "marker-start", "marker-end"})
      if (p.hasAttributeNS(DRAW, name) || p.hasAttributeNS(FO, name))
        note(slide.unsupported,
             "Shadows, clipping, markers and advanced paint properties");
  }
  ScenePaint paint(const Source &source, const XmlElement &node,
                   SlideScene &slide) {
    ScenePaint result;
    result.font.size = 18;
    if (auto p = defaults.find("paragraph"); p != defaults.end())
      paint_properties(result, p->second, slide);
    for (auto family : {Text("graphic"), Text("presentation")}) {
      auto name = node.attributeNS(family == "graphic" ? DRAW : PRESENTATION,
                                   "style-name");
      if (family == "presentation" && name.empty())
        continue;
      for (auto style : chain(source, family, name))
        paint_properties(result, style, slide);
    }
    return result;
  }
  SceneTransform position(const XmlElement &node,
                          const SceneTransform &parent) {
    auto transform = parse_scene_transform(node.attributeNS(DRAW, "transform"));
    SceneTransform xy;
    xy.e = length(node.attributeNS(SVG, "x"));
    xy.f = length(node.attributeNS(SVG, "y"));
    auto result = parent * transform * xy;
    valid(result);
    return result;
  }
  std::vector<ScenePoint> polygon(const XmlElement &node, double width,
                                  double height) {
    Scanner values(node.attributeNS(DRAW, "points"));
    std::vector<ScenePoint> points;
    while (!values.done()) {
      auto x = values.number(), y = values.number();
      points.push_back({x, y});
      if (points.size() > 1024)
        fail("Presentation polygon exceeds the point limit");
    }
    auto view = node.attributeNS(SVG, "viewBox");
    if (!view.empty()) {
      Scanner box(view);
      auto x = box.number(), y = box.number(), w = box.number(),
           h = box.number();
      if (!box.done() || w <= 0 || h <= 0)
        fail("Invalid presentation viewBox");
      for (auto &p : points) {
        p.x = (p.x - x) * width / w;
        p.y = (p.y - y) * height / h;
      }
    }
    for (const auto &point : points)
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          std::abs(point.x) > 1e9 || std::abs(point.y) > 1e9)
        fail("Presentation polygon exceeds the geometry limit");
    return points;
  }
  void collect_editors(const XmlElement &parent, SlideScene &slide,
                       const std::string &path = {}, int depth = 0) {
    if (depth > 256)
      fail("Presentation group nesting exceeds the limit");
    size_t order = 0;
    for (auto e = parent.firstChild(); !e.isNull(); e = e.nextSibling())
      if (e.isElement()) {
        auto node = e.toElement();
        if (node.namespaceURI() != DRAW || node.localName() == "layer-set")
          continue;
        auto key = path + "/" + std::to_string(order++);
        auto display = node.attributeNS(DRAW, "display", "always");
        if (display == "none" || display == "printer")
          continue;
        if (node.localName() == "frame") {
          if (slide.editable_sources.size() >= 100000)
            fail("Presentation frame limit exceeded");
          editor_ids[key] = slide.editable_sources.size();
          slide.editable_sources.push_back(node);
        } else if (node.localName() == "g")
          collect_editors(node, slide, key, depth + 1);
      }
  }
  void shapes(const XmlElement &parent, const Source &source,
              const SceneTransform &transform, SlideScene &slide, bool master,
              int depth = 0, const std::string &path = {}) {
    if (depth > 256)
      fail("Presentation group nesting exceeds the limit");
    struct Item {
      XmlElement node;
      int z;
      size_t order;
    };
    std::vector<Item> items;
    for (auto node = parent.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement()) {
        auto e = node.toElement();
        if (e.namespaceURI() != DRAW) {
          if (e.namespaceURI().contains("animation"))
            note(slide.unsupported, "Animations and transitions");
          continue;
        }
        if (e.localName() == "layer-set")
          continue;
        if (++nodes > 100000)
          fail("Presentation shape count exceeds the limit");
        bool ok;
        int z = e.attributeNS(DRAW, "z-index", Text::number(items.size()))
                    .toInt(&ok);
        if (!ok || z < 0)
          fail("Invalid presentation stacking order");
        items.push_back({e, z, items.size()});
      }
    // Groups form stacking contexts; preserve stable order for equal z-indices.
    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) { return a.z < b.z; });
    for (const auto &item : items) {
      auto node = item.node;
      auto name = node.localName();
      auto display = node.attributeNS(DRAW, "display", "always");
      if (display == "none" || display == "printer")
        continue;
      if (master && node.attributeNS(PRESENTATION, "placeholder") == "true") {
        note(slide.unsupported, "Master placeholder fields");
        continue;
      }
      if (name == "g") {
        auto group = transform *
                     parse_scene_transform(node.attributeNS(DRAW, "transform"));
        valid(group);
        if (node.hasAttributeNS(DRAW, "style-name"))
          note(slide.unsupported, "Group paint effects");
        shapes(node, source, group, slide, master, depth + 1,
               path + "/" + std::to_string(item.order));
        continue;
      }
      SceneShape shape;
      shape.master = master;
      shape.name = node.attributeNS(DRAW, "name");
      shape.transform = position(node, transform);
      shape.paint = paint(source, node, slide);
      shape.width =
          length(node.attributeNS(SVG, "width"), name == "frame" ? 24 * cm : 0);
      shape.height =
          length(node.attributeNS(SVG, "height"), name == "frame" ? 3 * cm : 0);
      if (shape.width < 0 || shape.height < 0)
        fail("Negative presentation shape extent");
      shape.text = joined(node);
      if (!shape.text.empty()) {
        for (auto style :
             chain(source, "paragraph",
                   node.attributeNS(DRAW, "text-style-name"), false))
          paint_properties(shape.paint, style, slide);
        auto paragraphs = node.elementsByTagNameNS(TEXT, "p");
        if (!paragraphs.empty())
          for (auto style :
               chain(source, "paragraph",
                     paragraphs[0].attributeNS(TEXT, "style-name"), false))
            paint_properties(shape.paint, style, slide);
        if (!node.elementsByTagNameNS(TEXT, "span").empty() ||
            paragraphs.size() > 1)
          note(slide.unsupported, "Rich text runs and full paragraph layout");
      }
      if (name == "frame") {
        shape.kind = SceneShape::Frame;
        shape.image = child(node, DRAW, "image").attributeNS(XLINK, "href");
        if (!master)
          shape.editor_index =
              editor_ids.at(path + "/" + std::to_string(item.order));
      } else if (name == "rect") {
        shape.kind = SceneShape::Rectangle;
        if (node.hasAttributeNS(DRAW, "corner-radius"))
          note(slide.unsupported, "Rounded rectangles");
      } else if (name == "ellipse" || name == "circle") {
        shape.kind = SceneShape::Ellipse;
        if (node.attributeNS(DRAW, "kind", "full") != "full") {
          note(slide.unsupported, "Partial ellipses and pie shapes");
          continue;
        }
        if (node.hasAttributeNS(SVG, "r"))
          shape.width = shape.height = 2 * length(node.attributeNS(SVG, "r"));
        else if (node.hasAttributeNS(SVG, "rx") &&
                 node.hasAttributeNS(SVG, "ry")) {
          shape.width = 2 * length(node.attributeNS(SVG, "rx"));
          shape.height = 2 * length(node.attributeNS(SVG, "ry"));
        }
        if (shape.width < 0 || shape.height < 0)
          fail("Negative ellipse radius");
        if (node.hasAttributeNS(SVG, "cx") && node.hasAttributeNS(SVG, "cy")) {
          SceneTransform offset;
          offset.e = length(node.attributeNS(SVG, "cx")) - shape.width / 2;
          offset.f = length(node.attributeNS(SVG, "cy")) - shape.height / 2;
          shape.transform =
              transform *
              parse_scene_transform(node.attributeNS(DRAW, "transform")) *
              offset;
        }
      } else if (name == "polygon" || name == "polyline") {
        shape.kind =
            name == "polygon" ? SceneShape::Polygon : SceneShape::Polyline;
        auto points = polygon(node, shape.width, shape.height);
        if (points.size() < 2)
          fail("Invalid presentation polygon");
        shape.contours.push_back({std::move(points), name == "polygon"});
      } else if (name == "line") {
        shape.kind = SceneShape::Line;
        shape.transform = transform * parse_scene_transform(
                                          node.attributeNS(DRAW, "transform"));
        shape.contours.push_back({{{length(node.attributeNS(SVG, "x1")),
                                    length(node.attributeNS(SVG, "y1"))},
                                   {length(node.attributeNS(SVG, "x2")),
                                    length(node.attributeNS(SVG, "y2"))}},
                                  false});
      } else {
        note(slide.unsupported,
             "Custom shapes, paths, connectors and 3D objects");
        continue;
      }
      if (shape.contours.empty()) {
        std::vector<ScenePoint> points;
        if (shape.kind == SceneShape::Ellipse) {
          for (int i = 0; i < 96; ++i) {
            double t = 2 * std::numbers::pi * i / 96;
            points.push_back({shape.width * (1 + cos(t)) / 2,
                              shape.height * (1 + sin(t)) / 2});
          }
        } else
          points = {{0, 0},
                    {shape.width, 0},
                    {shape.width, shape.height},
                    {0, shape.height}};
        shape.contours.push_back({std::move(points), true});
      }
      for (const auto &c : shape.contours) {
        points += c.points.size();
        if (points > 1000000)
          fail("Presentation geometry exceeds the point limit");
        for (auto p : c.points) {
          auto q = shape.transform.map(p);
          if (!std::isfinite(q.x) || !std::isfinite(q.y) ||
              std::abs(q.x) > 1e9 || std::abs(q.y) > 1e9)
            fail("Presentation shape coordinates exceed the limit");
        }
      }
      if (shape.editor_index >= 0)
        slide.editor_shapes.at(shape.editor_index) = slide.shapes.size();
      slide.shapes.push_back(std::move(shape));
    }
  }
  SlideScene slide(const XmlElement &page, int index) {
    SlideScene slide;
    slide.name =
        page.attributeNS(DRAW, "name", Text("Slide %1").arg(index + 1));
    slide.master = page.attributeNS(DRAW, "master-page-name");
    XmlElement master;
    if (!slide.master.empty()) {
      auto found = masters.find(slide.master);
      if (found == masters.end())
        note(slide.unsupported, "Missing master page");
      else
        master = found->second;
    }
    if (!master.isNull()) {
      auto layout = master.attributeNS(STYLE, "page-layout-name");
      if (auto found = styles.layouts.find(layout);
          found != styles.layouts.end()) {
        auto properties = child(found->second, STYLE, "page-layout-properties");
        slide.width =
            length(properties.attributeNS(FO, "page-width"), slide.width);
        slide.height =
            length(properties.attributeNS(FO, "page-height"), slide.height);
      } else if (!layout.empty())
        note(slide.unsupported, "Missing master page layout");
    }
    if (slide.width <= 0 || slide.height <= 0 || slide.width > 10000 ||
        slide.height > 10000)
      fail("Invalid presentation page dimensions");
    ScenePaint background;
    if (!master.isNull())
      for (auto style : chain(styles, "drawing-page",
                              master.attributeNS(DRAW, "style-name")))
        paint_properties(background, style, slide, true);
    bool master_background = true, master_shapes = true;
    ScenePaint foreground;
    for (auto style :
         chain(content, "drawing-page", page.attributeNS(DRAW, "style-name"))) {
      auto p = child(style, STYLE, "drawing-page-properties");
      if (p.hasAttributeNS(PRESENTATION, "background-visible"))
        master_background =
            p.attributeNS(PRESENTATION, "background-visible") == "true";
      if (p.hasAttributeNS(PRESENTATION, "background-objects-visible"))
        master_shapes =
            p.attributeNS(PRESENTATION, "background-objects-visible") == "true";
      paint_properties(foreground, style, slide, true);
    }
    if (master_background && background.fill)
      slide.background = background.fill_color;
    if (foreground.fill)
      slide.background = foreground.fill_color;
    if (master_shapes && !master.isNull())
      shapes(master, styles, {}, slide, true);
    editor_ids.clear();
    collect_editors(page, slide);
    slide.editor_shapes.resize(slide.editable_sources.size());
    shapes(page, content, {}, slide, false);
    return slide;
  }
};
} // namespace
ScenePoint SceneTransform::map(ScenePoint p) const {
  return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
}
SceneTransform SceneTransform::operator*(const SceneTransform &m) const {
  return {a * m.a + c * m.b, b * m.a + d * m.b,     a * m.c + c * m.d,
          b * m.c + d * m.d, a * m.e + c * m.f + e, b * m.e + d * m.f + f};
}
std::optional<SceneTransform> SceneTransform::inverse() const {
  double determinant = a * d - b * c;
  if (!std::isfinite(determinant) || std::abs(determinant) < 1e-15)
    return {};
  SceneTransform result{d / determinant,
                        -b / determinant,
                        -c / determinant,
                        a / determinant,
                        0,
                        0};
  result.e = -result.a * e - result.c * f;
  result.f = -result.b * e - result.d * f;
  return result;
}
SceneTransform parse_scene_transform(const Text &value) {
  if (value.size() > 65536)
    fail("Presentation transform exceeds the limit");
  Scanner input(value);
  SceneTransform result;
  int count = 0;
  while (!input.done()) {
    if (++count > 256)
      fail("Too many presentation transforms");
    size_t start = input.at;
    while (input.at < input.text.size() &&
           std::isalpha(static_cast<unsigned char>(input.text[input.at])))
      ++input.at;
    auto command = input.text.substr(start, input.at - start);
    if (!input.consume('('))
      fail("Invalid presentation transform");
    SceneTransform operation;
    if (command == "translate") {
      operation.e = input.length();
      if (!input.consume(')')) {
        operation.f = input.length();
        if (!input.consume(')'))
          fail("Invalid translate transform");
      }
    } else if (command == "scale") {
      operation.a = input.number();
      operation.d = operation.a;
      if (!input.consume(')')) {
        operation.d = input.number();
        if (!input.consume(')'))
          fail("Invalid scale transform");
      }
    } else if (command == "rotate") {
      double angle = input.angle();
      operation.a = operation.d = cos(angle);
      operation.b = sin(angle);
      operation.c = -sin(angle);
      if (!input.consume(')')) {
        auto x = input.length(), y = input.length();
        SceneTransform offset;
        offset.e = x;
        offset.f = y;
        SceneTransform back;
        back.e = -x;
        back.f = -y;
        operation = offset * operation * back;
        if (!input.consume(')'))
          fail("Invalid rotation transform");
      }
    } else if (command == "skewX" || command == "skewY") {
      auto angle = input.angle();
      if (command == "skewX")
        operation.c = tan(angle);
      else
        operation.b = tan(angle);
      if (!input.consume(')'))
        fail("Invalid skew transform");
    } else if (command == "matrix") {
      operation.a = input.number();
      operation.b = input.number();
      operation.c = input.number();
      operation.d = input.number();
      operation.e = input.length();
      operation.f = input.length();
      if (!input.consume(')'))
        fail("Invalid matrix transform");
    } else
      fail("Unsupported presentation transform operation");
    result = operation * result;
    valid(result);
  }
  return result;
}
SceneRect SceneShape::bounds() const {
  double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
  for (const auto &c : contours)
    for (auto p : c.points) {
      auto q = transform.map(p);
      left = std::min(left, q.x);
      top = std::min(top, q.y);
      right = std::max(right, q.x);
      bottom = std::max(bottom, q.y);
    }
  return std::isfinite(left) ? SceneRect{left, top, right - left, bottom - top}
                             : SceneRect{};
}
bool SceneShape::hit(ScenePoint point) const {
  auto inverse = transform.inverse();
  if (!inverse)
    return false;
  auto p = inverse->map(point);
  if (kind == Frame)
    return p.x >= 0 && p.y >= 0 && p.x <= width && p.y <= height;
  bool inside = false;
  int winding = 0;
  for (const auto &contour : contours) {
    auto n = contour.points.size();
    if (contour.closed && paint.fill)
      for (size_t i = 0, j = n - 1; i < n; j = i++) {
        auto a = contour.points[i], b = contour.points[j];
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
          inside = !inside;
        double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (a.y <= p.y && b.y > p.y && cross > 0)
          ++winding;
        if (a.y > p.y && b.y <= p.y && cross < 0)
          --winding;
      }
    if (paint.stroke)
      for (size_t i = 1; i < n + (contour.closed ? 1 : 0); ++i) {
        auto a = contour.points[i - 1], b = contour.points[i % n];
        double dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
        double t =
            l ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l, 0., 1.)
              : 0;
        if (std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy) <=
            std::max(2., paint.stroke_width / 2))
          return true;
      }
  }
  return paint.even_odd ? inside : winding != 0;
}
int SlideScene::hit_test(ScenePoint p) const {
  for (size_t i = shapes.size(); i > 0; --i)
    if (!shapes[i - 1].master && shapes[i - 1].hit(p))
      return i - 1;
  return -1;
}
std::vector<SlideScene> read_presentation_scenes(const OdfPackage &package) {
  if (package.kind() != Kind::Stage)
    fail("Slide scenes require a presentation");
  Catalog catalog(package);
  auto pages = children(package.body(), DRAW, "page");
  if (pages.empty() || pages.size() > 4096)
    fail("Invalid presentation page count");
  std::vector<SlideScene> result;
  for (size_t i = 0; i < pages.size(); ++i)
    result.push_back(catalog.slide(pages[i], i));
  return result;
}
} // namespace DomeOffice
