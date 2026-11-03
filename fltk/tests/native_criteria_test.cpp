// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_criteria.h"
#include "native_formula.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
namespace {
using V = NativeValue;
using Json = nlohmann::json;
Json encode(const V &v) {
  Json out = {{"kind", v.kind},
              {"display", v.display().toStdString()},
              {"incomplete", v.incomplete}};
  if (v.isNumber())
    out["value"] = v.number;
  if (v.isArray()) {
    out["rows"] = v.rows;
    out["columns"] = v.columns;
    out["items"] = Json::array();
    for (const auto &item : *v.items)
      out["items"].push_back(encode(item));
  }
  return out;
}
void near(const V &v, double expected) {
  if (!v.isNumber() || v.incomplete ||
      std::abs(v.number - expected) > 1e-12 * std::max(1., std::abs(expected)))
    std::cerr << v.display().toStdString() << " != " << expected << '\n';
  assert(v.isNumber() && !v.incomplete);
  assert(std::abs(v.number - expected) <=
         1e-12 * std::max(1., std::abs(expected)));
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc >= 3 && std::string(argv[1]) == "--fods") {
    Json records = Json::array();
    for (int file = 2; file < argc; ++file) {
      File input(Text::fromUtf8(argv[file]));
      assert(input.open(IO::ReadOnly));
      OdfPackage package(Kind::Sheets);
      package.xml = read_document_xml(input.readAll());
      // Three retained LibreOffice fixtures have styled, empty repetitions
      // beyond the sheet limit. Trim only those empty tail slots in this test
      // copy; reject any actual data beyond the limit and retain all formulas.
      for (auto table : children(package.body(), TABLE, "table")) {
        Int64 row = 1;
        for (auto element : children(table, TABLE, "table-row")) {
          int repeat =
              element.attributeNS(TABLE, "number-rows-repeated", "1").toInt();
          if (row + repeat - 1 > KS_rowMax) {
            for (auto cell : children(element, TABLE)) {
              assert(!cell.hasAttributeNS(TABLE, "formula") &&
                     !cell.hasAttributeNS(OFFICE, "value-type"));
              assert(plain_text(cell).trimmed().empty());
            }
            if (row > KS_rowMax)
              table.removeChild(element);
            else
              element.setAttributeNS(TABLE, "table:number-rows-repeated",
                                     int(KS_rowMax - row + 1));
          }
          row += repeat;
        }
      }

      TempDir temp;
      auto path = temp.path() + "/source.ods";
      package.save(path);
      Workbook book;
      book.load(path);
      auto tables = children(package.body(), TABLE, "table");
      for (int sheet = 0; sheet < tables.size(); ++sheet) {
        int row = 1;
        for (auto xml_row : children(tables[sheet], TABLE, "table-row")) {
          int repeats =
              xml_row.attributeNS(TABLE, "number-rows-repeated", "1").toInt();
          int column = 1;
          for (auto cell : children(xml_row, TABLE)) {
            if (cell.localName() != "table-cell" &&
                cell.localName() != "covered-table-cell")
              continue;
            int columns =
                cell.attributeNS(TABLE, "number-columns-repeated", "1").toInt();
            auto formula = cell.attributeNS(TABLE, "formula");
            bool selected = false;
            if (!formula.empty()) {
              auto decoded = Formula::decode(formula).toUpper();
              for (auto name :
                   Text("COUNTIF COUNTIFS SUMIF SUMIFS AVERAGEIF AVERAGEIFS")
                       .split(' '))
                selected = selected || decoded.startsWith("=" + name + "(");
            }
            if (selected) {
              assert(repeats * columns < 10000);
              for (int r = 0; r < repeats; ++r)
                for (int c = 0; c < columns; ++c) {
                  auto cached = cell.attributeNS(OFFICE, "value-type");
                  Text text;
                  for (auto p : children(cell, TEXT, "p"))
                    text += plain_text(p);
                  bool failure =
                      cached == "error" ||
                      cell.attributeNS("urn:org:documentfoundation:names:"
                                       "experimental:calc:xmlns:calcext:1.0",
                                       "value-type") == "error";
                  Json record = {{"file", argv[file]},
                                 {"sheet", sheet},
                                 {"row", row + r},
                                 {"column", column + c},
                                 {"formula", formula.toStdString()},
                                 {"expected_type", cached.toStdString()},
                                 {"expected_text", text.toStdString()},
                                 {"expected_error", failure},
                                 {"actual", encode(book.value(sheet, column + c,
                                                              row + r))}};
                  if (cached == "float")
                    record["expected_value"] =
                        cell.attributeNS(OFFICE, "value").toDouble();
                  records.push_back(std::move(record));
                }
            }
            column += columns;
          }
          row += repeats;
        }
      }
    }
    std::cout << records.dump() << '\n';
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--document") {
    Workbook book;
    book.load(Text::fromUtf8(argv[2]));
    Json sheets = Json::array();
    for (int sheet = 0; sheet < book.sheet_count(); ++sheet) {
      Json rows = Json::array();
      for (int row = 1; row <= book.rows(sheet); ++row) {
        Json cells = Json::array();
        for (int column = 1; column <= book.columns(sheet); ++column)
          cells.push_back(encode(book.value(sheet, column, row).scalar()));
        rows.push_back(std::move(cells));
      }
      sheets.push_back(std::move(rows));
    }
    book.save(Text::fromUtf8(argv[3]));
    std::cout << sheets.dump() << '\n';
    return 0;
  }
  NativeCalculationSettings settings;
  NativeCriteriaSettings criteria;
  criteria.case_sensitive = false;
  settings.set_criteria(criteria);
  SheetEngine engine(settings);
  engine.addNewSheet("Data");
  engine.addNewSheet("Other");
  for (int row = 1; row <= 4; ++row) {
    engine.set(0, 1, row, Text::number(row));
    engine.set(0, 2, row, Text::number(row * 10));
    engine.set(0, 3, row, row % 2 ? "Red" : "blue", true);
  }
  engine.set(0, 1, 5, "7", true);
  engine.set(0, 1, 6, "TRUE");
  engine.set(0, 1, 7, "=\"\"");
  engine.set(0, 2, 5, "=1/0");
  engine.set(1, 1, 1, "Red", true);
  engine.addNamedRange("Data!A1:A4", "Numbers");
  auto eval = [&](const Text &formula) {
    engine.set(0, 20, 20, formula.startsWith('=') ? formula : "=" + formula);
    return engine.value(0, 20, 20);
  };
  if (argc == 2 && std::string(argv[1]) == "--eval") {
    std::string line;
    while (std::getline(std::cin, line))
      std::cout << encode(eval(Text(line))).dump() << '\n';
    return 0;
  }
  assert(conditional_signatures().size() == 6);
  for (auto [formula, expected] : std::vector<std::pair<Text, double>>{
           {"COUNTIF(A1:A4,\">2\")", 2},
           {"COUNTIF({1,2},1)", 1},
           {"SUMIF({-10;10;20;30},\">0\")", 60},
           {"COUNTIF(A1:A4,\"[\")", 0},
           {"SUMIF(A1:A4,\">2\")", 7},
           {"AVERAGEIF(A1:A4,\">2\")", 3.5},
           {"SUMIF(A1:A4,\">2\",B1)", 70},
           {"AVERAGEIF(A1:A4,\">2\",B1:B2)", 35},
           {"COUNTIFS(A1:A4,\">1\",C1:C4,\"red\")", 1},
           {"SUMIFS(B1:B4,A1:A4,\">1\",C1:C4,\"RED\")", 30},
           {"AVERAGEIFS(B1:B4,A1:A4,\">1\",C1:C4,\"RED\")", 30},
           {"SUMIF(C1:C4,Other!A1,B1)", 40},
           {"COUNTIF(Numbers,\">2\")", 2},
           {"SUMIF(OFFSET(A1,0,0,4,1),\">2\",B1)", 70},
           {"COUNTIF(INDIRECT(\"A1:A4\"),\">2\")", 2},
           {"COUNTIF(INDEX(A1:A4,0,1),\">2\")", 2},
           {"COUNTIF(A1:A10,\"=\")", 4},
           {"COUNTIF(A1:A10,\"<>\")", 6},
           {"COUNTIF(A1:A10,\"=0\")", 0},
           {"COUNTIF(A1:A10,\"<>0\")", 10},
           {"COUNTIF(A1:A10,0)", 0},
           {"COUNTIF(A1:A10,D1)", 0},
           {"COUNTIF(A1:A10,\"7\")", 1},
           {"COUNTIF(A1:A10,\"=7\")", 1},
           {"COUNTIF(A1:A10,7)", 0},
           {"COUNTIF(A1:A10,\">5\")", 0},
           {"COUNTIF(D1:D1048576,\"\")", 1048576},
           {"COUNTIFS(D1:D1048576,\"\",E1:E1048576,\"\")", 1048576},
           {"SUMIF(A1:A4,\">100\",B1)", 0},
           {"COUNTIF(C1:C4,\"r.*\")", 2},
           {"_xlfn.COUNTIFS(A1:A4,\">2\")", 2},
           {"COM.MICROSOFT.SUMIFS(B1:B4,A1:A4,\">2\")", 70}})
    near(eval(formula), expected);
  for (auto [formula, expected] : std::vector<std::pair<Text, Text>>{
           {"SUMIF(1,1)", "#VALUE!"},
           {"SUMIFS(B1:B4,A1:A3,\">0\")", "#VALUE!"},
           {"COUNTIFS(A1:A4,\">0\",C1:C3,\"red\")", "#VALUE!"},
           {"COUNTIFS(A1:A4,1,C1:C4)", "#VALUE!"},
           {"SUMIFS(B1:B4,A1:A4,1,7)", "#VALUE!"},
           {"COUNTIF(A1:A4,C1:C4)", "#VALUE!"},
           {"AVERAGEIF(A1:A4,\">100\")", "#DIV/0!"},
           {"SUMIF(A1:A5,\"7\",B1)", "#DIV/0!"},
           {"COUNTIF(A1:A4,NA())", "#N/A"}}) {
    auto v = eval(formula);
    assert(v.isError() && v.string == expected && !v.incomplete);
  }
  auto array = eval("COUNTIF(A1:A4,{\">1\",\">2\"})");
  assert(array.isArray() && array.rows == 1 && array.columns == 2);
  near(array.at(0, 0), 3);
  near(array.at(0, 1), 2);
  engine.set(0, 5, 1, "=SUMIF(A1:A4,{\">1\",\">2\"},B1)");
  engine.define_matrix(0, 5, 1, 2, 1);
  near(engine.value(0, 5, 1), 90);
  near(engine.value(0, 6, 1), 70);
  engine.set(0, 1, 4, "8");
  near(engine.value(0, 5, 1), 90);
  engine.set(0, 2, 4, "80");
  near(engine.value(0, 6, 1), 110);
  engine.set(0, KS_colMax, KS_rowMax, "9");
  Text last_column;
  for (int column = KS_colMax; column; column = (column - 1) / 26)
    last_column = Text(1, char32_t('A' + (column - 1) % 26)) + last_column;
  auto last = last_column + Text::number(KS_rowMax);
  near(eval("SUMIF(A1:B2,\">0\"," + last + ")"), 9);
  near(eval("AVERAGEIF(A1:B2,\">0\"," + last + ")"), 9);
  engine.set(0, 4, 10, "PT00H01M26.47S");
  near(eval("COUNTIF(D10,D10)"), 1);
  near(eval("COUNTIF(D10,\"=\"&D10)"), 0);
  engine.set(0, 4, 7, "=NA()");
  near(eval("COUNTIF(D7:D8,\"#N/A\")"), 1);
  near(eval("COUNTIF(D7:D8,\"<>\")"), 1);
  near(eval("COUNTIF(D7:D8,\"<>#N/A\")"), 1);
  engine.set(0, 4, 1, "=NATIVE_MISSING()");
  assert(eval("COUNTIF(D1:D5,\"x\")").incomplete);
  assert(eval("IFERROR(SUMIF(A1:A4,NATIVE_MISSING()),99)").incomplete);
  assert(eval("COUNTIF(T20,1)").string == "#CYCLE!");
  {
    NativeCriteriaSettings mode;
    mode.regular_expressions = false;
    mode.wildcards = true;
    CriteriaBudget budget;
    NativeCriterion star(V("a~*?"), mode, budget);
    assert(star.matches(V("a*𐐀")) && !star.matches(V("a*bcd")));
    NativeCriterion newline(V("*"), mode, budget);
    assert(newline.matches(V("a\nb")));
    assert(newline.matches(V("")) && !newline.matches(V()));
    NativeCriterion tilde(V("~~"), mode, budget);
    assert(tilde.matches(V("~")));
    mode.wildcards = false;
    mode.whole_cell = false;
    mode.case_sensitive = false;
    NativeCriterion substring(V("čes"), mode, budget);
    assert(substring.matches(V("Český")));
    mode.whole_cell = true;
    mode.case_sensitive = true;
    NativeCriterion literal(V("*"), mode, budget);
    assert(!literal.matches(V("word")));
    NativeCriterion sensitive(V("Red"), mode, budget);
    assert(!sensitive.matches(V("red")));
    auto epoch = std::chrono::sys_days(std::chrono::year(1904) / 1 / 1);
    NativeCriterion date(V(">=1904-01-02"), mode, budget, epoch);
    assert(date.matches(V::numeric(1)) && !date.matches(V::numeric(0)));
    NativeCriterion us(V("<1/3/1904"), mode, budget, epoch);
    assert(us.matches(V::numeric(1)) && !us.matches(V::numeric(2)));
    mode.regular_expressions = true;
    bool compile_limited = false;
    try {
      NativeCriterion huge(V("a{999999999999999999999}"), mode, budget);
    } catch (const V &v) {
      compile_limited = v.incomplete && v.string == "#LIMIT!";
    }
    assert(compile_limited);
    NativeCriterion costly(V("(a+)+$"), mode, budget);
    bool limited = false;
    try {
      costly.matches(V(Text(20000, 'a') + "!"));
    } catch (const V &v) {
      limited = v.incomplete && v.string == "#LIMIT!";
    }
    assert(limited);
  }
  std::cout << "PASS conditional criteria, sparse paired ranges, arrays, "
               "fields, dependencies, errors and regex limits\n";
}
