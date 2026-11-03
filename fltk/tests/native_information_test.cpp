// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_information.h"
#include "native_formula.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--eval") {
    SheetEngine engine;
    engine.addNewSheet("Sheet1");
    engine.addNewSheet("Sheet2");
    engine.addNewSheet("Sheet3");
    for (int r = 19; r <= 31; ++r)
      engine.set(0, 1, r, Text::number(1 << (r - 19)));
    engine.set(0, 2, 1, "=SUM(A19:A31)");
    engine.set(0, 2, 3, "7", true);
    engine.set(0, 2, 4, "2");
    engine.set(0, 2, 5, "3");
    engine.set(0, 2, 6, "TRUE");
    engine.set(0, 2, 7, "Hello", true);
    engine.set(0, 2, 9, "=#DIV/0!");
    engine.set(0, 2, 10, "0");
    engine.set(0, 3, 4, "4");
    engine.set(0, 3, 5, "5");
    engine.set(0, 3, 7, "2005-01-31", true);
    for (auto [r, v] : std::vector<std::pair<int, int>>{
             {11, 5}, {12, 6}, {13, 8}, {14, 4}, {15, 3}, {16, 2}, {17, 1}})
      engine.set(0, 3, r, Text::number(v));
    int col = 0;
    for (auto text : {"abc", "def", "efoob", "flka", "kde", "kde", "xxx"})
      engine.set(0, ++col, 1000, text, true);
    for (int r = 19; r <= 23; ++r)
      engine.set(0, 26, r, Text::number(1 << (23 - r)));
    engine.set(1, 2, 1, "=SUM(Sheet1!A19:Sheet1!A31)");
    engine.set(2, 1, 1, "1.1");
    engine.set(2, 1, 2, "2.2");
    auto encode = [&](auto &&self, const NativeValue &v) -> nlohmann::json {
      nlohmann::json out = {{"kind", int(v.kind)},
                            {"incomplete", v.incomplete},
                            {"display", v.display().toStdString()}};
      if (v.isNumber())
        out["value"] = v.number;
      if (v.isArray()) {
        out["rows"] = v.rows;
        out["columns"] = v.columns;
        out["items"] = nlohmann::json::array();
        for (const auto &item : *v.items)
          out["items"].push_back(self(self, item));
      }
      return out;
    };
    std::string line;
    while (std::getline(std::cin, line)) {
      Text formula(line);
      engine.set(0, 20, 20, formula.startsWith('=') ? formula : "=" + formula);
      std::cout << encode(encode, engine.value(0, 20, 20)).dump() << '\n';
    }
    return 0;
  }
  using V = NativeValue;
  SheetEngine e;
  e.addNewSheet("Metadata");
  e.addNewSheet("Other Ω");
  e.set(0, 1, 1, "7");
  e.set(0, 1, 2, "literal", true);
  e.set(0, 1, 3, "=NATIVE_MISSING()");
  e.set(0, 1, 4, "=1/0");
  e.set(0, 1, 5, "=FORMULA(A5)");
  e.set(0, 1, 6, "==N(7)");
  e.set(0, 1, 7, "=literal", true);
  e.set(1, 1, 1, "=SUM(1,2)");
  e.addNamedRange("'Other Ω'!A1", "FormulaName");
  auto eval = [&](const Text &f) {
    e.set(0, 20, 20, "=" + f);
    return e.value(0, 20, 20);
  };
  auto number = [&](const Text &f, double want) {
    auto v = eval(f);
    if (!v.isNumber() || v.incomplete || v.number != want)
      std::cerr << f.toStdString() << " -> " << v.display().toStdString()
                << '\n';
    assert(v.isNumber() && !v.incomplete && v.number == want);
  };
  auto boolean = [&](const Text &f, bool want) {
    auto v = eval(f);
    assert(v.isBoolean() && !v.incomplete && v.boolean == want);
  };
  auto text = [&](const Text &f, const Text &want) {
    auto v = eval(f);
    if (v.kind != V::String || v.incomplete || v.string != want)
      std::cerr << f.toStdString() << " -> " << v.display().toStdString()
                << '\n';
    assert(v.kind == V::String && !v.incomplete && v.string == want);
  };
  boolean("ISFORMULA(A1)", false);
  boolean("ISFORMULA(A3)", true);
  boolean("ISFORMULA(A4)", true);
  boolean("ISFORMULA(A5)", true);
  boolean("ISFORMULA(FormulaName)", true);
  boolean("ISFORMULA(7)", false);
  boolean("ISFORMULA(#REF!)", false);
  text("FORMULA(A3)", "=NATIVE_MISSING()");
  text("FORMULA(A4)", "=1/0");
  text("_xlfn.FORMULATEXT(A4)", "=1/0");
  assert(Formula::from_excel("=_xlfn.FORMULATEXT(A4)") == "=FORMULA(A4)");
  text("FORMULA(A5)", "=FORMULA(A5)");
  text("FORMULA(A6)", "==N(7)");
  boolean("ISFORMULA(A6)", true);
  boolean("ISFORMULA(A7)", false);
  boolean("ISFORMULA(A3:A4)", false);
  text("FORMULA(T20)", "=FORMULA(T20)");
  text("FORMULA(FormulaName)", "=SUM(1,2)");
  text("FORMULA(INDEX(A1:A4~'Other Ω'!A1,1,1,2))", "=SUM(1,2)");
  text("FORMULA(INDIRECT(\"FormulaName\"))", "=SUM(1,2)");
  text("FORMULA(IF(TRUE(),A4,NATIVE_MISSING()))", "=1/0");
  text("FORMULA(A3:A4)", "=NATIVE_MISSING()");
  assert(eval("FORMULA(A1)").string == "#N/A");
  assert(eval("FORMULA(7)").string == "#VALUE!");
  assert(eval("FORMULA(A3~A4)").string == "#VALUE!");
  boolean("ISFORMULA(A3~A4)", false);
  assert(eval("ISFORMULA(NATIVE_MISSING())").incomplete);
  assert(eval("TYPE(A3)").incomplete);
  assert(eval("ERROR.TYPE(A3)").incomplete);
  e.set(0, 9, 10, "=FORMULA(A1:A4)");
  e.define_matrix(0, 9, 10, 1, 4);
  assert(e.value(0, 9, 10).string == "#N/A");
  assert(e.value(0, 9, 11).string == "#N/A");
  assert(e.value(0, 9, 12).string == "=NATIVE_MISSING()" &&
         !e.value(0, 9, 12).incomplete);
  assert(e.value(0, 9, 13).string == "=1/0");
  text("FORMULA(I12)", "{=FORMULA(A1:A4)}");
  boolean("ISFORMULA(I12)", true);
  e.set(0, 10, 10, "=ISFORMULA(A1:A4)");
  e.define_matrix(0, 10, 10, 1, 4);
  assert(!e.value(0, 10, 10).boolean && e.value(0, 10, 13).boolean);
  number("TYPE(7)", 1);
  number("TYPE(Z999)", 1);
  number("TYPE(TRUE())", 4);
  number("TYPE(\"text\")", 2);
  number("TYPE(#N/A)", 16);
  number("TYPE({1})", 64);
  number("TYPE(A1:A2)", 64);
  assert(eval("TYPE(A1:A4)").incomplete);
  boolean("ISNONTEXT(Z999)", true);
  boolean("ISNOTTEXT(\"\")", false);
  boolean("ISNUM(3)", true);
  boolean("ISNUM(TRUE())", false);
  for (auto [error, code] : std::vector<std::pair<Text, int>>{{"#NULL!", 1},
                                                              {"#DIV/0!", 2},
                                                              {"#VALUE!", 3},
                                                              {"#REF!", 4},
                                                              {"#NAME?", 5},
                                                              {"#NUM!", 6},
                                                              {"#N/A", 7}}) {
    number("ERROR.TYPE(" + error + ")", code);
    number("ERRORTYPE(" + error + ")", code);
  }
  assert(eval("ERROR.TYPE(7)").string == "#N/A");
  for (auto n : {"0", "2", "-2.9", "9007199254740992", "1E308", "5E-324"})
    boolean("ISEVEN(" + Text(n) + ")", true);
  for (auto n : {"1", "-3.9", "9007199254740991"})
    boolean("ISODD(" + Text(n) + ")", true);
  assert(eval("ISEVEN(TRUE())").string == "#VALUE!");
  boolean("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISEVEN(2.9)", true);
  boolean("COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISODD(-3.9)", true);
  e.set(0, 3, 1, "2001-02-03");
  e.set(0, 3, 2, "2001-02-03T04:05:06.25Z");
  e.set(0, 3, 3, "PT4H5M6S");
  boolean("ISDATE(C1)", true);
  boolean("ISTIME(C1)", false);
  boolean("ISDATE(C2)", true);
  boolean("ISTIME(C2)", true);
  boolean("ISDATE(C3)", false);
  boolean("ISTIME(C3)", true);
  boolean("ISDATE(DATE(2001,2,3))", true);
  boolean("ISTIME(TIME(4,5,6))", true);
  boolean("ISDATE(\"2001-02-03\")", false);
  boolean("ISTIME(0.5)", false);
  boolean("ISDATE(INDEX(C1:C3,1))", true);
  boolean("ISTIME(IF(TRUE(),C2,0))", true);
  auto limited = eval("SUM(ISFORMULA(A1:XFD1048576))");
  assert(limited.incomplete && limited.string == "#LIMIT!");
  e.set(0, 11, 1, "=\"" + Text(4090, U'x') + "\"");
  e.define_matrix(0, 11, 1, 1, 300);
  limited = eval("SUM(LEN(FORMULA(K1:K300)))");
  assert(limited.incomplete && limited.string == "#LIMIT!");
  std::cout << "PASS native information metadata, matrix formulas, type/error "
               "tests, parity and temporal identity\n";
}
