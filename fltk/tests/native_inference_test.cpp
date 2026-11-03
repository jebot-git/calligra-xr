// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_inference.h"
#include "native_exact_sample.h"
#include "native_formula.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Tests");
  engine.addNewSheet("Other");
  for (int r = 1; r <= 5; ++r) {
    engine.set(0, 1, r, Text::number(2 * r));
    engine.set(0, 2, r, Text::number(4 * r));
  }
  engine.addNamedRange("Tests!A1:A5", "Samples");
  auto eval = [&](const Text &f) {
    engine.set(0, 20, 20, f.startsWith('=') ? f : "=" + f);
    return engine.value(0, 20, 20);
  };
  auto near = [&](const Text &f, double expected) {
    auto v = eval(f);
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - expected) >
            1e-12 * std::max(1e-300, std::abs(expected)))
      std::cerr << f.toStdString() << ": " << v.display().toStdString()
                << " != " << expected << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - expected) <=
               1e-12 * std::max(1e-300, std::abs(expected)));
  };
  for (auto [f, want] : std::vector<std::pair<Text, double>>{
           {"PROB({1,2,3},{.2,.3,.5},2)", .3},
           {"PROB({1,2,3},{.2,.3,.5},2,3)", .8},
           {"PROB({1,2,3},{.2,.3,.5},3,2)", 0},
           {"PROB({\"ignore\",1,2},{.25,.75},2)", .75},
           {"PROB({1,2},{1,1E-300},2)", 1E-300},
           {"PROB({1,2},{0,1},1)", 0},
           {"FTEST({1,2,3},{2,4,6})", .4},
           {"F.TEST({2,4,6},{1,2,3})", .4},
           {"TTEST({1,2,3},{2,4,6},2,1)", .07417990022744854},
           {"T.TEST({1,2,3},{2,4,6},2,2)", .19626117814926969},
           {"TTEST({1,2,3},{2,4,6},2,3)", .22088084049409593},
           {"TTEST({2,4,6},{1,2,3},1,3)", .11044042024704796},
           {"ZTEST({1,2,3},2)", .5},
           {"Z.TEST({1,2,3},0,1)", .00026600275256962485},
           {"Z.TEST({5},5,1)", .5},
           {"CHITEST({4,6},{5,5})", .5270892568655381},
           {"CHISQ.TEST({5,5},{5,5})", 1},
           {"CHITEST({4,6;5,5},{5,5;5,5})", .5270892568655381},
           {R"(CHITEST({"skip","skip"},{"skip","skip"}))", 1},
           {"LEGACY.CHITEST({4,6},{5,5})", .5270892568655381},
           {"_xlfn.COM.MICROSOFT.F.TEST({1,2,3},{2,4,6})", .4},
           {"ZTEST(OFFSET(A1,0,0,5,1),6)", .5},
           {"ZTEST(Samples,6)", .5},
           {"ZTEST(A1:A1048576,6)", .5},
           {"ZTEST({1E308,1E308},1E308,1)", .5},
           {"ZTEST({-1E308,1E308},0)", .5},
           {"FTEST({-1E308,1E308},{-1E308,1E308})", 1},
           {"FTEST({1E15,1E15+1,1E15+2},{1,2,3})", 1}})
    near(f, want);
  // Expected array counts map back to actual original bin indices, including
  // stable duplicate handling; no hard-coded reversal or four-bin exception.
  for (auto [f, want] : std::vector<std::pair<Text, std::vector<double>>>{
           {"FREQUENCY({1,2,3,4,5,6,7,8},{5,2,7})", {3, 2, 2, 1}},
           {"FREQUENCY({1,2,3,4,5,6,7,8},{5,2,5})", {3, 2, 0, 3}},
           {"FREQUENCY(A1:A1048576,{3;7})", {1, 2, 2}},
           {"FREQUENCY({1,2,3},{\"none\"})", {3}},
           {"FREQUENCY({\"none\"},{1,2})", {0, 0, 0}}}) {
    auto v = eval(f);
    assert(v.isArray() && v.columns == 1 && v.rows == want.size());
    for (size_t i = 0; i < want.size(); ++i)
      assert(v.at(i, 0).number == want[i]);
  }
  for (auto f : Text("PROB({1,2},{.2,.7},1) PROB({1,2},{.5,.4999999},1) "
                     "PROB({1,2},{-1,2},1) TTEST({1,2},{2,3},0,1) "
                     "TTEST({1,2},{2,3},2,4) ZTEST({1,2},0,0)")
                    .split(' '))
    assert(eval(f).string == "#NUM!");
  for (auto f :
       Text("FTEST({1},{2,3}) FTEST({1,1},{2,3}) TTEST({1,2},{2,3},2,1) "
            "ZTEST({1},1) ZTEST({1,1},0) CHITEST({1,2},{0,2})")
           .split(' '))
    assert(eval(f).string == "#DIV/0!");
  assert(eval("TTEST({1,2},{1,2,3},2,1)").string == "#N/A");
  assert(eval("CHITEST({1,2},{1;2})").string == "#N/A");
  assert(eval("CHITEST({1,2},{-1,2})").string == "#NUM!");
  assert(eval("PROB({1,2},{1},1)").string == "#N/A");
  for (auto f : Text("FTEST({NA(),1},{1,2}) PROB({1,NA()},{.5,.5},1) "
                     "FREQUENCY({1,NA()},{2}) CHITEST({NA(),2},{\"skip\",2})")
                    .split(' '))
    assert(eval(f).string == "#N/A");
  assert(eval("IFERROR(ZTEST({1,NATIVE_MISSING()},0),77)").incomplete);
  assert(eval("IFERROR(TTEST({1,NATIVE_MISSING()},{2,\"skip\"},2,1),77)")
             .incomplete);
  auto p = eval("PROB({1,2,3},{.2,.3,.5},{1,2;3,4})");
  assert(p.isArray() && p.rows == 2 && p.columns == 2 &&
         p.at(1, 0).number == .5 && p.at(1, 1).number == 0);
  auto z = eval("ZTEST({1,2,3},{1,2;3,4},1)");
  assert(z.isArray() && z.at(0, 1).number == .5);
  engine.set(1, 1, 1, "=FREQUENCY(Tests!A1:A5,{3;7})");
  engine.define_matrix(1, 1, 1, 1, 4);
  assert(engine.value(1, 1, 1).number == 1 &&
         engine.value(1, 1, 4).string == "#N/A");
  engine.set(0, 1, 1, "8");
  assert(engine.value(1, 1, 1).number == 0 &&
         engine.value(1, 1, 3).number == 3);
  assert(eval("ZTEST(A1:A5,A1:A5)").string == "#VALUE!");
  assert(eval("PROB({2,4,6,8,10},{.1,.2,.3,.2,.2},A1:A5)").string == "#VALUE!");
  engine.set(0, 20, 3, "=ZTEST(A1:A5,A1:A5)");
  // A1 was edited to8 above, so the sample mean is7.2 and the intersected
  // reference uses A3=6. Verify against the ordinary scalar-mean call.
  auto intersected = engine.value(0, 20, 3);
  auto scalar = eval("ZTEST(A1:A5,6)");
  assert(intersected.isNumber() && intersected.number == scalar.number);
  engine.set(1, 5, 8, "=PROB({2,4,6,8,10},{.1,.2,.3,.2,.2},Tests!A1:A5)");
  engine.define_matrix(1, 5, 8, 1, 5);
  assert(engine.value(1, 5, 8).number == .2 &&
         engine.value(1, 5, 10).number == .3);
  using NativeExact::Sample;
  Sample s(std::vector<double>{1E308, 1E308, 1E308});
  assert(s.difference(1E308) == 0 && s.variance() == 0);
  Sample diff(
      std::vector<std::pair<double, double>>{{1E308, -1E308}, {1E308, 1E308}});
  assert(diff.variance() > 0);
  assert(Formula::from_excel("TTEST(A1:A3,B1:B5,2,3)+ZTEST(A1,1,2)") ==
         "=T.TEST(A1:A3,B1:B5,2,3)+Z.TEST(A1,1,2)");
  // One-ULP perturbation among a million large observations: subtracting
  // rounded means would lose the entire signal even in long double.
  std::vector<double> big_x(1'000'000, 1E308), big_y = big_x;
  big_x[0] = std::nextafter(1E308, std::numeric_limits<double>::infinity());
  NativeHypothesisData big(std::move(big_x), std::move(big_y));
  auto result = big.evaluate(
      "T.TEST", {NativeValue::numeric(2), NativeValue::numeric(3)});
  assert(result.isNumber() && !result.incomplete &&
         std::abs(result.number - 0.3173107498338201) < 1e-13);
  std::cout
      << "PASS native probability/frequency, hypothesis tails, exact sample "
         "cancellation, Welch df, sparse references, arrays and errors\n";
}
