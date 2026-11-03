// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_layers.h"
#include "native_presentation.h"
#include "native_stage_order.h"
#include <limits>
#include <unordered_map>
#include <stdexcept>
namespace DomeOffice {
SceneLayers SceneLayers::scoped(XmlElement parent) const {
  const auto sets = children(parent, DRAW, "layer-set");
  if (sets.empty())
    return *this;
  if (sets.size() != 1)
    throw std::runtime_error(
        "A page or document must have at most one layer set");
  SceneLayers result;
  auto definitions = std::make_shared<std::map<Text, SceneLayer>>();
  result.layers_ = definitions;
  for (auto node : children(sets.front(), DRAW, "layer")) {
    if (definitions->size() >= 100000)
      throw std::runtime_error("Presentation layer count exceeds the limit");
    if (!node.hasAttributeNS(DRAW, "name"))
      throw std::runtime_error("A drawing layer requires a name");
    SceneLayer layer;
    const auto display = node.attributeNS(DRAW, "display", "always");
    if (display == "screen")
      layer.display = SceneLayer::Screen;
    else if (display == "printer")
      layer.display = SceneLayer::Printer;
    else if (display == "none")
      layer.display = SceneLayer::None;
    else if (display != "always")
      throw std::runtime_error("Invalid drawing layer display value");
    const auto locked = node.attributeNS(DRAW, "protected", "false").trimmed();
    if (locked == "true" || locked == "1")
      layer.protected_objects = true;
    else if (locked != "false" && locked != "0")
      throw std::runtime_error("Invalid drawing layer protection value");
    if (!definitions->emplace(node.attributeNS(DRAW, "name"), layer).second)
      throw std::runtime_error("Duplicate drawing layer name");
  }
  return result;
}
const SceneLayer *SceneLayers::find(XmlElement node) const {
  if (!layers_ || !node.hasAttributeNS(DRAW, "layer"))
    return nullptr;
  const auto at = layers_->find(node.attributeNS(DRAW, "layer"));
  return at == layers_->end() ? nullptr : &at->second;
}
bool SceneLayers::missing(XmlElement node) const {
  return node.hasAttributeNS(DRAW, "layer") && !find(node);
}
bool SceneLayers::visible(XmlElement node, SceneTarget target) const {
  const auto layer = find(node);
  if (!layer)
    return true;
  return layer->display == SceneLayer::Always ||
         (target == SceneTarget::Screen &&
          layer->display == SceneLayer::Screen) ||
         (target == SceneTarget::Print &&
          layer->display == SceneLayer::Printer);
}
bool SceneLayers::protected_object(XmlElement node) const {
  const auto layer = find(node);
  return layer && layer->protected_objects;
}
bool SlideScene::editable(XmlElement node, bool descendants) const {
  if (node.isNull() || source.isNull())
    return false;
  bool own = false;
  for (auto parent = node; !parent.isNull();
       parent = parent.parentNode().toElement()) {
    if (parent == source) {
      own = true;
      break;
    }
    if (layers.protected_object(parent))
      return false;
  }
  if (!own)
    return false;
  if (!descendants)
    return true;
  return !layers.protected_subtree(node);
}
bool SceneLayers::protected_subtree(XmlElement node) const {
  struct Entry {
    XmlElement node;
    int depth;
  };
  std::vector<Entry> pending{{node, 0}};
  size_t count = 0;
  while (!pending.empty()) {
    auto item = pending.back();
    pending.pop_back();
    if (++count > 1000000 || item.depth > 256)
      throw std::runtime_error("Layer protection traversal exceeds the limit");
    if (protected_object(item.node))
      return true;
    for (auto child = item.node.firstChild(); !child.isNull();
         child = child.nextSibling())
      if (child.isElement())
        pending.push_back({child.toElement(), item.depth + 1});
  }
  return false;
}
void SlideScene::require_editable(XmlElement node, bool descendants) const {
  if (!editable(node, descendants))
    throw std::runtime_error("Objects in a protected layer cannot be modified");
}
// Preserve protected object source, including z-index spelling/absence. Greedy
// integer labels are sufficient: equal labels retain physical XML order. With
// no fixed labels retain the established consecutive-index serialization.
void native_layer_stacking_indices(XmlElement parent,
                                   const std::vector<XmlElement> &order,
                                   const SceneLayers &layers) {
  const auto inventory = native_stacking_children(parent);
  std::unordered_map<const void *, int> fixed, physical;
  int rank = 0;
  for (auto node = parent.firstChild(); !node.isNull();
       node = node.nextSibling())
    if (node.isElement())
      physical.emplace(node.identity(), rank++);
  for (auto item : inventory)
    if (layers.protected_subtree(item.node))
      fixed.emplace(item.node.identity(), item.z);
  std::vector<int> labels;
  long long previous = -1;
  int previous_rank = -1;
  for (size_t i = 0; i < order.size(); ++i) {
    const auto node = order[i];
    const int current_rank = physical.at(node.identity());
    long long minimum =
        fixed.empty()
            ? i
            : std::max(0LL, previous + (current_rank < previous_rank ? 1 : 0));
    auto at = fixed.find(node.identity());
    const long long value = at == fixed.end() ? minimum : at->second;
    if (value < minimum || value > std::numeric_limits<int>::max())
      throw std::runtime_error(
          "No stacking index is available without changing a protected object");
    labels.push_back(int(value));
    previous = value;
    previous_rank = current_rank;
  }
  for (size_t i = 0; i < order.size(); ++i)
    if (!fixed.contains(order[i].identity())) {
      auto node = order[i];
      node.setAttributeNS(DRAW, "draw:z-index", Text::number(labels[i]));
    }
}
} // namespace DomeOffice
