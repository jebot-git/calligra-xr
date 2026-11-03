// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_paint.h"
#include "native_stage_source.h"
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
void validate(const NativePaintFormat &p) {
  for (auto mode : {p.fill, p.stroke})
    if (mode && *mode != "none" && *mode != "solid")
      fail("Choose None or Solid paint");
  for (auto value : {p.fill_color, p.stroke_color})
    if (value)
      (void)color(*value);
  for (auto value : {p.fill_opacity, p.stroke_opacity})
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
    out.stroke_width = parse_scene_length(get(SVG, "stroke-width", "1pt"));
    out.even_odd = get(SVG, "fill-rule", "nonzero") == "evenodd";
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
    if (patch.fill)
      put(DRAW, "draw:fill", *patch.fill);
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
bool changes(const NativePaintFormat &before, const NativePaintFormat &patch) {
  return (patch.fill && patch.fill != before.fill) ||
         (patch.stroke && patch.stroke != before.stroke) ||
         (patch.fill_color && color(*patch.fill_color) != *before.fill_color) ||
         (patch.stroke_color &&
          color(*patch.stroke_color) != *before.stroke_color) ||
         (patch.fill_opacity && patch.fill_opacity != before.fill_opacity) ||
         (patch.stroke_opacity &&
          patch.stroke_opacity != before.stroke_opacity) ||
         (patch.stroke_width && patch.stroke_width != before.stroke_width) ||
         (patch.even_odd && patch.even_odd != before.even_odd);
}
} // namespace
bool NativePaintFormat::empty() const {
  return !fill && !fill_color && !stroke && !stroke_color && !fill_opacity &&
         !stroke_opacity && !stroke_width && !even_odd;
}
NativePaintInfo
Presentation::object_paint(int index,
                           const std::vector<NativeObjectRef> &refs) const {
  const auto &model = scene(index);
  Catalog catalog(package_);
  NativePaintInfo result;
  auto nodes = objects(model, refs);
  result.editable = !read_only();
  for (auto ref : native_object_selection(model, refs))
    result.editable =
        result.editable && model.editable(native_object_source(model, ref));
  for (auto node : nodes) {
    auto info = catalog.info(node);
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
    }
  }
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
  bool changed = false;
  for (auto node : nodes)
    if (changes(catalog.info(node), patch)) {
      catalog.change(candidate, stage_candidate_source(node, candidate.xml),
                     patch);
      changed = true;
    }
  if (!changed)
    return {};
  candidate.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(candidate);
  auto scenes = read_presentation_scenes(candidate, reader);
  (void)read_presentation_scenes(candidate, reader, SceneTarget::Print);
  std::vector<XmlElement> chosen;
  for (auto ref : refs)
    chosen.push_back(stage_candidate_source(native_object_source(model, ref),
                                            candidate.xml));
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
  package_ = std::move(candidate);
  scenes_ = std::move(scenes);
  text_reader_ = std::move(reader);
  ++revision_;
  end_text_edit();
  remember(original, std::move(before), after);
  return after;
}
} // namespace DomeOffice
