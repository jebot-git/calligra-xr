// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
void apply_text_box_properties(TextCellStyle &, const XmlElement &properties);
// Immutable source-scoped style catalogue. Nested parsers share the expansion
// budget and return the original source block identities for repeated content.
class OdfTableModelReader {
public:
  using ReadCell = std::function<std::vector<TextBlock>(const XmlElement &)>;
  explicit OdfTableModelReader(const std::vector<XmlDocument> &);
  std::vector<XmlElement> cell_styles(const Text &name,
                                      bool styles_source) const;
  std::vector<XmlElement> style_chain(const Text &family, const Text &name,
                                      bool styles_source = false) const;
  // Resolve this definition itself, even when an automatic style shadows its
  // name.
  std::vector<XmlElement> definition_chain(const XmlElement &,
                                           bool styles_source = false) const;
  TextTable read(const XmlElement &, const ReadCell &, size_t &expanded_slots,
                 bool styles_source = false) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};
} // namespace DomeOffice
