// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_paint.h"
#include "native_stage_source.h"
#include "native_stroke.h"
#include "native_path.h"
#include "native_marker.h"
#include "text_documents.h"
#include <charconv>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_set>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void walk(XmlElement root, const std::function<void(XmlElement)> &visit) {
  std::vector<std::pair<XmlElement, int>> pending{{root, 0}};
  size_t count = 0;
  while (!pending.empty()) {
    auto [node, depth] = pending.back();
    pending.pop_back();
    if (node.isNull())
      continue;
    if (++count > 1000000 || depth > 256)
      fail("Drawing style traversal exceeds the limit");
    visit(node);
    for (auto child = node.lastChild(); !child.isNull();
         child = child.previousSibling())
      if (child.isElement())
        pending.emplace_back(child.toElement(), depth + 1);
  }
}
std::vector<XmlElement> objects(const SlideScene &scene,
                                const std::vector<NativeObjectRef> &refs) {
  std::vector<XmlElement> result;
  for (auto ref : native_object_selection(scene, refs)) {
    auto root = native_object_source(scene, ref);
    walk(root, [&](XmlElement node) {
      if (node.namespaceURI() != DRAW)
        return;
      auto name = node.localName();
      if (name == "g")
        return;
      // Drawables are the direct children of a page/group. Frame contents and
      // text children are retained under their enclosing shape.
      if (node != root && (node.parentNode().namespaceURI() != DRAW ||
                           node.parentNode().localName() != "g"))
        return;
      if (!TextList{"rect", "ellipse", "circle", "path", "polygon", "polyline",
                    "line", "custom-shape", "frame"}
               .contains(name))
        fail("This group contains an object without native paint editing");
      if (result.size() >= 100000)
        fail("Too many objects in drawing style selection");
      result.push_back(node);
    });
  }
  return result;
}
Text color(Text value) {
  if (value == "black")
    return "#000000";
  if (value == "white")
    return "#ffffff";
  auto s = value.toStdString();
  unsigned v = 0;
  if (s.size() != 7 || s[0] != '#')
    fail("Enter a color as #RRGGBB");
  auto r = std::from_chars(s.data() + 1, s.data() + 7, v, 16);
  if (r.ec != std::errc() || r.ptr != s.data() + 7)
    fail("Enter a color as #RRGGBB");
  for (auto &c : s)
    if (c >= 'A' && c <= 'F')
      c += 'a' - 'A';
  return Text(s);
}
double alpha(Text value) {
  bool percent = value.endsWith('%'), ok = false;
  if (percent)
    value.chop(1);
  double v = value.toDouble(&ok);
  if (percent)
    v /= 100;
  if (!ok || !std::isfinite(v) || v < 0 || v > 1)
    fail("Invalid drawing opacity");
  return v;
}
std::shared_ptr<const SceneMarkerDefinition>
marker_definition(const NativeMarkerDefinition &definition) {
  auto xml = read_document_xml(
      ("<draw:marker xmlns:draw=\"" + DRAW + "\" xmlns:svg=\"" + SVG + "\"/>")
          .toUtf8());
  auto node = xml.documentElement();
  node.setAttributeNS(SVG, "svg:d", definition.data);
  node.setAttributeNS(SVG, "svg:viewBox", definition.view_box);
  return read_scene_marker(node);
}
void validate(const NativePaintFormat &p) {
  for (const auto *m : {&p.marker_start, &p.marker_end}) {
    if (m->width &&
        (!std::isfinite(*m->width) || *m->width < 0 || *m->width > 10000))
      fail("Marker width must be between 0 and 10000 points");
    if (m->definition) {
      auto parsed = marker_definition(*m->definition);
      (void)marker_contours(*parsed, m->width.value_or(10), false,
                            {{0, 0}, {1, 0}});
    }
  }
  if (p.stroke_cap &&
      !TextList{"butt", "round", "square"}.contains(*p.stroke_cap))
    fail("Choose Butt, Round or Square stroke caps");
  if (p.stroke_join &&
      !TextList{"miter", "round", "bevel", "none"}.contains(*p.stroke_join))
    fail("Choose Miter, Round, Bevel or None stroke joins");
  if (p.fill && *p.fill != "none" && *p.fill != "solid")
    fail("Choose None or Solid fill");
  if (p.stroke && *p.stroke != "none" && *p.stroke != "solid" &&
      *p.stroke != "dash")
    fail("Choose None, Solid or Dash stroke");
  if (p.dash_pattern)
    (void)p.dash_pattern->lengths(p.stroke_width.value_or(1));
  if (p.dash_names && p.dash_names->size() > 63)
    fail("Too many drawing dash overlay passes");
  for (auto value : {p.fill_color, p.stroke_color})
    if (value)
      (void)color(*value);
  for (auto value : {p.fill_opacity, p.stroke_opacity, p.image_opacity})
    if (value && (!std::isfinite(*value) || *value < 0 || *value > 1))
      fail("Opacity must be between 0 and 100 percent");
  if (p.stroke_width && (!std::isfinite(*p.stroke_width) ||
                         *p.stroke_width < 0 || *p.stroke_width > 10000))
    fail("Stroke width must be between 0 and 10000 points");
}
struct Catalog {
  XmlDocument styles;
  std::map<std::pair<Text, Text>, XmlElement> common, automatic;
  std::map<Text, XmlElement> defaults;
  std::set<Text> reserved;
  std::map<Text, XmlElement> dashes;
  std::optional<Text> created_dash;
  std::map<Text, XmlElement> markers;
  std::map<std::pair<Text, Text>, Text> created_markers;
  explicit Catalog(const OdfPackage &p) {
    const auto bytes = p.entry("styles.xml");
    if (!bytes.empty()) {
      styles = read_document_xml(bytes);
      scan(styles, false);
    }
    scan(p.xml, true);
  }
  void scan(XmlDocument doc, bool content) {
    walk(doc.documentElement(), [&](XmlElement node) {
      for (const auto &a : node.attributes()) {
        reserved.insert(a.value);
        for (const auto &token : a.value.split(' '))
          reserved.insert(token);
      }
    });
    for (auto group : children(doc.documentElement(), OFFICE)) {
      const bool aut = group.localName() == "automatic-styles";
      if (!aut && group.localName() != "styles")
        continue;
      if (!aut) {
        for (auto node : children(group, DRAW, "stroke-dash"))
          dashes[node.attributeNS(DRAW, "name")] = node;
        for (auto node : children(group, DRAW, "marker"))
          markers[node.attributeNS(DRAW, "name")] = node;
      }
      for (auto node : children(group, STYLE)) {
        auto name = node.attributeNS(STYLE, "name"),
             family = node.attributeNS(STYLE, "family");
        if (node.localName() == "default-style")
          defaults[family] = node;
        if (node.localName() == "style" && (!aut || content))
          (aut ? automatic : common)[{family, name}] = node;
      }
    }
  }
  XmlElement own(const Text &family, const Text &name) const {
    auto a = automatic.find({family, name});
    if (a != automatic.end())
      return a->second;
    auto b = common.find({family, name});
    return b == common.end() ? XmlElement() : b->second;
  }
  std::vector<XmlElement> chain(const Text &family, Text name) const {
    std::vector<XmlElement> out;
    auto current = own(family, name);
    if (!name.empty() && current.isNull())
      fail("The object's drawing style is unresolved");
    while (!current.isNull()) {
      if (out.size() >= 256 ||
          std::find(out.begin(), out.end(), current) != out.end())
        fail("Cyclic drawing style inheritance");
      out.push_back(current);
      name = current.attributeNS(STYLE, "parent-style-name");
      current = {};
      if (!name.empty()) {
        auto found = common.find({family, name});
        if (found == common.end())
          fail("The parent drawing style must be a defined common style");
        current = found->second;
      }
    }
    if (auto d = defaults.find(family); d != defaults.end())
      out.push_back(d->second);
    std::reverse(out.begin(), out.end());
    return out;
  }
  using Properties = std::map<std::pair<Text, Text>, Text>;
  Properties effective(XmlElement object) const {
    if (object.hasAttributeNS(DRAW, "class-names") ||
        object.hasAttributeNS(PRESENTATION, "class-names"))
      fail("Drawing style class lists need native cascade support before "
           "editing");
    Properties out;
    for (auto family : {Text("graphic"), Text("presentation")}) {
      auto name = object.attributeNS(family == "graphic" ? DRAW : PRESENTATION,
                                     "style-name");
      if (family == "presentation" && name.empty())
        continue;
      for (auto style : chain(family, name))
        for (auto a : child(style, STYLE, "graphic-properties").attributes())
          out[{a.uri, a.name.mid(a.name.indexOf(':') + 1)}] = a.value;
    }
    return out;
  }
  NativePaintFormat info(XmlElement object) const {
    auto props = effective(object);
    auto get = [&](const Text &ns, const Text &name, const Text &fallback) {
      auto it = props.find({ns, name});
      return it == props.end() ? fallback : it->second;
    };
    NativePaintFormat out;
    out.fill = get(DRAW, "fill", "none");
    out.stroke = get(DRAW, "stroke", "none");
    out.fill_color = color(get(DRAW, "fill-color", "#ffffff"));
    out.stroke_color = color(get(SVG, "stroke-color", "#000000"));
    out.fill_opacity = alpha(get(DRAW, "opacity", "100%"));
    out.stroke_opacity = alpha(get(SVG, "stroke-opacity", "100%"));
    out.image_opacity = alpha(get(DRAW, "image-opacity", "100%"));
    out.stroke_width = parse_scene_length(get(SVG, "stroke-width", "1pt"));
    out.even_odd = get(SVG, "fill-rule", "nonzero") == "evenodd";
    out.stroke_cap = get(SVG, "stroke-linecap", "butt");
    out.stroke_join = get(DRAW, "stroke-linejoin", "miter");
    auto dash = get(DRAW, "stroke-dash", "");
    if (auto at = dashes.find(dash); at != dashes.end())
      out.dash_pattern = read_scene_dash_pattern(at->second);
    TextList names;
    std::istringstream input(get(DRAW, "stroke-dash-names", "").toStdString());
    for (std::string name; input >> name;)
      names.push_back(Text(name));
    out.dash_names = names;
    auto marker_info = [&](const Text &end) {
      NativeMarkerFormat m;
      m.name = get(DRAW, "marker-" + end, "");
      m.width =
          parse_scene_length(get(DRAW, "marker-" + end + "-width", "0pt"));
      auto center = get(DRAW, "marker-" + end + "-center", "false");
      if (center != "true" && center != "false" && center != "1" &&
          center != "0")
        fail("Invalid marker centering");
      m.centered = center == "true" || center == "1";
      if (auto at = markers.find(*m.name); at != markers.end())
        m.definition =
            NativeMarkerDefinition{at->second.attributeNS(SVG, "d"),
                                   at->second.attributeNS(SVG, "viewBox")};
      return m;
    };
    out.marker_start = marker_info("start");
    out.marker_end = marker_info("end");
    return out;
  }
  Text name() {
    for (size_t n = 1; n <= reserved.size() + 1; ++n) {
      auto candidate = Text("NativePaint%1").arg(int(n));
      if (reserved.insert(candidate).second)
        return candidate;
    }
    fail("No drawing style name is available");
  }
  XmlElement common_styles(OdfPackage &p) {
    auto root = styles.documentElement();
    if (root.isNull()) {
      styles = read_document_xml(
          ("<office:document-styles xmlns:office=\"" + OFFICE + "\"/>")
              .toUtf8());
      root = styles.documentElement();
      root.setAttributeNS(
          OFFICE, "office:version",
          p.xml.documentElement().attributeNS(OFFICE, "version", "1.2"));
    }
    auto common_node = child(root, OFFICE, "styles");
    if (common_node.isNull()) {
      common_node = styles.createElementNS(OFFICE, "office:styles");
      auto following = child(root, OFFICE, "automatic-styles");
      if (following.isNull())
        following = child(root, OFFICE, "master-styles");
      root.insertBefore(common_node, following);
    }
    return common_node;
  }
  Text marker(OdfPackage &p, const NativeMarkerDefinition &definition) {
    const auto key = std::make_pair(definition.data, definition.view_box);
    if (auto at = created_markers.find(key); at != created_markers.end())
      return at->second;
    auto common_node = common_styles(p);
    if (children(common_node, DRAW, "marker").size() >= 100000)
      fail("Too many drawing marker definitions");
    auto node = styles.createElementNS(DRAW, "draw:marker");
    auto id = name();
    node.setAttributeNS(DRAW, "draw:name", id);
    node.setAttributeNS(SVG, "svg:d", definition.data);
    node.setAttributeNS(SVG, "svg:viewBox", definition.view_box);
    common_node.appendChild(node);
    p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
    created_markers.emplace(key, id);
    return id;
  }
  Text dash(OdfPackage &p, const SceneDashPattern &pattern) {
    if (created_dash)
      return *created_dash;
    (void)pattern.lengths(1);
    auto common_node = common_styles(p);
    if (children(common_node, DRAW, "stroke-dash").size() >= 100000)
      fail("Too many drawing dash definitions");
    auto node = styles.createElementNS(DRAW, "draw:stroke-dash");
    auto id = name();
    node.setAttributeNS(DRAW, "draw:name", id);
    node.setAttributeNS(DRAW, "draw:style", pattern.round ? "round" : "rect");
    node.setAttributeNS(DRAW, "draw:dots1", Text::number(pattern.first));
    node.setAttributeNS(DRAW, "draw:dots2", Text::number(pattern.second));
    for (auto [key, value] : {std::pair{"dots1-length", pattern.first_length},
                              std::pair{"dots2-length", pattern.second_length},
                              std::pair{"distance", pattern.distance}})
      if (value)
        node.setAttributeNS(
            DRAW, Text("draw:") + key,
            Text::number(value->value * (value->relative ? 100 : 1), 'g', 17) +
                (value->relative ? "%" : "pt"));
    common_node.appendChild(node);
    p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
    created_dash = id;
    return id;
  }
  void change(OdfPackage &p, XmlElement object,
              const NativePaintFormat &patch) {
    auto props = effective(object);
    if (patch.fill_opacity && props.contains({DRAW, "opacity-name"}) &&
        !props.at({DRAW, "opacity-name"}).empty())
      fail("An opacity gradient must be edited before assigning uniform "
           "opacity");
    const Text family = object.attributeNS(PRESENTATION, "style-name").empty()
                            ? "graphic"
                            : "presentation";
    const Text uri = family == "graphic" ? DRAW : PRESENTATION;
    auto old = object.attributeNS(uri, "style-name");
    auto base = own(family, old);
    XmlElement style;
    if (automatic.contains({family, old}))
      style = p.xml.importNode(base, true).toElement();
    else {
      style = p.xml.createElementNS(STYLE, "style:style");
      style.setAttributeNS(STYLE, "style:family", family);
      if (!base.isNull())
        style.setAttributeNS(STYLE, "style:parent-style-name", old);
    }
    auto new_name = name();
    style.setAttributeNS(STYLE, "style:name", new_name);
    auto graphic = child(style, STYLE, "graphic-properties");
    if (graphic.isNull()) {
      graphic = p.xml.createElementNS(STYLE, "style:graphic-properties");
      style.insertBefore(graphic, style.firstChild());
    }
    auto put = [&](const Text &ns, const Text &key, const Text &value) {
      graphic.setAttributeNS(ns, key, value);
    };
    auto current = info(object);
    auto put_marker = [&](const Text &end, const NativeMarkerFormat &m,
                          const NativeMarkerFormat &before) {
      if (m.name && m.definition) {
        auto at = markers.find(*m.name);
        if (at == markers.end() ||
            at->second.attributeNS(SVG, "d") != m.definition->data ||
            at->second.attributeNS(SVG, "viewBox") != m.definition->view_box)
          fail("Choose an existing marker or a new definition");
      }
      if (m.name) {
        if (!m.name->empty() && !markers.contains(*m.name))
          fail("Choose a defined common marker name");
        put(DRAW, "draw:marker-" + end, *m.name);
      }
      if (m.definition && !m.name && m.definition != before.definition)
        put(DRAW, "draw:marker-" + end, marker(p, *m.definition));
      if (m.width)
        put(DRAW, "draw:marker-" + end + "-width",
            Text::number(*m.width, 'g', 17) + "pt");
      if (m.centered)
        put(DRAW, "draw:marker-" + end + "-center",
            *m.centered ? "true" : "false");
    };
    put_marker("start", patch.marker_start, current.marker_start);
    put_marker("end", patch.marker_end, current.marker_end);
    if (patch.fill)
      put(DRAW, "draw:fill", *patch.fill);
    if (patch.dash_pattern) {
      put(DRAW, "draw:stroke-dash", dash(p, *patch.dash_pattern));
      if (!patch.stroke)
        put(DRAW, "draw:stroke", "dash");
    }
    if (patch.dash_names) {
      for (const auto &id : *patch.dash_names)
        if (!dashes.contains(id))
          fail("Choose defined common dash names for overlays");
      put(DRAW, "draw:stroke-dash-names", patch.dash_names->join(' '));
    }
    if (patch.stroke)
      put(DRAW, "draw:stroke", *patch.stroke);
    if (patch.fill_color)
      put(DRAW, "draw:fill-color", color(*patch.fill_color));
    if (patch.stroke_color)
      put(SVG, "svg:stroke-color", color(*patch.stroke_color));
    if (patch.fill_opacity)
      put(DRAW, "draw:opacity",
          Text::number(*patch.fill_opacity * 100, 'g', 17) + "%");
    if (patch.stroke_opacity)
      put(SVG, "svg:stroke-opacity",
          Text::number(*patch.stroke_opacity * 100, 'g', 17) + "%");
    if (patch.image_opacity)
      put(DRAW, "draw:image-opacity",
          Text::number(*patch.image_opacity * 100, 'g', 17) + "%");
    if (patch.stroke_cap)
      put(SVG, "svg:stroke-linecap", *patch.stroke_cap);
    if (patch.stroke_join)
      put(DRAW, "draw:stroke-linejoin", *patch.stroke_join);
    if (patch.stroke_width)
      put(SVG, "svg:stroke-width",
          Text::number(*patch.stroke_width, 'g', 17) + "pt");
    if (patch.even_odd)
      put(SVG, "svg:fill-rule", *patch.even_odd ? "evenodd" : "nonzero");
    auto automatic_node =
        child(p.xml.documentElement(), OFFICE, "automatic-styles");
    if (automatic_node.isNull()) {
      automatic_node = p.xml.createElementNS(OFFICE, "office:automatic-styles");
      p.xml.documentElement().insertBefore(
          automatic_node, child(p.xml.documentElement(), OFFICE, "body"));
    }
    if (children(automatic_node, STYLE, "style").size() >= 100000)
      fail("Too many automatic drawing styles");
    automatic_node.appendChild(style);
    object.setAttributeNS(uri,
                          family == "graphic" ? "draw:style-name"
                                              : "presentation:style-name",
                          new_name);
  }
};
template <class T>
void aggregate(std::optional<T> &value, const std::optional<T> &next) {
  if (value != next)
    value.reset();
}
bool changes(const NativeMarkerFormat &before,
             const NativeMarkerFormat &patch) {
  return (patch.name && patch.name != before.name) ||
         (patch.definition && patch.definition != before.definition) ||
         (patch.width && patch.width != before.width) ||
         (patch.centered && patch.centered != before.centered);
}
void aggregate(NativeMarkerFormat &value, const NativeMarkerFormat &next) {
  aggregate(value.name, next.name);
  aggregate(value.definition, next.definition);
  aggregate(value.width, next.width);
  aggregate(value.centered, next.centered);
}
bool changes(const NativePaintFormat &before, const NativePaintFormat &patch) {
  return changes(before.marker_start, patch.marker_start) ||
         changes(before.marker_end, patch.marker_end) ||
         (patch.dash_pattern && (patch.dash_pattern != before.dash_pattern ||
                                 (!patch.stroke && before.stroke != "dash"))) ||
         (patch.dash_names && patch.dash_names != before.dash_names) ||
         (patch.stroke_cap && patch.stroke_cap != before.stroke_cap) ||
         (patch.stroke_join && patch.stroke_join != before.stroke_join) ||
         (patch.fill && patch.fill != before.fill) ||
         (patch.stroke && patch.stroke != before.stroke) ||
         (patch.fill_color && color(*patch.fill_color) != *before.fill_color) ||
         (patch.stroke_color &&
          color(*patch.stroke_color) != *before.stroke_color) ||
         (patch.fill_opacity && patch.fill_opacity != before.fill_opacity) ||
         (patch.stroke_opacity &&
          patch.stroke_opacity != before.stroke_opacity) ||
         (patch.image_opacity && patch.image_opacity != before.image_opacity) ||
         (patch.stroke_width && patch.stroke_width != before.stroke_width) ||
         (patch.even_odd && patch.even_odd != before.even_odd);
}
} // namespace
bool NativePaintFormat::empty() const {
  return !fill && !fill_color && !stroke && !stroke_color && !fill_opacity &&
         !stroke_opacity && !stroke_width && !even_odd && !image_opacity &&
         !stroke_cap && !stroke_join && !dash_pattern && !dash_names &&
         marker_start.empty() && marker_end.empty();
}
NativePaintInfo
Presentation::object_paint(int index,
                           const std::vector<NativeObjectRef> &refs) const {
  const auto &model = scene(index);
  Catalog catalog(package_);
  NativePaintInfo result;
  for (const auto &[name, node] : catalog.dashes)
    result.available_dashes.push_back(name);
  for (const auto &[name, node] : catalog.markers)
    result.available_markers.push_back(name);
  auto nodes = objects(model, refs);
  result.editable = !read_only();
  for (auto ref : native_object_selection(model, refs))
    result.editable =
        result.editable && model.editable(native_object_source(model, ref));
  std::optional<double> image_opacity;
  for (auto node : nodes) {
    auto info = catalog.info(node);
    if (!child(node, DRAW, "image").isNull()) {
      if (result.image_objects++ == 0)
        image_opacity = info.image_opacity;
      else
        aggregate(image_opacity, info.image_opacity);
    }
    if (result.objects++ == 0)
      result.values = info;
    else {
      aggregate(result.values.fill, info.fill);
      aggregate(result.values.fill_color, info.fill_color);
      aggregate(result.values.stroke, info.stroke);
      aggregate(result.values.stroke_color, info.stroke_color);
      aggregate(result.values.fill_opacity, info.fill_opacity);
      aggregate(result.values.stroke_opacity, info.stroke_opacity);
      aggregate(result.values.stroke_width, info.stroke_width);
      aggregate(result.values.even_odd, info.even_odd);
      aggregate(result.values.stroke_cap, info.stroke_cap);
      aggregate(result.values.stroke_join, info.stroke_join);
      aggregate(result.values.dash_pattern, info.dash_pattern);
      aggregate(result.values.dash_names, info.dash_names);
      aggregate(result.values.marker_start, info.marker_start);
      aggregate(result.values.marker_end, info.marker_end);
    }
  }
  result.values.image_opacity = image_opacity;
  return result;
}
std::optional<PresentationPosition>
Presentation::set_object_paint(int index,
                               const std::vector<NativeObjectRef> &selection,
                               const NativePaintFormat &patch,
                               std::optional<TextSelection> text_selection) {
  if (read_only())
    fail("This presentation is read-only");
  validate(patch);
  if (patch.empty())
    return {};
  finish_handle_drag();
  const auto &model = scene(index);
  const auto refs = native_object_selection(model, selection);
  if (refs.empty())
    fail("Select objects to format");
  for (auto ref : refs)
    model.require_editable(native_object_source(model, ref));
  auto nodes = objects(model, refs);
  if (nodes.empty())
    fail("No drawable objects are selected");
  Catalog catalog(package_);
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  // Capture all candidate identities before inserting an automatic-styles
  // sibling. Structural insertion changes serialized element-rank paths.
  std::vector<XmlElement> chosen, candidate_nodes;
  for (auto ref : refs)
    chosen.push_back(stage_candidate_source(native_object_source(model, ref),
                                            candidate.xml));
  for (auto node : nodes)
    candidate_nodes.push_back(stage_candidate_source(node, candidate.xml));
  bool changed = false;
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto node = nodes[i];
    auto requested = patch;
    if (child(node, DRAW, "image").isNull())
      requested.image_opacity.reset();
    if (changes(catalog.info(node), requested)) {
      catalog.change(candidate, candidate_nodes[i], requested);
      changed = true;
    }
  }
  if (!changed)
    return {};
  candidate.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(candidate);
  auto scenes = read_presentation_scenes(candidate, reader);
  (void)read_presentation_scenes(candidate, reader, SceneTarget::Print);
  for (const auto &slide : scenes)
    for (const auto &shape : slide.shapes)
      if (shape.paint.dashed || !shape.paint.dash_names.empty() ||
          shape.paint.marker_start.definition ||
          shape.paint.marker_end.definition) {
        auto contours = shape.path ? flatten_path(*shape.path, shape.path_map,
                                                  shape.transform)
                                   : shape.contours;
        auto geometry =
            marker_stroke_geometry(shape, contours, shape.transform);
        (void)stroke_pass_outlines(geometry.shafts, shape.paint,
                                   shape.transform, .125, 200000,
                                   geometry.markers);
      }
  auto next_refs = native_object_selection(
      scenes.at(index), native_object_references(scenes.at(index), chosen));
  if (next_refs.empty())
    fail("The formatted objects have no selectable native scene");
  PresentationPosition before{index,
                              model.shapes.at(refs.front().shape).editor_index,
                              refs.front().shape};
  before.objects = refs;
  PresentationPosition after{
      index, scenes.at(index).shapes.at(next_refs.front().shape).editor_index,
      next_refs.front().shape};
  after.objects = next_refs;
  if (refs.size() == 1 && refs.front().ancestors == 0 && before.box >= 0) {
    before.selection = text_selection;
    after.selection = text_selection;
  }
  auto original = package_.xml.toByteArray();
  auto parts = candidate.part_changes_from(package_);
  package_ = std::move(candidate);
  scenes_ = std::move(scenes);
  text_reader_ = std::move(reader);
  ++revision_;
  end_text_edit();
  remember(original, std::move(before), after, false, std::move(parts));
  return after;
}
} // namespace DomeOffice
