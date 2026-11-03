// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <map>
#include <memory>
namespace DomeOffice {
enum class SceneTarget { Screen, Print };
struct SceneLayer {
  enum Display { Always, Screen, Printer, None } display = Always;
  bool protected_objects = false;
};
// A local set replaces the document set, including an explicitly empty set.
class SceneLayers {
public:
  SceneLayers scoped(XmlElement parent) const;
  bool visible(XmlElement, SceneTarget) const;
  bool protected_object(XmlElement) const;
  bool missing(XmlElement) const;
  bool protected_subtree(XmlElement) const;

private:
  std::shared_ptr<const std::map<Text, SceneLayer>> layers_;
  const SceneLayer *find(XmlElement) const;
};
void native_layer_stacking_indices(XmlElement parent,
                                   const std::vector<XmlElement> &order,
                                   const SceneLayers &);
} // namespace DomeOffice
