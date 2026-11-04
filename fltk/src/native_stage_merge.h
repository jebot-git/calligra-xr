// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_import.h"
namespace DomeOffice {
// Internal assembly draft, never a publishable paste transaction by itself.
// The remapped source retains its defaults and page context. A later context
// isolation step must finish the imported styles/cell instances in package;
// it must also resolve navigation/field semantics before live publication.
struct NativeStageMergeDraft {
  NativeStageMergeDraft() = default;
  NativeStageMergeDraft(NativeStageMergeDraft &&) noexcept = default;
  NativeStageMergeDraft &operator=(NativeStageMergeDraft &&) noexcept = default;
  NativeStageMergeDraft(const NativeStageMergeDraft &) = delete;
  NativeStageMergeDraft &operator=(const NativeStageMergeDraft &) = delete;
  OdfPackage package{Kind::Stage};
  OdfPackage source{Kind::Stage};
  XmlDocument source_styles; // Keeps declaration identities in imported alive.
  // Source identities map to their new receiving-document counterparts. This
  // includes transferred style/font definitions and every imported element.
  std::map<const void *, XmlElement> imported;
  std::vector<XmlElement> roots, selected;
  XmlElement candidate_node(XmlElement) const;
};
// Decode, reserve/remap names and resources, then assemble definitions, package
// assets, effective page layers and front stacking in a detached destination.
// Existing receiving source/definitions/defaults/assets are never replaced.
// A page inheriting document layers gets a local snapshot; later document-layer
// edits do not reach those local copies. The final paste policy must account
// for this scope change as well as default/context and navigation semantics.
NativeStageMergeDraft prepare_stage_merge(const OdfPackage &, int slide,
                                          const Bytes &native_fragment);
} // namespace DomeOffice
