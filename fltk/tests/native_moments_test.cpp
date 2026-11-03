// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_sheets.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
using V = NativeValue;
void near(const V &got, double expected, double tolerance = 1e-12) {
  if (!got.isNumber() || got.incomplete ||
      std::abs(got.number - expected) >
          tolerance * std::max(1., std::abs(expected)))
    std::cerr << got.display().toStdString() << " != " << expected << '\n';
  assert(got.isNumber() && !got.incomplete);
  assert(std::abs(got.number - expected) <=
         tolerance * std::max(1., std::abs(expected)));
}
} // namespace
int main() {
  SheetEngine engine;
  engine.addNewSheet("Moments");
  engine.addNewSheet("Other");
  for (int i = 1; i <= 5; ++i)
    engine.set(0, 1, i, Text::number(i));
  engine.addNamedRange("Moments!A1:A5", "Samples");
  auto eval = [&](const Text &formula) {
    engine.set(0, 20, 20, "=" + formula);
    return engine.value(0, 20, 20);
  };
  for (auto [formula, expected] : std::vector<std::pair<Text, double>>{
           {"SKEW(1,2,4)", .9352195295828245},
           {"SKEWP(1,2,4)", .3818017741606063},
           {"SKEW.P({1,2,4})", .3818017741606063},
           {"COM.MICROSOFT.SKEW.P({1,2,4})", .3818017741606063},
           {"KURT(1,2,3,4)", -1.2},
           {"KURTP(1,2,3,4)", -1.36},
           {"KURTP(21,33,54,23)", -1.0207681267737483},
           {"KURT(Samples)", -1.2},
           {"SKEW(Samples)", 0},
           {"KURT(OFFSET(A1,0,0,5,1))", -1.2},
           {"KURTP(INDIRECT(\"A1:A5\"))", -1.3},
           {"SKEWP(A1:A1048576)", 0},
           {"KURT({1,2,3,4,TRUE,\"99\"})", -1.2},
           {"KURT(\"1\",2,3,4)", -1.2},
           {"KURT(TRUE,2,3,4)", -1.2},
           {"KURT({-1E308,-5E307,5E307,1E308})", -3.3},
           {"KURT({-1E-300,-5E-301,5E-301,1E-300})", -3.3},
           {"SKEWP({1E308,-1E308,0})", 0},
           {"KURT({1E15,1E15+1,1E15+2,1E15+3})", -1.2},
           {"TRIMMEAN({1,2,3,4,5,10,20,30,40,99},.6)", 9.75},
           {"TRIMMEAN({1,2,3,4,100},.8)", 3},
           {"TRIMMEAN({1,2,3,4,100},0)", 22},
           {"TRIMMEAN({1E308,1E308},0)", 1E308},
           {"TRIMMEAN({1E308,1,-1E308},0)", 1. / 3},
           {"TRIMMEAN({1E-300,2E-300},0)", 1.5E-300},
           {"STANDARDIZE(1,2.5,.1)", -15},
           {"STANDARDIZE(-1,-2,2)", .5},
           {"STANDARDIZE(1E308,-1E308,1E308)", 2},
           {"_xlfn.STANDARDIZE(4,2,1)", 2},
           {"COM.MICROSOFT.STDEV.S(A1:A5)", std::sqrt(2.5)},
           {"COM.MICROSOFT.STDEV.P(A1:A5)", std::sqrt(2.)},
           {"COM.MICROSOFT.VAR.S(A1:A5)", 2.5},
           {"COM.MICROSOFT.VAR.P(A1:A5)", 2},
           {"VARIANCE(A1:A5)", 2.5}})
    near(eval(formula), expected);
  for (auto formula :
       Text("SKEW(1,2) SKEWP(1,2) KURT(1,2,3) KURTP(1,2,3) SKEW(1,1,1) "
            "SKEWP(1,1,1) KURT(1,1,1,1) KURTP(1,1,1,1)")
           .split(' ')) {
    auto got = eval(formula);
    assert(got.isError() && got.string == "#DIV/0!" && !got.incomplete);
  }
  for (auto formula : Text("TRIMMEAN(A1:A5,-.1) TRIMMEAN(A1:A5,1) "
                           "STANDARDIZE(1,2,0) STANDARDIZE(1,2,-1)")
                          .split(' '))
    assert(eval(formula).string == "#NUM!");
  for (auto formula : Text("KURT({1,2,3,NA()}) SKEW({1,2,NA()}) "
                           "TRIMMEAN({1,NA()},0) STANDARDIZE(NA(),1,2)")
                          .split(' '))
    assert(eval(formula).string == "#N/A");
  for (auto formula :
       Text("SKEW(NATIVE_MISSING()) KURT(NATIVE_MISSING()) "
            "TRIMMEAN(NATIVE_MISSING(),0) STANDARDIZE(NATIVE_MISSING(),1,2) "
            "IFERROR(SKEW(NATIVE_MISSING()),77)")
           .split(' '))
    assert(eval(formula).incomplete);
  auto a = eval("STANDARDIZE({1,3;5,7},{0,1;2,3},2)");
  assert(a.isArray() && a.rows == 2 && a.columns == 2);
  near(a.at(0, 0), .5);
  near(a.at(1, 1), 2);
  auto mixed = eval("STANDARDIZE({1,2},0,{1,0})");
  near(mixed.at(0, 0), 1);
  assert(mixed.at(0, 1).string == "#NUM!");
  assert(eval("STANDARDIZE({1,2},0,{1;2})").string == "#VALUE!");
  auto trim = eval("TRIMMEAN({1,2,3,4,100},{0,.8})");
  near(trim.at(0, 0), 22);
  near(trim.at(0, 1), 3);
  engine.set(1, 1, 1, "=TRIMMEAN(Moments!A1:A5,{0,.8})");
  engine.define_matrix(1, 1, 1, 2, 1);
  near(engine.value(1, 1, 1), 3);
  near(engine.value(1, 2, 1), 3);
  engine.set(0, 1, 5, "100");
  near(engine.value(1, 1, 1), 22);
  near(engine.value(1, 2, 1), 3);
  engine.set(0, 1, 1, "=T20");
  assert(eval("SKEW(A1:A5)").string == "#CYCLE!");
  std::cout << "PASS higher moments, scaled extremes, population/sample "
               "definitions, trimmed means, normalization, aliases, matrices, "
               "errors and dependencies\n";
}
