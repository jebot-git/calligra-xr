// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
#include <algorithm>
namespace DomeOffice {
std::vector<NativeStackItem>
native_stacking_children(const XmlElement &parent) {
  const Text DR3D = "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0";
  std::vector<NativeStackItem> items;
  size_t draw_order = 0;
  for (auto node = parent.firstChild(); !node.isNull();
       node = node.nextSibling()) {
    if (!node.isElement())
      continue;
    const auto uri = node.namespaceURI(), name = node.localName();
    const bool drawing = uri == DRAW && name != "layer-set";
    if (!drawing && !(uri == DR3D && name == "scene") &&
        !(uri == OFFICE && name == "annotation"))
      continue;
    if (items.size() >= 100000)
      throw std::runtime_error("Presentation shape count exceeds the limit");
    bool ok;
    int z = node.attributeNS(DRAW, "z-index", Text::number(items.size()))
                .toInt(&ok);
    if (!ok || z < 0)
      throw std::runtime_error("Invalid presentation stacking order");
    items.push_back({node, z, drawing ? draw_order++ : 0});
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const auto &a, const auto &b) { return a.z < b.z; });
  return items;
}
PresentationPosition Presentation::reorder_shape(int slide_index, int index,
                                                 NativeShapeOrder order,
                                                 NativeOrderScope scope) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  const auto &shape = scene(slide_index).shapes.at(index);
  if (shape.master || shape.source.isNull())
    throw std::runtime_error("This shape is not editable");
  PresentationPosition previous{slide_index, shape.editor_index, index};
  auto before = package_.xml.toByteArray();
  OdfPackage candidate = package_;
  candidate.xml = read_document_xml(before);
  auto selected = stage_candidate_source(shape.source, candidate.xml);
  auto target = selected;
  switch (scope) {
  case NativeOrderScope::Object:
    break;
  case NativeOrderScope::ContainingGroup:
    target = target.parentNode().toElement();
    if (target.namespaceURI() != DRAW || target.localName() != "g")
      throw std::runtime_error("This object has no containing group");
    break;
  default:
    throw std::runtime_error("Invalid object ordering scope");
  }
  const auto parent = target.parentNode().toElement();
  if (parent.namespaceURI() != DRAW ||
      (parent.localName() != "page" && parent.localName() != "g"))
    throw std::runtime_error("This object has no editable stacking context");
  auto items = native_stacking_children(parent);
  auto found = std::find_if(items.begin(), items.end(), [&](const auto &item) {
    return item.node == target;
  });
  if (found == items.end())
    throw std::runtime_error("This object is outside its stacking context");
  const size_t old = found - items.begin();
  size_t next;
  switch (order) {
  case NativeShapeOrder::Back:
    next = 0;
    break;
  case NativeShapeOrder::Backward:
    next = old ? old - 1 : old;
    break;
  case NativeShapeOrder::Forward:
    next = std::min(old + 1, items.size() - 1);
    break;
  case NativeShapeOrder::Front:
    next = items.size() - 1;
    break;
  default:
    throw std::runtime_error("Invalid object ordering command");
  }
  // A boundary command must preserve exact source, saved state and redo.
  if (next == old)
    return previous;
  const auto item = *found;
  items.erase(found);
  items.insert(items.begin() + next, item);
  // Explicit consecutive indices resolve ties, missing values and saturated
  // source indices without changing XML child order or keyboard navigation.
  for (size_t i = 0; i < items.size(); ++i)
    items[i].node.setAttributeNS(DRAW, "draw:z-index", Text::number(i));
  auto reader = std::make_shared<OdfTextReader>(candidate);
  auto scenes = read_presentation_scenes(candidate, reader);
  PresentationPosition position{slide_index, -1, -1};
  const auto &shapes = scenes.at(slide_index).shapes;
  for (size_t i = 0; i < shapes.size(); ++i)
    if (shapes[i].source == selected) {
      position = {slide_index, shapes[i].editor_index, int(i)};
      break;
    }
  if (position.shape < 0)
    throw std::runtime_error("Reordered object has no native scene");
  package_ = std::move(candidate);
  text_reader_ = std::move(reader);
  scenes_ = std::move(scenes);
  ++revision_;
  remember(before, previous, position);
  return position;
}
} // namespace DomeOffice
