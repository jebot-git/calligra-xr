// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_regression.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  bool valid = false;
  assert(Text("4.9406564584124654e-324").toDouble(&valid) ==
             std::numeric_limits<double>::denorm_min() &&
         valid);
  Text("1e999").toDouble(&valid);
  assert(!valid);
  Text("1e-999").toDouble(&valid);
  assert(!valid);
  SheetEngine engine;
  engine.addNewSheet("Regression");
  engine.addNewSheet("Other");
  for (int row = 1; row <= 5; ++row) {
    engine.set(0, 1, row, Text::number(row));
    engine.set(0, 2, row, Text::number(2 * row + 3));
  }
  engine.addNamedRange("Regression!A1:A5", "Inputs");
  auto eval = [&](const Text &f) {
    engine.set(0, 20, 20, "=" + f);
    return engine.value(0, 20, 20);
  };
  auto near = [](const NativeValue &v, double n) {
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - n) > 1e-12 * std::max(1e-300, std::abs(n)))
      std::cerr << v.display().toStdString() << " != " << n << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - n) <= 1e-12 * std::max(1e-300, std::abs(n)));
  };
  auto coefficients = eval("LINEST(B1:B5,Inputs,TRUE,TRUE)");
  assert(coefficients.rows == 5 && coefficients.columns == 2);
  near(coefficients.at(0, 0), 2);
  near(coefficients.at(0, 1), 3);
  near(coefficients.at(1, 0), 0);
  near(coefficients.at(2, 0), 1);
  near(coefficients.at(3, 1), 3);
  near(coefficients.at(4, 0), 40);
  near(coefficients.at(4, 1), 0);
  assert(coefficients.at(3, 0).string == "#DIV/0!");
  near(eval("INDEX(LINEST({5,7,9,11,13},,TRUE,FALSE),1,1)"), 2);
  near(eval("INDEX(LINEST({5,7,9,11,13}),1,2)"), 3);
  near(eval("SUM(TREND(B1:B5,Inputs,{6;7}))"), 32);
  near(eval("SUM(TREND(B1:B5,,,TRUE))"), 45);
  near(eval("SUM(TREND({5,9;7,11},,{5,6;7,8}))"), 64);
  near(eval("SUM(INDEX({1,2;3,4},,2))"), 6);
  near(eval("SUM(INDEX({1,2;3,4},2,))"), 7);
  near(eval("INDEX(_xlfn.COM.MICROSOFT.LINEST({3,5,7},{1,2,3}),1,1)"), 2);
  near(eval("INDEX(LINEST({1,2,3},{0,0,0},FALSE,TRUE),4,2)"), 3);
  near(eval("INDEX(LINEST({1,2,3},{1,1,1},TRUE,TRUE),1,2)"), 2);
  near(eval("SUM(TREND({2,4,6},,{-1E307,0,1E307}))"), 0);
  near(eval("SUM(TREND({0,5E-324,1E-323},{0,1E-323,2E-323},{1E308,-1E308}))"),
       0);
  // Coefficients can overflow while an exact rational forecast still fits.
  assert(eval("INDEX(LINEST({0,1,2},{0,5E-324,1E-323}),1,1)").string ==
         "#NUM!");
  near(eval("INDEX(TREND({0,1,2},{0,5E-324,1E-323},{5E-324}),1,1)"), 1);
  near(eval("INDEX(LINEST({\"1\",TRUE,3},{0,1,2}),1,1)"), 1);
  for (auto f : {"LINEST({1,2})", "LINEST({1,2},{1,2})",
                 "LINEST({1,2,3},{1,2;3,4})", "LINEST({1,2,3},{1,2,\"text\"})",
                 "TREND({1;2;3;4},{1,0;0,1;1,1;2,1},{1,2,3})"})
    assert(eval(f).isError());
  assert(eval("IFERROR(SUM(LINEST({1,2,3},,{TRUE,FALSE})),77)").incomplete);
  auto incomplete = eval("IFERROR(SUM(1,,3),77)");
  assert(incomplete.incomplete);
  engine.set(0, 3, 20, "=IFERROR(NATIVE_MISSING(),1)");
  assert(eval("SUM(LINEST(B1:B5,A1:A5,C1:C25))").incomplete);
  engine.set(0, 1, 4, "=IFERROR(NATIVE_MISSING(),4)");
  assert(eval("SUM(LINEST(B1:B5,A1:A5))").incomplete);
  engine.set(0, 1, 4, "4");
  engine.set(0, 4, 1, "=TREND(B1:B5,A1:A5,{6;7})");
  engine.define_matrix(0, 4, 1, 1, 2);
  near(engine.value(0, 4, 2), 17);
  near(eval("SUM(TREND(B1:B5,A1:A5,D1:D2))"), 70);
  // Sparse geometry, including absent input zeros and all-zero predictors.
  NativeRegressionMatrix y;
  y.rows = 1'000'000;
  y.columns = 1;
  y.entries.emplace(999'999, 1);
  NativeRegressionMatrix x;
  x.rows = y.rows;
  x.columns = 1;
  auto sparse = NativeRegressionModel(y, x, true).coefficients(true);
  near(sparse.at(0, 0), 0);
  near(sparse.at(0, 1), 1E-6);
  near(sparse.at(3, 1), 999999);
  std::cout << "PASS exact native LINEST/TREND, geometry, omission, sparse "
               "data and incomplete provenance\n";
}
