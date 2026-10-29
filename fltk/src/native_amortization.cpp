// SPDX-License-Identifier: LGPL-2.0-only
// Retained French linear/degressive depreciation without Qt calculation types.
#include "native_amortization.h"
#include "native_calendar.h"
#include "native_financial_value.h"
namespace DomeOffice {
bool amortization_function_name(const Text &name) {
  return name == "AMORLINC" || name == "AMORDEGRC";
}
NativeValue
amortization_function(const Text &name, const std::vector<NativeValue> &args,
                      const NativeCalculationSettings &settings) try {
  using namespace FinancialValues;
  if (!amortization_function_name(name))
    return V::error("#NAME?", true);
  if (args.size() < 6 || args.size() > 7)
    fail("#VALUE!");
  for (const auto &arg : args)
    if (arg.isArray())
      fail("#VALUE!");
  Real cost = number(args[0]), salvage = number(args[3]);
  Real period = std::trunc(number(args[4])), rate = number(args[5]);
  auto purchase = calendar_date(args[1], settings),
       end = calendar_date(args[2], settings);
  int basis = args.size() > 6 ? integer(args[6], 0, 4) : 0;
  if (cost < 0 || salvage < 0 || period < 0 || rate < 0)
    fail("#VALUE!");
  Real fraction = year_fraction(purchase, end, basis);
  if (name == "AMORLINC") {
    // Zero cost/rate passed the old validation but led to an undefined
    // floating-to-integer conversion. Their depreciation is zero.
    if (cost == 0 || rate == 0)
      return V::numeric(0);
    Real annual = cost * rate, first = fraction * annual;
    Real full = std::trunc((cost - salvage - first) / annual);
    Real out = period == 0      ? first
               : period <= full ? annual
               : period == full + 1
                   ? std::max(0.L, cost - salvage - annual * full - first)
                   : 0;
    return V::numeric(double(out));
  }
  if (cost <= 0 || rate <= 0 || salvage > cost || purchase > end)
    fail("#VALUE!");
  // The retained recurrence and coefficient selection use binary64. Keeping
  // the input's wider representation changes bands (1/.2) and half-unit
  // rounds (.15*2.5*1316), so retain those explicit rounding boundaries.
  double declining_rate = double(rate);
  double life = 1. / declining_rate;
  double coefficient = life < 3 ? 1 : life < 5 ? 1.5 : life <= 6 ? 2 : 2.5;
  declining_rate *= coefficient;
  double remaining_cost = double(cost);
  double depreciation =
      double(std::round(fraction * declining_rate * remaining_cost));
  remaining_cost -= depreciation;
  double rest = remaining_cost - double(salvage);
  for (size_t n = 0; Real(n) < period; ++n) {
    if (n == 100000)
      throw V::error("#LIMIT!", true);
    depreciation = std::round(declining_rate * remaining_cost);
    double next_rest = rest - depreciation;
    if (!std::isfinite(depreciation) || !std::isfinite(next_rest))
      fail("#NUM!");
    if (next_rest < 0)
      return V::numeric(period - Real(n) <= 1 ? std::round(remaining_cost * .5)
                                              : 0);
    double next_cost = remaining_cost - depreciation;
    if (next_cost == remaining_cost && next_rest == rest)
      return V::numeric(depreciation);
    remaining_cost = next_cost;
    rest = next_rest;
  }
  return V::numeric(depreciation);
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
