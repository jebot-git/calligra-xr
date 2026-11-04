// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "text_documents.h"
namespace DomeOffice {
// Internal detached source transaction, shared by Delete and delayed Cut.
struct NativeStageRemoval {
  OdfPackage package{Kind::Stage};
  std::vector<XmlElement> selected;
  PresentationPosition previous;
};
NativeStageRemoval prepare_stage_removal(const OdfPackage &, const SlideScene &,
                                         int,
                                         const std::vector<NativeObjectRef> &);
} // namespace DomeOffice
