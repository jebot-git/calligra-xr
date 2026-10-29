// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_calendar.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Calendar");
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
  near("YEARFRAC(\"2024-02-29\",\"2024-03-31\",0)", 31. / 360);
  near("DAYS360(\"2024-02-29\",\"2024-03-31\")", 30);
  near("DAYS360(\"2024-03-31\",\"2024-02-29\")", -31);
  near("DAYS360(\"2024-03-31\",\"2024-02-29\",TRUE())", -31);
  near("YEARFRAC(\"2024-02-29\",\"2025-02-28\",0)", 1);
  near("DAYS360(\"2024-02-29\",\"2025-02-28\")", 358);
  near("YEARFRAC(\"2024-10-01\",\"2025-01-11\",1)", 102. / 366);
  near("YEARFRAC(\"2025-01-11\",\"2024-10-01\",1)", 102. / 366);
  near("YEARFRAC(\"2024-02-29\",\"2025-02-28\",1)", 365. / 366);
  near("YEARFRAC(\"2023-03-01\",\"2024-02-28\",1)", 364. / 365);
  near("YEARFRAC(\"2023-03-01\",\"2024-02-29\",1)", 365. / 366);
  near("YEARFRAC(\"2000-01-01\",\"2002-01-01\",1)", 731. / (365 + 1. / 3));
  near("YEARFRAC(\"1900-01-01\",\"1901-01-01\",1)", 1);
  near("YEARFRAC(0.9,365.9,3.9)", 1);
  near("YEARFRAC(365.9,0.9,3)", 1);
  near("YEARFRAC(0,0,1)", 0);
  near("ACCRINTM(\"2024-02-01\",\"2024-05-01\",.1,1000,0)", 25);
  near("ACCRINTM(0,365,.1)", 100);
  near("ACCRINTM(0,0,.1)", 0);
  near("DISC(0,365,95,100,3)", .05);
  near("INTRATE(0,365,100,110,3)", .1);
  near("YIELDDISC(0,365,100,110,3)", .1);
  near("PRICEDISC(0,365,.1,100,3)", 90);
  near("RECEIVED(0,365,100,.1,3)", 100. / .9);
  near("RECEIVED(0,365,100,2,3)", -100);
  near("SUM(YEARFRAC({0,0},{360,365},{2,3}))", 2);
  near("SUM(DISC({0,0},365,{90,95},100,3))", .15);
  near("_xlfn.YEARFRAC(0,365,3)", 1);
  near("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETYEARFRAC(0,365,3)", 1);
  near("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETPRICEDISC(0,365,.1,100,3)", 90);
  for (auto [f, want] : std::vector<std::pair<Text, Text>>{
           {"YEARFRAC(0,365,5)", "#VALUE!"},
           {"YEARFRAC(0,365,-1)", "#VALUE!"},
           {"YEARFRAC(\"1900-02-29\",365)", "#VALUE!"},
           {"YEARFRAC(-1000000000,365)", "#NUM!"},
           {"YEARFRAC(0,NA(),3)", "#N/A"},
           {"DAYS360(0,365,\"bad\")", "#VALUE!"},
           {"DISC(365,0,95,100)", "#NUM!"},
           {"DISC(0,365,95,100,5)", "#NUM!"},
           {"INTRATE(0,0,100,110)", "#VALUE!"},
           {"INTRATE(0,365,0,110)", "#VALUE!"},
           {"RECEIVED(0,365,100,1,3)", "#VALUE!"},
           {"PRICEDISC(0,365,0,100)", "#VALUE!"},
           {"YIELDDISC(0,365,100,0)", "#VALUE!"}}) {
    auto got = eval(f);
    assert(got.isError() && got.string == want && !got.incomplete);
  }
  auto partial = eval("IFERROR(YEARFRAC(UNKNOWN_DATE(),365),77)");
  assert(partial.incomplete && partial.number == 77);
  engine.set(0, 1, 1, "2024-02-01");
  engine.set(0, 2, 1, "2024-05-01");
  engine.set(0, 3, 1, "=ACCRINTM(A1,B1,.1,1000,0)");
  assert(engine.value(0, 3, 1).number == 25);
  engine.set(0, 2, 1, "2024-06-01");
  assert(std::abs(engine.value(0, 3, 1).number - 100. / 3) < 1e-12);
  std::cout << "Native day-count conventions, signed DAYS360, Gregorian leap "
               "spans, securities and dependency updates pass\n";
}
