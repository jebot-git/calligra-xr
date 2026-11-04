// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
#include "native_stage_names.h"
#include <cmath>
namespace DomeOffice {
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
bool valid_id(const Text &id) {
  return !id.empty() && xmlValidateNCName(BAD_CAST id.toUtf8().c_str(), 0) == 0;
}
Text number(double n) {
  if (!std::isfinite(n) || std::abs(n) > 1e9)
    fail("Connector coordinates exceed the native limit");
  return Text::number(n == 0 ? 0 : n, 'g', 17);
}
std::map<Text, size_t> xml_ids(XmlElement root) {
  std::map<Text, size_t> result;
  size_t count = 0;
  auto walk = [&](auto &&self, XmlElement n, size_t depth) -> void {
    if (++count > 1000000 || depth > 256)
      fail("Connector identity inventory exceeds the limit");
    if (n.hasAttributeNS(XML, "id"))
      ++result[n.attributeNS(XML, "id")];
    for (auto child = n.firstChild(); !child.isNull();
         child = child.nextSibling())
      if (child.isElement())
        self(self, child.toElement(), depth + 1);
  };
  walk(walk, root, 0);
  return result;
}
} // namespace
struct NativeConnectorEdit::State {
  OdfPackage package;
  int slide;
  XmlElement source;
  SceneTransform frame;
  uint64_t revision;
  std::shared_ptr<const char> identity;
  NativeConnectorValues original;
  PresentationPosition previous;
  std::vector<NativeConnectorTarget> targets;
  std::vector<SceneConnectorTarget> sources;
  State(const OdfPackage &p) : package(p) {}
};
NativeConnectorEdit::NativeConnectorEdit(const OdfPackage &package, int slide,
                                         XmlElement source, uint64_t revision,
                                         std::shared_ptr<const char> identity,
                                         SceneTransform frame,
                                         PresentationPosition previous) {
  auto state = std::make_shared<State>(package);
  state->slide = slide;
  state->source = source;
  state->frame = frame;
  state->previous = std::move(previous);
  state->revision = revision;
  state->identity = std::move(identity);
  state->sources = read_presentation_connector_targets(package, slide, source);
  auto ids = xml_ids(package.xml.documentElement());
  std::set<Text> labels;
  const auto active_page = children(package.body(), DRAW, "page").at(slide);
  for (auto &target : state->sources) {
    NativeConnectorTarget info;
    info.identity = target.identity;
    info.error = target.error;
    info.geometry = std::move(target.geometry);
    if (target.source.hasAttributeNS(XML, "id") &&
        (!valid_id(info.identity) || ids[info.identity] != 1))
      info.error = "The target has an invalid or duplicate XML identity";
    else if (!info.identity.empty() && !valid_id(info.identity))
      info.error = "The target has an invalid drawing identity";
    else if (info.identity.empty() && !target.can_assign_identity)
      info.error = "The protected target needs an identifier";
    else if (info.geometry && info.geometry->points.empty())
      info.error = "The target has no connector glue points";
    auto label = target.source.attributeNS(DRAW, "name").trimmed();
    if (label.empty())
      label = "Object " + Text::number(state->targets.size() + 1);
    auto owner_page = target.source;
    while (!owner_page.isNull() && !(owner_page.namespaceURI() == DRAW &&
                                     owner_page.localName() == "page"))
      owner_page = owner_page.parentNode().toElement();
    if (owner_page != active_page)
      label =
          owner_page.attributeNS(DRAW, "name", "Other slide") + " / " + label;
    const auto base = label;
    for (size_t occurrence = 2; labels.contains(label); ++occurrence)
      label = base + " (" + Text::number(occurrence) + ")";
    labels.insert(label);
    info.label = label;
    state->targets.push_back(std::move(info));
  }
  auto endpoint = [&](const SceneConnectorEnd &end) {
    NativeConnectorEndpoint result;
    result.glue = end.shape.empty() ? std::optional<uint64_t>() : end.glue;
    result.point = frame.map({end.x.value_or(0), end.y.value_or(0)});
    if (!end.shape.empty()) {
      for (size_t i = 0; i < state->targets.size(); ++i)
        if (state->targets[i].identity == end.shape) {
          result.target = i;
          return result;
        }
      result.unresolved = end.shape;
    }
    return result;
  };
  if (!source.isNull()) {
    auto spec = read_scene_connector(source);
    values.type = spec.type;
    values.skew = spec.skew;
    values.start = endpoint(spec.start);
    values.end = endpoint(spec.end);
    try {
      auto ends = resolve_scene_connector_ends(
          spec, frame, [&](const Text &id) -> const SceneConnectorObject * {
            for (auto &target : state->targets)
              if (target.identity == id && target.error.empty() &&
                  target.geometry)
                return &*target.geometry;
            return static_cast<const SceneConnectorObject *>(nullptr);
          });
      values.start.point = ends.start.slide_point;
      values.end.point = ends.end.slide_point;
    } catch (const std::exception &) {
      // Keep unresolved source available for repair; unchanged Apply is a
      // no-op.
    }
  } else {
    auto scenes = read_presentation_scenes(package);
    const auto &scene = scenes.at(slide);
    values.start.point = {scene.width * .25, scene.height * .35};
    values.end.point = {scene.width * .75, scene.height * .65};
  }
  state->original = values;
  state_ = std::move(state);
}
bool NativeConnectorEdit::creating() const { return state_->source.isNull(); }
const std::vector<NativeConnectorTarget> &NativeConnectorEdit::targets() const {
  return state_->targets;
}
SceneConnectorEnds
NativeConnectorEdit::endpoints(const NativeConnectorValues &values) const {
  SceneConnectorSpec spec;
  auto end = [&](const NativeConnectorEndpoint &value) {
    SceneConnectorEnd out;
    if (!value.unresolved.empty())
      fail("Choose an existing target or a free endpoint");
    if (value.target) {
      if (*value.target >= state_->targets.size())
        fail("Invalid connector target");
      const auto &target = state_->targets[*value.target];
      if (!target.error.empty() || !target.geometry)
        throw std::runtime_error((target.error.empty()
                                      ? Text("Unavailable connector target")
                                      : target.error)
                                     .toStdString());
      out.shape = "target-" + Text::number(*value.target);
      out.glue = value.glue;
    } else {
      if (value.glue)
        fail("A free endpoint has no glue point");
      out.x = value.point.x;
      out.y = value.point.y;
    }
    return out;
  };
  spec.start = end(values.start);
  spec.end = end(values.end);
  return resolve_scene_connector_ends(spec, {}, [&](const Text &id) {
    for (auto index : {values.start.target, values.end.target})
      if (index && id == "target-" + Text::number(*index))
        return &*state_->targets[*index].geometry;
    return static_cast<const SceneConnectorObject *>(nullptr);
  });
}
NativeConnectorEdit
Presentation::edit_connector(int slide, int shape,
                             PresentationPosition previous) const {
  if (read_only())
    fail("This presentation is read-only");
  if (drag_)
    fail("Finish the active gesture before editing a connector");
  const auto &model = scene(slide);
  XmlElement source;
  SceneTransform frame;
  if (shape >= 0) {
    const auto &object = model.shapes.at(shape);
    if (object.master || object.source.localName() != "connector")
      fail("Select one connector");
    model.require_editable(object.source, false);
    source = object.source;
    frame = object.transform;
  }
  previous.slide = slide;
  if (shape >= 0 && previous.shape < 0) {
    previous.shape = shape;
    previous.box = model.shapes.at(shape).editor_index;
    previous.objects = {{shape, 0}};
  }
  if (!previous.objects.empty())
    previous.objects = native_object_selection(model, previous.objects);
  return NativeConnectorEdit(package_, slide, source, revision_,
                             document_identity_, frame, std::move(previous));
}
std::optional<PresentationPosition>
Presentation::commit_connector(const NativeConnectorEdit &draft) {
  const auto &state = *draft.state_;
  if (state.identity != document_identity_ || state.revision != revision_)
    fail("The connector draft belongs to an earlier document revision");
  if (read_only())
    fail("This presentation is read-only");
  if (drag_)
    fail("Finish the active gesture before editing a connector");
  const auto &v = draft.values;
  if (!draft.creating() && v == state.original)
    return {};
  if (v.type < SceneConnectorSpec::Standard ||
      v.type > SceneConnectorSpec::Curve)
    fail("Choose a supported connector route");
  if (v.skew.size() > 3)
    fail("A connector accepts at most three skew offsets");
  for (auto value : v.skew)
    (void)number(value);
  auto inverse = state.frame.inverse();
  if (!inverse)
    fail("The connector coordinate frame is singular");
  const auto &model = scene(state.slide);
  if (!state.source.isNull())
    model.require_editable(state.source, false);
  auto candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  std::map<size_t, XmlElement> targets;
  for (const auto *end : {&v.start, &v.end}) {
    if (!end->unresolved.empty())
      fail("Choose an existing target or a free endpoint");
    if (end->target) {
      if (*end->target >= state.targets.size())
        fail("Invalid connector target");
      const auto &info = state.targets[*end->target];
      if (!info.error.empty() || !info.geometry)
        throw std::runtime_error((info.error.empty()
                                      ? Text("Unavailable connector target")
                                      : info.error)
                                     .toStdString());
      targets.emplace(*end->target,
                      stage_candidate_source(state.sources[*end->target].source,
                                             candidate.xml));
    } else {
      if (end->glue)
        fail("A free endpoint has no glue point");
      (void)number(end->point.x);
      (void)number(end->point.y);
    }
  }
  // Clone targets before insertion can add automatic-styles before the body.
  auto node = draft.creating()
                  ? append_stage_connector(candidate, state.slide)
                  : stage_candidate_source(state.source, candidate.xml);
  NativeStageNames names(candidate);
  std::map<size_t, Text> assigned;
  auto endpoint = [&](const NativeConnectorEndpoint &end, const Text &side,
                      const Text &axis) {
    if (end.target) {
      const auto index = *end.target;
      auto id = state.targets[index].identity;
      if (id.empty()) {
        auto at = assigned.find(index);
        if (at != assigned.end())
          id = at->second;
        else {
          if (!state.sources[index].can_assign_identity)
            fail("The protected target needs an identifier");
          id = names.take();
          targets.at(index).setAttributeNS(XML, "xml:id", id);
          assigned.emplace(index, id);
        }
      }
      node.setAttributeNS(DRAW, "draw:" + side + "-shape", id);
      if (end.glue)
        node.setAttributeNS(DRAW, "draw:" + side + "-glue-point",
                            Text::number(*end.glue));
      else
        node.removeAttributeNS(DRAW, side + "-glue-point");
    } else {
      node.removeAttributeNS(DRAW, side + "-shape");
      node.removeAttributeNS(DRAW, side + "-glue-point");
      auto point = inverse->map(end.point);
      node.setAttributeNS(SVG, "svg:x" + axis, number(point.x) + "pt");
      node.setAttributeNS(SVG, "svg:y" + axis, number(point.y) + "pt");
    }
  };
  endpoint(v.start, "start", "1");
  endpoint(v.end, "end", "2");
  const Text types[]{"standard", "line", "lines", "curve"};
  node.setAttributeNS(DRAW, "draw:type", types[v.type]);
  if (v.skew.empty())
    node.removeAttributeNS(DRAW, "line-skew");
  else {
    TextList offsets;
    for (double value : v.skew)
      offsets << number(value) + "pt";
    node.setAttributeNS(DRAW, "draw:line-skew", offsets.join(' '));
  }
  node.removeAttributeNS(SVG, "d");
  node.setAttributeNS(SVG, "svg:viewBox", "0 0 1000 1000");
  refresh_presentation_connector_paths(candidate, {node});
  if (candidate.xml.toByteArray() == package_.xml.toByteArray() &&
      candidate.part_changes_from(package_).empty())
    return {};
  auto before = state.previous;
  end_text_edit();
  return commit_object_structure(std::move(candidate), {node}, before);
}
} // namespace DomeOffice
