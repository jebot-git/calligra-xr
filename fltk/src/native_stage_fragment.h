// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_selection.h"
namespace DomeOffice {
struct NativeStageFragment {
  Bytes archive; // A standalone ODP containing only selected object roots.
  Text text;     // Plain paragraph text fallback, in source order.
};
NativeStageFragment native_stage_fragment(const OdfPackage &,
                                          const SlideScene &,
                                          const std::vector<NativeObjectRef> &);
} // namespace DomeOffice
