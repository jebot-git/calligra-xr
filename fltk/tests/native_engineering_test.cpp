// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_engineering.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
namespace {
using V = NativeValue;
using Json = nlohmann::json;
Json encode(const V &value) {
  static const char *names[] = {"empty", "number", "boolean", "string",
                                "error", "array",  "complex"};
  Json out = {{"kind", names[value.kind]},
              {"display", value.display().toStdString()},
              {"incomplete", value.incomplete}};
  if (value.isNumber())
    out["real"] = value.number;
  if (value.isComplex()) {
    out["imag"] = value.imaginary;
    out["suffix"] = std::string(1, value.complex_suffix);
  }
  if (value.isBoolean())
    out["boolean"] = value.boolean;
  if (value.isArray()) {
    out["rows"] = value.rows;
    out["columns"] = value.columns;
    out["items"] = Json::array();
    for (const auto &v : *value.items)
      out["items"].push_back(encode(v));
  }
  return out;
}
void near(double a, double b, double tolerance = 1e-12) {
  if (std::abs(a - b) > tolerance * std::max(1., std::abs(b)))
    std::cerr << a << " != " << b << '\n';
  assert(std::abs(a - b) <= tolerance * std::max(1., std::abs(b)));
}
void fixture(const Text &path) {
  Workbook book;
  book.set(0, 1, 1, "3");
  for (auto [column, formula] : std::vector<std::pair<int, Text>>{
           {2, "=COMPLEX(A1,4,\"j\")"},
           {3, "=IMABS(B1)"},
           {4, "=CONVERT(A1,\"m\",\"cm\")"},
           {5, "=DEC2HEX(A1+12)"},
           {6, "=IMREAL(IMPRODUCT(B1,COMPLEX(1,-1)))"},
           {7, "=BESSELJ(A1,2)"}})
    book.set(0, column, 1, formula);
  book.save(path);
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc == 3 && std::string(argv[1]) == "--fixture") {
    fixture(Text::fromUtf8(argv[2]));
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--document") {
    Workbook book;
    book.load(Text::fromUtf8(argv[2]));
    Json rows = Json::array();
    for (int row = 1; row <= 2; ++row) {
      Json cells = Json::array();
      for (int col = 1; col <= 14; ++col)
        cells.push_back(encode(book.value(0, col, row)));
      rows.push_back(std::move(cells));
    }
    std::cout << rows.dump() << '\n';
    return 0;
  }
  SheetEngine engine;
  engine.addNewSheet("Engineering");
  auto eval = [&](const Text &expression) {
    engine.set(0, 2, 1,
               expression.startsWith('=') ? expression : "=" + expression);
    return engine.value(0, 2, 1);
  };
  if (argc == 2 && std::string(argv[1]) == "--eval") {
    std::string input;
    while (std::getline(std::cin, input))
      std::cout << encode(eval(Text(input))).dump() << '\n';
    return 0;
  }
  assert(engineering_signatures().size() == 50);
  for (auto [formula, expected] : std::vector<std::pair<Text, Text>>{
           {"BASE(31,16,4)", "001F"},
           {"BASE(12)", "12"},
           {"DECIMAL(\"0xFFh\",16)", "255"},
           {"BIN2DEC(1111111111)", "-1"},
           {"BIN2HEX(1111111111)", "FFFFFFFFFF"},
           {"DEC2BIN(-512)", "1000000000"},
           {"DEC2OCT(-1,2)", "7777777777"},
           {"DEC2HEX(-1)", "FFFFFFFFFF"},
           {"HEX2DEC(\"8000000000\")", "-549755813888"},
           {"HEX2BIN(\"FFFFFFFFFF\")", "1111111111"},
           {"OCT2DEC(7777777777)", "-1"},
           {"OCT2HEX(12)", "A"},
           {"OCT2BIN(12,5)", "01010"},
           {"HEX2OCT(\"FF\")", "377"},
           {"BIN2OCT(1010)", "12"},
           {"DELTA(1,1)", "1"},
           {"GESTEP(-1)", "0"},
           {"GESTEP(2,1)", "1"},
           {"BESSELJ(0,0)", "1"},
           {"BESSELI(0,1)", "0"},
           {"CONVERT(1,\"km2\",\"m2\")", "1000000"},
           {"CONVERT(1,\"mm3\",\"m3\")", "1e-09"},
           {"CONVERT(1,\"KiB\",\"bit\")", "#N/A"},
           {"CONVERT(1,\"Kibyte\",\"bit\")", "8192"},
           {"CONVERT(1,\"μm\",\"m\")", "1e-06"},
           {"ISNUMBER(COMPLEX(1,2))", "TRUE"},
           {"ISTEXT(COMPLEX(1,2))", "FALSE"},
           {"SUM(COMPLEX(3,4),2)", "5"},
           {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBIN2DEC(1010)", "10"},
           {"IMREAL(_xlfn.IMCOSH(COMPLEX(0,0)))", "1"}}) {
    auto result = eval(formula);
    if (result.display() != expected)
      std::cerr << formula.toStdString() << ": "
                << result.display().toStdString() << " expected "
                << expected.toStdString() << '\n';
    assert(result.display() == expected);
  }
  for (auto formula :
       {"BIN2DEC(2)", "DEC2BIN(512)", "DEC2HEX(-549755813889)", "DEC2BIN(1,0)",
        "HEX2DEC(\"G\")", "BASE(1,1)", "DECIMAL(\"2\",2)", "BESSELY(0,1)",
        "BESSELK(-1,1)", "BESSELJ(1,-1)", "IMREAL(\"bad\")", "IMLN(0)"})
    assert(eval(formula).isError());
  assert(eval("BESSELJ(1,1000001)").incomplete);
  near(eval("ERF(0.5)").number, .5204998778130465);
  near(eval("ERFC(3)").number, .00002209049699858544);
  assert(eval("ERF(8,9)").number > 1e-30);
  near(eval("ERF(2,1)").number, -eval("ERF(1,2)").number);
  near(eval("CONVERT(32,\"C\",\"F\")").number, 89.6);
  near(eval("CONVERT(273.15,\"K\",\"C\")").number, 0, 1e-10);
  near(eval("CONVERT(1,\"pweight\",\"g\")").number, 1.55517384);
  near(eval("CONVERT(1,\"MTON\",\"ft3\")").number, 40);
  near(eval("CONVERT(1,\"GRT\",\"ft3\")").number, 100);
  near(eval("CONVERT(1,\"flb\",\"J\")").number, 1.3558179483314004);
  near(eval("CONVERT(1,\"admkn\",\"m/s\")").number, 6080 * .3048 / 3600);
  near(eval("CONVERT(1,\"mK\",\"K\")").number, .001);
  near(eval("CONVERT(300,\"K\",\"kK\")").number, .3);
  near(eval("CONVERT(1,\"cm^3\",\"m3\")").number, 1e-6);
  for (auto formula :
       {"CONVERT(1,\"min\",\"ft\")", "CONVERT(1,\"Kibit\",\"m\")",
        "CONVERT(1,\"Kig\",\"g\")", "CONVERT(1,\"mft\",\"m\")",
        "CONVERT(1,\"mC\",\"C\")", "IMPRODUCT(\"bad\",2)", "GESTEP(\"1\",0)",
        "GESTEP(TRUE,0)", "IMPOWER(0,0)", "IMPOWER(0,2)"})
    assert(eval(formula).isError());
  auto z = eval("IMPRODUCT(COMPLEX(3,4,\"j\"),COMPLEX(1,-1,\"j\"))");
  assert(z.isComplex() && z.complex_suffix == 'j');
  near(z.number, 7);
  near(z.imaginary, 1);
  z = eval("IMSQRT(\"-4\")");
  assert(z.isComplex());
  near(z.number, 0);
  near(z.imaginary, 2);
  z = eval("IMSUM({\"1+2i\",\"bad\";3,TRUE})");
  assert(z.isComplex());
  near(z.number, 4);
  near(z.imaginary, 2);
  z = eval("IMREAL(IMDIV({\"2+4i\",\"4+8i\"},2))");
  assert(z.isArray() && z.rows == 1 && z.columns == 2);
  near(z.at(0, 0).number, 1);
  near(z.at(0, 1).number, 2);
  engine.set(0, 1, 1, "=COMPLEX(2,3)");
  engine.set(0, 1, 2, "TRUE");
  engine.set(0, 1, 3, "5");
  z = eval("IMSUM(A1:A3)");
  assert(z.isComplex());
  near(z.number, 7);
  near(z.imaginary, 3);
  assert(eval("IMREAL(IFERROR(NOT_A_FUNCTION(),COMPLEX(2,3)))").incomplete);
  std::cout
      << "PASS engineering functions, complex values/coercion, namespaces, "
         "typed arrays, ranges and error/resource semantics\n";
  TempDir temp;
  auto path = temp.path() + "/engineering.ods";
  fixture(path);
  Workbook reopened;
  reopened.load(path);
  assert(reopened.value(0, 2, 1).isComplex());
  near(reopened.value(0, 3, 1).number, 5);
  reopened.set(0, 1, 1, "0");
  reopened.save(path);
  auto package = read_document_archive(path);
  auto xml = read_document_xml(package.value("content.xml"));
  auto table = child(child(child(xml.documentElement(), OFFICE, "body"), OFFICE,
                           "spreadsheet"),
                     TABLE, "table");
  auto row = child(table, TABLE, "table-row");
  auto cells = children(row, TABLE, "table-cell");
  assert(cells[1].attributeNS(OFFICE, "value-type") == "string" &&
         cells[1].attributeNS(OFFICE, "string-value") == "0+4j");
  assert(cells[2].attributeNS(OFFICE, "value-type") == "float" &&
         cells[2].attributeNS(OFFICE, "value") == "4");
  reopened.load(path);
  assert(reopened.value(0, 2, 1).isComplex());
  near(reopened.value(0, 6, 1).number, 4);
  std::cout << "PASS complex formula caches remain strings in ODS, dependent "
               "recalculation and typed reload\n";
}
