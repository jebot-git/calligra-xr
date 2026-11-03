// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_information.h"
namespace DomeOffice {
const NativeSignatures &information_signatures() {
  static const NativeSignatures names = {
      {"ERROR.TYPE", {1, 1}}, {"FORMULA", {1, 1}}, {"ISFORMULA", {1, 1}},
      {"ISNONTEXT", {1, 1}},  {"ISEVEN", {1, 1}},  {"ISODD", {1, 1}},
      {"ISDATE", {1, 1}},     {"ISTIME", {1, 1}},  {"TYPE", {1, 1}}};
  return names;
}
Text information_name(const Text &name) {
  static const std::map<Text, Text> aliases = {
      {"ERRORTYPE", "ERROR.TYPE"},
      {"FORMULATEXT", "FORMULA"},
      {"ISNOTTEXT", "ISNONTEXT"},
      {"ISNUM", "ISNUMBER"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISEVEN", "ISEVEN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISODD", "ISODD"}};
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  if (auto found = aliases.find(base); found != aliases.end())
    return found->second;
  return information_signatures().contains(base) || base == "ISNUMBER" ? base
                                                                       : name;
}
NativeValue information_function(const Text &name, const NativeValue &value) {
  using V = NativeValue;
  auto result = [&]() -> V {
    if (name == "TYPE")
      return V::numeric(value.isArray()           ? 64
                        : value.isError()         ? 16
                        : value.kind == V::String ? 2
                        : value.isBoolean()       ? 4
                                                  : 1);
    if (name == "ISNONTEXT")
      return V::logical(value.kind != V::String);
    if (name == "ISDATE")
      return V::logical(value.kind == V::Number &&
                        (value.temporal == V::CalendarDate ||
                         value.temporal == V::CalendarDateTime));
    if (name == "ISTIME")
      return V::logical(value.kind == V::Number &&
                        (value.temporal == V::ClockTime ||
                         value.temporal == V::ElapsedTime ||
                         value.temporal == V::CalendarDateTime));
    if (name == "ERROR.TYPE") {
      static const std::map<Text, int> codes = {
          {"#NULL!", 1}, {"#DIV/0!", 2}, {"#VALUE!", 3}, {"#REF!", 4},
          {"#NAME?", 5}, {"#NUM!", 6},   {"#N/A", 7}};
      if (value.isError())
        if (auto found = codes.find(value.string); found != codes.end())
          return V::numeric(found->second);
      return V::error("#N/A");
    }
    if (name == "ISEVEN" || name == "ISODD") {
      if (value.isError())
        return value;
      if (value.isBoolean() || value.isComplex() || value.isArray())
        return V::error("#VALUE!");
      double number = 0;
      if (value.kind == V::Number)
        number = value.number;
      else if (value.kind == V::String) {
        bool valid = false;
        number = value.string.toDouble(&valid);
        if (!valid)
          return V::error("#VALUE!");
      }
      if (!std::isfinite(number))
        return V::error("#NUM!");
      bool even = std::fmod(std::trunc(number), 2) == 0;
      return V::logical(name == "ISEVEN" ? even : !even);
    }
    // FORMULA/ISFORMULA consume reference metadata in SheetEngine, before any
    // referenced cell is evaluated. They must not come through this value API.
    return V::error("#VALUE!", true);
  }();
  result.incomplete = result.incomplete || value.incomplete;
  return result;
}
} // namespace DomeOffice
