// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_calendar.h"
#include "native_financial_dates.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  using namespace std::chrono;
  using V = NativeValue;
  auto epoch = sys_days(year(1899) / 12 / 30);
  assert(calendar_date(V::numeric(0)) == epoch);
  assert(calendar_date(V::numeric(-1.9)) == epoch - days(1));
  assert(calendar_date(V("2000-02-29")) == sys_days(year(2000) / 2 / 29));
  assert(calendar_date(V("2026-10-29T23:59:59+02:00")) ==
         sys_days(year(2026) / 10 / 29));
  SheetEngine engine;
  engine.addNewSheet("Dates");
  auto eval = [&](const Text &f) {
    engine.set(0, 20, 1, "=" + f);
    return engine.value(0, 20, 1);
  };
  auto near = [&](const Text &f, double want, double tolerance = 1e-10) {
    auto got = eval(f);
    if (!got.isNumber() ||
        std::abs(got.number - want) > tolerance * std::max(1., std::abs(want)))
      std::cerr << f.toStdString() << ": " << got.display().toStdString()
                << " != " << want << '\n';
    assert(got.isNumber() && !got.incomplete);
    assert(std::abs(got.number - want) <=
           tolerance * std::max(1., std::abs(want)));
  };
  near("XIRR({-100,110},{0,365})", .1);
  near("XIRR({-1,2},{0,2958000})", std::expm1(std::log(2.) * 365 / 2958000),
       1e-12);
  near("XIRR({-1,2},{0,2958000},.5)", std::expm1(std::log(2.) * 365 / 2958000),
       1e-12);
  near("XIRR({-100,90},{0,365})", -.1);
  near("XIRR({-100,230,-132},{0,365,730},.3)", .2);
  near("XIRR({-100,230,-132},{0,365,730},.05)", .1);
  near("XIRR({0,-100,110},{0,2958000,2958365},.5)", .1);
  near("XIRR({-100,110},{.9,365.9})", .1);
  near("XIRR({-20000,25000},{DATE(2000,1,1),DATE(2001,1,1)})",
       0.24923812998001549425896882669743020092815077010058124126692);
  near("XIRR({-100,110},{\"2024-01-01\",\"2024-12-31\"})", .1);
  near("XNPV(.1,{-1000,2000,3000},{DATE(2005,1,1),DATE(2005,1,10),DATE(2005,1,"
       "15)})",
       3984.3581140636);
  near("XNPV(0,{-1000,2000,3000},{0,10,20})", 4000);
  near("XNPV(.1,-100,0)", -100);
  near("XNPV(-.99999,{1,-1},{2958465,2958465})", 0);
  near("XNPV(.1,{-100,20;30,80},{0;90;180;365})",
       -100 + 20 / std::pow(1.1, 90. / 365) + 30 / std::pow(1.1, 180. / 365) +
           80 / 1.1);
  near("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETXIRR({-100,110},{0,365})", .1);
  near("_xlfn.XNPV(.1,{-100,110},{0,365})", 0);
  engine.set(0, 1, 1, "-100");
  engine.set(0, 1, 2, "110");
  engine.set(0, 2, 1, "2024-01-01");
  engine.set(0, 2, 2, "2024-12-31");
  near("XIRR(A1:A2,B1:B2)", .1);
  near("XNPV(.1,A1:A2,B1:B2)", 0);
  for (auto [f, want] : std::vector<std::pair<Text, Text>>{
           {"XIRR({100,110},{0,365})", "#NUM!"},
           {"XIRR({100,-100},{0,0})", "#NUM!"},
           {"XIRR({-100,\"ignored\",110},{0,180,365})", "#NUM!"},
           {"XNPV(.1,{-100,\"bad\",110},{0,180,365})", "#VALUE!"},
           {"XNPV(.1,{-100,110},{0,180,365})", "#NUM!"},
           {"XNPV(.1,{-100,110},{365,0})", "#NUM!"},
           {"XNPV(-1,{-100,110},{0,365})", "#NUM!"},
           {"XNPV(.1,{-100,110},{0,2958466})", "#NUM!"},
           {"XNPV(.1,{-100,110},{\"bad\",365})", "#VALUE!"},
           {"XNPV(.1,{-100,110},{\"1900-02-29\",365})", "#VALUE!"},
           {"XNPV(.1,{-100,110},{0,NA()})", "#N/A"}}) {
    auto got = eval(f);
    assert(got.isError() && got.string == want && !got.incomplete);
  }
  auto partial = eval("IFERROR(XNPV(.1,{UNKNOWN_DATE(),110},{0,365}),77)");
  assert(partial.incomplete && partial.number == 77);
  std::cout
      << "Native irregular cash-flow dates, signed returns, duplicate-date "
         "cancellation, positional pairing and namespaces pass\n";
}
