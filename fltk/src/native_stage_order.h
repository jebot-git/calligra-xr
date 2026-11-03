// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
enum class NativeShapeOrder { Back, Backward, Forward, Front };
enum class NativeOrderScope { Object, ContainingGroup };
struct NativeStackItem {
  XmlElement node;
  int z;
  // The existing source/editor path counts only drawing namespace children.
  size_t order;
};
// One parent is one stacking context. Opaque objects participate in ordering
// even when their contents cannot yet be rendered by the native engine.
std::vector<NativeStackItem> native_stacking_children(const XmlElement &);
} // namespace DomeOffice
