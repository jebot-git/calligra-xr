// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_xml_source.h"
namespace DomeOffice {
inline XmlElement stage_candidate_source(const XmlElement &source,
                                         const XmlDocument &candidate) {
  return document_source_clone(source, candidate);
}
} // namespace DomeOffice
