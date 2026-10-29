// SPDX-License-Identifier: LGPL-2.0-only
// Native port of the retained simple-interest, maturity and odd-last-period
// functions. Source-specific day-count rules are deliberately identified here.
#include "native_security.h"
#include "native_calendar.h"
#include "native_financial_value.h"
namespace DomeOffice {
namespace {
using namespace FinancialValues;
using namespace std::chrono;
Real accrued_fraction(sys_days issue, sys_days settlement, int basis) {
  auto a = year_month_day(issue), b = year_month_day(settlement);
  int days = int((settlement - issue).count());
  if (basis == 0 || basis == 4) {
    int months = (int(b.year()) - int(a.year())) * 12 +
                 int(unsigned(b.month())) - int(unsigned(a.month()));
    days = months * 30 + int(unsigned(b.day())) - int(unsigned(a.day()));
    // This is the old financial helper, not YEARFRAC/COUPDAYBS: it adjusts
    // every February start within the same year, even when not month-end.
    if (basis == 0 && a.month() == February && b.month() != February &&
        a.year() == b.year())
      days -= a.year().is_leap() ? 1 : 2;
  }
  int year_days = basis == 1   ? (a.year().is_leap() ? 366 : 365)
                  : basis == 3 ? 365
                               : 360;
  return Real(days) / year_days;
}
} // namespace
bool security_function_name(const Text &name) {
  return name == "ACCRINT" || name == "ODDLPRICE" || name == "ODDLYIELD" ||
         name == "PRICEMAT" || name == "YIELDMAT" || name == "TBILLEQ" ||
         name == "TBILLPRICE" || name == "TBILLYIELD";
}
NativeValue security_function(const Text &name,
                              const std::vector<NativeValue> &args) try {
  using namespace FinancialValues;
  using namespace std::chrono;
  if (!security_function_name(name))
    return V::error("#NAME?", true);
  auto n = [&](size_t i) { return number(args.at(i)); };
  auto result = [](Real value) { return V::numeric(double(value)); };
  if (name == "ACCRINT") {
    if (args.size() < 6 || args.size() > 7)
      fail("#VALUE!");
    auto issue = calendar_date(args[0]), settlement = calendar_date(args[2]);
    // The retained six/seven-argument algorithm validates but does not use
    // the first-interest date to divide the interval into coupon periods.
    (void)calendar_date(args[1]);
    int frequency = integer(args[5], 1, 12);
    int basis = args.size() > 6 ? integer(args[6], 0, 4) : 0;
    Real rate = n(3), par = n(4);
    if (12 % frequency || issue >= settlement || rate <= 0 || par < 0)
      fail("#VALUE!");
    return result(par * rate * accrued_fraction(issue, settlement, basis));
  }
  if (name == "TBILLEQ" || name == "TBILLPRICE" || name == "TBILLYIELD") {
    if (args.size() != 3)
      fail("#VALUE!");
    auto start = calendar_date(args[0]), end = calendar_date(args[1]);
    Real value = n(2);
    if (value <= 0)
      fail("#VALUE!");
    if (name == "TBILLEQ") {
      // Preserve the source's inclusive maturity and 30/360 convention.
      if (start >= end + days(1))
        fail("#VALUE!");
      int count = basis_days(start, end + days(1), 0);
      if (count > 360)
        fail("#VALUE!");
      return result(divide(365 * value, 360 - value * count));
    }
    if (start >= end)
      fail("#VALUE!");
    if (name == "TBILLPRICE") {
      if ((end - start).count() > 365)
        fail("#VALUE!");
      Real fraction = year_fraction(start, end + days(1), 0);
      if (std::trunc(fraction) == fraction)
        fail("#VALUE!");
      return result(100 * (1 - value * fraction));
    }
    int count = basis_days(start, end, 0) + 1;
    if (count > 360)
      fail("#VALUE!");
    return result(divide((100 - value) * 360, value * count));
  }
  bool odd = name == "ODDLPRICE" || name == "ODDLYIELD";
  size_t required = odd ? 7 : 5;
  if (args.size() < required || args.size() > required + 1)
    fail("#VALUE!");
  auto settlement = calendar_date(args[0]), maturity = calendar_date(args[1]);
  auto issue = calendar_date(args[2]);
  int basis = args.size() > required ? integer(args[required], 0, 4) : 0;
  Real rate = n(3), quote = n(4);
  if (settlement >= maturity || rate < 0)
    fail("#VALUE!");
  Real total = year_fraction(issue, maturity, basis);
  Real accrued = year_fraction(issue, settlement, basis);
  Real remaining = year_fraction(settlement, maturity, basis);
  if (odd) {
    int frequency = integer(args[6], 1, 4);
    if ((frequency != 1 && frequency != 2 && frequency != 4) ||
        issue >= settlement || rate <= 0 || n(5) <= 0 ||
        (name == "ODDLPRICE" ? quote < 0 : quote <= 0))
      fail("#VALUE!");
    // Frequency cancels algebraically in the retained odd-last formulas.
    Real redemption = n(5);
    if (name == "ODDLPRICE")
      return result(
          divide(redemption + 100 * rate * total, 1 + quote * remaining) -
          100 * rate * accrued);
    return result(divide(
        divide(redemption + 100 * rate * total, quote + 100 * rate * accrued) -
            1,
        remaining));
  }
  if (name == "PRICEMAT") {
    if (quote < 0)
      fail("#VALUE!");
    return result(100 * (divide(1 + total * rate, 1 + remaining * quote) -
                         accrued * rate));
  }
  if (quote <= 0 || rate <= 0)
    fail("#VALUE!");
  return result(divide(
      divide(1 + total * rate, quote / 100 + accrued * rate) - 1, remaining));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
