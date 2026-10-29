// SPDX-License-Identifier: LGPL-2.0-only
// Native date conversion and irregular cash-flow calculations. The retained
// financial.cpp and OpenFormula 6.12.51/52 supply the behavioral references.
#include "native_financial_dates.h"
#include "native_calendar.h"
#include <limits>
#include <map>
#include <span>
namespace DomeOffice {
namespace {
using V = NativeValue;
using Real = long double;
[[noreturn]] void fail(const char *message) { throw V::error(message); }
Real scalar(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber())
    return v.number;
  if (v.kind == V::Empty)
    return 0;
  if (v.isBoolean())
    return v.boolean ? 1 : 0;
  if (v.isArray())
    fail("#VALUE!");
  bool valid = false;
  auto value = v.string.toDouble(&valid);
  if (!valid)
    fail("#VALUE!");
  if (!std::isfinite(value))
    fail("#NUM!");
  return value;
}
std::span<const V> items(const V &v) {
  if (v.isError())
    throw v;
  if (!v.isArray())
    return {&v, 1};
  if (!v.items || v.items->size() > 1000000)
    throw V::error("#LIMIT!", true);
  return *v.items;
}
struct Sum {
  Real value = 0, compensation = 0;
  void add(Real n) {
    auto adjusted = n - compensation, next = value + adjusted;
    compensation = (next - value) - adjusted;
    value = next;
  }
};
struct CashFlow {
  Real amount, time;
};
struct Flows {
  std::vector<CashFlow> entries;
  Real norm = 0;
  bool positive = false, negative = false;
};
Flows flows(const V &values, const V &dates, bool numeric_sequence) {
  std::vector<Real> amounts;
  for (const auto &v : items(values)) {
    if (v.isError())
      throw v;
    if (v.isNumber())
      amounts.push_back(v.number);
    else if (numeric_sequence && values.isArray())
      continue;
    else if (numeric_sequence)
      amounts.push_back(scalar(v));
    else
      fail("#VALUE!");
  }
  auto date_items = items(dates);
  if (amounts.empty() || amounts.size() != date_items.size())
    fail("#NUM!");
  const auto first = calendar_date(date_items.front());
  Flows out;
  for (auto amount : amounts)
    out.norm = std::max(out.norm, std::abs(amount));
  // Combine equal dates before solving. Opposing same-day flows can cancel
  // exactly; their cancellation must not manufacture a spurious IRR.
  std::map<int, Sum> by_day;
  for (size_t i = 0; i < amounts.size(); ++i) {
    auto day = calendar_date(date_items[i]);
    int delta = int((day - first).count());
    if (delta < 0)
      fail("#NUM!");
    by_day[delta].add(out.norm == 0 ? 0 : amounts[i] / out.norm);
  }
  for (const auto &[day, sum] : by_day) {
    if (sum.value == 0)
      continue;
    out.entries.push_back({sum.value, Real(day) / 365});
    out.positive = out.positive || sum.value > 0;
    out.negative = out.negative || sum.value < 0;
  }
  return out;
}
struct Evaluation {
  Real value = 0, derivative = 0, magnitude = 0, shift = 0;
};
Evaluation evaluate(const Flows &flows, Real log_rate, Real origin = 0) {
  Evaluation out;
  out.shift =
      flows.entries.empty() ? 0 : -std::numeric_limits<Real>::infinity();
  for (const auto &flow : flows.entries)
    out.shift = std::max(out.shift, -(flow.time - origin) * log_rate);
  Sum value, derivative, magnitude;
  for (const auto &flow : flows.entries) {
    Real time = flow.time - origin;
    Real term = flow.amount * std::exp(-time * log_rate - out.shift);
    value.add(term);
    derivative.add(-time * term);
    magnitude.add(std::abs(term));
  }
  out.value = value.value;
  out.derivative = derivative.value;
  out.magnitude = magnitude.value;
  return out;
}
Real xirr(const Flows &flows, Real guess) {
  if (guess <= -1 || !flows.positive || !flows.negative ||
      flows.entries.size() < 2)
    fail("#NUM!");
  const Real lower = std::log1p(Real(std::nextafter(-1., 0.)));
  const Real upper = std::log(Real(std::numeric_limits<double>::max()));
  Real u = std::log1p(guess);
  size_t work = 0;
  auto at = [&](Real point) {
    if (flows.entries.size() > 10000000 - work)
      throw V::error("#LIMIT!", true);
    work += flows.entries.size();
    // Leading zero flows contribute a common time factor. Removing it keeps
    // distant leading dates from flattening an otherwise well-conditioned root.
    return evaluate(flows, point, flows.entries.front().time);
  };
  auto solved = [](const Evaluation &v) {
    return v.magnitude > 0 && std::abs(v.value) <= 1e-14L * v.magnitude;
  };
  auto result = [](Real point) {
    Real rate = std::expm1(point);
    if (!std::isfinite(rate) || double(rate) <= -1)
      fail("#NUM!");
    return rate;
  };
  auto value = at(u);
  if (solved(value))
    return result(u);
  std::optional<std::pair<Real, Real>> bracket;
  Real low_value = 0;
  // Search outwards from the supplied guess for a sign-changing interval.
  // Newton is retained for roots which merely touch zero and have no bracket.
  for (Real distance = .125L; distance < 2048 && !bracket; distance *= 2) {
    for (Real candidate :
         {std::max(lower, u - distance), std::min(upper, u + distance)}) {
      if (candidate == u)
        continue;
      auto probe = at(candidate);
      if (solved(probe))
        return result(candidate);
      if (std::signbit(probe.value) != std::signbit(value.value)) {
        bracket = std::minmax(u, candidate);
        low_value = candidate < u ? probe.value : value.value;
        break;
      }
    }
  }
  for (size_t i = 0; i < 256; ++i) {
    if (solved(value))
      return result(u);
    if (bracket) {
      if (std::signbit(value.value) == std::signbit(low_value)) {
        bracket->first = u;
        low_value = value.value;
      } else
        bracket->second = u;
    }
    // Differentiate the original NPV, without differentiating its common
    // exponential scale. A bracket supplies bisection when Newton is unsafe.
    Real next = value.derivative == 0 ? std::numeric_limits<Real>::quiet_NaN()
                                      : u - value.value / value.derivative;
    if (bracket) {
      Real margin = (bracket->second - bracket->first) * .2L;
      // Contract the bracket even when a tiny Newton step near an extreme
      // endpoint would otherwise be valid but make almost no progress.
      if (!std::isfinite(next) || next <= bracket->first + margin ||
          next >= bracket->second - margin)
        next = (bracket->first + bracket->second) / 2;
    } else {
      if (!std::isfinite(next))
        fail("#NUM!");
      next = std::clamp(next, std::max(lower, u - 8), std::min(upper, u + 8));
    }
    if (next == u)
      fail("#NUM!");
    u = next;
    value = at(u);
  }
  fail("#NUM!");
}
} // namespace
NativeValue financial_date_function(const Text &name,
                                    const std::vector<V> &args) try {
  bool irr = name == "XIRR";
  if (name != "XIRR" && name != "XNPV")
    return V::error("#NAME?", true);
  if ((irr && (args.size() < 2 || args.size() > 3)) ||
      (!irr && args.size() != 3))
    fail("#VALUE!");
  auto data = flows(args[irr ? 0 : 1], args[irr ? 1 : 2], irr);
  if (irr)
    return V::numeric(
        double(xirr(data, args.size() == 3 ? scalar(args[2]) : .1L)));
  Real rate = scalar(args[0]);
  if (rate <= -1)
    fail("#NUM!");
  auto sum = evaluate(data, std::log1p(rate));
  if (sum.value == 0 || data.norm == 0)
    return V::numeric(0);
  Real magnitude =
      std::exp(std::log(std::abs(sum.value)) + std::log(data.norm) + sum.shift);
  return V::numeric(double(std::copysign(magnitude, sum.value)));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
