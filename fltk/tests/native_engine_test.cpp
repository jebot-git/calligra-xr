// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  Workbook w;
  w.set(0, 1, 1, "2");
  w.set(0, 1, 2, "8");
  for (auto [formula, expected] :
       std::initializer_list<std::pair<const char *, const char *>>{
           {"=SUM(A1:A2)", "10"},
           {"=AVERAGE(A1:A2)", "5"},
           {"=MIN(A1:A2)", "2"},
           {"=MAX(A1:A2)", "8"},
           {"=IF(A1>1,A2,0)", "8"},
           {"=COUNT(A1:A2)", "2"},
           {"=2^3+4*5", "28"}}) {
    w.set(0, 2, 1, formula);
    auto result = w.display(0, 2, 1);
    std::cout << formula << " -> " << result.toStdString() << std::endl;
    assert(result == expected);
  }
  for (auto [formula, expected] :
       std::initializer_list<std::pair<const char *, const char *>>{
           {"=ROUND(1.234,2)", "1.23"},
           {"=ROUND(-1.25,1)", "-1.3"},
           {"=SIN(0)", "0"},
           {"=SQRT(16)", "4"},
           {"=LEN(\"日本語\")", "3"},
           {"=IF(FALSE,1/0,42)", "42"},
           {"=IFERROR(1/0,7)", "7"},
           {"=IFERROR(1+\"x\",7)", "7"},
           {"=ISERROR(-\"x\")", "TRUE"},
           {"=IFNA(\"#N/A\",7)", "#N/A"},
           {"=ISNA(\"#N/A\")", "FALSE"},
           {"=ISNA(NA())", "TRUE"},
           {"=DATE(\"nan\",1,1)", "#NUM!"},
           {"=DAY(60)", "28"},
           {"=DATE(1900,1,1)", "2"},
           {"=DATE(2024,1,1)", "45292"},
           {"=SUM(\"3\",TRUE)", "4"},
           {"=ROUND(1)", "#VALUE!"},
           {"=REPT(\"\",2147483647)", ""},
           {"=REPT(\"a\",600000)&REPT(\"b\",600000)", "#LIMIT!"},
           {"=MOD(-3,2)", "1"},
           {"=UNAVAILABLE_NATIVE_FUNCTION(A1)", "#NAME?"},
           {"=1/0", "#DIV/0!"},
           {"=#REF!", "#REF!"}}) {
    w.set(0, 2, 1, formula);
    assert(w.display(0, 2, 1) == expected);
  }
  w.set(0, 2, 1, "=PMT(0.05/12,360,100000)");
  assert(std::abs(w.display(0, 2, 1).toDouble() + 536.82162301214) < 1e-9);
  w.set(0, 2, 1, "=B1");
  assert(w.display(0, 2, 1) == "#CYCLE!");
  std::cout << "PASS mathematical, text, financial, lazy branches, errors and "
               "cycle detection"
            << std::endl;
  w.set(0, 2, 1, "=UNAVAILABLE_NATIVE_FUNCTION(A1)");
  w.set(0, 3, 1, "=B1*2");
  assert(w.display(0, 3, 1).startsWith('#'));
  Text deep("=1");
  for (int i = 0; i < 300; ++i)
    deep += "+1";
  w.set(0, 2, 1, deep);
  assert(w.display(0, 2, 1) == "#DEPTH!");
  w.set(0, 2, 1, "=" + Text(9000, 'Z') + "1");
  assert(w.display(0, 2, 1) == "#LIMIT!");
  w.set(0, 2, 1, "=" + Text(100, 'Z') + "1");
  assert(w.display(0, 2, 1) == "#NAME?");
  w.set(0, 4, 1, "2024-01-01T12:30:00.000Z");
  w.set(0, 2, 1, "=YEAR(D1)");
  assert(w.display(0, 2, 1) == "2024");
  w.set(0, 2, 1, "=MINUTE(D1)");
  assert(w.display(0, 2, 1) == "30");
  w.set(0, 4, 1, "PT129600S");
  w.set(0, 2, 1, "=D1+1");
  assert(w.display(0, 2, 1) == "2.5");
  w.set(0, 5, 1, "2024-01-01T12:30:00.000Z");
  w.set(0, 6, 1, "=YEAR(E1)");
  TempDir temp;
  auto path = temp.path() + "/dates.ods";
  w.save(path);
  Workbook reopened;
  reopened.load(path);
  assert(reopened.display(0, 2, 1) == "2.5");
  assert(reopened.display(0, 6, 1) == "2024");
  assert(reopened.display(0, 5, 1) == "2024-01-01T12:30:00.000Z");
  std::cout
      << "PASS malformed/deep formulas and non-finite coercion are bounded"
      << std::endl;
  std::cout
      << "PASS unsupported formula errors propagate rather than becoming zero"
      << std::endl;
}
