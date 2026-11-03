// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_paired.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
using V = NativeValue;
void near(const V &v, double expected, double absolute = 1e-12) {
  if (!v.isNumber() || v.incomplete ||
      std::abs(v.number - expected) > absolute + 1e-12 * std::abs(expected))
    std::cerr << v.display().toStdString() << " != " << expected << '\n';
  assert(v.isNumber() && !v.incomplete &&
         std::abs(v.number - expected) <=
             absolute + 1e-12 * std::abs(expected));
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--benchmark") {
    auto start = std::chrono::steady_clock::now();
    std::vector<std::pair<double, double>> values;
    values.reserve(1'000'000);
    for (int i = 0; i < 1'000'000; ++i)
      values.emplace_back(2. * i + 1, i);
    NativePairedData data(std::move(values));
    near(data.evaluate("SLOPE"), 2);
    near(data.evaluate("INTERCEPT"), 1);
    near(data.evaluate("STEYX"), 0);
    std::cout << "PASS one million paired observations, exact "
                 "slope/intercept/zero residuals, seconds="
              << std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << '\n';
    return 0;
  }
  SheetEngine engine;
  engine.addNewSheet("Pairs");
  engine.addNewSheet("Other");
  for (int r = 1; r <= 5; ++r) {
    engine.set(0, 1, r, Text::number(r));
    engine.set(0, 2, r, Text::number(2 * r + 1));
  }
  engine.addNamedRange("Pairs!A1:A5", "Xs");
  engine.addNamedRange("Pairs!B1:B5", "Ys");
  auto eval = [&](const Text &formula) {
    engine.set(0, 20, 20, "=" + formula);
    return engine.value(0, 20, 20);
  };
  assert(paired_signatures().size() == 11);
  for (auto [formula, expected] : std::vector<std::pair<Text, double>>{
           {"CORREL(A1:A5,B1:B5)", 1},
           {"COVAR(\"1\",TRUE)", 0},
           {"SUMX2PY2(TRUE,2)", 5},
           {"PEARSON(A1:A5,B1:B5)", 1},
           {"RSQ(A1:A5,B1:B5)", 1},
           {"COVAR(A1:A5,B1:B5)", 4},
           {"COVARIANCE.P(A1:A5,B1:B5)", 4},
           {"COVARIANCE.S(A1:A5,B1:B5)", 5},
           {"SLOPE(B1:B5,A1:A5)", 2},
           {"INTERCEPT(B1:B5,A1:A5)", 1},
           {"STEYX(B1:B5,A1:A5)", 0},
           {"FORECAST(10,B1:B5,A1:A5)", 21},
           {"FORECAST.LINEAR(10,Ys,Xs)", 21},
           {"SUMX2MY2({1,2,3},{3,2,1})", 0},
           {"SUMX2PY2({1,2,3},{3,2,1})", 28},
           {"SUMXMY2({1,2,3},{3,2,1})", 8},
           {"SUM2XMY({1,2,3},{3,2,1})", 8},
           {"SLOPE(Ys,OFFSET(A1,0,0,5,1))", 2},
           {"INTERCEPT(INDIRECT(\"B1:B5\"),A1:A5)", 1},
           {"SLOPE(B1:B1048576,A1:A1048576)", 2},
           {"COVAR({5},{2})", 0},
           {"COVARIANCE.S({1,2},{2,4})", 1},
           {"SLOPE({1,\"ignore\",3},{2,100,6})", .5},
           {"SLOPE({1,2,3},{2,TRUE,6})", .5},
           {"CORREL({1,2,3},{3,2,1})", -1},
           {"SLOPE({7,7,7},{1,2,3})", 0},
           {"INTERCEPT({7,7,7},{1,2,3})", 7},
           {"STEYX({7,7,7},{1,2,3})", 0},
           {"SLOPE({1E308,-1E308},{1E-300,-1E-300})", 0},
           {"COM.MICROSOFT.COVARIANCE.S(A1:A5,B1:B5)", 5},
           {"COM.MICROSOFT.FORECAST.LINEAR(10,Ys,Xs)", 21},
           {"SUMX2MY2({1E308,1},{1E308,0})", 1},
           {"INTERCEPT({2000000000000001,2000000000000003,2000000000000007},{"
            "1000000000000000,1000000000000001,1000000000000003})",
            1},
           {"FORECAST(0,{2000000000000001,2000000000000003,2000000000000007},{"
            "1000000000000000,1000000000000001,1000000000000003})",
            1},
           {"STEYX({1,2,3},{3,6,9})", 0},
           {"CORREL({-1E308,0,1E308},{-1E-300,0,1E-300})", 1},
           {"COVAR({-1E308,1E308},{-1E-300,1E-300})", 1E8}}) {
    // An overflowing slope remains an error, while a centered forecast can fit.
    if (formula == "SLOPE({1E308,-1E308},{1E-300,-1E-300})")
      assert(eval(formula).string == "#NUM!");
    else
      near(eval(formula), expected);
  }
  near(eval("FORECAST(0,{1E308,-1E308},{1E-300,-1E-300})"), 0);
  auto tiny = eval("FORECAST(1E-300,{-1E308,1E308},{-1E308,1E308})");
  assert(tiny.isNumber() && std::abs(tiny.number - 1E-300) <= 1E-312);
  for (auto formula :
       Text("CORREL({1,2},{1,1}) RSQ({1,1},{2,3}) SLOPE({1,2},{1,1}) "
            "INTERCEPT({1,2},{1,1}) STEYX({1,2},{1,2}) COVARIANCE.S({1},{2})")
           .split(' '))
    assert(eval(formula).string == "#DIV/0!");
  assert(eval("CORREL({1,2},{1;2})").string == "#N/A");
  assert(eval("SLOPE({1,2},{1,2,3})").string == "#N/A");
  assert(eval("CORREL({NA(),1},{\"ignore\",2})").string == "#N/A");
  assert(eval("CORREL({NATIVE_MISSING(),1},{\"ignore\",2})").incomplete);
  assert(eval("IFERROR(SLOPE({NATIVE_MISSING(),1},{2,3}),77)").incomplete);
  assert(eval("SUMXMY2(\"bad\",1)").string == "#VALUE!");
  assert(eval("FORECAST(A1:A5,B1:B5,A1:A5)").string == "#VALUE!");
  auto forecasts = eval("FORECAST({0,10;3,4},B1:B5,A1:A5)");
  assert(forecasts.isArray() && forecasts.rows == 2 && forecasts.columns == 2);
  near(forecasts.at(0, 0), 1);
  near(forecasts.at(0, 1), 21);
  near(forecasts.at(1, 0), 7);
  engine.set(1, 1, 1, "=FORECAST({0,10},Pairs!B1:B5,Pairs!A1:A5)");
  engine.define_matrix(1, 1, 1, 2, 1);
  near(engine.value(1, 2, 1), 21);
  engine.set(0, 2, 5, "13");
  near(engine.value(1, 2, 1), 24.2);
  engine.set(0, 1, 1, "=T20");
  assert(eval("COVAR(A1:A5,B1:B5)").string == "#CYCLE!");
  std::cout << "PASS native paired data, centered regression, sparse "
               "positions, arrays, aliases, errors and dependencies\n";
}
