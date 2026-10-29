// SPDX-License-Identifier: LGPL-2.0-only
// Toolkit-independent port of the coupon schedule/conventions retained in
// sheets/functions/financial.cpp, originally adapted there from Gnumeric.
#include "native_coupon.h"
#include <cstdio>
namespace DomeOffice {
namespace {
using namespace std::chrono;
using V = NativeValue;
[[noreturn]] void fail(const char *code) { throw V::error(code); }
bool month_end(year_month_day d) {
  return d == year_month_day(d.year() / d.month() / last);
}
sys_days anchored_month(year_month_day maturity, int months_back, bool eom) {
  auto month = maturity.year() / maturity.month() - months(months_back);
  auto end = year_month_day(month / last);
  auto day = eom ? end.day() : std::min(maturity.day(), end.day());
  return sys_days(month / day);
}
int coupon_days(sys_days start, sys_days end, int basis) {
  if (basis >= 1 && basis <= 3)
    return int((end - start).count());
  auto a = year_month_day(start), b = year_month_day(end);
  int first = unsigned(a.day()), second = unsigned(b.day());
  if (basis == 0) {
    // The retained coupon model uses MSRB 30/360, independently adjusting
    // both February endpoints. This differs from YEARFRAC's Procedure A.
    if (a.month() == February && month_end(a))
      first = 30;
    if (b.month() == February && month_end(b))
      second = 30;
    if (second == 31 && first >= 30)
      second = 30;
    if (first == 31)
      first = 30;
  } else {
    if (first == 31)
      first = 30;
    if (second == 31 && basis == 4)
      second = 30;
    // In 30E+/360, replacing an ending 31st by day 1 of the next month
    // leaves its linear 30-day-month coordinate unchanged.
  }
  return (int(b.year()) - int(a.year())) * 360 +
         (int(unsigned(b.month())) - int(unsigned(a.month()))) * 30 + second -
         first;
}
long double numeric(const V &v) {
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
bool logical(const V &v) {
  if (v.kind == V::String) {
    auto s = v.string.toUpper();
    if (s == "TRUE")
      return true;
    if (s == "FALSE")
      return false;
  }
  return numeric(v) != 0;
}
V date_result(sys_days date, const NativeCalculationSettings &settings) {
  auto d = year_month_day(date);
  if (int(d.year()) < 1 || int(d.year()) > 9999)
    fail("#NUM!");
  auto out = V::numeric((date - settings.reference_date()).count());
  out.format = V::Date;
  char text[32];
  std::snprintf(text, sizeof text, "%04d-%02u-%02u", int(d.year()),
                unsigned(d.month()), unsigned(d.day()));
  out.string = Text(text);
  return out;
}
} // namespace
CouponPeriod coupon_period(std::chrono::sys_days settlement,
                           std::chrono::sys_days maturity, int frequency,
                           int basis, bool end_of_month) {
  if (settlement >= maturity || frequency < 1 || frequency > 12 ||
      12 % frequency != 0 || basis < 0 || basis > 5)
    fail("#VALUE!");
  auto a = year_month_day(settlement), b = year_month_day(maturity);
  int months_apart = (int(b.year()) - int(a.year())) * 12 +
                     int(unsigned(b.month())) - int(unsigned(a.month()));
  int step = 12 / frequency;
  int remaining = months_apart / step;
  bool eom = end_of_month && month_end(b);
  if (anchored_month(b, remaining * step, eom) > settlement)
    ++remaining;
  CouponPeriod out;
  out.previous = anchored_month(b, remaining * step, eom);
  out.next = anchored_month(b, (remaining - 1) * step, eom);
  out.remaining = remaining;
  out.days = basis == 1 ? (out.next - out.previous).count()
                        : (basis == 3 ? 365.L : 360.L) / frequency;
  out.before = coupon_days(out.previous, settlement, basis);
  out.after = coupon_days(settlement, out.next, basis);
  return out;
}
bool coupon_function_name(const Text &name) {
  return name == "COUPDAYBS" || name == "COUPDAYS" || name == "COUPDAYSNC" ||
         name == "COUPNCD" || name == "COUPNUM" || name == "COUPPCD";
}
NativeValue coupon_function(const Text &name,
                            const std::vector<NativeValue> &args,
                            const NativeCalculationSettings &settings) try {
  if (!coupon_function_name(name))
    return V::error("#NAME?", true);
  if (args.size() < 3 || args.size() > (name == "COUPNUM" ? 5 : 4))
    fail("#VALUE!");
  auto settlement = calendar_date(args[0], settings),
       maturity = calendar_date(args[1], settings);
  auto frequency = std::trunc(numeric(args[2]));
  auto basis = args.size() > 3 ? std::trunc(numeric(args[3])) : 0;
  if (frequency < 1 || frequency > 12 || basis < 0 || basis > 5)
    fail("#VALUE!");
  auto period = coupon_period(settlement, maturity, int(frequency), int(basis),
                              args.size() < 5 || logical(args[4]));
  if (name == "COUPNCD" || name == "COUPPCD")
    return date_result(name == "COUPNCD" ? period.next : period.previous,
                       settings);
  return V::numeric(double(name == "COUPNUM"     ? period.remaining
                           : name == "COUPDAYS"  ? period.days
                           : name == "COUPDAYBS" ? period.before
                                                 : period.after));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
