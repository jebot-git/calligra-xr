// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
// Embedded documents have their own style, font and text namespaces. Until
// their own renderer handles them, retain and diagnose them without flattening
// their content into the surrounding document's editable text flow.
inline bool native_embedded_document_boundary(XmlElement node) {
  if (node.namespaceURI() == DRAW)
    return node.localName() == "object" || node.localName() == "object-ole";
  return node.namespaceURI() == OFFICE &&
         TextList{"document", "document-content", "document-styles"}.contains(
             node.localName());
}
} // namespace DomeOffice
