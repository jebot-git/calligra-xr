// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
// A draw:name/draw:id on an object is global identity. Equation names,
// numbered glue points and plugin parameter names belong to local grammars.
inline bool native_stage_object_identity(XmlElement node) {
  const auto name = node.localName();
  if (node.namespaceURI() == DRAW)
    return TextList{
        "caption",      "circle",   "connector",      "control",
        "custom-shape", "ellipse",  "frame",          "g",
        "line",         "measure",  "page-thumbnail", "path",
        "polygon",      "polyline", "rect",           "regular-polygon"}
        .contains(name);
  return node.namespaceURI() ==
             "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0" &&
         TextList{"cube", "extrude", "rotate", "scene", "sphere"}.contains(
             name);
}
inline bool native_stage_text_anchor(XmlElement node) {
  return node.namespaceURI() == TEXT &&
         TextList{
             "bookmark",       "bookmark-start",       "bookmark-end",
             "reference-mark", "reference-mark-start", "reference-mark-end"}
             .contains(node.localName());
}
inline bool native_stage_section_target(XmlElement node) {
  return node.namespaceURI() == TEXT && node.localName() == "section";
}
inline bool native_stage_local_section_source(XmlElement node) {
  return node.namespaceURI() == TEXT && node.localName() == "section-source" &&
         node.attributeNS(XLINK, "href").empty();
}
// Children survive ungrouping: test only the removed group's own identity,
// including references originating in those retained children.
void check_stage_group_identity_references(const OdfPackage &, XmlElement);
std::set<Text>
check_stage_removed_objects_references(const OdfPackage &,
                                       const std::vector<XmlElement> &);
void remove_stage_navigation_ids(XmlDocument &, const std::set<Text> &);
} // namespace DomeOffice
