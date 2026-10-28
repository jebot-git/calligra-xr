// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_path.h"
namespace DomeOffice {
struct EnhancedContext {
  SceneRect viewbox{0, 0, 21600, 21600};
  double width = 0, height = 0;
  bool has_fill = false, has_stroke = false;
  std::optional<double> xstretch, ystretch;
  bool modern_angles = true;
  bool legacy_polar_radius_first = false;
  std::vector<double> modifiers;
  std::map<Text, Text> equations;
};
class EnhancedEvaluator {
public:
  explicit EnhancedEvaluator(EnhancedContext);
  double evaluate(const Text &) const;
  double reference(const Text &) const;
  const EnhancedContext &context() const;
  // Length of a known equation name at the start of UTF-8 data after '?'.
  size_t reference_length(std::string_view) const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
ScenePath parse_enhanced_path(const Text &, const EnhancedEvaluator &);
struct EnhancedGeometry {
  std::shared_ptr<const ScenePath> path;
  SceneTransform mapping;
  std::optional<SceneRect> text_area;
  std::optional<SceneRect> vertical_text_area;
  std::vector<Text> unsupported;
  std::vector<SceneHandle> handles;
};
EnhancedGeometry read_enhanced_geometry(const XmlElement &, double width,
                                        double height, const ScenePaint &,
                                        bool legacy_polar_radius_first = false);
bool enhanced_legacy_polar_order(const OdfPackage &);
// Target is shape-local. Only directly referenced modifiers are writable;
// formula/constant coordinates stay fixed. No XML is mutated by this function.
std::vector<double> adjusted_enhanced_modifiers(
    const XmlElement &, double width, double height, const ScenePaint &,
    size_t handle, ScenePoint target, bool legacy_polar_radius_first = false);
} // namespace DomeOffice
