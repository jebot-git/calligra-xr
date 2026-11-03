// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_regression.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine e;
  e.addNewSheet("Logarithmic");
  auto eval = [&](const Text &f) {
    e.set(0, 20, 20, "=" + f);
    return e.value(0, 20, 20);
  };
  auto near = [](const NativeValue &v, double n) {
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - n) > 2e-12 * std::max(1e-300, std::abs(n)))
      std::cerr << v.display().toStdString() << " != " << n << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - n) <= 2e-12 * std::max(1e-300, std::abs(n)));
  };
  auto fit = eval("LOGEST({3,9,27,81},,TRUE,TRUE)");
  assert(fit.rows == 5 && fit.columns == 2 && !fit.incomplete);
  near(fit.at(0, 0), 3);
  near(fit.at(0, 1), 1);
  near(fit.at(1, 0), 0);
  near(fit.at(1, 1), 0);
  near(fit.at(2, 0), 1);
  near(fit.at(2, 1), 0);
  assert(fit.at(3, 0).string == "#DIV/0!");
  near(fit.at(3, 1), 2);
  near(fit.at(4, 0), 5 * std::log(3) * std::log(3));
  near(fit.at(4, 1), 0);
  near(eval("SUM(GROWTH({3,9,27,81},,{5,6}))"), 972);
  near(
      eval("INDEX(LOGEST({10;15;30;60;90;360},{1,0;0,1;1,1;2,1;1,2;3,2}),1,1)"),
      3);
  near(
      eval("INDEX(LOGEST({10;15;30;60;90;360},{1,0;0,1;1,1;2,1;1,2;3,2}),1,2)"),
      2);
  near(
      eval("INDEX(LOGEST({10;15;30;60;90;360},{1,0;0,1;1,1;2,1;1,2;3,2}),1,3)"),
      5);
  near(eval("SUM(GROWTH({10;15;30;60;90;360},{1,0;0,1;1,1;2,1;1,2;3,2},{4,2;3,"
            "3}))"),
       1800);
  auto singular =
      eval("LOGEST({21;63;189;567;1701},{1,2;2,4;3,6;4,8;5,10},TRUE,TRUE)");
  near(singular.at(0, 0), 1);
  near(singular.at(0, 1), 3);
  near(singular.at(0, 2), 7);
  near(singular.at(1, 0), 0);
  near(singular.at(4, 1), 0);
  near(singular.at(3, 1), 3);
  auto nc = eval("LOGEST({3,9,27,81},,FALSE,TRUE)");
  near(nc.at(0, 0), 3);
  near(nc.at(0, 1), 1);
  near(nc.at(1, 1), 0);
  near(nc.at(4, 1), 0);
  auto constant = eval("LOGEST({7,7,7},,TRUE,TRUE)");
  near(constant.at(0, 0), 1);
  near(constant.at(0, 1), 7);
  assert(constant.at(2, 0).string == "#DIV/0!");
  near(constant.at(4, 1), 0);
  near(eval("SUM(GROWTH({1,1,1}))"), 3);
  near(eval("INDEX(COM.MICROSOFT.LOGEST({1,4,16}),1,1)"), 4);
  near(eval("INDEX(_xlfn.GROWTH({1,4,16},,{4}),1,1)"), 64);
  // Displayed growth factors overflow, while cancellation precedes exp in a
  // forecast at a tiny original input, so the predicted value remains finite.
  assert(eval("INDEX(LOGEST({1,2,4},{0,5E-324,1E-323}),1,1)").string ==
         "#NUM!");
  near(eval("SUM(GROWTH({1,2,4},{0,5E-324,1E-323},{5E-324}))"), 2);
  near(eval("SUM(GROWTH({1,2,4},{0,1,2},{-1074}))"),
       std::numeric_limits<double>::denorm_min());
  // Two individually unrepresentable growth factors cancel before evaluation.
  Text tinyX = "{2^(-1020),0;2^(-1020),2^(-1020);2*2^(-1020),2^(-1020);3*2^(-"
               "1020),2^(-1020);2^(-1020),2*2^(-1020)}";
  near(eval("SUM(GROWTH({2;1;2;4;.5}," + tinyX + ",{1E308,1E308}))"), 1);
  auto tinyFit = eval("LOGEST({2;1;2;4;.5}," + tinyX + ",TRUE,TRUE)");
  near(tinyFit.at(0, 0), 0);
  assert(tinyFit.at(0, 1).string == "#NUM!");
  near(tinyFit.at(0, 2), 1);
  near(tinyFit.at(4, 1), 0);
  // This composite passes the first seven prime Miller-Rabin bases. Complete
  // factorization must expose ln(p*q)=ln(p)+ln(q) for a zero-residual fit.
  auto pseudoprime =
      eval("LOGEST({10670053;32010157;341550071728321;1;113850031022809;"
           "1024650151164649},{1,0;0,1;1,1;0,0;2,0;0,2},TRUE,TRUE)");
  near(pseudoprime.at(0, 0), 32010157);
  near(pseudoprime.at(0, 1), 10670053);
  near(pseudoprime.at(4, 1), 0);
  for (auto f : {"LOGEST({1,0,2})", "LOGEST({1,-1,2})", "LOGEST({1,2})",
                 "GROWTH({1,2,4},{1,2,\"bad\"})"})
    assert(eval(f).isError() && !eval(f).incomplete);
  assert(eval("IFERROR(SUM(LOGEST({1,2,4},,{TRUE,FALSE})),77)").incomplete);
  e.set(0, 1, 1, "1");
  e.set(0, 1, 2, "=IFERROR(NATIVE_MISSING(),2)");
  e.set(0, 1, 3, "4");
  assert(eval("SUM(LOGEST(A1:A3))").incomplete);
  e.set(0, 1, 2, "2");
  e.set(0, 2, 1, "=GROWTH(A1:A3,,{4;5})");
  e.define_matrix(0, 2, 1, 1, 2);
  near(e.value(0, 2, 2), 16);
  std::cout << "PASS native matrix LOGEST/GROWTH, exact geometric residuals, "
               "degeneracy and provenance\n";
}
