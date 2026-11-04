// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_selection.h"
#include <cstdint>
namespace DomeOffice {
using NativeStageObjectPath = std::vector<uint32_t>;
struct NativeStageFragment {
  Bytes archive; // A standalone ODP containing only selected object roots.
  Text text;     // Plain paragraph text fallback, in source order.
  // Element-child indices below the fragment's sole draw:page. Context groups
  // remain in the archive, but are not implicitly selected for a later paste.
  std::vector<NativeStageObjectPath> selection;
};
inline constexpr const char *native_stage_fragment_mime =
    "application/x-calligra-stage-objects";
// Private, versioned clipboard envelope. Both operations canonicalize the ODP
// subset, independently resolve the paths and derive text from selected source.
// Raw ODP remains a separate public clipboard representation.
Bytes encode_stage_fragment(const NativeStageFragment &);
NativeStageFragment decode_stage_fragment(const Bytes &);
std::vector<XmlElement>
stage_fragment_nodes(const OdfPackage &,
                     const std::vector<NativeStageObjectPath> &);
NativeStageFragment native_stage_fragment(const OdfPackage &,
                                          const SlideScene &,
                                          const std::vector<NativeObjectRef> &);
} // namespace DomeOffice
