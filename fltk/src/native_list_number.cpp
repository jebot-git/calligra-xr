// SPDX-License-Identifier: LGPL-2.0-or-later
// Native counterpart of KoOdfNumberDefinition's numbering formats.
#include "native_list_number.h"
#include <unicode/uchar.h>
namespace DomeOffice {
namespace {
std::optional<char32_t> digit_zero(const Text &format) {
  if (format.size() == 1 && u_charType(format[0]) == U_DECIMAL_DIGIT_NUMBER)
    return char32_t(format[0] - u_charDigitValue(format[0]));
  for (auto [name, zero] :
       std::vector<std::pair<Text, char32_t>>{{"ก, ข, ค, ...", 0xe50},
                                              {"༡, ༢, ༣, ...", 0xf20},
                                              {"౧, ౨, ౩, ...", 0xc66},
                                              {"௧, ௨, ௪, ...", 0xbe6},
                                              {"୧, ୨, ୩, ...", 0xb66},
                                              {"൧, ൨, ൩, ...", 0xd66},
                                              {"೧, ೨, ೩, ...", 0xce6},
                                              {"੧, ੨, ੩, ...", 0xa66},
                                              {"૧, ૨, ૩, ...", 0xae6},
                                              {"১, ২, ৩, ...", 0x9e6}})
    if (format == name)
      return zero;
  return {};
}
const std::vector<Text> *alphabet(const Text &format) {
  static const std::vector<Text> arabic = {
      "ا", "ب", "ت", "ث", "ج", "ح", "خ", "د", "ذ", "ر", "ز", "س", "ش", "ص",
      "ض", "ط", "ظ", "ع", "غ", "ف", "ق", "ك", "ل", "م", "ن", "ه", "و", "ي"};
  static const std::vector<Text> abjad = {
      "أ", "ب", "ج", "د", "ﻫ", "و", "ز", "ح", "ط", "ي", "ك",
      "ل", "م", "ن", "س", "ع", "ف", "ص", "ق", "ر", "ش", "ت"};
  static const std::vector<Text> minor = {
      "ﺃ", "ﺏ", "ﺝ", "ﺩ", "ﻫ", "ﻭ", "ﺯ", "ﺡ", "ﻁ", "ﻱ", "ﻙ",
      "ﻝ", "ﻡ", "ﻥ", "ﺹ", "ﻉ", "ﻑ", "ﺽ", "ﻕ", "ﺭ", "ﺱ", "ﺕ"};
  if (format == "أ, ب, ت, ...")
    return &arabic;
  if (format == "أ, ب, ج, ...")
    return &abjad;
  if (format == "ﺃ,ﺏ, ﺝ, ... ")
    return &minor;
  return nullptr;
}
bool padded(const Text &format) {
  if (format.empty() || format.size() > 32 || format.back() != '1')
    return false;
  for (size_t i = 0; i + 1 < format.size(); ++i)
    if (format[i] != '0')
      return false;
  return true;
}
} // namespace
Text canonical_list_format(const Text &format) {
  auto at = format.indexOf(',');
  if (at < 0)
    return format;
  auto first = format.left(at).trimmed();
  if (!padded(first))
    return format;
  auto stem = first.left(first.size() - 1);
  return format == first + ", " + stem + "2, " + stem + "3, ..." ? first
                                                                 : format;
}
bool supported_list_number_format(const Text &input) {
  auto format = canonical_list_format(input);
  return format.empty() || format == "a" || format == "A" || format == "i" ||
         format == "I" || padded(format) || digit_zero(format) ||
         alphabet(format);
}
Text format_list_number(int64_t value, const Text &input, bool synchronized) {
  auto format = canonical_list_format(input);
  if (value < 0)
    throw std::runtime_error("Negative list counter");
  if (format.empty())
    return {};
  if (padded(format)) {
    auto text = Text::number(value);
    return Text(std::max<int>(0, int(format.size()) - int(text.size())), U'0') +
           text;
  }
  if (auto zero = digit_zero(format)) {
    Text result;
    for (auto digit : Text::number(value))
      result += char32_t(*zero + digit - U'0');
    return result;
  }
  if (const auto *symbols = alphabet(format))
    return value == 0                         ? Text("0")
           : value > int64_t(symbols->size()) ? Text("*")
                                              : symbols->at(value - 1);
  if (format == "a" || format == "A") {
    if (!value)
      return "0";
    char32_t base = format == "a" ? U'a' : U'A';
    if (synchronized) {
      auto count = (value - 1) / 26 + 1;
      if (count > 4096)
        throw std::runtime_error("Synchronized list label exceeds limit");
      return Text(size_t(count), char32_t(base + (value - 1) % 26));
    }
    Text result;
    while (value) {
      --value;
      result = Text(1, char32_t(base + value % 26)) + result;
      value /= 26;
    }
    return result;
  }
  if (format == "i" || format == "I") {
    if (value == 0 || value > 9999)
      return Text::number(value);
    Text result;
    for (auto [n, roman] : std::vector<std::pair<int, Text>>{{1000, "m"},
                                                             {900, "cm"},
                                                             {500, "d"},
                                                             {400, "cd"},
                                                             {100, "c"},
                                                             {90, "xc"},
                                                             {50, "l"},
                                                             {40, "xl"},
                                                             {10, "x"},
                                                             {9, "ix"},
                                                             {5, "v"},
                                                             {4, "iv"},
                                                             {1, "i"}})
      while (value >= n) {
        result += roman;
        value -= n;
      }
    return format == "I" ? result.toUpper() : result;
  }
  throw std::runtime_error("Unsupported list numbering format");
}
} // namespace DomeOffice
