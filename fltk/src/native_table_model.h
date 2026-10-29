// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
// Immutable source-scoped style catalogue. Nested parsers share the expansion
// budget and return the original source block identities for repeated content.
class OdfTableModelReader {
public:
  using ReadCell = std::function<std::vector<TextBlock>(const XmlElement &)>;
  explicit OdfTableModelReader(const std::vector<XmlDocument> &);
  TextTable read(const XmlElement &, const ReadCell &, size_t &expanded_slots,
                 bool styles_source = false) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};
} // namespace DomeOffice
