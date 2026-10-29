// SPDX-License-Identifier: LGPL-2.0-only
// OpenFormula 1.4 sections 4.11.7, 6.10.7 and 6.10.25. DAYS360's US method
// deliberately differs from Basis 0, and unlike YEARFRAC preserves date order.
#include "native_calendar.h"
#include "native_time.h"
namespace DomeOffice {
namespace {
using namespace std::chrono;
using V = NativeValue;
[[noreturn]] void fail(const char *message) { throw V::error(message); }
void check_basis(int basis) {
  if (basis < 0 || basis > 4)
    fail("#VALUE!");
}
bool february_end(year_month_day date) {
  return date.month() == February &&
         date == year_month_day(date.year() / February / last);
}
int convention_days(year_month_day start, year_month_day end, bool european,
                    bool financial_basis) {
  int d1 = unsigned(start.day()), d2 = unsigned(end.day());
  if (d1 == 31)
    d1 = 30;
  if (european) {
    if (d2 == 31)
      d2 = 30;
  } else if (financial_basis) {
    // Preserve the procedure's order: the February adjustment follows the
    // end-of-month-31 rule. Counterfactual February 30 is never constructed.
    if (d1 == 30 && d2 == 31)
      d2 = 30;
    if (february_end(start) && february_end(end))
      d2 = 30;
    if (february_end(start))
      d1 = 30;
  } else {
    if (february_end(start))
      d1 = 30;
    if (d1 == 30 && d2 == 31)
      d2 = 30;
  }
  return (int(end.year()) - int(start.year())) * 360 +
         (int(unsigned(end.month())) - int(unsigned(start.month()))) * 30 + d2 -
         d1;
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
  bool valid = false;
  auto result = v.string.toDouble(&valid);
  if (!valid)
    fail("#VALUE!");
  if (!std::isfinite(result))
    fail("#NUM!");
  return result;
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
int leap_years_through(int year) { return year / 4 - year / 100 + year / 400; }
} // namespace
std::chrono::sys_days calendar_date(const NativeValue &v) {
  using namespace std::chrono;
  if (v.isError())
    throw v;
  const auto epoch = sys_days(year(1899) / 12 / 30);
  if (v.isNumber()) {
    auto serial = std::trunc(v.number);
    auto minimum = (sys_days(year(1) / 1 / 1) - epoch).count();
    auto maximum = (sys_days(year(9999) / 12 / 31) - epoch).count();
    if (!std::isfinite(serial) || serial < minimum || serial > maximum)
      fail("#NUM!");
    return epoch + days(int(serial));
  }
  if (v.kind != V::String || v.string.size() > 128)
    fail("#VALUE!");
  auto date = Date::fromString(v.string, ISODate);
  if (!date.valid) {
    auto time = DateTime::fromString(v.string, ISODate);
    if (time.valid)
      date = time.day;
  }
  if (!date.valid || int(date.day.year()) < 1 || int(date.day.year()) > 9999)
    fail("#VALUE!");
  return sys_days(date.day);
}
int basis_days(std::chrono::sys_days start, std::chrono::sys_days end,
               int basis) {
  check_basis(basis);
  if (start > end)
    std::swap(start, end);
  if (start == end)
    return 0;
  if (basis != 0 && basis != 4)
    return int((end - start).count());
  return convention_days(year_month_day(start), year_month_day(end), basis == 4,
                         true);
}
long double basis_year_length(std::chrono::sys_days start,
                              std::chrono::sys_days end, int basis) {
  check_basis(basis);
  if (basis != 1)
    return basis == 3 ? 365 : 360;
  if (start > end)
    std::swap(start, end);
  auto a = year_month_day(start), b = year_month_day(end);
  int y1 = int(a.year()), y2 = int(b.year());
  bool different = y1 != y2;
  bool over_year = different && (y2 != y1 + 1 || a.month() < b.month() ||
                                 (a.month() == b.month() && a.day() < b.day()));
  if (over_year) {
    int years = y2 - y1 + 1;
    int leaps = leap_years_through(y2) - leap_years_through(y1 - 1);
    return 365.L + static_cast<long double>(leaps) / years;
  }
  // Procedure E explicitly includes a leap start year when crossing a year
  // boundary, including intervals beginning after February.
  if (different && a.year().is_leap())
    return 366;
  for (auto y : {a.year(), b.year()}) {
    if (y.is_leap()) {
      auto leap = sys_days(y / February / 29);
      if (start <= leap && leap <= end)
        return 366;
    }
  }
  return 365;
}
long double year_fraction(std::chrono::sys_days start,
                          std::chrono::sys_days end, int basis) {
  return basis_days(start, end, basis) / basis_year_length(start, end, basis);
}
const NativeSignatures &calendar_signatures() {
  static const NativeSignatures values = {{"YEARFRAC", {2, 3}},
                                          {"DAYS360", {2, 3}}};
  return values;
}
Text calendar_name(const Text &name) {
  Text base = name;
  if (base == "COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETYEARFRAC")
    return "YEARFRAC";
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  return calendar_signatures().contains(base) ? base : name;
}
NativeValue calendar_function(const Text &input,
                              const std::vector<NativeValue> &args) try {
  auto name = calendar_name(input);
  if (!calendar_signatures().contains(name))
    return V::error("#NAME?", true);
  if (args.size() < 2 || args.size() > 3)
    fail("#VALUE!");
  auto start = calendar_date(args[0]), end = calendar_date(args[1]);
  if (name == "YEARFRAC") {
    auto basis = args.size() == 3 ? std::trunc(numeric(args[2])) : 0;
    if (basis < 0 || basis > 4)
      fail("#VALUE!");
    return V::numeric(double(year_fraction(start, end, int(basis))));
  }
  bool european = args.size() == 3 && logical(args[2]);
  // European arithmetic is antisymmetric; the NASD method operates directly
  // on the ordered pair without swapping its endpoints.
  return V::numeric(convention_days(year_month_day(start), year_month_day(end),
                                    european, false));
} catch (const NativeValue &failure) {
  return failure;
}
} // namespace DomeOffice
