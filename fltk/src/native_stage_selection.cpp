// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_selection.h"
#include <unordered_map>
#include <unordered_set>
namespace DomeOffice {
bool native_object_contains(XmlElement parent, XmlElement child) {
  for (int depth = 0; !child.isNull() && depth <= 256;
       ++depth, child = child.parentNode().toElement())
    if (parent == child)
      return true;
  return false;
}
XmlElement native_object_source(const SlideScene &scene, NativeObjectRef ref) {
  if (ref.ancestors < 0 || ref.ancestors > 256)
    throw std::runtime_error("Invalid group selection depth");
  const auto &shape = scene.shapes.at(ref.shape);
  if (shape.master || shape.source.isNull())
    throw std::runtime_error("Master objects cannot be selected for editing");
  auto node = shape.source;
  for (int depth = 0; depth < ref.ancestors; ++depth) {
    node = node.parentNode().toElement();
    if (node.namespaceURI() != DRAW || node.localName() != "g")
      throw std::runtime_error("This object has no containing group");
  }
  return node;
}
std::vector<NativeObjectRef>
native_object_references(const SlideScene &scene,
                         const std::vector<XmlElement> &nodes) {
  std::unordered_set<const void *> wanted;
  for (auto node : nodes)
    wanted.insert(node.identity());
  std::unordered_map<const void *, NativeObjectRef> found;
  for (size_t i = 0; i < scene.shapes.size(); ++i) {
    if (scene.shapes[i].master)
      continue;
    auto node = scene.shapes[i].source;
    for (int depth = 0; !node.isNull() && depth <= 256; ++depth) {
      if (wanted.contains(node.identity()))
        found.try_emplace(node.identity(), NativeObjectRef{int(i), depth});
      node = node.parentNode().toElement();
      if (node.namespaceURI() != DRAW || node.localName() != "g")
        break;
    }
  }
  std::vector<NativeObjectRef> result;
  for (auto node : nodes)
    if (auto at = found.find(node.identity()); at != found.end())
      result.push_back(at->second);
  return result;
}
std::optional<NativeObjectRef> native_object_reference(const SlideScene &scene,
                                                       XmlElement node) {
  auto refs = native_object_references(scene, {node});
  if (refs.empty())
    return {};
  return refs.front();
}
std::vector<NativeObjectRef>
native_object_selection(const SlideScene &scene,
                        const std::vector<NativeObjectRef> &refs) {
  if (refs.size() > 100000)
    throw std::runtime_error("Object selection exceeds the native limit");
  std::unordered_set<const void *> wanted, emitted;
  std::vector<XmlElement> nodes;
  for (auto ref : refs) {
    nodes.push_back(native_object_source(scene, ref));
    wanted.insert(nodes.back().identity());
  }
  std::vector<NativeObjectRef> result;
  for (size_t i = 0; i < refs.size(); ++i) {
    auto parent = nodes[i].parentNode().toElement();
    bool contained = false;
    for (int depth = 0; !parent.isNull() && depth <= 256;
         ++depth, parent = parent.parentNode().toElement())
      if (wanted.contains(parent.identity())) {
        contained = true;
        break;
      }
    if (!contained && emitted.insert(nodes[i].identity()).second)
      result.push_back(refs[i]);
  }
  return result;
}
std::vector<int>
native_object_leaves(const SlideScene &scene,
                     const std::vector<NativeObjectRef> &refs) {
  std::unordered_set<const void *> selected;
  for (auto ref : native_object_selection(scene, refs))
    selected.insert(native_object_source(scene, ref).identity());
  std::vector<int> result;
  for (size_t i = 0; i < scene.shapes.size(); ++i) {
    if (scene.shapes[i].master)
      continue;
    auto node = scene.shapes[i].source;
    for (int depth = 0; !node.isNull() && depth <= 256;
         ++depth, node = node.parentNode().toElement())
      if (selected.contains(node.identity())) {
        result.push_back(int(i));
        break;
      }
  }
  return result;
}
} // namespace DomeOffice
