// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
// Children survive ungrouping: test only the removed group's own identity,
// including references originating in those retained children.
void check_stage_group_identity_references(const OdfPackage &, XmlElement);
std::set<Text>
check_stage_removed_objects_references(const OdfPackage &,
                                       const std::vector<XmlElement> &);
void remove_stage_navigation_ids(XmlDocument &, const std::set<Text> &);
} // namespace DomeOffice
