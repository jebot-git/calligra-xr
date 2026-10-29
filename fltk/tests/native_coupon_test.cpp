// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_coupon.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  using namespace std::chrono;
  SheetEngine engine;
  engine.addNewSheet("Coupons");
  auto eval = [&](const Text &f) {
    engine.set(0, 20, 1, "=" + f);
    return engine.value(0, 20, 1);
  };
  auto near = [&](const Text &f, double want) {
    auto got = eval(f);
    if (!got.isNumber() ||
        std::abs(got.number - want) > 1e-11 * std::max(1., std::abs(want)))
      std::cerr << f.toStdString() << ": " << got.display().toStdString()
                << " != " << want << '\n';
    assert(got.isNumber() && !got.incomplete);
    assert(std::abs(got.number - want) <= 1e-11 * std::max(1., std::abs(want)));
  };
  auto date = [&](const Text &f, const char *want) {
    auto got = eval(f);
    assert(got.kind == NativeValue::Number && got.format == NativeValue::Date);
    assert(!got.incomplete && got.string == want);
    assert(calendar_date(got) == calendar_date(NativeValue(want)));
  };
  date("COUPPCD(\"2024-03-01\",\"2025-08-31\",2,1)", "2024-02-29");
  date("COUPNCD(\"2024-03-01\",\"2025-08-31\",2,1)", "2024-08-31");
  date("COUPNCD(\"2024-08-31\",\"2025-08-31\",2,1)", "2025-02-28");
  date("COUPPCD(\"2024-08-31\",\"2025-08-31\",2,1)", "2024-08-31");
  date("COUPNCD(\"2024-03-01\",\"2025-02-28\",2,1)", "2024-08-31");
  near("COUPDAYBS(\"2024-03-01\",\"2025-08-31\",2,1)", 1);
  near("COUPDAYS(\"2024-03-01\",\"2025-08-31\",2,1)", 184);
  near("COUPDAYSNC(\"2024-03-01\",\"2025-08-31\",2,1)", 183);
  near("COUPDAYS(\"2024-03-01\",\"2025-08-31\",2,3)", 182.5);
  near("COUPDAYSNC(\"2024-01-15\",\"2025-02-28\",2,0)", 45);
  near("COUPDAYSNC(\"2024-01-15\",\"2025-02-28\",2,4)", 44);
  near("COUPDAYSNC(\"2024-08-30\",\"2025-08-31\",2,4)", 0);
  near("COUPDAYSNC(\"2024-08-30\",\"2025-08-31\",2,5)", 1);
  near("COUPNUM(\"2024-08-30\",\"2025-02-28\",2,1,TRUE())", 2);
  near("COUPNUM(\"2024-08-30\",\"2025-02-28\",2,1,FALSE())", 1);
  near("COUPNUM(\"2024-01-01\",\"2024-04-30\",12,1)", 4);
  near("COUPNUM(\"0001-01-01\",\"9999-12-31\",12,1)", 119988);
  near("SUM(COUPNUM({\"2024-01-01\",\"2024-07-01\"},\"2025-01-01\",2))", 3);
  near("COUPNUM(\"2024-01-01\",\"2025-01-01\",2.9,1.9)", 2);
  near("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETCOUPNUM(\"2024-01-01\",\"2025-01-"
       "01\",2)",
       2);
  near("_xlfn.COUPNUM(\"2024-01-01\",\"2025-01-01\",2)", 2);
  for (auto [f, want] : std::vector<std::pair<Text, Text>>{
           {"COUPNUM(0,365,0)", "#VALUE!"},
           {"COUPNUM(0,365,5)", "#VALUE!"},
           {"COUPNUM(0,365,13)", "#VALUE!"},
           {"COUPNUM(0,365,2,-1)", "#VALUE!"},
           {"COUPNUM(0,365,2,6)", "#VALUE!"},
           {"COUPNUM(365,0,2)", "#VALUE!"},
           {"COUPNUM(0,0,2)", "#VALUE!"},
           {"COUPNUM(0,365,2,1,\"bad\")", "#VALUE!"},
           {"COUPNUM(NA(),365,2)", "#N/A"},
           {"COUPPCD(\"0001-01-01\",\"0001-06-30\",1)", "#NUM!"},
           {"COUPNCD(0,365,2,1,FALSE())", "#VALUE!"}}) {
    auto got = eval(f);
    assert(got.isError() && got.string == want && !got.incomplete);
  }
  auto partial = eval("IFERROR(COUPNUM(UNKNOWN_DATE(),365,2),77)");
  assert(partial.incomplete && partial.number == 77);
  auto period = coupon_period(sys_days(year(2024) / 8 / 30),
                              sys_days(year(2025) / 2 / 28), 2, 1, false);
  assert(period.previous == sys_days(year(2024) / 8 / 28) &&
         period.next == sys_days(year(2025) / 2 / 28));
  engine.set(0, 1, 1, "2024-08-30");
  engine.set(0, 2, 1, "=COUPNUM(A1,DATE(2025,2,28),2)");
  assert(engine.value(0, 2, 1).number == 2);
  engine.set(0, 1, 1, "2024-08-31");
  assert(engine.value(0, 2, 1).number == 1);
  std::cout << "Native maturity-anchored coupon schedules, end-of-month rules, "
               "day bases, typed dates and dependency updates pass\n";
}
