// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_bond.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Bonds");
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
  near("PRICE(\"2024-01-01\",\"2025-01-01\",.1,.1,100,1)", 100);
  near("PRICE(\"2024-01-01\",\"2025-01-01\",.1,0,100,1)", 110);
  near("PRICE(\"2024-01-01\",\"2025-01-01\",0,0,100,1)", 100);
  near("YIELD(\"2024-01-01\",\"2025-01-01\",.1,100,100,1)", .1);
  near("YIELD(\"2024-01-01\",\"2025-01-01\",.1,110,100,1)", 0);
  near("YIELD(\"2024-01-01\",\"2025-01-01\",.1,120,100,1)", -1. / 12);
  near("DURATION(\"2024-01-01\",\"2025-01-01\",0,1e300,1)", 1);
  near("MDURATION(\"2024-01-01\",\"2025-01-01\",0,.1,1)", 1 / 1.1);
  near("DURATION_ADD(\"1998-01-01\",\"2006-01-01\",.08,.09,2,1)", 5.9943834131);
  near("MDURATION(\"2004-02-01\",\"2004-05-31\",.08,.09,2,0)", .3189792663);
  near("DURATION(.1,1000,2000)", 7.2725408973);
  near("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETDURATION(.1,1000,2000)",
       7.2725408973);
  for (int frequency : {1, 2, 4}) {
    Text args = "\"2024-01-01\",\"2034-01-01\",.07,";
    Text freq = Text::number(frequency);
    near("YIELD(" + args + "PRICE(" + args + ".09,100," + freq + "),100," +
             freq + ")",
         .09);
    long double pv = 0, weighted = 0;
    for (int n = 1; n <= 10 * frequency; ++n) {
      auto value = (7.L / frequency + (n == 10 * frequency ? 100 : 0)) /
                   std::pow(1 + .09L / frequency, n);
      pv += value;
      weighted += value * n / frequency;
    }
    near("PRICE(" + args + ".09,100," + freq + ")", double(pv));
    near("DURATION(" + args + ".09," + freq + ")", double(weighted / pv));
    near("MDURATION(" + args + ".09," + freq + ")",
         double(weighted / pv / (1 + .09L / frequency)));
  }
  near("SUM(PRICE(\"2024-01-01\",\"2025-01-01\",{0,.1},0,100,1))", 210);
  near("_xlfn.YIELD(\"2024-01-01\",\"2025-01-01\",.1,100,100,1)", .1);
  for (auto [f, want] : std::vector<std::pair<Text, Text>>{
           {"DURATION(0,365,.1,.1)", "#VALUE!"},
           {"PRICE(365,0,.1,.1,100,2)", "#VALUE!"},
           {"PRICE(0,365,.1,-.1,100,2)", "#VALUE!"},
           {"PRICE(0,365,.1,.1,100,5)", "#VALUE!"},
           {"PRICE(0,365,.1,.1,100,2,5)", "#VALUE!"},
           {"PRICE(0,365,.1,.1,0,2)", "#VALUE!"},
           {"YIELD(0,365,.1,0,100,2)", "#VALUE!"},
           {"YIELD(0,365,.1,100,100,12)", "#VALUE!"},
           {"YIELD(\"2024-01-30\",\"2024-01-31\",.1,100,100,1)", "#NUM!"},
           {"MDURATION(0,365,-.1,.1,2)", "#VALUE!"},
           {"MDURATION(0,365,.1,-.1,2)", "#VALUE!"},
           {"PRICE(0,365,NA(),.1,100,2)", "#N/A"}}) {
    auto got = eval(f);
    assert(got.isError() && got.string == want && !got.incomplete);
  }
  auto partial = eval("IFERROR(PRICE(0,365,UNKNOWN_RATE(),.1,100,2),77)");
  assert(partial.incomplete && partial.number == 77);
  engine.set(0, 1, 1, ".1");
  engine.set(0, 2, 1, "=PRICE(DATE(2024,1,1),DATE(2025,1,1),.1,A1,100,1)");
  near("B1", 100);
  engine.set(0, 1, 1, "0");
  near("B1", 110);
  std::cout << "Native bond valuation, inverse yield, duration, legacy "
               "overloads, arrays and dependent edits pass\n";
}
