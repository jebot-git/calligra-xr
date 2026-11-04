// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_connector.h"
namespace DomeOffice {
struct PresentationPosition;
struct NativeConnectorEndpoint {
  std::optional<size_t> target;
  std::optional<uint64_t> glue;
  Text unresolved;
  ScenePoint point; // Slide coordinates; meaningful for a free endpoint.
  bool operator==(const NativeConnectorEndpoint &other) const {
    return target == other.target && glue == other.glue &&
           unresolved == other.unresolved &&
           (target || !unresolved.empty() ||
            (point.x == other.point.x && point.y == other.point.y));
  }
};
struct NativeConnectorValues {
  SceneConnectorSpec::Type type = SceneConnectorSpec::Standard;
  NativeConnectorEndpoint start, end;
  std::vector<double> skew;
  bool recompute = false;
  bool operator==(const NativeConnectorValues &) const = default;
};
struct NativeConnectorTarget {
  Text label, identity, error;
  std::optional<SceneConnectorObject> geometry;
};
// A revision-bound, read-only source snapshot plus editable value fields.
// Copying a draft never shares a mutable candidate document.
class NativeConnectorEdit {
public:
  NativeConnectorValues values;
  bool creating() const;
  SceneConnectorEnds endpoints(const NativeConnectorValues &) const;
  const std::vector<NativeConnectorTarget> &targets() const;

private:
  friend class Presentation;
  struct State;
  std::shared_ptr<const State> state_;
  NativeConnectorEdit(const OdfPackage &, int slide, XmlElement source,
                      uint64_t revision, std::shared_ptr<const char> identity,
                      SceneTransform frame, PresentationPosition previous);
};
} // namespace DomeOffice
