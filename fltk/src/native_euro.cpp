// SPDX-License-Identifier: LGPL-2.0-only
// Retained fixed historical euro conversions, not live exchange rates.
#include "native_euro.h"
#include "native_financial_value.h"
namespace DomeOffice {
namespace {
using namespace FinancialValues;
const std::map<Text, Real> &rates() {
  static const std::map<Text, Real> values = {
      {"ATS", 13.7603L}, {"BEF", 40.3399L}, {"CYP", .585274L},
      {"DEM", 1.95583L}, {"EEK", 15.6466L}, {"ESP", 166.386L},
      {"EUR", 1},        {"FIM", 5.94573L}, {"FRF", 6.55957L},
      {"GRD", 340.75L},  {"IEP", .787564L}, {"ITL", 1936.27L},
      {"LTL", 3.45280L}, {"LUX", 40.3399L}, {"LUF", 40.3399L},
      {"LVL", .702804L}, {"MTL", .4293L},   {"NLG", 2.20371L},
      {"PTE", 200.482L}, {"SIT", 239.64L},  {"SKK", 30.126L}};
  return values;
}
Real factor(const Text &code) {
  auto found = rates().find(code);
  if (found == rates().end())
    fail("#NUM!");
  return found->second;
}
Real rounded(Real value, Real digits) {
  // Beyond the precision of any binary64-derived intermediate, rounding is
  // an identity. Avoid overflowing a power or casting an enormous count.
  if (digits > 500)
    return value;
  Real scale = std::pow(10.L, int(digits));
  return std::round(value * scale) / scale;
}
} // namespace
bool euro_function_name(const Text &name) {
  return name == "EURO" || name == "EUROCONVERT" ||
         name == "ORG.OPENOFFICE.CONVERT";
}
NativeValue euro_function(const Text &name,
                          const std::vector<NativeValue> &args) try {
  using namespace FinancialValues;
  if (!euro_function_name(name))
    return V::error("#NAME?", true);
  if (name == "EURO") {
    if (args.size() != 1)
      fail("#VALUE!");
    return V::numeric(double(factor(text(args[0]).toUpper())));
  }
  bool old_convert = name == "ORG.OPENOFFICE.CONVERT";
  if (args.size() < 3 || args.size() > (old_convert ? 3 : 5))
    fail("#VALUE!");
  Real value = number(args[0]);
  auto from = text(args[1]), to = text(args[2]);
  if (old_convert && (from != from.toUpper() || to != to.toUpper()))
    fail("#VALUE!");
  from = from.toUpper();
  to = to.toUpper();
  Real from_rate = factor(from), to_rate = factor(to);
  bool full = old_convert || (args.size() >= 4 && logical(args[3]));
  Real precision = args.size() == 5 ? std::floor(number(args[4])) : 0;
  if (args.size() == 5 && precision < 3)
    fail("#VALUE!");
  if (from == to)
    return V::numeric(double(value));
  Real intermediate = value / from_rate;
  if (precision > 0)
    intermediate = rounded(intermediate, precision);
  Real converted = intermediate * to_rate;
  if (!full) {
    bool whole =
        to == "BEF" || to == "ESP" || to == "ITL" || to == "LUF" || to == "LUX";
    converted = rounded(converted, whole ? 0 : 2);
  }
  return V::numeric(double(converted));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
