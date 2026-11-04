// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_presentation.h"
#include "native_document_scope.h"
#include "native_enhanced_geometry.h"
#include "native_path.h"
#include "native_marker.h"
#include "native_gradient.h"
#include "native_connector.h"
#include "native_stage_identity.h"
#include "native_stage_order.h"
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
Text surrounding_text(const XmlElement &paragraph) {
  Text result;
  size_t nodes = 0;
  auto append = [&](const Text &text) {
    if (text.size() > 4000000 - result.size())
      fail("Presentation text exceeds the limit");
    result += text;
  };
  auto walk = [&](auto &&self, XmlElement parent, int depth) -> void {
    if (++nodes > 1000000 || depth > 256)
      fail("Presentation text traversal exceeds the limit");
    for (auto node = parent.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isText() || node.isCDATASection())
        append(node.nodeValue());
      else if (node.isElement()) {
        auto e = node.toElement();
        if (native_embedded_document_boundary(e))
          continue;
        if (e.namespaceURI() == TEXT && e.localName() == "s") {
          bool ok = false;
          int count = e.attributeNS(TEXT, "c", "1").toInt(&ok);
          if (!ok || count < 1 || count > 65536)
            fail("Invalid repeated spaces in document");
          append(Text(size_t(count), U' '));
        } else if (e.namespaceURI() == TEXT && e.localName() == "tab")
          append("\t");
        else if (e.namespaceURI() == TEXT && e.localName() == "line-break")
          append("\n");
        else
          self(self, e, depth + 1);
      }
  };
  walk(walk, paragraph, 0);
  return result;
}
Text joined(const XmlElement &parent) {
  TextList parts;
  auto walk = [&](auto &&self, const XmlElement &e) -> void {
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        auto p = n.toElement();
        if (native_embedded_document_boundary(p))
          continue;
        if (p.namespaceURI() == TEXT &&
            (p.localName() == "p" || p.localName() == "h"))
          parts << surrounding_text(p);
        else
          self(self, p);
      }
  };
  walk(walk, parent);
  return parts.join('\n');
}
struct Source {
  XmlElement root;
  std::map<std::pair<Text, Text>, XmlElement> automatic;
  std::map<Text, XmlElement> layouts;
  std::map<Text, Text> fonts;
};
struct Catalog {
  const OdfPackage &source_package;
  Source styles, content;
  std::map<std::pair<Text, Text>, XmlElement> common;
  std::map<Text, XmlElement> defaults, masters, dash_definitions,
      marker_definitions, gradient_definitions;
  std::map<Text, std::shared_ptr<const SceneMarkerDefinition>> marker_cache;
  std::map<Text, std::shared_ptr<const SceneGradientDefinition>> gradient_cache;
  std::shared_ptr<OdfTextReader> text_reader;
  std::vector<XmlDocument> documents;
  std::map<std::string, int> editor_ids;
  size_t nodes = 0, points = 0;
  bool legacy_polar_order = false;
  SceneTarget target;
  SceneLayers document_layers;
  struct TargetRecord {
    XmlElement node;
    const Source *source = nullptr;
    std::optional<size_t> parent;
    std::vector<size_t> children;
    bool master = false;
    int state = 0;
    SceneTransform local;
    std::optional<SceneTransform> group_frame;
    std::optional<SceneConnectorObject> object;
    std::vector<Text> diagnostics;
  };
  std::vector<TargetRecord> targets;
  std::map<std::pair<bool, Text>, size_t> target_ids;
  size_t target_depth = 0;
  std::set<const void *> recompute_connectors;
  bool strict_connector_updates = false;

  Catalog(const OdfPackage &package, std::shared_ptr<OdfTextReader> reader,
          SceneTarget target_)
      : source_package(package), target(target_),
        text_reader(reader ? std::move(reader)
                           : std::make_shared<OdfTextReader>(package)) {
    legacy_polar_order = enhanced_legacy_polar_order(package);
    auto bytes = package.entry("styles.xml");
    if (!bytes.empty())
      scan(read_document_xml(bytes), styles);
    scan(package.xml, content);
    for (auto node :
         children(child(styles.root, OFFICE, "master-styles"), STYLE))
      if (node.localName() == "master-page" ||
          node.localName() == "handout-master")
        index_targets(node, styles, {}, true);
    for (auto page : children(package.body(), DRAW, "page"))
      index_targets(page, content, {}, false);
  }
  void index_targets(XmlElement parent, const Source &source,
                     std::optional<size_t> group, bool master, int depth = 0) {
    if (depth > 256)
      fail("Connector identity group nesting exceeds the limit");
    const Text XML = "http://www.w3.org/XML/1998/namespace";
    for (auto node = parent.firstChild(); !node.isNull();
         node = node.nextSibling()) {
      if (!node.isElement() || !native_stage_object_identity(node))
        continue;
      if (targets.size() >= 100000)
        fail("Connector identity catalog exceeds the limit");
      const auto index = targets.size();
      TargetRecord record;
      record.node = node;
      record.source = &source;
      record.parent = group;
      record.master = master;
      targets.push_back(std::move(record));
      if (group)
        targets[*group].children.push_back(index);
      const auto id =
          (node.hasAttributeNS(XML, "id") ? node.attributeNS(XML, "id")
                                          : node.attributeNS(DRAW, "id"))
              .trimmed();
      if (!id.empty()) {
        auto [at, inserted] =
            target_ids.emplace(std::make_pair(&source == &styles, id), index);
        if (!inserted)
          at->second = SIZE_MAX;
      }
      if (node.namespaceURI() == DRAW && node.localName() == "g")
        index_targets(node, source, index, master, depth + 1);
    }
  }
  SceneTransform target_parent(const TargetRecord &record) {
    if (!record.parent)
      return {};
    auto &parent = targets.at(*record.parent);
    if (!parent.group_frame) {
      auto matrix =
          target_parent(parent) *
          parse_scene_transform(parent.node.attributeNS(DRAW, "transform"));
      valid(matrix);
      parent.group_frame = matrix;
    }
    return *parent.group_frame;
  }
  SceneConnectorObject *target_object(size_t index) {
    auto &record = targets.at(index);
    if (record.state == 2)
      return &*record.object;
    if (record.state == 1 || target_depth >= 256)
      fail("Cyclic or excessive connector geometry dependency");
    record.state = 1;
    ++target_depth;
    try {
      SceneConnectorObject object;
      const auto parent = target_parent(record);
      if (record.node.namespaceURI() == DRAW &&
          record.node.localName() == "g") {
        record.local =
            parse_scene_transform(record.node.attributeNS(DRAW, "transform"));
        object.transform = parent * record.local;
        valid(object.transform);
        record.group_frame = object.transform;
        bool first = true;
        for (auto child : record.children) {
          auto *part = target_object(child);
          for (const auto &message : targets[child].diagnostics)
            note(record.diagnostics, message);
          const auto m = targets[child].local;
          auto b = part->bounds;
          for (auto p : {ScenePoint{b.x, b.y},
                         {b.x + b.width, b.y},
                         {b.x, b.y + b.height},
                         {b.x + b.width, b.y + b.height}}) {
            p = m.map(p);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                std::abs(p.x) > 1e9 || std::abs(p.y) > 1e9)
              fail("Connector group bounds exceed the limit");
            if (first) {
              object.bounds = {p.x, p.y, 0, 0};
              first = false;
            } else {
              double right =
                         std::max(object.bounds.x + object.bounds.width, p.x),
                     bottom =
                         std::max(object.bounds.y + object.bounds.height, p.y);
              object.bounds.x = std::min(object.bounds.x, p.x);
              object.bounds.y = std::min(object.bounds.y, p.y);
              object.bounds.width = right - object.bounds.x;
              object.bounds.height = bottom - object.bounds.y;
            }
          }
        }
      } else {
        SlideScene scratch;
        auto geometry =
            make_geometry(record.node, *record.source, parent, scratch,
                          record.master, SceneLayers{}, false);
        if (!geometry)
          fail("Connector target has no native geometry");
        if (record.node.localName() == "connector")
          for (const auto &message : scratch.unsupported)
            if (message.startsWith("Connector cached path") ||
                message.startsWith("Unresolved native connector"))
              note(record.diagnostics, message);
        auto &shape = geometry->shape;
        record.local = geometry->local;
        object.transform = shape.transform;
        if (shape.path)
          object.bounds = path_bounds(*shape.path, shape.path_map);
        else if (shape.contours.empty())
          object.bounds = {0, 0, shape.width, shape.height};
        else {
          auto local = shape;
          local.transform = {};
          object.bounds = local.bounds();
        }
        if (record.node.localName() == "connector" && shape.path &&
            shape.path->subpaths.empty())
          fail("Connector target geometry is unresolved");
      }
      object.points = read_scene_glue_points(record.node, object.bounds);
      if (record.node.namespaceURI() == DRAW &&
          record.node.localName() == "custom-shape") {
        auto geometry = child(record.node, DRAW, "enhanced-geometry");
        if (!geometry.isNull()) {
          SlideScene scratch;
          auto effective = geometry_paint(*record.source, record.node);
          auto enhanced = read_enhanced_glue_points(
              geometry, length(record.node.attributeNS(SVG, "width")),
              length(record.node.attributeNS(SVG, "height")), effective,
              object.bounds, legacy_polar_order);
          for (auto point : object.points)
            if (!point.standard) {
              std::erase_if(enhanced,
                            [&](const auto &p) { return p.id == point.id; });
              enhanced.push_back(std::move(point));
            }
          object.points = std::move(enhanced);
        }
      }
      record.object = std::move(object);
      record.state = 2;
      --target_depth;
      return &*record.object;
    } catch (...) {
      record.state = 0;
      record.object.reset();
      record.diagnostics.clear();
      --target_depth;
      throw;
    }
  }
  const SceneConnectorObject *find_target(const Source &source, const Text &id,
                                          SlideScene *consumer = nullptr) {
    auto at = target_ids.find({&source == &styles, id});
    if (at == target_ids.end())
      return nullptr;
    if (at->second == SIZE_MAX)
      fail("Ambiguous drawing identity in connector source part");
    auto *object = target_object(at->second);
    if (consumer)
      for (const auto &message : targets[at->second].diagnostics)
        note(consumer->unsupported, message);
    return object;
  }
  void scan(const XmlDocument &doc, Source &source) {
    source.root = doc.documentElement();
    documents.push_back(doc);
    for (auto e : children(child(source.root, OFFICE, "font-face-decls"), STYLE,
                           "font-face"))
      source.fonts[e.attributeNS(STYLE, "name")] =
          e.attributeNS(SVG, "font-family");
    for (auto group : children(doc.documentElement(), OFFICE)) {
      bool automatic = group.localName() == "automatic-styles";
      if (automatic || group.localName() == "styles") {
        if (!automatic)
          for (auto e : children(group, DRAW, "gradient")) {
            if (gradient_definitions.size() >= 100000)
              fail("Too many drawing gradient definitions");
            gradient_definitions[e.attributeNS(DRAW, "name")] = e;
          }
        if (!automatic)
          for (auto e : children(group, DRAW, "marker")) {
            if (marker_definitions.size() >= 100000)
              fail("Too many marker definitions");
            marker_definitions[e.attributeNS(DRAW, "name")] = e;
          }
        if (!automatic)
          for (auto e : children(group, DRAW, "stroke-dash")) {
            if (dash_definitions.size() >= 100000)
              fail("Too many drawing dash definitions");
            dash_definitions[e.attributeNS(DRAW, "name")] = e;
          }
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
      } else if (group.localName() == "master-styles") {
        document_layers = document_layers.scoped(group);
        for (auto e : children(group, STYLE, "master-page"))
          masters[e.attributeNS(STYLE, "name")] = e;
      }
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
                        SlideScene &slide, bool page = false,
                        bool *legacy_middle = nullptr) {
    auto p = child(style, STYLE,
                   page ? "drawing-page-properties" : "graphic-properties");
    if (p.hasAttributeNS(DRAW, "fill")) {
      auto value = p.attributeNS(DRAW, "fill");
      paint.fill = value == "solid" || value == "gradient";
      paint.gradient = value == "gradient";
      if (value != "solid" && value != "none" && value != "gradient")
        note(slide.unsupported, "Bitmap and hatch fills");
    }
    if (p.hasAttributeNS(DRAW, "fill-gradient-name"))
      paint.gradient_name = p.attributeNS(DRAW, "fill-gradient-name");
    if (p.hasAttributeNS(DRAW, "gradient-step-count"))
      paint.gradient_steps =
          read_scene_gradient_steps(p.attributeNS(DRAW, "gradient-step-count"));
    if (p.hasAttributeNS(DRAW, "fill-color")) {
      auto alpha = paint.fill_color.alpha;
      paint.fill_color = color(p.attributeNS(DRAW, "fill-color"));
      paint.fill_color.alpha *= alpha;
    }
    if (p.hasAttributeNS(DRAW, "opacity"))
      paint.fill_color.alpha = opacity(p.attributeNS(DRAW, "opacity"));
    if (p.hasAttributeNS(DRAW, "image-opacity"))
      paint.image_opacity = opacity(p.attributeNS(DRAW, "image-opacity"));
    if (p.hasAttributeNS(DRAW, "opacity-name") &&
        !p.attributeNS(DRAW, "opacity-name").empty())
      note(slide.unsupported, "Opacity gradients");
    if (p.hasAttributeNS(SVG, "fill-rule"))
      paint.even_odd = p.attributeNS(SVG, "fill-rule") == "evenodd";
    if (p.hasAttributeNS(DRAW, "stroke")) {
      auto value = p.attributeNS(DRAW, "stroke");
      paint.stroke = value == "solid" || value == "dash";
      paint.dashed = value == "dash";
      if (value != "solid" && value != "none" && value != "dash")
        note(slide.unsupported, "Advanced strokes");
    }
    if (p.hasAttributeNS(DRAW, "stroke-dash"))
      paint.dash_name = p.attributeNS(DRAW, "stroke-dash");
    if (p.hasAttributeNS(DRAW, "stroke-dash-names")) {
      paint.dash_names.clear();
      std::istringstream input(
          p.attributeNS(DRAW, "stroke-dash-names").toStdString());
      for (std::string name; input >> name;) {
        if (paint.dash_names.size() >= 63)
          fail("Too many drawing dash overlay passes");
        paint.dash_names.push_back(Text(name));
      }
    }
    for (auto [name, marker] : {std::pair{"marker-start", &paint.marker_start},
                                std::pair{"marker-end", &paint.marker_end}}) {
      if (p.hasAttributeNS(DRAW, name))
        marker->name = p.attributeNS(DRAW, name);
      auto width_name = Text(name) + "-width",
           center_name = Text(name) + "-center";
      if (p.hasAttributeNS(DRAW, width_name)) {
        marker->width = length(p.attributeNS(DRAW, width_name));
        if (marker->width < 0)
          fail("Negative marker width");
      }
      if (p.hasAttributeNS(DRAW, center_name)) {
        auto value = p.attributeNS(DRAW, center_name);
        if (value != "true" && value != "false" && value != "1" && value != "0")
          fail("Invalid marker centering value");
        marker->centered = value == "true" || value == "1";
      }
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
    if (p.hasAttributeNS(SVG, "stroke-linecap")) {
      const auto cap = p.attributeNS(SVG, "stroke-linecap");
      if (cap == "butt")
        paint.stroke_cap = ScenePaint::Cap::Butt;
      else if (cap == "round")
        paint.stroke_cap = ScenePaint::Cap::Round;
      else if (cap == "square")
        paint.stroke_cap = ScenePaint::Cap::Square;
      else
        fail("Invalid presentation stroke cap");
    }
    if (p.hasAttributeNS(DRAW, "stroke-linejoin")) {
      const auto join = p.attributeNS(DRAW, "stroke-linejoin");
      if (legacy_middle)
        *legacy_middle = join == "middle";
      if (join == "miter")
        paint.stroke_join = ScenePaint::Join::Miter;
      else if (join == "round")
        paint.stroke_join = ScenePaint::Join::Round;
      else if (join == "bevel")
        paint.stroke_join = ScenePaint::Join::Bevel;
      else if (join == "none")
        paint.stroke_join = ScenePaint::Join::None;
      else if (join == "middle") {
        paint.stroke_join = ScenePaint::Join::Miter;
      } else
        fail("Invalid presentation stroke join");
    }
    if (p.hasAttributeNS("http://www.calligra.org/2005/", "stroke-miterlimit"))
      note(slide.unsupported, "Calligra stroke miter-limit extension");
    if (p.hasAttributeNS(FO, "padding")) {
      paint.padding = length(p.attributeNS(FO, "padding"));
      if (paint.padding < 0)
        fail("Negative text frame padding");
      paint.padding_left = paint.padding_right = paint.padding_top =
          paint.padding_bottom = paint.padding;
    }
    for (auto [name, target] :
         std::initializer_list<std::pair<const char *, double *>>{
             {"padding-left", &paint.padding_left},
             {"padding-right", &paint.padding_right},
             {"padding-top", &paint.padding_top},
             {"padding-bottom", &paint.padding_bottom}})
      if (p.hasAttributeNS(FO, name)) {
        *target = length(p.attributeNS(FO, name));
        if (*target < 0)
          fail("Negative text frame padding");
      }
    if (p.hasAttributeNS(DRAW, "textarea-vertical-align")) {
      auto value = p.attributeNS(DRAW, "textarea-vertical-align");
      paint.vertical_alignment = value == "middle"   ? ScenePaint::Middle
                                 : value == "bottom" ? ScenePaint::Bottom
                                                     : ScenePaint::Top;
      if (value != "top" && value != "middle" && value != "bottom")
        note(slide.unsupported, "Distributed vertical text alignment");
    }
    if (p.attributeNS(DRAW, "fit-to-size") == "true" ||
        p.attributeNS(DRAW, "auto-grow-width") == "true" ||
        p.attributeNS(DRAW, "auto-grow-height") == "true")
      note(slide.unsupported, "Text frame auto-growth and fit-to-size");
    auto text = child(style, STYLE, "text-properties");
    if (text.hasAttributeNS(FO, "color")) {
      paint.text_color = color(text.attributeNS(FO, "color"));
      paint.use_window_text_color = false;
    }
    if (text.hasAttributeNS(STYLE, "use-window-font-color"))
      paint.use_window_text_color =
          text.attributeNS(STYLE, "use-window-font-color") == "true";
    if (text.hasAttributeNS(STYLE, "font-name")) {
      auto name = text.attributeNS(STYLE, "font-name");
      const auto &font_source =
          style.ownerDocument().documentElement() == styles.root ? styles
                                                                 : content;
      paint.font.family = font_source.fonts.contains(name)
                              ? font_source.fonts.at(name)
                          : styles.fonts.contains(name) ? styles.fonts.at(name)
                                                        : name;
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
    if (text.hasAttributeNS(FO, "language")) {
      paint.font.language = text.attributeNS(FO, "language");
      if (paint.font.language == "none")
        paint.font.language.clear();
      auto country = text.attributeNS(FO, "country");
      if (!paint.font.language.empty() && !country.empty() && country != "none")
        paint.font.language += "-" + country;
    }
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
    for (const auto &name : {"shadow", "clip"})
      if (p.hasAttributeNS(DRAW, name) || p.hasAttributeNS(FO, name))
        note(slide.unsupported,
             "Shadows, clipping and advanced paint properties");
  }
  void resolve_gradient(ScenePaint &paint, SlideScene &slide) {
    if (!paint.fill || !paint.gradient)
      return;
    auto at = gradient_definitions.find(paint.gradient_name);
    if (at == gradient_definitions.end()) {
      note(slide.unsupported,
           "Missing or unsupported drawing gradient definition");
      paint.fill = false;
      return;
    }
    auto &cached = gradient_cache[paint.gradient_name];
    if (!cached)
      cached = std::make_shared<const SceneGradientDefinition>(
          read_scene_gradient(at->second));
    paint.gradient_definition = cached;
  }
  ScenePaint paint(const Source &source, const XmlElement &node,
                   SlideScene &slide) {
    ScenePaint result;
    bool legacy_middle = false;
    result.font.size = 18;
    if (auto p = defaults.find("text"); p != defaults.end())
      paint_properties(result, p->second, slide, false, &legacy_middle);
    if (auto p = defaults.find("paragraph"); p != defaults.end())
      paint_properties(result, p->second, slide, false, &legacy_middle);
    for (auto family : {Text("graphic"), Text("presentation")}) {
      auto name = node.attributeNS(family == "graphic" ? DRAW : PRESENTATION,
                                   "style-name");
      if (family == "presentation" && name.empty())
        continue;
      for (auto style : chain(source, family, name))
        paint_properties(result, style, slide, false, &legacy_middle);
    }
    if (result.stroke && legacy_middle)
      note(slide.unsupported, "Deprecated middle stroke joins");
    resolve_gradient(result, slide);
    auto resolve = [&](const Text &name) {
      if (auto at = dash_definitions.find(name); at != dash_definitions.end())
        return read_scene_dash_pattern(at->second);
      note(slide.unsupported,
           "Missing drawing dash definition; solid fallback");
      return SceneDashPattern{};
    };
    if (result.stroke) {
      result.stroke_patterns.push_back(result.dashed ? resolve(result.dash_name)
                                                     : SceneDashPattern{});
      for (const auto &name : result.dash_names)
        result.stroke_patterns.push_back(resolve(name));
    }
    for (auto marker : {&result.marker_start, &result.marker_end})
      if (!marker->name.empty()) {
        if (auto at = marker_definitions.find(marker->name);
            at != marker_definitions.end()) {
          auto &cached = marker_cache[marker->name];
          if (!cached)
            cached = read_scene_marker(at->second);
          marker->definition = cached;
        } else
          note(slide.unsupported, "Missing drawing marker definition");
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
        if (!slide.layers.visible(node, target) || display == "none" ||
            (target == SceneTarget::Screen && display == "printer") ||
            (target == SceneTarget::Print && display == "screen"))
          continue;
        bool supported =
            TextList{"frame",        "rect",     "ellipse",  "circle",
                     "path",         "polygon",  "polyline", "line",
                     "custom-shape", "connector"}
                .contains(node.localName());
        bool has_text = !children(node, TEXT, "p").empty() ||
                        !children(node, TEXT, "h").empty() ||
                        !child(node, DRAW, "text-box").isNull();
        if (supported && (node.localName() == "frame" || has_text)) {
          if (slide.editable_sources.size() >= 100000)
            fail("Presentation frame limit exceeded");
          editor_ids[key] = slide.editable_sources.size();
          slide.editable_sources.push_back(node);
        } else if (node.localName() == "g")
          collect_editors(node, slide, key, depth + 1);
      }
  }
  ScenePaint geometry_paint(const Source &source, XmlElement node) {
    ScenePaint out;
    auto consume = [&](const Text &family, const Text &name) {
      for (auto style : chain(source, family, name)) {
        auto p = child(style, STYLE, "graphic-properties");
        if (p.hasAttributeNS(DRAW, "fill"))
          out.fill = p.attributeNS(DRAW, "fill") != "none";
        if (p.hasAttributeNS(DRAW, "stroke"))
          out.stroke = p.attributeNS(DRAW, "stroke") != "none";
      }
    };
    consume("graphic", node.attributeNS(DRAW, "style-name"));
    if (node.hasAttributeNS(PRESENTATION, "style-name"))
      consume("presentation", node.attributeNS(PRESENTATION, "style-name"));
    return out;
  }
  struct ReadGeometry {
    SceneShape shape;
    SceneTransform local;
  };
  std::optional<ReadGeometry>
  make_geometry(XmlElement node, const Source &source,
                const SceneTransform &transform, SlideScene &slide, bool master,
                const SceneLayers &layers, bool layout = true) {
    const auto name = node.localName();
    SceneShape shape;
    shape.parent_transform = transform;
    shape.master = master;
    shape.layer_protected = layers.protected_object(node);
    if (!master)
      shape.source = node;
    shape.name = node.attributeNS(DRAW, "name");
    SceneTransform local = position(node, {});
    shape.transform = transform * local;
    shape.paint =
        layout ? paint(source, node, slide) : geometry_paint(source, node);
    shape.width =
        length(node.attributeNS(SVG, "width"), name == "frame" ? 24 * cm : 0);
    shape.height =
        length(node.attributeNS(SVG, "height"), name == "frame" ? 3 * cm : 0);
    if (shape.width < 0 || shape.height < 0)
      fail("Negative presentation shape extent");
    if (layout)
      shape.text = joined(node);
    if (!child(node, DRAW, "object").isNull() ||
        !child(node, DRAW, "object-ole").isNull())
      note(slide.unsupported, "Embedded document objects");
    shape.paragraph_style = node.attributeNS(DRAW, "text-style-name");
    const auto text_box = child(node, DRAW, "text-box");
    const bool has_flow = !text_box.isNull() ||
                          !children(node, TEXT, "p").empty() ||
                          !children(node, TEXT, "h").empty() ||
                          !children(node, TABLE, "table").empty();
    if (has_flow && layout) {
      TextParagraph base;
      base.font = shape.paint.font;
      if (!shape.paint.use_window_text_color)
        base.color = shape.paint.text_color;
      auto container = text_box;
      if (container.isNull())
        container = node;
      shape.text_flow =
          text_reader->frame(container, base, shape.paragraph_style, master);
      for (const auto &feature : shape.text_flow.unsupported)
        note(slide.unsupported, feature);
    }
    if (name == "frame") {
      shape.kind = SceneShape::Frame;
      shape.image = child(node, DRAW, "image").attributeNS(XLINK, "href");
      if (!shape.image.empty() && layout)
        shape.raster = source_package.image_asset(shape.image);
    } else if (name == "custom-shape") {
      shape.kind = SceneShape::Custom;
      auto geometry = child(node, DRAW, "enhanced-geometry");
      if (geometry.isNull()) {
        shape.path = std::make_shared<ScenePath>();
        note(slide.unsupported, "Custom shape without enhanced geometry");
      } else {
        if (shape.paint.gradient && shape.paint.gradient_definition &&
            shape.paint.gradient_definition->type >=
                SceneGradientDefinition::Radial) {
          const auto concentric = geometry.attributeNS(
              DRAW, "concentric-gradient-fill-allowed", "false");
          if (concentric == "true" || concentric == "1")
            note(slide.unsupported, "Custom-path concentric gradient geometry");
        }
        auto enhanced =
            read_enhanced_geometry(geometry, shape.width, shape.height,
                                   shape.paint, legacy_polar_order);
        shape.path = std::move(enhanced.path);
        shape.path_map = enhanced.mapping;
        shape.text_area = enhanced.text_area;
        shape.handles = std::move(enhanced.handles);
        for (const auto &feature : enhanced.unsupported)
          note(slide.unsupported, feature);
      }
    } else if (name == "rect") {
      shape.kind = SceneShape::Rectangle;
      double rx = length(node.attributeNS(DRAW, "corner-radius"));
      double ry = rx;
      if (node.hasAttributeNS(SVG, "rx"))
        rx = length(node.attributeNS(SVG, "rx"));
      if (node.hasAttributeNS(SVG, "ry"))
        ry = length(node.attributeNS(SVG, "ry"));
      else if (node.hasAttributeNS(SVG, "rx"))
        ry = rx;
      if (node.hasAttributeNS(SVG, "ry") && !node.hasAttributeNS(SVG, "rx") &&
          !node.hasAttributeNS(DRAW, "corner-radius"))
        rx = ry;
      if (rx < 0 || ry < 0)
        fail("Negative rectangle corner radius");
      if (rx > 0 && ry > 0)
        shape.path = std::make_shared<ScenePath>(
            rounded_rectangle_path(shape.width, shape.height, rx, ry));
    } else if (name == "ellipse" || name == "circle") {
      shape.kind = SceneShape::Ellipse;
      auto kind = node.attributeNS(DRAW, "kind", "full");
      if (kind != "full" && kind != "arc" && kind != "cut" && kind != "section")
        fail("Invalid ellipse kind");
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
        local =
            parse_scene_transform(node.attributeNS(DRAW, "transform")) * offset;
        shape.transform = transform * local;
      }
      double start = 0, sweep = 360;
      if (kind != "full") {
        auto degrees = [&](const Text &value) {
          Scanner input(value);
          double n = input.number();
          if (!input.done())
            fail("Invalid ellipse angle");
          return n;
        };
        start = degrees(node.attributeNS(DRAW, "start-angle", "0"));
        double end = degrees(node.attributeNS(DRAW, "end-angle", "360"));
        sweep = std::fmod(end - start, 360);
        if (sweep <= 0)
          sweep += 360;
      }
      auto closure = kind == "arc"       ? EllipseClosure::Open
                     : kind == "section" ? EllipseClosure::Pie
                                         : EllipseClosure::Chord;
      shape.path = std::make_shared<ScenePath>(
          ellipse_path(shape.width, shape.height, start, sweep, closure));
    } else if (name == "path") {
      shape.kind = SceneShape::Path;
      if (!node.hasAttributeNS(SVG, "d"))
        fail("Path data is missing");
      shape.path = std::make_shared<ScenePath>(
          parse_svg_path(node.attributeNS(SVG, "d")));
      auto view = node.attributeNS(SVG, "viewBox");
      if (!view.empty()) {
        Scanner values(view);
        auto x = values.number(), y = values.number(), w = values.number(),
             h = values.number();
        if (!values.done() || w <= 0 || h <= 0)
          fail("Invalid path viewBox");
        shape.path_map = {
            shape.width / w,      0, 0, shape.height / h, -x * shape.width / w,
            -y * shape.height / h};
      }
    } else if (name == "polygon" || name == "polyline") {
      shape.kind =
          name == "polygon" ? SceneShape::Polygon : SceneShape::Polyline;
      auto points = polygon(node, shape.width, shape.height);
      if (points.size() < 2)
        fail("Invalid presentation polygon");
      shape.contours.push_back({std::move(points), name == "polygon"});
    } else if (name == "connector") {
      shape.kind = SceneShape::Path;
      local = parse_scene_transform(node.attributeNS(DRAW, "transform"));
      shape.transform = transform * local;
      try {
        auto spec = read_scene_connector(node);
        SceneConnectorEnds ends;
        try {
          ends = resolve_scene_connector_ends(
              spec, shape.transform,
              [&](const Text &id) { return find_target(source, id, &slide); });
        } catch (const std::exception &e) {
          if (strict_connector_updates)
            throw;
          if (!spec.path || !spec.start.x || !spec.start.y || !spec.end.x ||
              !spec.end.y)
            throw;
          ends.start.point = {*spec.start.x, *spec.start.y};
          ends.end.point = {*spec.end.x, *spec.end.y};
          note(slide.unsupported,
               "Connector cached path used: " + Text(e.what()));
        }
        auto routed = scene_connector_geometry(
            spec, ends, 12, 100000,
            recompute_connectors.contains(node.identity()));
        shape.path = std::move(routed.path);
        shape.path_map = routed.mapping;
        auto bounds = path_bounds(*shape.path, shape.path_map);
        shape.width = bounds.width;
        shape.height = bounds.height;
        shape.text_area = bounds;
        if (has_flow && layout) {
          double width = std::max(bounds.width, shape.paint.font.size * 6),
                 height = std::max(bounds.height, shape.paint.font.size * 2);
          shape.text_area =
              SceneRect{bounds.x + (bounds.width - width) / 2,
                        bounds.y + (bounds.height - height) / 2, width, height};
        }
      } catch (const std::exception &e) {
        if (strict_connector_updates)
          throw;
        note(slide.unsupported,
             "Unresolved native connector: " + Text(e.what()));
        return {};
      }
    } else if (name == "line") {
      shape.kind = SceneShape::Line;
      local = parse_scene_transform(node.attributeNS(DRAW, "transform"));
      shape.transform = transform * local;
      shape.contours.push_back({{{length(node.attributeNS(SVG, "x1")),
                                  length(node.attributeNS(SVG, "y1"))},
                                 {length(node.attributeNS(SVG, "x2")),
                                  length(node.attributeNS(SVG, "y2"))}},
                                false});
    } else {
      note(slide.unsupported, "Unsupported drawing object geometry");
      return {};
    }
    return ReadGeometry{std::move(shape), local};
  }
  void shapes(const XmlElement &parent, const Source &source,
              const SceneTransform &transform, SlideScene &slide, bool master,
              const SceneLayers &layers, int depth = 0,
              const std::string &path = {}) {
    if (depth > 256)
      fail("Presentation group nesting exceeds the limit");
    for (auto node = parent.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement() && node.namespaceURI().contains("animation"))
        note(slide.unsupported, "Animations and transitions");
    const auto items = native_stacking_children(parent);
    nodes += items.size();
    if (nodes > 100000)
      fail("Presentation shape count exceeds the limit");
    for (const auto &item : items) {
      auto node = item.node;
      if (node.namespaceURI() != DRAW) {
        note(slide.unsupported, "Annotations and 3D objects");
        continue;
      }
      auto name = node.localName();
      auto display = node.attributeNS(DRAW, "display", "always");
      if (layers.missing(node))
        note(slide.unsupported,
             "Missing drawing layer (displayed with default settings)");
      if (!layers.visible(node, target) || display == "none" ||
          (target == SceneTarget::Screen && display == "printer") ||
          (target == SceneTarget::Print && display == "screen"))
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
        shapes(node, source, group, slide, master, layers, depth + 1,
               path + "/" + std::to_string(item.order));
        continue;
      }
      if (name == "glue-point")
        continue;
      auto geometry =
          make_geometry(node, source, transform, slide, master, layers);
      if (!geometry)
        continue;
      auto shape = std::move(geometry->shape);
      if (!master)
        if (auto found =
                editor_ids.find(path + "/" + std::to_string(item.order));
            found != editor_ids.end())
          shape.editor_index = found->second;
      if (shape.path) {
        path_bounds(*shape.path, shape.transform * shape.path_map);
        shape.contours =
            flatten_path(*shape.path, shape.path_map, shape.transform, .1);
      }
      if (shape.contours.empty() && !shape.path) {
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
      if (shape.paint.stroke && (shape.paint.marker_start.definition ||
                                 shape.paint.marker_end.definition))
        shape.marker_contours =
            marker_stroke_geometry(shape, shape.contours, shape.transform, .1)
                .markers;
      for (const auto &c : shape.marker_contours) {
        points += c.points.size();
        if (points > 1000000)
          fail("Presentation marker geometry exceeds the point limit");
        for (auto p : c.points) {
          auto q = shape.transform.map(p);
          if (!std::isfinite(q.x) || !std::isfinite(q.y) ||
              std::abs(q.x) > 1e9 || std::abs(q.y) > 1e9)
            fail("Presentation marker coordinates exceed the limit");
        }
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
      if (shape.layer_protected)
        for (auto &handle : shape.handles)
          handle.editable = false;
      if (shape.editor_index >= 0)
        slide.editor_shapes.at(shape.editor_index) = slide.shapes.size();
      slide.shapes.push_back(std::move(shape));
    }
  }
  SlideScene slide(const XmlElement &page, int index) {
    SlideScene slide;
    slide.source = page;
    slide.layers = document_layers.scoped(page);
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
    if (master_background)
      resolve_gradient(background, slide);
    resolve_gradient(foreground, slide);
    if (master_background && background.fill) {
      slide.background = background.fill_color;
      slide.background_paints.push_back(background);
    }
    if (foreground.fill) {
      slide.background = foreground.fill_color;
      slide.background_paints.push_back(foreground);
    }
    if (master_shapes && !master.isNull())
      shapes(master, styles, {}, slide, true, document_layers.scoped(master));
    editor_ids.clear();
    collect_editors(page, slide);
    slide.editor_shapes.resize(slide.editable_sources.size());
    shapes(page, content, {}, slide, false, slide.layers);
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
double parse_scene_length(const Text &value, double fallback) {
  return length(value, fallback);
}
Text format_scene_transform(const SceneTransform &m) {
  TextList values;
  for (double v : {m.a, m.b, m.c, m.d})
    values << Text::number(v, 'g', 17);
  values << "0" << "0";
  return "matrix(" + values.join(' ') + ") translate(" +
         Text::number(m.e, 'g', 17) + "pt " + Text::number(m.f, 'g', 17) +
         "pt)";
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
  if (path)
    return path_bounds(*path, transform * path_map);
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
  std::map<size_t, std::pair<bool, int>> fills;
  for (const auto &contour : contours) {
    auto n = contour.points.size();
    auto &[inside, winding] = fills[contour.paint_group];
    if (n >= 3 && paint.fill && contour.fill)
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
    if (paint.stroke && contour.stroke)
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
  bool even_odd = path && path->even_odd ? *path->even_odd : paint.even_odd;
  for (const auto &[group, state] : fills)
    if (even_odd ? state.first : state.second != 0)
      return true;
  int marker_winding = 0;
  for (const auto &c : marker_contours)
    for (size_t i = 0; i < c.points.size(); ++i) {
      auto a = c.points[i], b = c.points[(i + 1) % c.points.size()];
      double cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
      if (a.y <= p.y && b.y > p.y && cross > 0)
        ++marker_winding;
      if (a.y > p.y && b.y <= p.y && cross < 0)
        --marker_winding;
    }
  if (marker_winding)
    return true;
  if (!text.empty()) {
    auto area = text_area.value_or(SceneRect{0, 0, width, height});
    if (p.x >= area.x && p.y >= area.y && p.x <= area.x + area.width &&
        p.y <= area.y + area.height)
      return true;
  }
  return false;
}
int SlideScene::hit_test(ScenePoint p) const {
  for (size_t i = shapes.size(); i > 0; --i)
    if (!shapes[i - 1].master && shapes[i - 1].hit(p))
      return i - 1;
  return -1;
}
std::vector<SlideScene>
read_presentation_scenes(const OdfPackage &package,
                         std::shared_ptr<OdfTextReader> reader,
                         SceneTarget target) {
  if (package.kind() != Kind::Stage)
    fail("Slide scenes require a presentation");
  Catalog catalog(package, std::move(reader), target);
  auto pages = children(package.body(), DRAW, "page");
  if (pages.empty() || pages.size() > 4096)
    fail("Invalid presentation page count");
  std::vector<SlideScene> result;
  for (size_t i = 0; i < pages.size(); ++i)
    result.push_back(catalog.slide(pages[i], i));
  return result;
}
std::vector<SceneConnectorTarget>
read_presentation_connector_targets(const OdfPackage &package, int index,
                                    XmlElement excluded) {
  if (package.kind() != Kind::Stage)
    fail("Connector targets require a presentation");
  auto page = children(package.body(), DRAW, "page").at(index);
  Catalog catalog(package, {}, SceneTarget::Screen);
  catalog.strict_connector_updates = true;
  std::vector<SceneConnectorTarget> result, other_pages;
  size_t points = 0;
  const Text XML = "http://www.w3.org/XML/1998/namespace";
  for (size_t i = 0; i < catalog.targets.size(); ++i) {
    auto &record = catalog.targets[i];
    if (record.master)
      continue;
    auto parent = record.node;
    while (!parent.isNull() &&
           !(parent.namespaceURI() == DRAW && parent.localName() == "page"))
      parent = parent.parentNode().toElement();
    if (parent.isNull())
      continue;
    SlideScene context;
    context.source = parent;
    context.layers = catalog.document_layers.scoped(parent);
    SceneConnectorTarget target;
    target.source = record.node;
    target.identity = record.node.hasAttributeNS(XML, "id")
                          ? record.node.attributeNS(XML, "id")
                          : record.node.attributeNS(DRAW, "id");
    target.can_assign_identity = context.editable(record.node, false);
    for (auto n = excluded; !n.isNull(); n = n.parentNode().toElement())
      if (n == record.node)
        target.error = "This attachment would depend on its own connector";
    if (target.error.empty())
      try {
        if (!target.identity.empty()) {
          auto at = catalog.target_ids.find({false, target.identity});
          if (at == catalog.target_ids.end() || at->second != i)
            fail("Ambiguous drawing identity");
        }
        target.geometry = *catalog.target_object(i);
        if (target.geometry->points.size() > 1000000 - points)
          fail("Connector target point inventory exceeds the limit");
        points += target.geometry->points.size();
      } catch (const std::exception &e) {
        target.geometry.reset();
        target.error = Text(e.what());
      }
    if (parent == page)
      result.push_back(std::move(target));
    else
      other_pages.push_back(std::move(target));
  }
  result.insert(result.end(), std::make_move_iterator(other_pages.begin()),
                std::make_move_iterator(other_pages.end()));
  return result;
}
void refresh_presentation_connector_paths(
    OdfPackage &candidate, const std::vector<XmlElement> &roots) {
  if (roots.empty())
    return;
  if (candidate.kind() != Kind::Stage || candidate.read_only())
    fail("Connector updates require an editable presentation candidate");
  if (candidate.xml.elementsByTagNameNS(DRAW, "connector").empty())
    return;
  Catalog catalog(candidate, {}, SceneTarget::Screen);
  catalog.strict_connector_updates = true;
  using Key = std::pair<bool, Text>;
  std::map<Key, std::vector<size_t>> dependents;
  std::map<const void *, size_t> identities;
  for (size_t i = 0; i < catalog.targets.size(); ++i) {
    const auto &record = catalog.targets[i];
    identities.emplace(record.node.identity(), i);
    if (record.node.localName() != "connector")
      continue;
    for (auto end : {"start-shape", "end-shape"}) {
      auto ref = record.node.attributeNS(DRAW, end).trimmed();
      if (!ref.empty())
        dependents[{record.source == &catalog.styles, ref}].push_back(i);
    }
  }
  std::set<size_t> root_indices;
  for (const auto &root : roots) {
    if (!root.isElement() || root.ownerDocument().documentElement() !=
                                 candidate.xml.documentElement())
      fail("Connector geometry root belongs to another document");
    auto at = identities.find(root.identity());
    if (at == identities.end() || catalog.targets[at->second].master)
      fail("Connector geometry root is not an editable drawing object");
    root_indices.insert(at->second);
  }
  std::vector<bool> affected(catalog.targets.size());
  std::vector<size_t> pending;
  auto enqueue = [&](size_t i) {
    if (!affected[i]) {
      affected[i] = true;
      pending.push_back(i);
    }
  };
  size_t work = 0;
  for (size_t i = 0; i < catalog.targets.size(); ++i)
    for (auto at = std::optional<size_t>(i); at;
         at = catalog.targets[*at].parent) {
      if (++work > 1000000)
        fail("Connector edit dependency traversal exceeds the limit");
      if (root_indices.contains(*at)) {
        enqueue(i);
        break;
      }
    }
  const Text XML = "http://www.w3.org/XML/1998/namespace";
  for (size_t n = 0; n < pending.size(); ++n) {
    const auto &record = catalog.targets[pending[n]];
    if (record.parent)
      enqueue(*record.parent);
    auto id = (record.node.hasAttributeNS(XML, "id")
                   ? record.node.attributeNS(XML, "id")
                   : record.node.attributeNS(DRAW, "id"))
                  .trimmed();
    auto at = dependents.find({record.source == &catalog.styles, id});
    if (id.empty() || at == dependents.end())
      continue;
    for (auto dependent : at->second) {
      if (++work > 1000000)
        fail("Connector edit dependency traversal exceeds the limit");
      enqueue(dependent);
      catalog.recompute_connectors.insert(
          catalog.targets[dependent].node.identity());
    }
  }
  struct Update {
    XmlElement node;
    SceneConnectorEnds ends;
    SceneConnectorGeometry geometry;
  };
  std::vector<Update> updates;
  for (auto i : pending) {
    auto &record = catalog.targets[i];
    if (record.node.localName() != "connector")
      continue;
    auto page = record.node.parentNode().toElement();
    while (!page.isNull() &&
           !(page.namespaceURI() == DRAW && page.localName() == "page"))
      page = page.parentNode().toElement();
    if (page.isNull() || record.master)
      fail("Dependent connector has no editable slide");
    SlideScene context;
    context.source = page;
    context.layers = catalog.document_layers.scoped(page);
    context.require_editable(record.node, false);
    auto spec = read_scene_connector(record.node);
    if (!spec.enabled)
      continue;
    auto parent = catalog.target_parent(record);
    auto matrix = parent * parse_scene_transform(
                               record.node.attributeNS(DRAW, "transform"));
    auto ends = resolve_scene_connector_ends(spec, matrix, [&](const Text &id) {
      return catalog.find_target(*record.source, id);
    });
    auto geometry = scene_connector_geometry(
        spec, ends, 12, 100000,
        catalog.recompute_connectors.contains(record.node.identity()));
    updates.push_back({record.node, std::move(ends), std::move(geometry)});
  }
  // Resolve the graph before writing caches, so dependent routes do not vary
  // with XML order. Each caller owns an isolated, unpublished candidate.
  for (const auto &update : updates)
    write_scene_connector_geometry(update.node, update.ends, update.geometry);
}
} // namespace DomeOffice
