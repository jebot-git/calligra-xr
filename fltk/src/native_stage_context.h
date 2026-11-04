// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_presentation.h"
namespace DomeOffice {
// Materialize the source default grid after its last explicit stop, through a
// finite frame extent, and convert origins to the receiving paragraph default.
// The caller supplies the resolved paragraph direction. Beyond this coverage,
// subsequent edits/resizing follow the receiving document's default grid.
// Tab-followed visible list labels need separate list geometry isolation.
std::vector<TextTabStop>
native_stage_portable_tabs(const TextParagraph &source,
                           const TextParagraph &receiving_defaults,
                           double frame_extent, bool right_to_left);
// Point-of-use style materialization for receiving documents with different
// defaults. Names are allocated by the caller against both packages.
// Definitions are detached: the caller chooses their valid common/font
// declaration groups. These are native formatting contexts, not a complete
// destination paste or an encoder for unknown ODF properties. Original
// dependency definitions must also be retained by the import transaction.
class NativeStageContextStyles {
public:
  NativeStageContextStyles(XmlDocument, std::function<Text()> fresh_name);
  XmlElement graphic(const ScenePaint &, const Text &family = "graphic");
  // Encodes the supplied resolved state. Prefix a cloned style chain with its
  // pre-explicit-style context; a direct override instead needs the effective
  // paragraph. List geometry needs separate treatment. This does not rewrite
  // next-style links or list numbering/structure.
  XmlElement paragraph(const TextParagraph &);
  // Explicit tab stops retain their resolved leader formatting. Their default
  // interval/relative origin stay in the input model; valid ODF only evaluates
  // those properties on a default style, so callers must not write them onto
  // this named paragraph style and assume destination isolation is complete.
  const std::vector<XmlElement> &supporting_styles() const {
    return supporting_;
  }
  const std::vector<XmlElement> &font_declarations() const { return fonts_; }

private:
  XmlDocument document_;
  std::function<Text()> fresh_;
  std::vector<XmlElement> supporting_, fonts_;
  Text unconstrained_font_;
  std::set<Text> allocated_;
  size_t definitions_ = 0, tabs_ = 0;
  Text name();
  XmlElement style(const Text &family);
  void font(XmlElement, const TextFont &);
  void characters(XmlElement, const TextParagraph &);
  void decoration(XmlElement, const Text &, const TextDecoration &);
  void tab_stops(XmlElement, const std::vector<TextTabStop> &);
};
} // namespace DomeOffice
