// SPDX-License-Identifier: LGPL-2.0-only
// Native regular-coupon valuation, inversion and duration. PRICE follows the
// retained/OpenFormula exponential discounting, including one remaining coupon.
#include "native_bond.h"
#include "native_coupon.h"
#include <limits>
namespace DomeOffice {
namespace {
using V = NativeValue;
using Real = long double;
[[noreturn]] void fail(const char *code) { throw V::error(code); }
Real numeric(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber())
    return v.number;
  if (v.kind == V::Empty)
    return 0;
  if (v.isBoolean())
    return v.boolean;
  if (v.isArray())
    fail("#VALUE!");
  bool ok = false;
  auto n = v.string.toDouble(&ok);
  if (!ok)
    fail("#VALUE!");
  if (!std::isfinite(n))
    fail("#NUM!");
  return n;
}
struct Series {
  int length = 0;
  Real power = 1, sum = 0, moment = 0;
};
Series append(const Series &a, const Series &b) {
  return {a.length + b.length, a.power * b.power, a.sum + a.power * b.sum,
          a.moment + a.power * (a.length * b.sum + b.moment)};
}
Series series(int count, Real discount) {
  // Binary concatenation computes both sum(q^k) and sum(k*q^k) in O(log N).
  // q is in [0,1], so zero rates and extreme rate/span products need neither
  // cancellation-prone closed fractions nor a loop over every payment.
  Series total, block{1, discount, 1, 0};
  while (count) {
    if (count & 1)
      total = append(total, block);
    count >>= 1;
    if (count)
      block = append(block, block);
  }
  return total;
}
struct Evaluation {
  Real log_value, time;
};
struct Bond {
  int count, frequency;
  Real coupon, redemption, offset, accrued;
  Evaluation at(Real u) const {
    Real scale = std::max(coupon, redemption);
    Real c = coupon / scale, r = redemption / scale;
    auto sums = series(count, std::exp(-std::abs(u)));
    Real last = std::exp(-(count - 1) * std::abs(u));
    Real sum, weighted, shift;
    if (u >= 0) {
      sum = c * sums.sum + r * last;
      weighted = c * (offset * sums.sum + sums.moment) +
                 r * (count - 1 + offset) * last;
      shift = offset;
    } else {
      // Reverse the polynomial and factor out its last payment. The scaled
      // value and first moment remain finite even near yield = -frequency.
      shift = count - 1 + offset;
      sum = c * sums.sum + r;
      weighted = c * (shift * sums.sum - sums.moment) + r * shift;
    }
    if (sum == 0) {
      // A zero-coupon bond may underflow its discounted redemption before
      // the logarithm is taken; its log value and duration are exact directly.
      Real time = count - 1 + offset;
      return {std::log(redemption) - time * u, time};
    }
    return {std::log(scale) + std::log(sum) - shift * u, weighted / sum};
  }
  Real price(Real u) const { return std::exp(at(u).log_value) - accrued; }
  Real yield(Real clean_price) const {
    Real dirty = clean_price + accrued;
    // A zero-time payment is undiscountable. Equality at the infinite-yield
    // limit is not evidence of a finite root; a lone zero-time payment has
    // no uniquely determined yield at all.
    if (offset == 0 && (count == 1 || dirty <= coupon))
      fail("#NUM!");
    Real target = std::log(dirty);
    Real lower =
        std::log1p(Real(std::nextafter(-double(frequency), 0.)) / frequency);
    Real upper =
        std::log1p(Real(std::numeric_limits<double>::max()) / frequency);
    if (at(lower).log_value < target || at(upper).log_value > target)
      fail("#NUM!");
    Real u = 0;
    for (size_t i = 0; i < 256; ++i) {
      auto value = at(u);
      Real residual = value.log_value - target;
      if (std::abs(residual) <= 1e-14L) {
        Real rate = std::expm1(u) * frequency;
        if (!std::isfinite(double(rate)) || double(rate) <= -frequency)
          fail("#NUM!");
        return rate;
      }
      if (residual > 0)
        lower = u;
      else
        upper = u;
      Real next = value.time == 0 ? std::numeric_limits<Real>::quiet_NaN()
                                  : u + residual / value.time;
      Real margin = (upper - lower) * .2L;
      if (!std::isfinite(next) || next <= lower + margin ||
          next >= upper - margin)
        next = (lower + upper) / 2;
      if (next == u)
        fail("#NUM!");
      u = next;
    }
    fail("#NUM!");
  }
};
} // namespace
bool bond_function_name(const Text &name) {
  return name == "PRICE" || name == "YIELD" || name == "DURATION" ||
         name == "DURATION_ADD" || name == "MDURATION";
}
NativeValue bond_function(const Text &name,
                          const std::vector<NativeValue> &args,
                          const NativeCalculationSettings &settings) try {
  if (!bond_function_name(name))
    return V::error("#NAME?", true);
  bool valuation = name == "PRICE" || name == "YIELD";
  size_t required = valuation ? 6 : 5;
  if (args.size() < required || args.size() > required + 1)
    fail("#VALUE!");
  auto settlement = calendar_date(args[0], settings),
       maturity = calendar_date(args[1], settings);
  Real coupon_rate = numeric(args[2]), rate_or_price = numeric(args[3]);
  Real redemption = valuation ? numeric(args[4]) : 100;
  Real frequency = std::trunc(numeric(args[required - 1]));
  Real basis = args.size() > required ? std::trunc(numeric(args[required])) : 0;
  bool valid_frequency = frequency == 1 || frequency == 2 || frequency == 4;
  // Retain PRICE's extra regular frequencies; other original bond functions
  // expose only annual, semiannual and quarterly schedules.
  if (name == "PRICE")
    valid_frequency =
        frequency >= 1 && frequency <= 12 && 12 % int(frequency) == 0;
  if (!valid_frequency || basis < 0 || basis > 4 || coupon_rate < 0 ||
      redemption <= 0 || settlement >= maturity ||
      (name == "YIELD" ? rate_or_price <= 0 : rate_or_price < 0))
    fail("#VALUE!");
  auto period = coupon_period(settlement, maturity, int(frequency), int(basis));
  Real payment = 100 * coupon_rate / frequency;
  Bond bond{period.remaining,
            int(frequency),
            payment,
            redemption,
            period.after / period.days,
            payment * period.before / period.days};
  if (name == "YIELD")
    return V::numeric(double(bond.yield(rate_or_price)));
  Real u = std::log1p(rate_or_price / frequency);
  if (name == "PRICE")
    return V::numeric(double(bond.price(u)));
  // Retained Calligra duration (also used by LibreOffice's analysis helper)
  // dates the final cash flow with YEARFRAC. All dated duration entry points
  // share that clock; it is distinct from PRICE's coupon-period exponent.
  bond.offset = year_fraction(settlement, maturity, int(basis)) * frequency -
                (period.remaining - 1);
  Real duration = bond.at(u).time / frequency;
  if (name == "MDURATION")
    duration /= 1 + rate_or_price / frequency;
  return V::numeric(double(duration));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
