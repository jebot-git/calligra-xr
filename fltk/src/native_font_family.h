// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
namespace DomeOffice {
// Shared by the resolver and edit validation. Keep the existing document
// grammar: comma-separated names, with quotes protecting embedded commas.
inline TextList document_font_families(const Text &requested) {
  TextList result;
  Text item;
  Char quote = 0;
  auto append = [&] {
    auto name = item.trimmed();
    item.clear();
    if (!name.empty()) {
      if (result.size() >= 32)
        throw std::runtime_error("Document font-family list exceeds the limit");
      result.push_back(std::move(name));
    }
  };
  for (auto c : requested) {
    if (quote) {
      if (c == quote)
        quote = 0;
      else
        item += c;
    } else if (c == '\'' || c == '"')
      quote = c;
    else if (c == ',')
      append();
    else
      item += c;
  }
  if (quote)
    throw std::runtime_error("Invalid quoted document font family");
  append();
  return result;
}
} // namespace DomeOffice
