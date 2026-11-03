// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_database.h"
#include "workbook.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
int main() {
  SheetEngine e;
  e.addNewSheet("Database");
  e.addNewSheet("Other");
  for (auto [col, name] : std::vector<std::pair<int, Text>>{
           {1, "Name"}, {2, "Score"}, {3, "Group"}, {4, "Flag"}})
    e.set(0, col, 1, name);
  std::vector<Text> names = {"alpha", "beta",  "ALPHA",
                             "gamma", "alpha", "delta"},
                    groups = {"A", "B", "A", "B", "C", "A"};
  for (int i = 0; i < 6; ++i) {
    e.set(0, 1, i + 2, names[i]);
    if (i < 5)
      e.set(0, 2, i + 2, Text::number(2 * i + 2));
    e.set(0, 3, i + 2, groups[i]);
    e.set(0, 4, i + 2, i % 2 ? "=FALSE()" : "=TRUE()");
  }
  e.set(0, 8, 1, "Group");
  e.set(0, 8, 2, "A");
  e.set(0, 9, 1, "Group");
  e.addNamedRange("Database!A1:D7", "Records");
  e.addNamedRange("Database!H1:H2", "Selection");
  auto eval = [&](const Text &f) {
    e.set(0, 20, 20, "=" + f);
    return e.value(0, 20, 20);
  };
  auto near = [](const NativeValue &v, double want) {
    if (!v.isNumber() || v.incomplete ||
        std::abs(v.number - want) > 2e-12 * std::max(1e-300, std::abs(want)))
      std::cerr << v.display().toStdString() << " != " << want << '\n';
    assert(v.isNumber() && !v.incomplete &&
           std::abs(v.number - want) <=
               2e-12 * std::max(1e-300, std::abs(want)));
  };
  for (auto [function, want] :
       std::vector<std::pair<Text, double>>{{"DSUM", 8},
                                            {"DAVERAGE", 4},
                                            {"DCOUNT", 2},
                                            {"DCOUNTA", 2},
                                            {"DMIN", 2},
                                            {"DMAX", 6},
                                            {"DPRODUCT", 12},
                                            {"DVAR", 8},
                                            {"DVARP", 4},
                                            {"DSTDEV", std::sqrt(8.)},
                                            {"DSTDEVP", 2}})
    near(eval(function + "(Records,\"sCoRe\",Selection)"), want);
  near(eval("DCOUNT(Records,,Selection)"), 3);
  near(eval("DCOUNTA(Records,0,Selection)"), 3);
  near(eval("DSUM(Records,2.9,Selection)"), 8);
  near(eval("DSUM(Records,4,Selection)"), 2);
  near(eval("DCOUNT(Records,4,Selection)"), 3);
  near(eval("DSUM(Records,\"Score\",{\"Score\",\"Score\";\">2\",\"<8\"})"), 10);
  near(eval("DCOUNT(Records,,{\"Group\";\"A\";\"B\"})"), 5);
  near(eval("DSUM(Records,\"Score\",I1:I2)"), 30);
  near(eval("DCOUNT(A1:D1048576,,I1:I2)"), 1048575);
  near(eval("DCOUNT(A1:D1048576,,{\"Group\";\"=\"})"), 1048569);
  near(eval("DCOUNT(Records,,{\"Score\";E50})"), 0);
  near(eval("DGET(Records,\"Score\",{\"Name\";\"beta\"})"), 4);
  auto text = eval("DGET(Records,\"Name\",{\"Name\";\"delta\"})");
  assert(text.kind == NativeValue::String && text.string == "delta" &&
         !text.incomplete);
  auto logical = eval("DGET(Records,\"Flag\",{\"Name\";\"beta\"})");
  assert(logical.isBoolean() && !logical.boolean);
  assert(eval("DGET(Records,\"Score\",{\"Name\";\"delta\"})").string ==
         "#VALUE!");
  assert(eval("DGET(Records,\"Score\",{\"Group\";\"A\"})").string == "#NUM!");
  for (auto f :
       {"DGET(Records,2,{\"Name\";\"absent\"})", "DSUM(Records,0,Selection)",
        "DCOUNT(Records,\"missing\",Selection)",
        "DSUM(Records,2,{\"missing\";0})", "DSUM(Records,2,\"Group\")"})
    assert(eval(f).isError() && !eval(f).incomplete);
  near(eval("DSUM({\"G\",\"N\";\"4\",2;4,3;TRUE,7},2,{\"G\";\">=4\"})"), 5);
  near(eval("DSUM({\"N\";\"4\";4;TRUE},1,{\"N\";\"<>text\"})"), 5);
  near(eval("DCOUNT({\"N\";\"4\";4;TRUE},1,{\"N\";\"<>text\"})"), 2);
  near(eval("DSUM({\"N\";1E308;1;-1E308},1,{\"N\";\"<>x\"})"), 1);
  near(eval("DAVERAGE({\"N\";1E308;1E308},1,{\"N\";\"<>x\"})"), 1E308);
  near(eval("DSTDEVP({\"N\";-1E308;1E308},1,{\"N\";\"<>x\"})"), 1E308);
  near(eval("DPRODUCT({\"N\";1E308;1E308;1E-308;1E-308},1,{\"N\";\">0\"})"), 1);
  near(eval("DPRODUCT({\"N\";5E-324;2},1,{\"N\";\">0\"})"),
       2 * std::numeric_limits<double>::denorm_min());
  for (auto f : {"DSUM", "DMIN", "DMAX", "DPRODUCT", "DCOUNT", "DCOUNTA"})
    near(eval(Text(f) + "(Records,2,{\"Name\";\"absent\"})"), 0);
  assert(eval("DVAR(Records,2,{\"Name\";\"absent\"})").string == "#DIV/0!");
  e.set(0, 16, 20, "Score");
  near(eval("DCOUNT(Records,P1:P25,Selection)"), 2);
  auto array = eval("DCOUNT(Records,{\"Score\",\"Flag\"},Selection)");
  assert(array.rows == 1 && array.columns == 2);
  near(array.at(0, 0), 2);
  near(array.at(0, 1), 3);
  e.set(0, 14, 1, "Calculated");
  e.set(0, 16, 1, "5");
  e.set(0, 14, 2, "=$B2>=$P$1");
  near(eval("DSUM(Records,2,N1:N2)"), 24);
  e.set(0, 13, 1, "Group");
  e.set(0, 13, 3, "A");
  near(eval("DSUM(Records,2,M1:N3)"), 26);
  e.set(0, 14, 2, "=IFERROR(NATIVE_MISSING(),TRUE())");
  assert(eval("DCOUNT(Records,,N1:N2)").incomplete);
  e.set(0, 14, 2, "=AND($B2>2,$B2<10)");
  near(eval("DSUM(Records,2,N1:N2)"), 18);
  e.set(0, 14, 2, "=DCOUNT($A$1:$D$7,\"Score\",$H$1:$H$2)>0");
  near(eval("DCOUNT(Records,,N1:N2)"), 6);
  e.set(0, 2, 4, "=IFERROR(NATIVE_MISSING(),6)");
  assert(eval("DSUM(Records,2,Selection)").incomplete);
  assert(eval("DCOUNT(Records,2,Selection)").incomplete);
  e.set(0, 2, 4, "=#N/A");
  assert(eval("DSUM(Records,2,Selection)").string == "#N/A");
  near(eval("DCOUNT(Records,2,Selection)"), 1);
  near(eval("DCOUNTA(Records,2,Selection)"), 2);
  e.addNamedRange("Database!A1:A2 Database!C1:C2", "UnsupportedUnion");
  assert(eval("IFERROR(SUM(UnsupportedUnion),77)").incomplete);
  e.set(0, 18, 1, "=Z999");
  near(e.value(0, 18, 1), 0);
  near(eval(R"(SUM(N({1,TRUE,"text"})))"), 2);
  assert(eval("N(#N/A)").string == "#N/A");
  assert(eval(R"(DSUM({"G","N";.01,2},2,{"G";">0,005"}))").incomplete);
  assert(eval(R"(DSUM({"G","N";"1,23",2},2,{"G";">0.5"}))").incomplete);
  NativeCalculationSettings partial;
  auto flags = partial.criteria();
  flags.whole_cell = false;
  partial.set_criteria(flags);
  SheetEngine partial_engine(partial);
  partial_engine.addNewSheet("Partial");
  partial_engine.set(0, 1, 1, R"(=DSUM({"G","N";"123",2},2,{"G";"1"}))");
  near(partial_engine.value(0, 1, 1), 0);
  // A definite query error stays the same for later fields even when the
  // original failed selection has consumed the work budget.
  CriteriaBudget tiny_budget;
  tiny_budget.remaining = 7;
  NativeDatabaseSource tiny_data;
  tiny_data.rows = 4;
  tiny_data.columns = 1;
  tiny_data.read = [](size_t r, size_t) {
    return r ? NativeValue::numeric(1) : NativeValue(Text("N"));
  };
  NativeDatabaseSource tiny_criteria;
  tiny_criteria.rows = 2;
  tiny_criteria.columns = 1;
  tiny_criteria.read = [](size_t, size_t) {
    return NativeValue(Text("Calculated"));
  };
  tiny_criteria.predicate = [](size_t, size_t) {
    return NativeDatabaseSource::Predicate([](size_t r) {
      if (r == 2)
        throw NativeValue::error("#DIV/0!");
      return NativeValue::logical(true);
    });
  };
  NativeDatabaseModel prepared(tiny_data, tiny_criteria,
                               NativeCalculationSettings(), tiny_budget);
  assert(prepared.calculate("DCOUNT", NativeValue::numeric(1)).string ==
         "#DIV/0!");
  assert(prepared.calculate("DSUM", NativeValue::numeric(1)).string ==
         "#DIV/0!");
  e.addNamedRange("of:=5", "ConstantExpression");
  near(eval("ConstantExpression"), 5);
  e.addNamedRange("of:=[.A1]", "RelativeExpression");
  assert(eval("IFERROR(RelativeExpression,77)").incomplete);
  TempDir temporary;
  assert(temporary.isValid());
  Workbook sparse;
  sparse.set(0, 1, 50, "17");
  sparse.set(0, 9, 100, "=A50+1");
  auto sparse_file = temporary.path() + "/sparse.ods";
  sparse.save(sparse_file);
  OdfPackage package(Kind::Sheets);
  package.load(sparse_file);
  for (auto row : package.body().elementsByTagNameNS(TABLE, "table-row"))
    assert(!children(row, TABLE, "table-cell").empty());
  Workbook reopened;
  reopened.load(sparse_file);
  near(reopened.value(0, 9, 100), 18);
  std::cout << "PASS twelve native database functions, exact aggregates, "
               "sparse geometry, arrays, calculated criteria and provenance\n";
}
