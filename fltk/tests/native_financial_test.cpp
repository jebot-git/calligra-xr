// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_financial.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
namespace {
using V = NativeValue;
using Json = nlohmann::json;
Json encode(const V &value) {
  Json out = {{"kind", value.kind},
              {"display", value.display().toStdString()},
              {"incomplete", value.incomplete}};
  if (value.isNumber())
    out["value"] = value.number;
  if (value.isArray()) {
    out["rows"] = value.rows;
    out["columns"] = value.columns;
    out["items"] = Json::array();
    for (const auto &v : *value.items)
      out["items"].push_back(encode(v));
  }
  return out;
}
void near(const V &actual, double expected, double tolerance = 1e-10) {
  if (!actual.isNumber() || std::abs(actual.number - expected) >
                                tolerance * std::max(1., std::abs(expected)))
    std::cerr << actual.display().toStdString() << " != " << expected << '\n';
  assert(actual.isNumber() && !actual.incomplete);
  assert(std::abs(actual.number - expected) <=
         tolerance * std::max(1., std::abs(expected)));
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--document") {
    Workbook book;
    book.load(Text::fromUtf8(argv[2]));
    Json rows = Json::array();
    for (int row = 1; row <= 3; ++row) {
      Json values = Json::array();
      for (int column = 1; column <= 21; ++column)
        values.push_back(encode(book.value(0, column, row)));
      rows.push_back(std::move(values));
    }
    if (argc == 4)
      book.save(Text::fromUtf8(argv[3]));
    std::cout << rows.dump() << '\n';
    return 0;
  }
  SheetEngine engine;
  engine.addNewSheet("Financial");
  auto eval = [&](const Text &expression) {
    engine.set(0, 20, 1,
               expression.startsWith('=') ? expression : "=" + expression);
    return engine.value(0, 20, 1);
  };
  if (argc == 2 && std::string(argv[1]) == "--eval") {
    std::string line;
    while (std::getline(std::cin, line))
      std::cout << encode(eval(Text(line))).dump() << '\n';
    return 0;
  }
  assert(financial_signatures().size() == 41);
  for (auto [formula, expected] : std::vector<std::pair<Text, double>>{
           {"PMT(5%,12,1000)", -112.8254100208},
           {"PMT(5%,12,1000,100,1)", -113.4361438313},
           {"PV(10%,12,-100,100)", 649.5061005186},
           {"FV(10%,12,-100,100)", 1824.5855390489},
           {"NPER(0,-100,1000)", 10},
           {"NPER(-1%,-100,1000)", 9.48328306572157},
           {"RATE(4*12,-200,8000)", .00770147248820137},
           {"IPMT(5%/12,10,360,100000)", -412.0850243},
           {"IPMT(5%/12,10,360,100000,0,1)", -410.3751278993},
           {"PPMT(8%,10,24,10000,2000,1)", -332.6779394454},
           {"ISPMT(5%/12,12,360,100000)", -402.7777777778},
           {"CUMIPMT(.06/12,60,100000,5,12,0)", -3562.187023},
           {"CUMPRINC(.06/12,60,100000,5,12,1)", -11844.830051},
           {"IRR({-100,39,59,55,20})", .28094842115996},
           {"IRR({-100,0,0,74})", -.0954958303489726},
           {"IRR({-100,230,-132},.1)", .1},
           {"IRR({-100,230,-132},.3)", .2},
           {"IRR({-7986.870194491923,1280.0790965455528,123.10338713543123,"
            "393.41668896656336,79.365202910718807,1338.329795613251})",
            -.23214248796276282},
           {"MIRR({100,200,-50,300,-200},5%,6%)", .342823387842},
           {"FVSCHEDULE(1000000,{.03,.04,.05})", 1124760},
           {"EFFECT(.12,12)", .12682503013197},
           {"EFFECTIVE(.12,12)", .12682503013197},
           {"NOMINAL(13.5%,12)", .1273031669590416},
           {"PDURATION(.1,10,100)", 24.1588579280968},
           {"DURATION(.1,10,100)", 24.1588579280968},
           {"RRI(12,5000,10000)", .0594630943592953},
           {"_xlfn.RRI(.5,100,200)", 3},
           {"COM.MICROSOFT.RRI(.5,100,200)", 3},
           {"COMPOUND(1000,.1,12,5)", 1645.30893477859},
           {"CONTINUOUS(1000,.1,5)", 1648.721270700128},
           {"FV_ANNUITY(1000,.05,5)", 5525.63125},
           {"PV_ANNUITY(1000,.05,5)", 4329.47667063082},
           {"LEVEL_COUPON(1000,.13,2,4,.1)", 1096.9481913913939},
           {"ZERO_COUPON(1000,.1,2)", 826.446280991736},
           {"SLN(30000,7500,10)", 2250},
           {"SYD(30000,7500,10,1)", 4090.90909090909},
           {"DB(1000000,100000,6,7,7)", 15845.09847384807},
           {"DDB(2500,500,24,19,2)", 22.0906464672553},
           {"DDB(2400,300,10,.8,20)", 2100},
           {"VDB(10000,600,10,.875,1.875,1.5)", 1303.125},
           {"VDB(10000,600,10,4.875,5.875,1.5)", 769.36046875},
           {"VDB(10000,600,10,9.875,10,1.5)", 95.926328125},
           {"DOLLARDE(1.02,16)", 1.125},
           {"DOLLARFR(-1.125,16)", -1.02},
           {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETFVSCHEDULE(100,{.1,.2})",
            132},
           {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETNOMINAL(.135,12)",
            .1273031669590416}})
    near(eval(formula), expected, 1e-9);
  // Cancellation at tiny rates used to make PMT/PV/FV divide by zero or
  // silently lose all interest. Cumulative sums also retain that small term.
  near(eval("PMT(1e-20,10,1000)"), -100, 1e-14);
  near(eval("PV(1e-20,10,-100)"), 1000, 1e-14);
  near(eval("FV(1e-20,10,-100)"), 1000, 1e-14);
  auto tiny = eval("CUMIPMT(1e-20,10,10000,1,10,0)");
  assert(tiny.isNumber() && std::abs(tiny.number / (-5.5e-16) - 1) < 1e-12);
  tiny = eval("CUMIPMT(1e-20,10,10000,1,10,1)");
  assert(tiny.isNumber() && std::abs(tiny.number / (-4.5e-16) - 1) < 1e-12);
  for (int due : {0, 1}) {
    Text tail = ",60,100000,1,60," + Text::number(due) + ")";
    near(eval("CUMPRINC(.005" + tail), -100000, 1e-12);
    near(eval("CUMIPMT(.005" + tail + "+CUMPRINC(.005" + tail +
              "-60*PMT(.005,60,100000,0," + Text::number(due) + ")"),
         0, 1e-9);
  }
  // Direct NumberSequence scalars coerce; referenced text and logical cells
  // are excluded. Sparse references retain row order without dense expansion.
  engine.set(0, 1, 1, "-1000");
  engine.set(0, 1, 2, "ignored", true);
  engine.set(0, 1, 4, "600");
  engine.set(0, 1, 5, "700");
  engine.set(0, 1, 6, "TRUE");
  near(eval("NPV(0,TRUE,\"2\",3)"), 6);
  near(eval("NPV(0,A1:A1000000)"), 300);
  near(eval("IRR(A1:A1000000)"), .188819441731558885, 1e-12);
  near(eval("FVSCHEDULE(100,A2)"), 100);
  near(eval("FVSCHEDULE(100,\".1\")"), 110);
  auto array = eval("PMT({.01,.02},12,1000)");
  assert(array.isArray() && array.items->size() == 2);
  near(array.at(0, 0), -88.8487886783417);
  near(array.at(0, 1), -94.5595966229514);
  for (auto [formula, expected] : std::vector<std::pair<Text, Text>>{
           {"PMT(0,0,1000)", "#DIV/0!"},
           {"RATE(0,-100,1000)", "#VALUE!"},
           {"RATE(10,0,0,100)", "#NUM!"},
           {"IRR({1,2,3})", "#NUM!"},
           {"MIRR({1,2,3},.1,.1)", "#DIV/0!"},
           {"CUMIPMT(.05,10,100,1,11,0)", "#VALUE!"},
           {"CUMIPMT(.05,10,100,1,5,.5)", "#VALUE!"},
           {"DB(0,500,4,2)", "#NUM!"},
           {"DDB(1000,0,10,11)", "#NUM!"},
           {"PDURATION(0,100,200)", "#VALUE!"},
           {"VDB(100,0,1e9,0,1e9)", "#LIMIT!"},
           {"PV(\"bad\",10,10)", "#VALUE!"},
           {"RATE(10,NA(),100)", "#N/A"}}) {
    auto value = eval(formula);
    assert(value.isError() && value.string == expected);
  }
  for (const auto &[name, arity] : financial_signatures()) {
    auto wrong = financial_function(name, {});
    assert(wrong.isError() && wrong.string == "#VALUE!" && !wrong.incomplete);
  }
  auto unsupported = eval("IFERROR(DURATION(45000,46000,.05,.07,2),99)");
  assert(unsupported.incomplete && unsupported.number == 99);
  // The implemented failure is ordinary, while the unported coupon overload
  // remains incomplete and cannot overwrite a preserved source cache.
  assert(!eval("IFERROR(RATE(0,10,100),99)").incomplete);
  TempDir temporary;
  Workbook book;
  book.set(0, 1, 1, "1000");
  book.set(0, 2, 1, "=PMT(.005,60,A1)");
  book.set(0, 3, 1, "=RATE(60,B1,A1)");
  auto path = temporary.path() + "/financial.ods";
  book.save(path);
  Workbook restored;
  restored.load(path);
  near(restored.value(0, 3, 1), .005);
  std::cout << "Native financial annuities, amortization, cash flow, "
               "depreciation, arrays, tiny rates and round trips pass\n";
}
