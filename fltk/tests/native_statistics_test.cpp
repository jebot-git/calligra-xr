// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_statistics.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
#include <limits>
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
    for (const auto &item : *value.items)
      out["items"].push_back(encode(item));
  }
  return out;
}
void near(const V &got, double expected, double tolerance = 2e-13) {
  if (!got.isNumber() || got.incomplete ||
      std::abs(got.number - expected) >
          tolerance * std::max(std::abs(expected), 1e-300))
    std::cerr << got.display().toStdString() << " != " << expected << '\n';
  assert(got.isNumber() && !got.incomplete);
  assert(std::abs(got.number - expected) <=
         tolerance * std::max(std::abs(expected), 1e-300));
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--document") {
    Workbook book;
    book.load(Text::fromUtf8(argv[2]));
    Json sheets = Json::array();
    for (int s = 0; s < book.sheet_count(); ++s) {
      Json rows = Json::array();
      for (int r = 1; r <= book.rows(s); ++r) {
        Json row = Json::array();
        for (int c = 1; c <= book.columns(s); ++c)
          row.push_back(encode(book.value(s, c, r)));
        rows.push_back(std::move(row));
      }
      sheets.push_back(std::move(rows));
    }
    book.save(Text::fromUtf8(argv[3]));
    std::cout << sheets.dump() << '\n';
    return 0;
  }
  SheetEngine engine;
  engine.addNewSheet("Statistics");
  auto eval = [&](const Text &formula) {
    engine.set(0, 20, 1, formula.startsWith('=') ? formula : "=" + formula);
    return engine.value(0, 20, 1);
  };
  if (argc == 2 && (std::string(argv[1]) == "--eval" ||
                    std::string(argv[1]) == "--retained-eval")) {
    if (std::string(argv[1]) == "--retained-eval") {
      for (auto [row, input] :
           std::vector<std::pair<int, Text>>{{3, "7"},
                                             {4, "2"},
                                             {5, "3"},
                                             {6, "TRUE"},
                                             {7, "Hello"},
                                             {9, "=1/0"},
                                             {10, "0"},
                                             {11, "3"},
                                             {12, "4"},
                                             {13, "2005-0131T01:00:00"},
                                             {14, "1"},
                                             {15, "2"},
                                             {16, "3"},
                                             {17, "4"}})
        engine.set(0, 2, row, input, row == 3 || row == 7 || row == 13);
      for (auto [row, input] : std::vector<std::pair<int, Text>>{{4, "4"},
                                                                 {5, "5"},
                                                                 {6, "7"},
                                                                 {11, "5"},
                                                                 {12, "6"},
                                                                 {13, "8"},
                                                                 {14, "4"},
                                                                 {15, "3"},
                                                                 {16, "2"},
                                                                 {17, "1"}})
        engine.set(0, 3, row, input);
    }
    std::string line;
    while (std::getline(std::cin, line))
      std::cout << encode(eval(Text(line))).dump() << '\n';
    return 0;
  }
  assert(statistics_signatures().size() == 14);
  for (const auto &[name, arity] : statistics_signatures()) {
    assert(arity.first == 1 && arity.second == 255);
    assert(eval(name + "()").string == "#VALUE!");
    assert(eval(name + "(NA())").string == "#N/A");
    assert(eval(name + "(NATIVE_MISSING())").incomplete);
  }
  for (const auto &[formula, expected] : std::vector<std::pair<Text, double>>{
           {"AVERAGEA({2,4;TRUE,\"text\"})", 1.75},
           {"AVERAGEA(\"7\",TRUE,FALSE,2)", .75},
           {"AVERAGEA(\"\")", 0},
           {"DEVSQ({2,4,6,8})", 20},
           {"DEVSQA({2,4,TRUE,\"text\"})", 8.75},
           {"VARA({2,4,TRUE,\"text\"})", 8.75 / 3},
           {"VARPA({2,4,TRUE,\"text\"})", 8.75 / 4},
           {"STDEVA({2,4,TRUE,\"text\"})", std::sqrt(8.75 / 3)},
           {"STDEVPA({2,4,TRUE,\"text\"})", std::sqrt(8.75 / 4)},
           {"AVEDEV({2,4,6,8})", 2},
           {"GEOMEAN({1,2,4,8})", std::sqrt(8)},
           {"HARMEAN({1,2,4})", 12. / 7},
           {"GEOMEAN({1E300,1E300})", 1e300},
           {"GEOMEAN({1E-300,1E-300})", 1e-300},
           {"GEOMEAN({1E300,1E-300})", 1},
           {"HARMEAN({1E308,1E308})", 1e308},
           {"HARMEAN({1E-300,1E300})", 2e-300},
           {"AVERAGEA({1E308,1E308})", 1e308},
           {"AVERAGEA({1E308,-1E308})", 0},
           {"AVERAGEA(1E308,1,-1E308)", 1. / 3},
           {"STDEVPA({1E308,-1E308})", 1e308},
           {"DEVSQ({1000000000000,1000000000001,1000000000002})", 2},
           {"AVEDEV({1000000000000,1000000000001,1000000000002})", 2. / 3},
           {"VARPA({1E308,1E308})", 0},
           {"DEVSQ(\"2\",TRUE,5)", 26. / 3},
           {"GEOMEAN(\"4\",TRUE)", 2},
           {"DEVSQ({1,TRUE,\"99\",3})", 2}})
    near(eval(formula), expected);
  for (const auto &[formula, expected] :
       std::vector<std::pair<Text, Text>>{{"GEOMEAN(0,2)", "#NUM!"},
                                          {"GEOMEAN(-1,-2)", "#NUM!"},
                                          {"HARMEAN(0,2)", "#NUM!"},
                                          {"HARMEAN(-1,2)", "#NUM!"},
                                          {"VARA(4)", "#DIV/0!"},
                                          {"STDEVA(4)", "#DIV/0!"},
                                          {"DEVSQ(\"words\",2)", "#VALUE!"},
                                          {"VARPA({1E308,-1E308})", "#NUM!"},
                                          {"DEVSQ({1,1/0})", "#DIV/0!"}}) {
    auto value = eval(formula);
    assert(value.isError() && value.string == expected && !value.incomplete);
  }
  engine.set(0, 1, 1, "2");
  engine.set(0, 1, 2, "4");
  engine.set(0, 1, 3, "TRUE");
  engine.set(0, 1, 4, "7", true);
  engine.set(0, 1, 5, "text", true);
  engine.set(0, 1, 6, "=\"\"");
  engine.addNamedRange("A1:A6", "Observations");
  near(eval("AVERAGEA(Observations)"), 7. / 6);
  near(eval("DEVSQ(A1:A6)"), 2);
  near(eval("GEOMEAN(A1:A6)"), std::sqrt(8));
  near(eval("AVERAGEA(A1:A1048576)"), 7. / 6);
  near(eval("STDEVPA(A1)"), 0);
  near(eval("VARPA(A1)"), 0);
  for (auto name : Text("AVERAGEA AVEDEV DEVSQ DEVSQA GEOMEAN HARMEAN "
                        "STDEVA STDEVPA VARA VARPA")
                       .split(' '))
    assert(eval(name + "(C1:C3)").string == "#DIV/0!");
  engine.set(0, 2, 1, "=AVERAGEA(A1:A6)");
  engine.set(0, 2, 2, "=B1*2");
  near(engine.value(0, 2, 2), 7. / 3);
  engine.set(0, 1, 1, "8");
  near(engine.value(0, 2, 2), 13. / 3);
  engine.set(0, 3, 1, "=NATIVE_MISSING()");
  assert(eval("IFERROR(AVERAGEA(C1:C3),77)").incomplete);
  assert(eval("AVERAGEA(T1)").string == "#CYCLE!");
  auto denormal = std::numeric_limits<double>::denorm_min();
  auto small = statistics_function("HARMEAN", {denormal, denormal});
  assert(small.isNumber() && small.number == denormal);
  std::vector<double> excessive(1'000'001, 1);
  assert(statistics_function("AVERAGEA", excessive).incomplete);
  TempDir temporary;
  Text directory = temporary.path();
  if (argc == 3 && std::string(argv[1]) == "--artifacts") {
    directory = Text::fromUtf8(argv[2]);
    Dir().mkpath(directory);
  }
  Workbook fresh;
  fresh.set(0, 21, 1, "=AVERAGEA(2,4,TRUE)");
  fresh.add_sheet("Empty");
  fresh.save(directory + "/new-statistics.ods");
  OdfPackage saved(Kind::Sheets);
  saved.load(directory + "/new-statistics.ods");
  auto tables = children(saved.body(), TABLE, "table");
  assert(children(tables[0], TABLE, "table-column")[0].attributeNS(
             TABLE, "number-columns-repeated") == "21");
  assert(children(tables[1], TABLE, "table-column")[0].attributeNS(
             TABLE, "number-columns-repeated") == "1");
  Workbook again;
  again.load(directory + "/new-statistics.ods");
  near(again.value(0, 21, 1), 7. / 3);
  std::cout << "PASS descriptive statistics, typed coercion, sparse ranges, "
               "dependencies, errors and extreme numeric scales\n";
}
