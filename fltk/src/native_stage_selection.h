// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
// A rendered leaf plus its number of containing groups. Group references need
// no invented XML identity and remain meaningful for anonymous source groups.
struct NativeObjectRef {
  int shape = -1;
  int ancestors = 0;
  bool operator==(const NativeObjectRef &) const = default;
};
XmlElement native_object_source(const SlideScene &, NativeObjectRef);
bool native_object_contains(XmlElement parent, XmlElement child);
std::optional<NativeObjectRef> native_object_reference(const SlideScene &,
                                                       XmlElement);
std::vector<NativeObjectRef>
native_object_references(const SlideScene &, const std::vector<XmlElement> &);
std::vector<NativeObjectRef>
native_object_selection(const SlideScene &,
                        const std::vector<NativeObjectRef> &);
std::vector<int> native_object_leaves(const SlideScene &,
                                      const std::vector<NativeObjectRef> &);
} // namespace DomeOffice
