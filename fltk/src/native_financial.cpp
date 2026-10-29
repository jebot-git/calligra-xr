// SPDX-FileCopyrightText: 2007 Sascha Pfau <MrPeacock@gmail.com>
// SPDX-FileCopyrightText: 2002, 2006 Ariya Hidayat <ariya@kde.org>
// SPDX-FileCopyrightText: 2006 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
// SPDX-FileCopyrightText: 2005 Tomas Mecir <mecirt@gmail.com>
// SPDX-FileCopyrightText: 2002 Norbert Andres <nandres@web.de>
// SPDX-FileCopyrightText: 1999-2000 Laurent Montel <montel@kde.org>
// SPDX-FileCopyrightText: 1999-2000 Jonathan Singer <jsinger@genome.wi.mit.edu>
// SPDX-FileCopyrightText: 1998-1999 Torben Weis <weis@kde.org>
// SPDX-License-Identifier: LGPL-2.0-only
// Native adaptation of the annuity/depreciation algorithms in
// sheets/functions/financial.cpp. No original Qt value or calculation classes
// participate in this module.
#include "native_financial.h"
#include "native_bond.h"
#include "native_calendar.h"
#include "native_coupon.h"
#include "native_financial_dates.h"
#include <limits>
namespace DomeOffice {
const NativeSignatures &financial_signatures() {
  static const NativeSignatures values = {
      {"PMT", {3, 5}},          {"PV", {3, 5}},
      {"FV", {3, 5}},           {"NPER", {3, 5}},
      {"RATE", {3, 6}},         {"IPMT", {4, 6}},
      {"PPMT", {4, 6}},         {"ISPMT", {4, 4}},
      {"CUMIPMT", {6, 6}},      {"CUMPRINC", {6, 6}},
      {"IRR", {1, 2}},          {"MIRR", {3, 3}},
      {"NPV", {2, 255}},        {"FVSCHEDULE", {2, 2}},
      {"EFFECT", {2, 2}},       {"EFFECTIVE", {2, 2}},
      {"NOMINAL", {2, 2}},      {"PDURATION", {3, 3}},
      {"DURATION", {3, 6}},     {"RRI", {3, 3}},
      {"COMPOUND", {4, 4}},     {"CONTINUOUS", {3, 3}},
      {"FV_ANNUITY", {3, 3}},   {"PV_ANNUITY", {3, 3}},
      {"LEVEL_COUPON", {5, 5}}, {"ZERO_COUPON", {3, 3}},
      {"SLN", {3, 3}},          {"SYD", {4, 4}},
      {"DB", {4, 5}},           {"DDB", {4, 5}},
      {"VDB", {5, 7}},          {"DOLLARDE", {2, 2}},
      {"DOLLARFR", {2, 2}},     {"XIRR", {2, 3}},
      {"XNPV", {3, 3}},         {"ACCRINTM", {3, 5}},
      {"DISC", {4, 5}},         {"INTRATE", {4, 5}},
      {"PRICEDISC", {4, 5}},    {"RECEIVED", {4, 5}},
      {"YIELDDISC", {4, 5}},    {"COUPDAYBS", {3, 4}},
      {"COUPDAYS", {3, 4}},     {"COUPDAYSNC", {3, 4}},
      {"COUPNCD", {3, 4}},      {"COUPNUM", {3, 5}},
      {"COUPPCD", {3, 4}},      {"PRICE", {6, 7}},
      {"YIELD", {6, 7}},        {"MDURATION", {5, 6}},
      {"DURATION_ADD", {5, 6}}};
  return values;
}
Text financial_name(const Text &name) {
  static const Text prefix = "COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GET";
  static const std::set<Text> aliases = {
      "CUMIPMT",   "CUMPRINC",   "DOLLARDE", "DOLLARFR",   "DURATION",
      "EFFECT",    "FVSCHEDULE", "NOMINAL",  "XIRR",       "XNPV",
      "ACCRINTM",  "DISC",       "INTRATE",  "PRICEDISC",  "RECEIVED",
      "YIELDDISC", "COUPDAYBS",  "COUPDAYS", "COUPDAYSNC", "COUPNCD",
      "COUPNUM",   "COUPPCD",    "PRICE",    "YIELD",      "MDURATION"};
  if (name.startsWith(prefix)) {
    auto base = name.mid(prefix.size());
    if (aliases.contains(base))
      return base;
  }
  Text base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  return financial_signatures().contains(base) ? base : name;
}
namespace {
using V = NativeValue;
using Real = long double;
[[noreturn]] void fail(const char *code) { throw V::error(code); }
Real number(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber())
    return v.number;
  if (v.kind == V::Empty)
    return 0;
  if (v.kind == V::Boolean)
    return v.boolean ? 1 : 0;
  if (v.isArray())
    fail("#VALUE!");
  bool ok = false;
  auto result = v.string.toDouble(&ok);
  if (!ok)
    fail("#VALUE!");
  if (!std::isfinite(result))
    fail("#NUM!");
  return result;
}
Real divide(Real a, Real b) {
  if (b == 0)
    fail("#DIV/0!");
  return a / b;
}
Real growth(Real rate, Real periods) {
  return rate > -1 ? std::exp(periods * std::log1p(rate))
                   : std::pow(1 + rate, periods);
}
struct Series {
  Real growth = 1, annuity = 0, excess = 0, derivative = 0;
};
Series series(Real rate, Real periods) {
  Series s;
  s.growth = growth(rate, periods);
  // Directly form annuity-minus-periods as well as its derivative near zero;
  // subtracting the two close quantities loses small-rate interest payments.
  if (std::abs(rate * periods) < 1e-4L && std::abs(rate) < 1e-4L) {
    Real c2 = periods * (periods - 1) / 2;
    Real c3 = c2 * (periods - 2) / 3;
    Real c4 = c3 * (periods - 3) / 4;
    Real c5 = c4 * (periods - 4) / 5;
    s.excess = rate * (c2 + rate * (c3 + rate * (c4 + rate * c5)));
    s.annuity = periods + s.excess;
    s.derivative = c2 + rate * (2 * c3 + rate * (3 * c4 + 4 * rate * c5));
  } else {
    s.annuity = rate > -1 ? std::expm1(periods * std::log1p(rate)) / rate
                          : (s.growth - 1) / rate;
    s.excess = s.annuity - periods;
    s.derivative = (periods * growth(rate, periods - 1) - s.annuity) / rate;
  }
  return s;
}
Real payment(Real rate, Real periods, Real present, Real future, bool due) {
  auto s = series(rate, periods);
  return divide(-present * s.growth - future, (1 + rate * due) * s.annuity);
}
Real future(Real rate, Real periods, Real pay, Real present, bool due) {
  auto s = series(rate, periods);
  return -present * s.growth - pay * (1 + rate * due) * s.annuity;
}
Real interest(Real rate, Real period, Real periods, Real present, Real fv,
              bool due) {
  if (period < 1 || period > periods)
    fail("#VALUE!");
  Real pay = payment(rate, periods, present, fv, due);
  if (period == 1)
    return due ? 0 : -present * rate;
  return rate * (future(rate, period - (due ? 2 : 1), pay, present, due) -
                 (due ? pay : 0));
}
bool rate_iteration(Real periods, Real pay, Real present, Real fv, bool due,
                    Real &guess) {
  Real x = guess;
  bool integral = periods == std::round(periods);
  if (!integral && x <= -1)
    return false;
  for (size_t i = 0; i < 150; ++i) {
    auto s = series(x, periods);
    Real a = present * s.growth, b = pay * (1 + x * due) * s.annuity;
    Real f = fv + a + b;
    Real scale = std::max(Real(1), std::abs(fv) + std::abs(a) + std::abs(b));
    if (!std::isfinite(f) || !std::isfinite(scale))
      return false;
    if (std::abs(f) <= 1e-15L * scale) {
      guess = x;
      return x > -1;
    }
    Real derivative = present * periods * growth(x, periods - 1) +
                      pay * (due * s.annuity + (1 + x * due) * s.derivative);
    Real next = derivative == 0 ? x + 1.1e-7L : x - f / derivative;
    if (!std::isfinite(next) || (!integral && next <= -1))
      return false;
    x = next;
  }
  return false;
}
std::vector<Real> sequence(const V &input) {
  std::vector<Real> values;
  auto add = [&](const V &v) {
    if (v.isError())
      throw v;
    if (v.isNumber())
      values.push_back(v.number);
  };
  if (input.isArray()) {
    if (!input.items || input.items->size() > 1000000)
      throw V::error("#LIMIT!", true);
    for (const auto &v : *input.items)
      add(v);
  } else
    values.push_back(number(input));
  return values;
}
Real irr(std::vector<Real> values, Real guess) {
  bool positive = false, negative = false;
  Real norm = 0;
  for (auto v : values) {
    positive = positive || v > 0;
    negative = negative || v < 0;
    norm = std::max(norm, std::abs(v));
  }
  if (!positive || !negative || guess <= -1)
    fail("#NUM!");
  auto first =
      std::find_if(values.begin(), values.end(), [](auto v) { return v != 0; });
  values.erase(values.begin(), first);
  while (!values.empty() && values.back() == 0)
    values.pop_back();
  for (auto &v : values)
    v /= norm;
  Real x = guess;
  size_t work = 0;
  for (size_t attempt = 0; attempt < 128; ++attempt) {
    if (values.size() > 10000000 - work)
      throw V::error("#LIMIT!", true);
    work += values.size();
    Real f = 0, derivative = 0, scale = 0;
    // Evaluate in the direction whose multiplier is at most one, avoiding
    // huge discounted powers for negative rates and long cash-flow vectors.
    if (x < 0) {
      Real factor = 1 + x;
      for (auto v : values) {
        derivative = derivative * factor + f;
        f = f * factor + v;
        scale = scale * factor + std::abs(v);
      }
      // This polynomial is NPV multiplied by factor^(count-1). Remove the
      // derivative of that scale so Newton follows the same NPV function on
      // both sides of zero, instead of oscillating between representations.
      derivative -= (values.size() - 1) * f / factor;
    } else {
      Real q = 1 / (1 + x);
      for (auto it = values.rbegin(); it != values.rend(); ++it) {
        derivative = derivative * q + f;
        f = f * q + *it;
        scale = scale * q + std::abs(*it);
      }
      derivative *= -q * q;
    }
    if (std::abs(f) <= 1e-14L * scale)
      return x;
    if (derivative == 0 || !std::isfinite(derivative))
      fail("#NUM!");
    Real next = x - f / derivative;
    if (!std::isfinite(next))
      fail("#NUM!");
    if (next <= -1)
      next = (x - 1) / 2;
    x = next;
  }
  fail("#NUM!");
}
// Retained variable-declining-balance helpers, with checked loop bounds and
// compensated sums. Their fractional boundary behavior is shared with Calligra.
double declining(double cost, double salvage, double life, double period,
                 double factor) {
  double rate = std::min(1., factor / life);
  double old = rate == 1 ? (period == 1 ? cost : 0)
                         : cost * std::pow(1 - rate, period - 1);
  double next = cost * std::pow(1 - rate, period);
  return std::max(0., old - std::max(salvage, next));
}
double variable_sum(double cost, double salvage, double life, double remaining,
                    double periods, double factor) {
  if (periods > 1000000)
    throw V::error("#LIMIT!", true);
  size_t end = size_t(std::ceil(periods));
  double sum = 0, compensation = 0, balance = cost - salvage, straight = 0;
  bool switched = false;
  for (size_t i = 1; i <= end; ++i) {
    double term;
    if (!switched) {
      auto step = declining(cost, salvage, life, double(i), factor);
      straight = balance / (remaining - double(i - 1));
      if (straight > step) {
        term = straight;
        switched = true;
      } else {
        term = step;
        balance -= step;
      }
    } else
      term = straight;
    if (i == end)
      term *= periods + 1 - std::ceil(periods);
    double adjusted = term - compensation, next = sum + adjusted;
    compensation = (next - sum) - adjusted;
    sum = next;
  }
  return sum;
}
} // namespace
NativeValue financial_function(const Text &input,
                               const std::vector<NativeValue> &args) try {
  const auto name = financial_name(input);
  auto signature = financial_signatures().find(name);
  if (signature == financial_signatures().end())
    return V::error("#NAME?", true);
  if (args.size() < signature->second.first ||
      args.size() > signature->second.second)
    fail("#VALUE!");
  size_t records = 0;
  size_t record_limit = (name == "XIRR" || name == "XNPV") ? 2000003 : 1000000;
  for (const auto &arg : args) {
    size_t size = arg.isArray() && arg.items ? arg.items->size() : 1;
    if (size > record_limit - records)
      throw V::error("#LIMIT!", true);
    records += size;
  }
  if (name == "XIRR" || name == "XNPV")
    return financial_date_function(name, args);
  if (coupon_function_name(name))
    return coupon_function(name, args);
  if (bond_function_name(name) && (name != "DURATION" || args.size() != 3))
    return bond_function(name, args);
  auto n = [&](size_t index, Real fallback = 0) {
    return index < args.size() ? number(args[index]) : fallback;
  };
  auto result = [](Real value) { return V::numeric(double(value)); };
  if (name == "ACCRINTM" || name == "DISC" || name == "INTRATE" ||
      name == "PRICEDISC" || name == "RECEIVED" || name == "YIELDDISC") {
    auto start = calendar_date(args[0]), end = calendar_date(args[1]);
    const char *invalid = name == "DISC" ? "#NUM!" : "#VALUE!";
    auto basis = std::trunc(n(4));
    if (basis < 0 || basis > 4 || start > end ||
        (start == end && name != "ACCRINTM"))
      fail(invalid);
    Real first = n(2), second = n(3, 1000);
    if (first <= 0 || second <= 0)
      fail(invalid);
    Real fraction = year_fraction(start, end, int(basis));
    if (name == "ACCRINTM")
      return result(second * first * fraction);
    if (name == "PRICEDISC")
      return result(second * (1 - first * fraction));
    if (name == "RECEIVED") {
      Real denominator = 1 - second * fraction;
      if (denominator == 0)
        fail(invalid);
      return result(first / denominator);
    }
    Real denominator = name == "DISC" ? second : first;
    return result(divide((second - first) / denominator, fraction));
  }
  if (name == "PMT" || name == "PV" || name == "FV") {
    Real rate = n(0), periods = n(1), a = n(2), b = n(3);
    bool due = n(4) != 0;
    auto s = series(rate, periods);
    if (name == "PMT")
      return result(payment(rate, periods, a, b, due));
    if (name == "PV")
      return result(divide(-b - a * (1 + rate * due) * s.annuity, s.growth));
    return result(future(rate, periods, a, b, due));
  }
  if (name == "NPER") {
    Real rate = n(0), pay = n(1), pv = n(2), fv = n(3);
    bool due = n(4) != 0;
    if (pv + fv == 0)
      return result(0);
    if (rate == 0)
      return result(divide(-pv - fv, pay));
    if (rate <= -1)
      fail("#VALUE!");
    Real denominator = pay * (1 + rate * due) + rate * pv;
    Real change = divide(-(pv + fv) * rate, denominator);
    if (change <= -1)
      fail("#VALUE!");
    return result(std::log1p(change) / std::log1p(rate));
  }
  if (name == "RATE") {
    Real periods = n(0), pay = n(1), pv = n(2), fv = n(3), original = n(5, .1L),
         guess = original;
    bool due = n(4) != 0;
    if (periods <= 0)
      fail("#VALUE!");
    bool found = rate_iteration(periods, pay, pv, fv, due, guess);
    if (!found && args.size() < 6)
      for (size_t i = 2; i <= 10 && !found; ++i) {
        guess = original * i;
        found = rate_iteration(periods, pay, pv, fv, due, guess);
        if (!found) {
          guess = original / i;
          found = rate_iteration(periods, pay, pv, fv, due, guess);
        }
      }
    if (!found)
      fail("#NUM!");
    return result(guess);
  }
  if (name == "IPMT" || name == "PPMT") {
    Real rate = n(0), period = n(1), periods = n(2), pv = n(3), fv = n(4);
    bool due = n(5) != 0;
    Real ip = interest(rate, period, periods, pv, fv, due);
    return result(name == "IPMT" ? ip
                                 : payment(rate, periods, pv, fv, due) - ip);
  }
  if (name == "ISPMT") {
    Real rate = n(0), period = n(1), periods = n(2), pv = n(3);
    if (period < 1 || period > periods)
      fail("#VALUE!");
    return result(-pv * rate * (1 - divide(period, periods)));
  }
  if (name == "CUMIPMT" || name == "CUMPRINC") {
    Real rate = n(0), periods = n(1), pv = n(2), start = std::trunc(n(3)),
         end = std::trunc(n(4)), type = n(5);
    if (rate <= 0 || periods <= 0 || pv <= 0 || start < 1 || end < start ||
        end > periods || (type != 0 && type != 1))
      fail("#VALUE!");
    Real pay = payment(rate, periods, pv, 0, type != 0),
         count = end - start + 1;
    Real first = type ? std::max(Real(2), start) : start;
    Real ip = 0;
    if (first <= end) {
      Real m = end - first + 1, exponent = first - (type ? 2 : 1);
      auto s = series(rate, m);
      Real g = growth(rate, exponent);
      Real sum = g * s.annuity;
      Real excess = m * std::expm1(exponent * std::log1p(rate)) + g * s.excess;
      ip = -rate * pv * sum - pay * (excess + (type ? rate * sum : 0));
    }
    return result(name == "CUMIPMT" ? ip : pay * count - ip);
  }
  if (name == "IRR")
    return result(irr(sequence(args[0]), n(1, .1L)));
  if (name == "MIRR") {
    auto values = sequence(args[0]);
    Real finance = n(1), reinvest = n(2);
    if (values.size() < 2)
      fail("#DIV/0!");
    if (finance <= -1 || reinvest <= -1)
      fail("#NUM!");
    Real positive = 0, negative = 0;
    for (size_t i = 0; i < values.size(); ++i)
      if (values[i] > 0)
        positive += values[i] * growth(reinvest, values.size() - 1 - i);
      else
        negative += values[i] / growth(finance, i);
    if (positive == 0 || negative == 0)
      fail("#DIV/0!");
    return result(
        std::expm1(std::log(-positive / negative) / (values.size() - 1)));
  }
  if (name == "NPV") {
    Real rate = n(0), sum = 0, compensation = 0;
    size_t period = 1;
    for (size_t i = 1; i < args.size(); ++i)
      for (auto v : sequence(args[i])) {
        Real term = divide(v, growth(rate, period++)) - compensation,
             next = sum + term;
        compensation = (next - sum) - term;
        sum = next;
      }
    return result(sum);
  }
  if (name == "FVSCHEDULE") {
    Real value = n(0);
    for (auto rate : sequence(args[1]))
      value *= 1 + rate;
    return result(value);
  }
  if (name == "EFFECT" || name == "EFFECTIVE" || name == "NOMINAL") {
    Real rate = n(0), periods = n(1);
    if (name == "NOMINAL" && periods == 0)
      fail("#DIV/0!");
    if (rate < 0 || (name == "NOMINAL" && rate == 0) || periods < 1 ||
        periods != std::floor(periods))
      fail("#VALUE!");
    return result(name == "NOMINAL"
                      ? periods * std::expm1(std::log1p(rate) / periods)
                      : std::expm1(periods * std::log1p(rate / periods)));
  }
  if (name == "PDURATION" || name == "DURATION") {
    Real rate = n(0), pv = n(1), fv = n(2);
    if (rate <= 0 || pv <= 0 || fv <= 0)
      fail("#VALUE!");
    return result((std::log(fv) - std::log(pv)) / std::log1p(rate));
  }
  if (name == "RRI") {
    Real periods = n(0), pv = n(1), fv = n(2);
    if (periods <= 0 || pv <= 0 || fv <= 0)
      fail("#VALUE!");
    return result(std::expm1((std::log(fv) - std::log(pv)) / periods));
  }
  if (name == "COMPOUND") {
    Real pv = n(0), rate = n(1), periods = n(2), years = n(3);
    return result(pv * growth(divide(rate, periods), periods * years));
  }
  if (name == "CONTINUOUS")
    return result(n(0) * std::exp(n(1) * n(2)));
  if (name == "ZERO_COUPON")
    return result(divide(n(0), growth(n(1), n(2))));
  if (name == "FV_ANNUITY" || name == "PV_ANNUITY") {
    Real amount = n(0), rate = n(1), periods = n(2);
    auto s = series(rate, periods);
    return result(name == "FV_ANNUITY" ? amount * s.annuity
                                       : divide(amount * s.annuity, s.growth));
  }
  if (name == "LEVEL_COUPON") {
    Real face = n(0), coupon = n(1), frequency = n(2), years = n(3),
         rate = divide(n(4), frequency);
    auto s = series(rate, years * frequency);
    return result(
        divide(face + divide(face * coupon, frequency) * s.annuity, s.growth));
  }
  if (name == "DOLLARDE" || name == "DOLLARFR") {
    Real value = n(0), fraction = std::trunc(n(1));
    if (fraction <= 0)
      fail("#VALUE!");
    Real whole = 0, part = std::modf(value, &whole),
         scale = std::pow(10.L, std::ceil(std::log10(fraction)));
    return result(whole + part * (name == "DOLLARDE" ? scale / fraction
                                                     : fraction / scale));
  }
  if (name == "SLN") {
    Real cost = n(0), salvage = n(1), life = n(2);
    if (life == 0)
      fail("#VALUE!");
    return result((cost - salvage) / life);
  }
  if (name == "SYD") {
    Real cost = n(0), salvage = n(1), life = n(2), period = n(3);
    if (life <= 0 || period <= 0 || period > life)
      fail("#VALUE!");
    return result((cost - salvage) * (life - period + 1) * 2 /
                  (life * (life + 1)));
  }
  if (name == "DB") {
    Real cost = n(0), salvage = n(1), life = std::trunc(n(2)),
         period = std::trunc(n(3)), month = n(4, 12);
    if (cost <= 0 || salvage < 0 || salvage > cost || life <= 0 ||
        period <= 0 || period > life + 1 || month < 1 || month > 12)
      fail("#NUM!");
    Real rate =
        std::floor(1000 * (1 - std::pow(salvage / cost, 1 / life)) + .5L) /
        1000;
    Real first = cost * rate * month / 12;
    if (period == 1)
      return result(first);
    Real remaining =
        (cost - first) * std::pow(1 - rate, std::min(period - 2, life - 1));
    return result(remaining * rate * (period > life ? (12 - month) / 12 : 1));
  }
  if (name == "DDB") {
    Real cost = n(0), salvage = n(1), life = n(2), period = n(3),
         factor = n(4, 2);
    if (cost <= 0 || salvage < 0 || life <= 0 || period <= 0 || period > life ||
        factor <= 0)
      fail("#NUM!");
    if (factor >= life)
      return result(period > 1 ? 0 : std::max(Real(0), cost - salvage));
    return result(declining(double(cost), double(salvage), double(life),
                            double(period), double(factor)));
  }
  if (name == "VDB") {
    double cost = double(n(0)), salvage = double(n(1)), life = double(n(2)),
           start = double(n(3)), end = double(n(4)), factor = double(n(5, 2));
    bool no_switch = n(6) != 0;
    if (cost < 0 || salvage < 0 || salvage > cost || life <= 0 || start < 0 ||
        end < start || end > life || factor <= 0)
      fail("#VALUE!");
    if (end > 1000000)
      throw V::error("#LIMIT!", true);
    double low = std::floor(start), high = std::ceil(end), sum = 0,
           compensation = 0;
    if (no_switch) {
      for (size_t i = size_t(low) + 1; i <= size_t(high); ++i) {
        double term = declining(cost, salvage, life, double(i), factor);
        if (i == size_t(low) + 1)
          term *= std::min(end, low + 1) - start;
        else if (i == size_t(high))
          term *= end + 1 - high;
        double adjusted = term - compensation, next = sum + adjusted;
        compensation = (next - sum) - adjusted;
        sum = next;
      }
    } else {
      double part = 0;
      if (start != low) {
        double value =
            cost - variable_sum(cost, salvage, life, life, low, factor);
        part += (start - low) *
                variable_sum(value, salvage, life, life - low, 1, factor);
      }
      if (end != high) {
        double value =
            cost - variable_sum(cost, salvage, life, life, high - 1, factor);
        part += (high - end) *
                variable_sum(value, salvage, life, life - high + 1, 1, factor);
      }
      cost -= variable_sum(cost, salvage, life, life, low, factor);
      sum = variable_sum(cost, salvage, life, life - low, high - low, factor) -
            part;
    }
    return result(sum);
  }
  return V::error("#NAME?", true);
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
