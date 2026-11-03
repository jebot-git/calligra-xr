// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_identity.h"
#include "native_stage_source.h"
#include <algorithm>
#include <unordered_set>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void context(XmlElement parent) {
  if (parent.namespaceURI() != DRAW ||
      (parent.localName() != "page" && parent.localName() != "g"))
    fail("Select objects in a slide or drawing group");
}
PresentationPosition previous(const SlideScene &scene, int slide,
                              const std::vector<NativeObjectRef> &refs) {
  if (refs.empty())
    fail("Select at least one object");
  PresentationPosition position{
      slide, scene.shapes.at(refs.front().shape).editor_index,
      refs.front().shape};
  position.objects = refs;
  return position;
}
bool identity(const SceneTransform &t) {
  return t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.e == 0 && t.f == 0;
}
void ungroup_guard(const OdfPackage &package, XmlElement group) {
  const Text XML = "http://www.w3.org/XML/1998/namespace";
  for (const auto &attribute : group.attributes()) {
    auto name = attribute.name.mid(attribute.name.indexOf(':') + 1);
    if ((attribute.uri == DRAW &&
         TextList{"name", "id", "z-index", "transform"}.contains(name)) ||
        (attribute.uri == XML && name == "id"))
      continue;
    if (attribute.uri == SVG && name == "y" &&
        parse_scene_length(attribute.value) == 0)
      continue;
    fail("Ungrouping this group's styles, anchors or extra attributes is not "
         "yet supported");
  }
  for (auto child = group.firstChild(); !child.isNull();
       child = child.nextSibling()) {
    if (child.isElement()) {
      const auto uri = child.namespaceURI(), name = child.localName();
      if ((uri == DRAW && name != "glue-point" && name != "layer-set") ||
          (uri == "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0" &&
           name == "scene") ||
          (uri == OFFICE && name == "annotation"))
        continue;
      fail("Ungrouping this group's metadata, events or glue points is not yet "
           "supported");
    }
    if ((child.isText() || child.isCDATASection()) &&
        !child.nodeValue().trimmed().empty())
      fail("This group contains unsupported text content");
  }
  check_stage_group_identity_references(package, group);
}
} // namespace
PresentationPosition Presentation::commit_object_structure(
    OdfPackage candidate, const std::vector<XmlElement> &selected,
    PresentationPosition before_position, bool allow_empty) {
  candidate.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(candidate);
  auto scenes = read_presentation_scenes(candidate, reader);
  const auto &model = scenes.at(before_position.slide);
  auto refs = native_object_references(model, selected);
  refs = native_object_selection(model, refs);
  if (refs.empty() && !allow_empty)
    fail("The changed objects have no selectable native scene");
  auto after_position =
      refs.empty() ? PresentationPosition{before_position.slide, -1, -1}
                   : previous(model, before_position.slide, refs);
  auto before = package_.xml.toByteArray();
  package_ = std::move(candidate);
  scenes_ = std::move(scenes);
  text_reader_ = std::move(reader);
  ++revision_;
  remember(before, std::move(before_position), after_position);
  return after_position;
}
PresentationPosition
Presentation::group_objects(int slide_index,
                            const std::vector<NativeObjectRef> &selection) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    fail("This presentation is read-only");
  const auto &model = scene(slide_index);
  auto refs = native_object_selection(model, selection);
  for (auto ref : refs)
    model.require_editable(native_object_source(model, ref));
  if (refs.size() < 2)
    fail("Select at least two objects or groups to group");
  auto before_position = previous(model, slide_index, refs);
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  std::vector<XmlElement> nodes;
  for (auto ref : refs)
    nodes.push_back(stage_candidate_source(native_object_source(model, ref),
                                           candidate.xml));
  auto parent = nodes.front().parentNode().toElement();
  context(parent);
  for (auto node : nodes)
    if (node.parentNode() != parent)
      fail("Select objects at the same group level; use Select containing "
           "group for nested objects");
  auto items = native_stacking_children(parent);
  std::unordered_set<const void *> chosen_nodes;
  for (auto node : nodes)
    chosen_nodes.insert(node.identity());
  auto chosen = [&](XmlElement node) {
    return chosen_nodes.contains(node.identity());
  };
  size_t last = 0, count = 0;
  for (size_t i = 0; i < items.size(); ++i)
    if (chosen(items[i].node)) {
      last = i;
      ++count;
    }
  if (count != nodes.size())
    fail("An object is outside the selected stacking context");
  auto group = candidate.xml.createElementNS(DRAW, "draw:g");
  parent.insertBefore(group, items[last].node);
  // Retain the source order of the grouped children, including tied z-indices.
  for (auto node = parent.firstChild(); !node.isNull();) {
    auto next = node.nextSibling();
    if (chosen(node))
      group.appendChild(node);
    node = next;
  }
  std::vector<XmlElement> order;
  for (size_t i = 0; i < items.size(); ++i) {
    if (!chosen(items[i].node))
      order.push_back(items[i].node);
    if (i == last)
      order.push_back(group);
  }
  // The group occupies the frontmost selected object's stacking position.
  native_layer_stacking_indices(parent, order, model.layers);
  return commit_object_structure(std::move(candidate), {group},
                                 std::move(before_position));
}
PresentationPosition
Presentation::ungroup_objects(int slide_index,
                              const std::vector<NativeObjectRef> &selection) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    fail("This presentation is read-only");
  const auto &model = scene(slide_index);
  auto refs = native_object_selection(model, selection);
  for (auto ref : refs)
    model.require_editable(native_object_source(model, ref));
  auto before_position = previous(model, slide_index, refs);
  // A selected leaf ungroup command addresses its immediate containing group.
  auto groups = refs;
  for (auto &ref : groups) {
    auto node = native_object_source(model, ref);
    if (node.namespaceURI() != DRAW || node.localName() != "g")
      ++ref.ancestors;
  }
  groups = native_object_selection(model, groups);
  std::vector<XmlElement> originals;
  for (auto ref : groups) {
    auto group = native_object_source(model, ref);
    model.require_editable(group);
    ungroup_guard(package_, group);
    originals.push_back(group);
  }
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  std::vector<XmlElement> targets, selected;
  // Clone paths before changing any parent/sibling positions.
  for (auto group : originals)
    targets.push_back(stage_candidate_source(group, candidate.xml));
  for (auto group : targets) {
    auto parent = group.parentNode().toElement();
    context(parent);
    const auto children = native_stacking_children(group);
    if (children.empty())
      fail("This group has no drawing objects");
    const auto transform =
        parse_scene_transform(group.attributeNS(DRAW, "transform"));
    if (!identity(transform))
      for (auto item : children) {
        if (item.node.namespaceURI() != DRAW || item.node.localName() == "a")
          fail("This transformed group contains objects whose transform cannot "
               "yet be flattened");
        auto own =
            parse_scene_transform(item.node.attributeNS(DRAW, "transform"));
        item.node.setAttributeNS(DRAW, "draw:transform",
                                 format_scene_transform(transform * own));
      }
    std::vector<XmlElement> order;
    for (const auto &item : native_stacking_children(parent)) {
      if (item.node != group)
        order.push_back(item.node);
      else
        for (const auto &child : children) {
          order.push_back(child.node);
          selected.push_back(child.node);
        }
    }
    while (!group.firstChild().isNull())
      parent.insertBefore(group.firstChild(), group);
    parent.removeChild(group);
    native_layer_stacking_indices(parent, order, model.layers);
  }
  return commit_object_structure(std::move(candidate), selected,
                                 std::move(before_position));
}
} // namespace DomeOffice
