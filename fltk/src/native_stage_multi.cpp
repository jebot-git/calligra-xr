// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_identity.h"
#include "native_stage_source.h"
#include <unordered_set>
namespace DomeOffice {
namespace {
PresentationPosition position(const SlideScene &model, int slide,
                              const std::vector<NativeObjectRef> &refs) {
  if (refs.empty())
    throw std::runtime_error("Select at least one object");
  PresentationPosition result{slide,
                              model.shapes.at(refs.front().shape).editor_index,
                              refs.front().shape};
  result.objects = refs;
  return result;
}
} // namespace
void Presentation::begin_objects_drag(
    int slide_index, const std::vector<NativeObjectRef> &selection,
    ScenePoint pointer, std::optional<NativeShapeHandle> handle) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &model = scene(slide_index);
  auto refs = native_object_selection(model, selection);
  auto at = position(model, slide_index, refs);
  NativeObjectGesture gesture(model, refs, pointer, handle);
  drag_ = HandleDrag{package_.xml.toByteArray(), at, -1000, state_};
  drag_->object_gesture = std::move(gesture);
}
void Presentation::begin_objects_move(
    int slide_index, const std::vector<NativeObjectRef> &selection,
    ScenePoint pointer) {
  begin_objects_drag(slide_index, selection, pointer, {});
}
void Presentation::begin_objects_transform(
    int slide_index, const std::vector<NativeObjectRef> &selection,
    NativeShapeHandle handle, ScenePoint pointer) {
  begin_objects_drag(slide_index, selection, pointer, handle);
}
void Presentation::update_objects_drag(int slide_index, ScenePoint pointer,
                                       bool constrain) {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  if (!drag_ || !drag_->object_gesture || drag_->position.slide != slide_index)
    throw std::runtime_error("No object selection gesture is active");
  auto action = drag_->object_gesture->update(pointer, constrain);
  auto candidate = read_document_xml(drag_->before);
  if (action)
    drag_->object_gesture->apply(*action, candidate);
  auto bytes = candidate.toByteArray();
  if (bytes == package_.xml.toByteArray())
    return;
  restore_content(bytes);
  drag_->changed = bytes != drag_->before;
  state_ = drag_->changed ? next_state_++ : drag_->state;
}
PresentationPosition Presentation::reorder_objects(
    int slide_index, const std::vector<NativeObjectRef> &selection,
    NativeShapeOrder command, NativeOrderScope scope) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &model = scene(slide_index);
  auto refs = native_object_selection(model, selection);
  auto before = position(model, slide_index, refs);
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  std::vector<XmlElement> nodes, parents;
  for (auto ref : refs)
    nodes.push_back(stage_candidate_source(native_object_source(model, ref),
                                           candidate.xml));
  auto operated = refs;
  if (scope == NativeOrderScope::ContainingGroup)
    for (auto &ref : operated)
      ++ref.ancestors;
  else if (scope != NativeOrderScope::Object)
    throw std::runtime_error("Invalid object ordering scope");
  operated = native_object_selection(model, operated);
  std::unordered_set<const void *> chosen, contexts;
  for (auto ref : operated) {
    auto node =
        stage_candidate_source(native_object_source(model, ref), candidate.xml);
    auto parent = node.parentNode().toElement();
    if (parent.namespaceURI() != DRAW ||
        (parent.localName() != "page" && parent.localName() != "g"))
      throw std::runtime_error("This object has no editable stacking context");
    chosen.insert(node.identity());
    if (contexts.insert(parent.identity()).second)
      parents.push_back(parent);
  }
  bool changed = false;
  for (auto parent : parents) {
    auto items = native_stacking_children(parent), original = items;
    auto selected = [&](const auto &item) {
      return chosen.contains(item.node.identity());
    };
    switch (command) {
    case NativeShapeOrder::Back:
      std::stable_partition(items.begin(), items.end(), selected);
      break;
    case NativeShapeOrder::Front:
      std::stable_partition(items.begin(), items.end(),
                            [&](const auto &item) { return !selected(item); });
      break;
    case NativeShapeOrder::Backward:
      for (size_t i = 1; i < items.size(); ++i)
        if (selected(items[i]) && !selected(items[i - 1]))
          std::swap(items[i], items[i - 1]);
      break;
    case NativeShapeOrder::Forward:
      for (size_t i = items.size(); i-- > 1;)
        if (!selected(items[i]) && selected(items[i - 1]))
          std::swap(items[i], items[i - 1]);
      break;
    default:
      throw std::runtime_error("Invalid object ordering command");
    }
    bool context_changed = false;
    for (size_t i = 0; i < items.size(); ++i)
      context_changed |= items[i].node != original[i].node;
    if (!context_changed)
      continue;
    changed = true;
    for (size_t i = 0; i < items.size(); ++i)
      items[i].node.setAttributeNS(DRAW, "draw:z-index", Text::number(i));
  }
  if (!changed)
    return before;
  return commit_object_structure(std::move(candidate), nodes, before);
}
PresentationPosition
Presentation::remove_objects(int slide_index,
                             const std::vector<NativeObjectRef> &selection) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &model = scene(slide_index);
  auto refs = native_object_selection(model, selection);
  auto before = position(model, slide_index, refs);
  std::vector<XmlElement> originals;
  for (auto ref : refs)
    originals.push_back(native_object_source(model, ref));
  auto ids = check_stage_removed_objects_references(package_, originals);
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(package_.xml.toByteArray());
  std::vector<XmlElement> targets;
  for (auto source : originals)
    targets.push_back(stage_candidate_source(source, candidate.xml));
  std::vector<XmlElement> next;
  std::unordered_set<const void *> removed;
  for (auto node : originals)
    removed.insert(node.identity());
  for (size_t i = 0; i < model.shapes.size(); ++i) {
    auto source = model.shapes[i].source;
    bool erased = model.shapes[i].master;
    for (auto node = source; !erased && !node.isNull();
         node = node.parentNode().toElement())
      erased = removed.contains(node.identity());
    if (erased)
      continue;
    next = {stage_candidate_source(source, candidate.xml)};
    if (int(i) >= before.shape)
      break;
  }
  for (auto node : targets)
    node.parentNode().removeChild(node);
  remove_stage_navigation_ids(candidate.xml, ids);
  return commit_object_structure(std::move(candidate), next, before, true);
}
} // namespace DomeOffice
