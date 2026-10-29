// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_financial.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Completion");
  auto eval = [&](const Text &formula) {
    engine.set(0, 20, 1, "=" + formula);
    return engine.value(0, 20, 1);
  };
  auto near = [&](const Text &formula, double want) {
    auto got = eval(formula);
    if (!got.isNumber() ||
        std::abs(got.number - want) > 1e-10 * std::max(1., std::abs(want)))
      std::cerr << formula.toStdString() << ": " << got.display().toStdString()
                << " != " << want << '\n';
    assert(got.isNumber() && !got.incomplete);
    assert(std::abs(got.number - want) <= 1e-10 * std::max(1., std::abs(want)));
  };
  near("ACCRINT(\"2004-02-01\",\"2004-04-01\",\"2004-05-01\",.1,1000,4,0)",
       100 * 89. / 360);
  near("ACCRINT(0,1,365,.1,100,2,3)", 10);
  near("ACCRINT(0,1,365,.1,0,2,3)", 0);
  near("PRICEMAT(0,365,-365,.1,.1,3)", 120 / 1.1 - 10);
  near("YIELDMAT(0,365,-365,.1,PRICEMAT(0,365,-365,.1,.1,3),3)", .1);
  near("ODDLPRICE(0,365,-365,.1,.1,100,2,3)", 120 / 1.1 - 10);
  near("ODDLYIELD(0,365,-365,.1,ODDLPRICE(0,365,-365,.1,.1,100,2,3),100,2,3)",
       .1);
  near("TBILLEQ(\"1996-01-01\",\"1996-02-01\",.05)", .0509136560);
  near("TBILLPRICE(\"1996-01-01\",\"1996-02-01\",.05)", 99.5694444444);
  near("TBILLYIELD(\"1996-01-01\",\"1996-02-01\",99.57)", .0501511337);
  near("AMORLINC(1000,0,365,100,0,.1,3)", 100);
  near("AMORLINC(1000,0,365,100,1,.1,3)", 100);
  near("AMORLINC(1000,0,365,100,100,.1,3)", 0);
  near("AMORLINC(1000,0,365,100,1,0,3)", 0);
  near("AMORDEGRC(1000,0,365,0,0,.1,3)", 250);
  near("AMORDEGRC(1000,0,365,0,1,.1,3)", 188);
  near("AMORDEGRC(2000,DATE(2004,2,1),DATE(2004,12,31),200,1,.15,1)", 494);
  near("AMORDEGRC(1000,0,365,0,0,.2,3)", 400);
  near("AMORDEGRC(1000,0,365,0,0,1/6,3)", 333);
  near("AMORDEGRC(100,0,365,0,1e300,.5,3)", 0);
  near("EURO(\"ats\")", 13.7603);
  near("EURO(\"LUX\")", 40.3399);
  near("EUROCONVERT(100,\"EUR\",\"DEM\")", 195.58);
  near("EUROCONVERT(-100,\"EUR\",\"DEM\")", -195.58);
  near("EUROCONVERT(100,\"ATS\",\"EUR\",FALSE(),3)", 7.27);
  near("EUROCONVERT(12.345,\"EUR\",\"EUR\")", 12.345);
  near("EUROCONVERT(100,\"ATS\",\"EUR\",TRUE(),1e100)", 100 / 13.7603);
  near("ORG.OPENOFFICE.CONVERT(100,\"ATS\",\"EUR\")", 100 / 13.7603);
  near("SUM(EURO({\"EUR\",\"DEM\"}))", 2.95583);
  near("SUM(PRICEMAT(0,365,-365,{.1,.1},.1,3))", 2 * (120 / 1.1 - 10));
  for (auto [formula, want] : std::vector<std::pair<Text, Text>>{
           {"AMORLINC({100,200},0,365,0,1,.1)", "#VALUE!"},
           {"AMORDEGRC(1000,0,365,100,1,0)", "#VALUE!"},
           {"AMORDEGRC(1e20,0,365,0,1e8,1e-6,3)", "#LIMIT!"},
           {"ACCRINT(0,1,365,.1,100,5)", "#VALUE!"},
           {"ACCRINT(0,NA(),365,.1,100,2)", "#N/A"},
           {"ODDLPRICE(0,365,0,.1,.1,100,2)", "#VALUE!"},
           {"PRICEMAT(365,0,-365,.1,.1)", "#VALUE!"},
           {"YIELDMAT(0,365,-365,0,100)", "#VALUE!"},
           {"TBILLPRICE(0,366,.05)", "#VALUE!"},
           {"EURO(\"USD\")", "#NUM!"},
           {"EUROCONVERT(100,\"ATS\",\"EUR\",FALSE(),2)", "#VALUE!"},
           {"ORG.OPENOFFICE.CONVERT(100,\"eur\",\"DEM\")", "#VALUE!"},
           {"EURO(NA())", "#N/A"}}) {
    auto got = eval(formula);
    assert(got.isError() && got.string == want);
    assert(got.incomplete == (want == "#LIMIT!"));
  }
  auto unsupported = eval("IFERROR(ODDFPRICE(0,365,-365,180,.1,.1,100,2),77)");
  assert(unsupported.incomplete && unsupported.number == 77);
  engine.set(0, 1, 1, "100");
  engine.set(0, 2, 1, "=EUROCONVERT(A1,\"EUR\",\"DEM\")");
  near("B1", 195.58);
  engine.set(0, 1, 1, "200");
  near("B1", 391.17);
  std::cout << "Native accrued interest, odd/maturity bonds, treasury bills, "
               "French depreciation and historical euro conversions pass\n";
}
