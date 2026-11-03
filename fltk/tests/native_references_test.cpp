// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_sheets.h"
#include "native_formula.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine e;
  e.addNewSheet("First");
  e.addNewSheet("O'Brien Ω");
  for (int r = 1; r <= 4; ++r) {
    e.set(0, 1, r, Text::number(r));
    e.set(0, 2, r, Text::number(r * 10));
    e.set(1, 1, r, Text::number(r * 100));
  }
  auto eval = [&](const Text &f) {
    e.set(0, 20, 20, "=" + f);
    return e.value(0, 20, 20);
  };
  auto near = [&](const Text &f, double want) {
    auto v = eval(f);
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - want) > 1e-12 * std::max(1., std::abs(want)))
      std::cerr << f.toStdString() << " -> " << v.display().toStdString()
                << " expected " << want << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - want) <= 1e-12 * std::max(1., std::abs(want)));
  };
  near("SUM(A1:A4~B1:B4)", 110);
  near("SUM(A1:A4~A2:A3~A2)", 17);
  near("SUM((A1:A4~B1:B4)~'O''Brien Ω'!A1:A4)", 1110);
  near("AREAS(A1:A4~A2:A3~A2)", 3);
  near("SUM(INDEX(A1:A4~B1:B4,0,0,2))", 100);
  near("INDEX(A1:A4~B1:B4,3,1,2)", 30);
  near("SUM(INDEX(A1:B4,2))", 22);
  near("SUM(INDEX(A1:B4))", 110);
  near("SUM(INDEX(A1:B4,0,0,))", 110);
  near("SUM(INDEX({1,2;3,4},2))", 7);
  near("SUM(INDEX({1,2;3,4}))", 10);
  near("COUNTBLANK(A1:A1048576~A1:A1048576)", 2097144);
  near("COUNT(A1:A1048576~A1:A1048576)", 8);
  e.set(0, 3, 1, "text");
  e.set(0, 3, 2, "=TRUE()");
  e.set(0, 3, 3, "=\"\"");
  near("COUNT(C1:C10~C1:C10)", 0);
  near("COUNTA(C1:C10~C1:C10)", 6);
  near("COUNTBLANK(C1:C10~C1:C10)", 16);
  auto logical = eval("ISREF(A1~B1)");
  assert(logical.isBoolean() && logical.boolean && !logical.incomplete);
  logical = eval("ISREF(7)");
  assert(logical.isBoolean() && !logical.boolean && !logical.incomplete);
  logical = eval("ISREF(#REF!)");
  assert(logical.isBoolean() && !logical.boolean && !logical.incomplete);
  for (auto f : {"SUM(A1~7)", "AREAS(7)", "DSUM(A1:B4,1,A1:A2~B1:B2)",
                 "SUM(INDEX(A1~B1,1,1,3))", "SUM(A1~B1+1)", "ROWS(A1~B1)",
                 "OFFSET(A1~B1,0,0)", "A1~B1"}) {
    auto v = eval(f);
    assert(v.isError() && !v.incomplete);
  }
  e.addNamedRange("First!A1:A4~'O''Brien Ω'!A1:A4", "TwoSheets");
  e.addNamedRange("TwoSheets~First!B1:B4", "AllSamples");
  near("SUM(AllSamples)", 1110);
  near("AREAS(CHOOSE(2,A1,AllSamples))", 3);
  near("SUM(IF(TRUE(),AllSamples,NATIVE_MISSING()))", 1110);
  assert(eval("ISREF(NATIVE_MISSING())").incomplete);
  assert(eval("SUM(A1~NATIVE_MISSING())").incomplete);
  near("AREAS(INDIRECT(\"AllSamples\"))", 3);
  near("SUM(INDEX(INDIRECT(\"AllSamples\"),0,0,3))", 100);
  auto encoded =
      Formula::encode("=SUM(First!$A$1:$A$4~'O''Brien Ω'!$A$1:$A$4)");
  assert(Formula::decode(encoded) ==
         "=SUM(First!$A$1:$A$4~'O''Brien Ω'!$A$1:$A$4)");
  e.addNamedRange(
      "of:" + Formula::encode("=First!$A$1:$A$4~'O''Brien Ω'!$A$1:$A$4"),
      "AbsoluteList");
  near("SUM(AbsoluteList)", 1010);
  e.addNamedRange("Cycle~A1", "Cycle");
  assert(eval("SUM(Cycle)").incomplete);
  e.set(0, 1, 2, "=IFERROR(NATIVE_MISSING(),2)");
  assert(eval("IFERROR(SUM(AllSamples),77)").incomplete);
  assert(eval("SUM(A1:A4)").incomplete);
  assert(eval("COUNT(A1:A4~B1:B4)").incomplete);
  assert(eval("COUNTA(A1:A4~B1:B4)").incomplete);
  assert(eval("COUNTBLANK(A1:A4~B1:B4)").incomplete);
  std::cout << "PASS ordered unions, duplicate/sparse cells, area selection, "
               "metadata and provenance\n";
}
