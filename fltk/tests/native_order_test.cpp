// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_order.h"
#include "native_formula.h"
#include "native_distribution.h"
#include "native_inference.h"
#include "native_regression.h"
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
      std::abs(v.number - expected) >
          1e-12 * std::max(1e-300, std::abs(expected)))
    std::cerr << v.display().toStdString() << " != " << expected << '\n';
  assert(v.isNumber() && !v.incomplete);
  assert(std::abs(v.number - expected) <=
         1e-12 * std::max(1e-300, std::abs(expected)));
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc >= 3 && (std::string(argv[1]) == "--regression-fods" ||
                    std::string(argv[1]) == "--inference-fods" ||
                    std::string(argv[1]) == "--distribution-fods" ||
                    std::string(argv[1]) == "--fods" ||
                    std::string(argv[1]) == "--inspect-fods")) {
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
        struct SelectedMatrix {
          int left, top, right, bottom;
          Text formula;
        };
        std::vector<SelectedMatrix> matrices;
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
              if (std::string(argv[1]) == "--regression-fods") {
                auto end = decoded.indexOf('(');
                selected =
                    end > 1 && regression_signatures().contains(
                                   regression_name(decoded.mid(1, end - 1)));
              } else if (std::string(argv[1]) == "--inference-fods") {
                auto end = decoded.indexOf('(');
                selected =
                    end > 1 && inference_signatures().contains(
                                   inference_name(decoded.mid(1, end - 1)));
              } else if (std::string(argv[1]) == "--distribution-fods") {
                auto end = decoded.indexOf('(');
                selected =
                    end > 1 && distribution_signatures().contains(
                                   distribution_name(decoded.mid(1, end - 1)));
              } else
                for (
                    auto name :
                    Text(
                        "CORREL PEARSON COVAR COVARIANCE.P COVARIANCE.S SLOPE "
                        "INTERCEPT RSQ STEYX FORECAST FORECAST.LINEAR SUMX2MY2 "
                        "SUMX2PY2 SUMXMY2 SUM2XMY COM.MICROSOFT.COVARIANCE.P "
                        "COM.MICROSOFT.COVARIANCE.S "
                        "SKEW SKEWP SKEW.P KURT KURTP TRIMMEAN STANDARDIZE "
                        "SMALL LARGE RANK RANK.EQ RANK.AVG MEDIAN MODE "
                        "MODE.SNGL MODE.MULT PERCENTILE PERCENTILE.INC "
                        "PERCENTILE.EXC PERCENTRANK PERCENTRANK.INC "
                        "PERCENTRANK.EXC QUARTILE QUARTILE.INC QUARTILE.EXC "
                        "COM.MICROSOFT.RANK.EQ COM.MICROSOFT.RANK.AVG "
                        "COM.MICROSOFT.MODE.SNGL COM.MICROSOFT.MODE.MULT "
                        "COM.MICROSOFT.PERCENTILE.INC "
                        "COM.MICROSOFT.PERCENTILE.EXC "
                        "COM.MICROSOFT.PERCENTRANK.INC "
                        "COM.MICROSOFT.PERCENTRANK.EXC "
                        "COM.MICROSOFT.QUARTILE.INC COM.MICROSOFT.QUARTILE.EXC")
                        .split(' '))
                  selected = selected || decoded.startsWith("=" + name + "(");
            }
            if (selected &&
                cell.hasAttributeNS(TABLE, "number-matrix-rows-spanned")) {
              matrices.push_back(
                  {column, row,
                   column +
                       cell.attributeNS(TABLE, "number-matrix-columns-spanned",
                                        "1")
                           .toInt() -
                       1,
                   row +
                       cell.attributeNS(TABLE, "number-matrix-rows-spanned",
                                        "1")
                           .toInt() -
                       1,
                   formula});
            }
            bool matrix_child = false;
            if (!selected)
              for (const auto &matrix : matrices)
                if (column >= matrix.left && column <= matrix.right &&
                    row >= matrix.top && row <= matrix.bottom) {
                  selected = matrix_child = true;
                  formula = matrix.formula;
                  break;
                }
            selected =
                selected || (std::string(argv[1]) == "--inspect-fods" &&
                             (cell.hasAttributeNS(OFFICE, "value-type") ||
                              !formula.empty()));
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
                  Json record = {
                      {"file", argv[file]},
                      {"sheet", sheet},
                      {"row", row + r},
                      {"column", column + c},
                      {"formula", formula.toStdString()},
                      {"matrix_child", matrix_child},
                      {"expected_type", cached.toStdString()},
                      {"expected_text", text.toStdString()},
                      {"expected_error", failure},
                      {"actual",
                       encode(
                           book.value(sheet, column + c, row + r).scalar())}};
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
  SheetEngine engine;
  engine.addNewSheet("Order");
  engine.addNewSheet("Other");
  for (int row = 1; row <= 5; ++row)
    engine.set(0, 1, row, Text::number(row * 2));
  engine.addNamedRange("Order!A1:A5", "Samples");
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
  assert(order_signatures().size() == 15);
  for (auto [formula, expected] : std::vector<std::pair<Text, double>>{
           {"SMALL(A1:A5,2)", 4},
           {"LARGE(A1:A5,2)", 8},
           {"SMALL(A1:A5,2.9)", 4},
           {"LARGE(A1:A5,2.9)", 8},
           {"SMALL(Samples,5)", 10},
           {"SMALL(OFFSET(A1,0,0,5,1),1)", 2},
           {"LARGE(INDIRECT(\"A1:A5\"),1)", 10},
           {"SMALL(A1:A1048576,5)", 10},
           {"PERCENTILE(A1:A5,0)", 2},
           {"PERCENTILE(A1:A5,1)", 10},
           {"PERCENTILE(A1:A5,.25)", 4},
           {"PERCENTILE(A1:A5,.3)", 4.4},
           {"PERCENTILE.EXC(A1:A5,.25)", 3},
           {"PERCENTILE.EXC(A1:A5,.5)", 6},
           {"PERCENTILE.EXC(A1:A5,1/6)", 2},
           {"PERCENTILE.EXC(A1:A5,5/6)", 10},
           {"QUARTILE(A1:A5,1.9)", 4},
           {"QUARTILE.EXC(A1:A5,1.9)", 3},
           {"RANK(4,A1:A5)", 4},
           {"RANK(4,A1:A5,1)", 2},
           {"RANK(4,A1:A5,.5)", 2},
           {"RANK.AVG(2,{1,2,2,4})", 2.5},
           {"RANK.EQ(2,{1,2,2,4})", 2},
           {"PERCENTRANK(A1:A5,4)", .25},
           {"PERCENTRANK(A1:A5,5,4)", .375},
           {"PERCENTRANK.EXC(A1:A5,4)", .333},
           {"PERCENTRANK.EXC({1,2,3,6,6,6,7,8,9},5.43)", .381},
           {"PERCENTRANK.EXC({1,2,3,6,6,6,7,8,9},5.43,1)", .3},
           {"PERCENTRANK.INC({13,12,11,8,4,3,2,1,1,1},4)", .555},
           {"PERCENTRANK({1,2,2,4},2)", .333},
           {"PERCENTRANK({1,2,2,4},3)", .833},
           {"PERCENTRANK({5},5)", 1},
           {"PERCENTRANK({0,1},1E-20,16)", 0},
           {"PERCENTRANK({0,1},1E-20,20)", 1E-20},
           {"PERCENTRANK.EXC({5},5)", .5},
           {"PERCENTILE.EXC({5},.5)", 5},
           {"MODE({1,1,2,2})", 1},
           {"MODE(5,5)", 5},
           {"MODE({4,4,1,1})", 1},
           {"MODE.SNGL({4,4,1,1})", 4},
           {"MODE.SNGL({1,1,2,3})", 1},
           {"MEDIAN({1E308,1E308})", 1e308},
           {"PERCENTILE({-1E308,1E308},.5)", 0},
           {"PERCENTILE({1E308,1E308},.5)", 1e308},
           {"PERCENTILE({1E-300,2E-300},.5)", 1.5e-300},
           {"_xlfn.PERCENTILE.INC(A1:A5,.5)", 6},
           {"COM.MICROSOFT.QUARTILE.EXC(A1:A5,2)", 6},
           {"SMALL({1,TRUE,\"7\",4},2)", 4},
           {"SMALL(\"4\",1)", 4}})
    near(eval(formula), expected);
  for (auto [formula, expected] :
       std::vector<std::pair<Text, Text>>{{"SMALL(A1:A5,0)", "#NUM!"},
                                          {"SMALL(A1:A5,6)", "#NUM!"},
                                          {"LARGE(A1:A5,0)", "#NUM!"},
                                          {"LARGE(A1:A5,6)", "#NUM!"},
                                          {"PERCENTILE(A1:A5,-.1)", "#NUM!"},
                                          {"PERCENTILE(A1:A5,1.1)", "#NUM!"},
                                          {"PERCENTILE.EXC(A1:A5,.1)", "#NUM!"},
                                          {"PERCENTILE.EXC(A1:A5,.9)", "#NUM!"},
                                          {"QUARTILE.EXC(A1:A5,0)", "#NUM!"},
                                          {"QUARTILE.EXC(A1:A5,4)", "#NUM!"},
                                          {"RANK(3,A1:A5)", "#N/A"},
                                          {"PERCENTRANK(A1:A5,1)", "#N/A"},
                                          {"PERCENTRANK(A1:A5,4,0)", "#NUM!"},
                                          {"MODE({1,2,3})", "#N/A"},
                                          {"SMALL({1,NA()},1)", "#N/A"},
                                          {"LARGE(A1:A5,NA())", "#N/A"},
                                          {"SMALL(A1:A5)", "#VALUE!"},
                                          {"RANK(1,2,3,4)", "#VALUE!"}}) {
    auto v = eval(formula);
    assert(v.isError() && v.string == expected && !v.incomplete);
  }
  auto precise = eval("PERCENTRANK.EXC({1,2,3,4,5},1,16)");
  assert(precise.isNumber() && precise.number == .1666666666666666);
  auto array = eval("SMALL(A1:A5,{1,5;2,4})");
  assert(array.isArray() && array.rows == 2 && array.columns == 2);
  near(array.at(0, 0), 2);
  near(array.at(0, 1), 10);
  near(array.at(1, 0), 4);
  near(array.at(1, 1), 8);
  auto modes = eval("MODE.MULT({2,1,1,2,3})");
  assert(modes.isArray() && modes.rows == 2 && modes.columns == 1);
  near(modes.at(0, 0), 2);
  near(modes.at(1, 0), 1);
  engine.set(1, 1, 1, "=MODE.MULT({2,1,1,2,3})");
  engine.define_matrix(1, 1, 1, 2, 4);
  near(engine.value(1, 1, 1), 2);
  near(engine.value(1, 1, 2), 1);
  assert(engine.value(1, 1, 3).string == "#N/A");
  assert(engine.value(1, 2, 1).string == "#N/A");
  auto mixed = eval("SMALL(A1:A5,{0,1,6})");
  assert(mixed.at(0, 0).isError() && mixed.at(0, 2).isError());
  near(mixed.at(0, 1), 2);
  engine.set(0, 2, 1, "=SMALL(A1:A5,{1,5})");
  engine.define_matrix(0, 2, 1, 2, 1);
  near(engine.value(0, 2, 1), 2);
  near(engine.value(0, 3, 1), 10);
  engine.set(0, 1, 5, "20");
  near(engine.value(0, 3, 1), 20);
  engine.set(0, 4, 1, "=NATIVE_MISSING()");
  assert(eval("SMALL(D1:D2,1)").incomplete);
  assert(eval("IFERROR(LARGE(D1:D2,1),77)").incomplete);
  assert(eval("SMALL(T20,1)").string == "#CYCLE!");
  for (auto name : Text("PERCENTRANK PERCENTRANK.EXC").split(' ')) {
    auto v = eval(name + "(A1:A5,4,1E300)");
    assert(v.isNumber() && !v.incomplete);
  }
  bool limited = false;
  try {
    NativeOrderData data(std::vector<double>(1'000'001, 1));
  } catch (const V &v) {
    limited = v.incomplete && v.string == "#LIMIT!";
  }
  assert(limited);
  std::cout << "PASS native order statistics, typed datasets, arrays, ties, "
               "endpoints, sparse ranges, errors and dependencies\n";
}
