// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_distribution.h"
#include "native_formula.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Probabilities");
  engine.addNewSheet("Other");
  engine.set(0, 1, 1, "2");
  auto eval = [&](const Text &f) {
    engine.set(0, 20, 20, f.startsWith('=') ? f : "=" + f);
    return engine.value(0, 20, 20);
  };
  auto near = [&](const Text &f, double expected) {
    auto v = eval(f);
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - expected) >
            2e-13 * std::max(1e-300, std::abs(expected)))
      std::cerr << f.toStdString() << ": " << v.display().toStdString()
                << " != " << expected << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - expected) <=
               2e-13 * std::max(1e-300, std::abs(expected)));
  };
  for (auto [f, expected] : std::vector<std::pair<Text, double>>{
           {"BETADIST(.5,2,3)", .6875},
           {"BETADIST(4,2,3,2,6,FALSE)", .375},
           {"BETA.DIST(4,2,3,FALSE,2,6)", .375},
           {"BETA.INV(0,2,3,2,6)", 2},
           {"BETAINV(1,2,3,2,6)", 6},
           {"BETADIST(-1,2,3)", 0},
           {"BETADIST(2,2,3)", 1},
           {"BINOMDIST(2,4,.5,FALSE)", .375},
           {"BINOM.DIST(2,4,.5,TRUE)", .6875},
           {"BINO(4,2,.5)", .375},
           {"B(4,.5,1,3)", .875},
           {"B(2E12,.5,1E12,1E12+1)", 1.1283791670948073E-6},
           {"BINOM.DIST.RANGE(4,.5,2)", .375},
           {"CRITBINOM(4,.5,.6875)", 2},
           {"CRITBINOM(4,.5,.6875000000001)", 3},
           {"BINOM.INV(4,.5,0)", 0},
           {"CRITBINOM(4,1,0)", 0},
           {"CRITBINOM(4,0,1)", 0},
           {"CRITBINOM(4,.5,1)", 4},
           {"BINOMDIST(2,4,0,TRUE)", 1},
           {"B(4,1,1,4)", 1},
           {"BINOMDIST(0,0,.5,FALSE)", 1},
           {"CHIDIST(1000,2)", 7.124576406741286e-218},
           {"CHISQDIST(2,2)", 1 - std::exp(-1.)},
           {"CHISQ.DIST(0,2,FALSE)", .5},
           {"CHISQINV(0,2)", 0},
           {"CHISQ.INV.RT(1,2)", 0},
           {"LEGACY.CHIDIST(-3,2)", 1},
           {"CONFIDENCE(.05,2,100)", .3919927969080108},
           {"CONFIDENCE(.05,2,100.5)",
            .3919927969080108 * std::sqrt(100 / 100.5)},
           {"CONFIDENCE.NORM(.05,2,100.5)", .3919927969080108},
           {"CONFIDENCE.T(.05,2,100)", .3968433903172831},
           {"EXPONDIST(1E-300,1)", 1E-300},
           {"EXPON.DIST(0,3,FALSE)", 3},
           {"EXPONDIST(-1,1)", 0},
           {"FDIST(3,2,2)", .75},
           {"F.DIST.RT(3,2,2)", .25},
           {"F.DIST(3,2,2,FALSE)", .0625},
           {"FINV(.75,2,2)", 3},
           {"LEGACY.FINV(.25,2,2)", 3},
           {"F.INV(0,2,2)", 0},
           {"F.INV.RT(1,2,2)", 0},
           {"FISHER(.5)", .5493061443340548},
           {"FISHERINV(1000)", 1},
           {"FISHERINV(-1000)", -1},
           {"GAMMADIST(3,2,3)", 1 - 2 / std::exp(1.)},
           {"GAMMA.DIST(3,2,3,FALSE)", 1 / (3 * std::exp(1.))},
           {"GAMMADIST(0,1,.1,FALSE)", 10},
           {"CHISQDIST(0,2,FALSE)", 0},
           {"GAMMAINV(0,2,3)", 0},
           {"GAMMA(5)", 24},
           {"GAMMA(-.5)", -2 * std::sqrt(std::acos(-1.))},
           {"GAMMALN.PRECISE(1)", 0},
           {"GAUSS(1E-300)", 3.989422804014327e-301},
           {"GAUSS(0)", 0},
           {"HYPGEOMDIST(4,12,7,20)", .35758513931888547},
           {"HYPGEOM.DIST(4,12,7,20,TRUE)", .6083591331269349},
           {"HYPGEOMDIST(0,10,10,10)", 0},
           {"HYPGEOMDIST(10,10,10,10)", 1},
           {"HYPGEOMDIST(0,0,0,0)", 1},
           {"LOGINV(.5)", 1},
           {"LOGNORM.INV(.5,2,3)", std::exp(2.)},
           {"LOGNORMDIST(1)", .5},
           {"LOGNORM.DIST(1,0,1,FALSE)", .3989422804014327},
           {"NEGBINOMDIST(2,3,.5)", .1875},
           {"NEGBINOM.DIST(2,3,.5,TRUE)", .5},
           {"NEGBINOM.DIST(0,1,1,FALSE)", 1},
           {"NEGBINOM.DIST(2,3,1,TRUE)", 1},
           {"NORMDIST(0,0,1)", .5},
           {"NORM.DIST(2,2,3,FALSE)", .1329807601338109},
           {"NORMINV(.5,5,2)", 5},
           {"NORM.S.INV(1E-300)", -37.0470962993612},
           {"LEGACY.NORMSDIST(0)", .5},
           {"NORM.S.DIST(0,FALSE)", .3989422804014327},
           {"PHI(0)", .3989422804014327},
           {"POISSON(0,2)", std::exp(-2.)},
           {"POISSON.DIST(1,2,FALSE)", 2 * std::exp(-2.)},
           {"TDIST(1,1,2)", .5},
           {"T.DIST(-1,1,TRUE)", .25},
           {"T.DIST(-1,1,FALSE)", 1 / (2 * std::acos(-1.))},
           {"T.DIST.RT(-1,1)", .75},
           {"T.DIST.2T(1,1)", .5},
           {"TINV(.5,1)", 1},
           {"T.INV(.25,1)", -1},
           {"T.INV.2T(1,3)", 0},
           {"WEIBULL(0,1,2,FALSE)", .5},
           {"WEIBULL.DIST(1,2,3,TRUE)", -std::expm1(-1. / 9)},
           {"WEIBULL(1E308,1E308,1,FALSE)", 0},
           {"_XLFN.COM.MICROSOFT.BETA.DIST(.5,2,3,TRUE)", .6875},
           {"NORMSDIST(\"0\")", .5},
           {"NORMSDIST(FALSE)", .5}})
    near(f, expected);
  for (auto f :
       Text("BETA.DIST(2,2,3,TRUE) BETADIST(.5,0,3) NORMSINV(0) NORMINV(1,0,1) "
            "NORMDIST(0,0,0) EXPON.DIST(-1,1,TRUE) CHISQ.DIST(0,1,FALSE) "
            "F.DIST(0,1,2,FALSE) T.DIST.2T(-1,2) TDIST(1,2,3) T.INV(1,2) "
            "TINV(0,2) GAMMAINV(1,2,3) GAMMA(0) GAMMALN(-1) "
            "GAMMADIST(0,.8,1,FALSE) LOGNORMDIST(-1,0,1,FALSE) "
            "NEGBINOMDIST(0,1,.5) CHISQ.DIST.RT(-1,2) FISHER(1) "
            "POISSON(0,0) BINOMDIST(3,2,.5,TRUE) WEIBULL(0,.5,1,FALSE)")
           .split(' '))
    assert(eval(f).string == "#NUM!");
  assert(eval("CONFIDENCE.T(.05,2,1)").string == "#DIV/0!");
  assert(eval("NORM.S.DIST(0)").string == "#VALUE!");
  assert(eval("LOGNORM.INV(.5)").string == "#VALUE!");
  assert(eval("NORMSDIST(\"bad\")").string == "#VALUE!");
  assert(eval("BETA.DIST(NA(),2,3,TRUE)").string == "#N/A");
  assert(eval("IFERROR(NORMSDIST(NATIVE_MISSING()),77)").incomplete);
  assert(eval("IFERROR(CRITBINOM(1E100,.5,.5),77)").incomplete);
  assert(eval("HYPGEOMDIST(5,10,20,1E12)").incomplete);
  auto array = eval("NORM.S.DIST({0,1;NA(),-1},TRUE)");
  assert(array.isArray() && array.rows == 2 && array.columns == 2);
  assert(array.at(0, 0).number == .5 && array.at(1, 0).string == "#N/A");
  assert(eval("NORMDIST({1,2},{1;2},1)").string == "#VALUE!");
  engine.set(1, 1, 1, "=BINOMDIST({0,1;2,3},Probabilities!A1,.5,FALSE)");
  engine.define_matrix(1, 1, 1, 2, 2);
  assert(engine.value(1, 2, 1).number == .5);
  engine.set(0, 1, 1, "4");
  assert(engine.value(1, 2, 1).number == .25);
  near(Formula::from_excel("FDIST(3,2,2)"), .25);
  near(Formula::from_excel("finv (.25,2,2)"), 3);
  assert(Formula::from_excel("IF(1,FDIST(3,2,2),\"FDIST(\"&\"\"\"FINV(\")") ==
         "=IF(1,LEGACY.FDIST(3,2,2),\"FDIST(\"&\"\"\"FINV(\")");
  assert(Formula::from_excel("'FDIST('!A1+F.DIST(3,2,2,TRUE)") ==
         "='FDIST('!A1+F.DIST(3,2,2,TRUE)");
  std::cout << "PASS native distributions, inverse/tail semantics, tiny "
               "probabilities, domains, arrays and Excel translation\n";
}
