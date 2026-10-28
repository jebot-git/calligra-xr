// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_formula.h"
#include "workbook.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <tuple>
using namespace DomeOffice;
int main(int argc, char **argv) {
  assert(argc > 1);
  size_t checked = 0;
  for (int arg = 1; arg < argc; ++arg) {
    File source(Text::fromUtf8(argv[arg]));
    assert(source.open(IO::ReadOnly));
    OdfPackage package(Kind::Sheets);
    package.xml = read_document_xml(source.readAll());
    TempDir temporary;
    auto path = temporary.path() + "/fixture.ods";
    package.save(path);
    Workbook workbook;
    workbook.load(path);
    auto tables = children(package.body(), TABLE, "table");
    for (size_t sheet = 0; sheet < tables.size(); ++sheet) {
      std::map<std::pair<int, int>, XmlElement> cells;
      std::vector<std::tuple<int, int, int, int>> selected;
      int row = 1;
      for (auto xml_row : children(tables[sheet], TABLE, "table-row")) {
        int repeats =
            xml_row.attributeNS(TABLE, "number-rows-repeated", "1").toInt();
        assert(repeats > 0);
        int column = 1;
        for (auto cell : children(xml_row, TABLE)) {
          if (cell.localName() != "table-cell" &&
              cell.localName() != "covered-table-cell")
            continue;
          int columns =
              cell.attributeNS(TABLE, "number-columns-repeated", "1").toInt();
          assert(columns > 0);
          auto formula = cell.attributeNS(TABLE, "formula");
          bool test = false;
          if (!formula.empty()) {
            auto decoded = Formula::decode(formula).toUpper();
            for (const char *name : {"=MMULT(", "=MDETERM(", "=MINVERSE(",
                                     "=TRANSPOSE(", "=SUMPRODUCT("})
              test = test || decoded.startsWith(name);
          }
          bool content =
              cell.hasAttributeNS(OFFICE, "value-type") || !formula.empty();
          if (content) {
            assert(Int64(repeats) * columns < 1'000'000);
            for (int dr = 0; dr < repeats; ++dr)
              for (int dc = 0; dc < columns; ++dc) {
                cells[{row + dr, column + dc}] = cell;
                if (test)
                  selected.emplace_back(
                      row + dr, column + dc,
                      cell.attributeNS(TABLE, "number-matrix-rows-spanned", "1")
                          .toInt(),
                      cell.attributeNS(TABLE, "number-matrix-columns-spanned",
                                       "1")
                          .toInt());
              }
          }
          column += columns;
        }
        row += repeats;
      }
      for (auto [r, c, rows, columns] : selected)
        for (int dr = 0; dr < rows; ++dr)
          for (int dc = 0; dc < columns; ++dc) {
            auto expected = cells[{r + dr, c + dc}];
            auto value = workbook.value(sheet, c + dc, r + dr).scalar();
            auto actual = value.display();
            auto type = expected.attributeNS(OFFICE, "value-type");
            bool valid = false, pass = false;
            if (type == "error" ||
                expected.attributeNS("urn:org:documentfoundation:names:"
                                     "experimental:calc:xmlns:calcext:1.0",
                                     "value-type") == "error")
              pass = value.isError();
            else if (type == "float" || type == "percentage" ||
                     type == "currency") {
              double reference =
                  expected.attributeNS(OFFICE, "value").toDouble(&valid);
              assert(valid);
              double result = actual.toDouble(&valid);
              pass = value.isNumber() && valid && std::isfinite(result) &&
                     std::abs(result - reference) <=
                         1e-8 * std::max(1., std::abs(reference));
            } else if (type == "boolean")
              pass = value.isBoolean() &&
                     actual == (expected.attributeNS(OFFICE, "boolean-value") ==
                                        "true"
                                    ? "TRUE"
                                    : "FALSE");
            else if (type == "date")
              pass = actual == expected.attributeNS(OFFICE, "date-value");
            else if (type == "time")
              pass = actual == expected.attributeNS(OFFICE, "time-value");
            else {
              auto text = expected.attributeNS(OFFICE, "string-value");
              if (!expected.hasAttributeNS(OFFICE, "string-value"))
                for (auto p : children(expected, TEXT, "p"))
                  text += plain_text(p);
              pass = actual == text;
            }
            if (!pass)
              std::cerr << argv[arg] << " sheet " << sheet << " row " << r + dr
                        << " col " << c + dc << " actual "
                        << actual.toStdString() << " expected type "
                        << type.toStdString() << " value "
                        << expected.attributeNS(OFFICE, "value").toStdString()
                        << '\n';
            assert(pass);
            ++checked;
          }
    }
    std::cout << "PASS independent cached matrix results from " << argv[arg]
              << std::endl;
  }
  assert(checked > 10);
  std::cout << "PASS " << checked
            << " matrix/array result cells from upstream FODS corpus"
            << std::endl;
}
