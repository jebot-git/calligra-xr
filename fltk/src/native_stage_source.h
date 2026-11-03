// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_xml.h"
namespace DomeOffice {
// Locate a source node in a content.xml clone, retaining whitespace siblings.
inline XmlElement stage_candidate_source(const XmlElement &source,
                                         const XmlDocument &candidate) {
  std::vector<size_t> path;
  auto original = source.ownerDocument().documentElement();
  for (auto node = source; node != original; node = node.parentNode()) {
    if (node.isNull())
      throw std::runtime_error("Shape is outside the presentation");
    size_t index = 0;
    for (auto previous = node.previousSibling(); !previous.isNull();
         previous = previous.previousSibling())
      ++index;
    path.push_back(index);
  }
  auto node = candidate.documentElement();
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    node = node.firstChild();
    for (size_t i = 0; i < *it && !node.isNull(); ++i)
      node = node.nextSibling();
    if (node.isNull())
      throw std::runtime_error("Invalid shape source path");
  }
  return node.toElement();
}
} // namespace DomeOffice
