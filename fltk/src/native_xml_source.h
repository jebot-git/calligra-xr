// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_xml.h"
namespace DomeOffice {
// Locate an element in a serialized document clone. Adjacent text nodes can
// coalesce during serialization/parsing after structural edits; element ranks
// remain stable while every whitespace/comment/source node is still retained.
inline XmlElement document_source_clone(const XmlElement &source,
                                        const XmlDocument &candidate) {
  if (!source.isElement())
    throw std::runtime_error("Source is not an XML element");
  std::vector<size_t> path;
  auto original = source.ownerDocument().documentElement();
  for (auto node = source; node != original; node = node.parentNode()) {
    if (node.isNull())
      throw std::runtime_error("Source is outside the document");
    size_t index = 0;
    for (auto previous = node.previousSibling(); !previous.isNull();
         previous = previous.previousSibling())
      if (previous.isElement())
        ++index;
    path.push_back(index);
  }
  auto node = candidate.documentElement();
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    node = node.firstChild();
    for (size_t i = 0; !node.isNull();) {
      if (node.isElement() && i++ == *it)
        break;
      node = node.nextSibling();
    }
    if (node.isNull())
      throw std::runtime_error("Invalid document source path");
  }
  if (node.namespaceURI() != source.namespaceURI() ||
      node.localName() != source.localName())
    throw std::runtime_error(
        "Document source element does not match its clone");
  return node.toElement();
}
} // namespace DomeOffice
