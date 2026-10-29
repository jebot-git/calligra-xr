// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_formula.h"
#include "native_sheets.h"
#include "workbook.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine engine;
  engine.addNewSheet("Data");
  engine.addNewSheet("Other; 日本語");
  for (int row = 1; row <= 3; ++row) {
    engine.set(0, 1, row, Text::number(row * 10));
    engine.set(0, 2, row, Text::number(row * 100));
    engine.set(1, 1, row, Text::number(row));
  }
  auto eval = [&](const char *formula) {
    engine.set(0, 90, 90, formula);
    return engine.value(0, 90, 90);
  };
  auto scalar = [&](const char *formula, const char *expected) {
    auto actual = eval(formula);
    std::cout << formula << " -> " << actual.display().toStdString()
              << std::endl;
    assert(!actual.isArray());
    assert(actual.display() == expected);
  };
  auto matrix = [&](const char *formula, size_t rows, size_t columns,
                    std::initializer_list<double> expected) {
    auto actual = eval(formula);
    assert(actual.isArray() && actual.rows == rows &&
           actual.columns == columns);
    assert(expected.size() == rows * columns);
    size_t at = 0;
    for (double number : expected) {
      const auto &value = actual.items->at(at++);
      assert(value.isNumber());
      assert(std::abs(value.number - number) < 1e-10);
    }
  };
  matrix("={1,2;3,4}", 2, 2, {1, 2, 3, 4});
  matrix("={1,2;3,4}+10", 2, 2, {11, 12, 13, 14});
  matrix("=-{1,-2;3,-4}", 2, 2, {-1, 2, -3, 4});
  matrix("=ABS({-1,2;-3,4})", 2, 2, {1, 2, 3, 4});
  matrix("=ROUND({1.25,2.55},1)", 1, 2, {1.3, 2.6});
  matrix("=LEN({\"日本語\",\"Český\"})", 1, 2, {3, 5});
  scalar("=SUM({1,2;3,4}*{2,3;4,5})", "40");
  scalar("=SUM({1,\"3\",TRUE})", "1");
  scalar("=COUNT({1,\"3\",TRUE,#N/A})", "1");
  scalar("=AND({TRUE,\"text\",0})", "FALSE");
  scalar("=AND({\"text\",\"other\"})", "#VALUE!");
  scalar("=OR({FALSE,\"text\"})", "FALSE");
  scalar("=SUM({1,2;3,4})", "10");
  scalar("=SUM({1,2}+{3;4})", "#VALUE!");
  scalar("=SUM({1,2;3})", "#PARSE!");
  scalar("=SUM({A1,2})", "12");
  matrix("={A1+1,SUM(A1:A2),DATE(2024,1,1)}", 1, 3, {11, 30, 45292});
  scalar("=SUM({A1:A2,2})", "#VALUE!");
  scalar("=SUM({{1,2},3})", "#VALUE!");
  scalar("=SUM({IF(TRUE,10,1/0),2})", "12");
  auto division = eval("={1,2;3,4}/{1,0;0,2}");
  assert(division.isArray() && division.at(0, 0).number == 1 &&
         division.at(1, 1).number == 2);
  assert(division.at(0, 1).string == "#DIV/0!" &&
         division.at(1, 0).string == "#DIV/0!");
  matrix("=IFERROR({1,2}/{0,2},9)", 1, 2, {9, 1});
  matrix("=IFNA({#N/A,2},7)", 1, 2, {7, 2});
  matrix("=IF({TRUE,FALSE},{1,2},{3,4})", 1, 2, {1, 4});
  matrix("=IF({FALSE,FALSE},1/0,{3,4})", 1, 2, {3, 4});
  matrix("=CHOOSE({1,2},10,20)", 1, 2, {10, 20});
  scalar("=CHOOSE(2,1/0,42)", "42");
  auto typed = eval("={1,TRUE,\"Český\";#N/A,FALSE,\"\"}");
  assert(typed.rows == 2 && typed.columns == 3 && typed.at(0, 1).isBoolean());
  assert(typed.at(0, 2).string == "Český" && typed.at(1, 0).isError() &&
         typed.at(1, 2).kind == NativeValue::String);
  scalar("=SUM(A1:B3)", "660");
  scalar("=SUMPRODUCT(A1:A3,B1:B3)", "14000");
  scalar("=SUMPRODUCT({1,2,3},{4,5,6})", "32");
  scalar("=SUMPRODUCT({TRUE,FALSE},{TRUE,TRUE},{5,2})", "5");
  scalar("=SUMPRODUCT({TRUE,FALSE},{3,7})", "3");
  scalar("=SUMPRODUCT({1,2},{3;4})", "#VALUE!");
  scalar("=SUMPRODUCT({1,#N/A},{0,0})", "#N/A");
  matrix("=TRANSPOSE({1,2,3;4,5,6})", 3, 2, {1, 4, 2, 5, 3, 6});
  matrix("=MMULT({1,2;3,4},{5,6;7,8})", 2, 2, {19, 22, 43, 50});
  scalar("=MDETERM({1,2;3,4})", "-2");
  scalar("=MDETERM({1,2;2,4})", "0");
  matrix("=MINVERSE({4,7;2,6})", 2, 2, {.6, -.7, -.2, .4});
  scalar("=MINVERSE({1,2;2,4})", "#NUM!");
  scalar("=MMULT({1,2},{3,4})", "#VALUE!");
  scalar("=INDEX(A1:B3,2,2)", "200");
  scalar("=INDEX({1,2,3},2)", "2");
  matrix("=INDEX({1,2;3,4},0,2)", 2, 1, {2, 4});
  scalar("=SUM(INDEX(A1:B3,0,2))", "600");
  scalar("=INDEX(A1:B3,4,1)", "#REF!");
  scalar("=SUM(OFFSET(A1,1,0,2,2))", "550");
  scalar("=OFFSET(A1,-1,0)", "#REF!");
  scalar("=OFFSET(A1,0,0,0,2)", "#VALUE!");
  scalar("=INDIRECT(\"B2\")", "200");
  scalar("=INDIRECT(\"R2C2\",FALSE)", "200");
  scalar("=SUM(INDIRECT(\"A1:B3\"))", "660");
  scalar("=INDIRECT(\"1+1\")", "#REF!");
  scalar("=ROWS(A1:B1048576)", "1048576");
  scalar("=COLUMNS(A1:B1048576)", "2");
  scalar("=SUM(A1:B1048576)", "660");
  scalar("=SUMPRODUCT(A1:B1048576)", "#LIMIT!");
  scalar("=COUNTBLANK(A1:C3)", "3");
  scalar("=COUNTBLANK(INDEX(A1:C3,0,3))", "3");
  scalar("=COUNTBLANK(OFFSET(A1,0,2,3,1))", "3");
  scalar("=ROW()", "90");
  scalar("=COLUMN()", "90");
  matrix("=ROW(A2:A4)", 3, 1, {2, 3, 4});
  matrix("=COLUMN(B2:D4)", 1, 3, {2, 3, 4});
  scalar("=SHEET('Other; 日本語'!A1)", "2");
  scalar("=SHEETS()", "2");
  scalar("=VLOOKUP(20,A1:B3,2,FALSE)", "200");
  scalar("=VLOOKUP(25,A1:B3,2)", "200");
  scalar("=VLOOKUP(5,A1:B3,2)", "#N/A");
  scalar("=VLOOKUP(20,A1:B3,3,FALSE)", "#REF!");
  matrix("=VLOOKUP({10,30},A1:B3,2,FALSE)", 1, 2, {100, 300});
  scalar("=HLOOKUP(2,{1,2,3;10,20,30},2,FALSE)", "20");
  scalar("=MATCH(20,A1:A3,0)", "2");
  scalar("=MATCH(25,A1:A3)", "2");
  scalar("=MATCH(25,{30;20;10},-1)", "1");
  scalar("=MATCH(\"日本語\",{\"Český\",\"日本語\"},0)", "2");
  scalar("=LOOKUP(25,A1:A3,B1:B3)", "200");
  scalar("=LOOKUP(25,A1:B3)", "200");
  scalar("=LOOKUP(2,{1,2,3;10,20,30})", "20");
  matrix("=MATCH({10,30},A1:A3,0)", 1, 2, {1, 3});
  engine.addNamedRange("A1:B3", "DataRange");
  engine.addNamedRange("DataRange", "Alias");
  scalar("=SUM(Alias)", "660");
  scalar("=INDEX(Alias,3,2)", "300");
  engine.addNamedRange("CycleTwo", "CycleOne");
  engine.addNamedRange("CycleOne", "CycleTwo");
  scalar("=SUM(CycleOne)", "#CYCLE!");
  engine.set(0, 90, 90, "=SUM(Later)");
  assert(engine.value(0, 90, 90).isError());
  engine.addNamedRange("A1:A3", "Later");
  assert(engine.value(0, 90, 90).number == 60);
  engine.set(0, 90, 90, "=SHEETS()");
  assert(engine.value(0, 90, 90).number == 2);
  engine.addNewSheet("Third");
  assert(engine.value(0, 90, 90).number == 3);
  for (const char *formula :
       {"=SUM({1,2;3,4})", "=\"a:=b;|,\"", "='Other; 日本語'!A1+1",
        "=IF(TRUE,\"a\"\";b\",\"c\")"}) {
    Text native(formula);
    assert(Formula::decode("of:" + Formula::encode(native)) == native);
  }
  assert(Formula::encode("=SUM({1,2;3,4})") == "=SUM({1;2|3;4})");
  assert(Formula::encode("=sum(a1:b2)") == "=sum([.a1:.b2])");
  assert(Formula::shift("=sum(a1,$b$2)", 1, 1) == "=sum(B2,$B$2)");
  assert(Formula::decode("='Other; 日本語'!A1") == "='Other; 日本語'!A1");
  engine.set(0, 10, 2, "=A1:A3+1");
  assert(engine.value(0, 10, 2).number == 21);
  engine.set(0, 10, 7, "=A1:A3+1");
  assert(engine.value(0, 10, 7).string == "#VALUE!");
  scalar("=SUMPRODUCT(A1:A3+1)", "63");
  scalar("=SUM(J7)", "#VALUE!");
  assert(eval("=IFERROR(UNAVAILABLE_NATIVE_FUNCTION(),7)").incomplete);
  assert(eval("=ISERROR(UNAVAILABLE_NATIVE_FUNCTION())").incomplete);
  assert(!eval("=IF(FALSE,UNAVAILABLE_NATIVE_FUNCTION(),7)").incomplete);
  assert(!eval("=1/0").incomplete);
  assert(!eval("=#NAME?").incomplete);
  matrix("=IF({TRUE,FALSE},TRANSPOSE(A1:A2),TRANSPOSE(A2:A3))", 1, 2, {10, 30});
  matrix("=IF({TRUE,FALSE},A1:B1,A2:B2)", 1, 2, {10, 200});
  matrix("=IFERROR({#REF!,2},A1:B1)", 1, 2, {10, 2});
  matrix("=CHOOSE({1,2},A1:B1,A2:B2)", 1, 2, {10, 200});
  Workbook workbook;
  workbook.add_sheet("Other; 日本語");
  workbook.set(1, 1, 1, "7");
  workbook.set(0, 1, 1, "=SUM({1,2;3,4})");
  workbook.set(0, 2, 1, "=MMULT({1,2;3,4},{5,6;7,8})");
  workbook.set(0, 3, 1, "='Other; 日本語'!A1+1");
  TempDir temporary;
  auto path = temporary.path() + "/arrays.ods";
  workbook.save(path);
  Workbook reopened;
  reopened.load(path);
  assert(reopened.display(0, 1, 1) == "10" &&
         reopened.display(0, 2, 1) == "19" && reopened.display(0, 3, 1) == "8");
  assert(reopened.input(0, 1, 1) == "=SUM({1,2;3,4})");
  Text quoted = "='One\";Part]Name:Here'!A1+SUM({1,2;3,4})";
  assert(Formula::decode("of:" + Formula::encode(quoted)) == quoted);
  std::cout
      << "PASS typed arrays, matrix math, lazy selection, reference/lookup "
         "functions, sparse bounds, names and ODF array round trips"
      << std::endl;
}
