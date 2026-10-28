// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
XmlElement cell(OdfPackage &package, XmlElement row, const Text &formula,
                const Text &cached) {
  auto c = package.xml.createElementNS(TABLE, "table:table-cell");
  if (!formula.empty())
    c.setAttributeNS(TABLE, "table:formula", formula);
  c.setAttributeNS(OFFICE, "office:value-type", "float");
  c.setAttributeNS(OFFICE, "office:value", cached);
  auto text = package.xml.createElementNS(TEXT, "text:p");
  set_plain_text(text, cached);
  c.appendChild(text);
  row.appendChild(c);
  return c;
}
OdfPackage fixture(bool caches = true) {
  OdfPackage package(Kind::Sheets);
  auto table = package.xml.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name", "Matrix");
  package.body().appendChild(table);
  auto row = package.xml.createElementNS(TABLE, "table:table-row");
  table.appendChild(row);
  auto master = cell(package, row, "of:=MMULT({1;2|3;4};{5;6|7;8})", "999");
  master.setAttributeNS(TABLE, "table:number-matrix-columns-spanned", "2");
  master.setAttributeNS(TABLE, "table:number-matrix-rows-spanned", "2");
  if (caches)
    cell(package, row, "", "-999");
  else
    row.appendChild(package.xml.createElementNS(TABLE, "table:table-cell"));
  cell(package, row, "of:=SUM([.A1:.B2])", "-999");
  auto next = package.xml.createElementNS(TABLE, "table:table-row");
  table.appendChild(next);
  if (caches) {
    cell(package, next, "", "-999");
    cell(package, next, "", "-999");
  } else {
    next.appendChild(package.xml.createElementNS(TABLE, "table:table-cell"));
    next.appendChild(package.xml.createElementNS(TABLE, "table:table-cell"));
  }
  cell(package, next, "of:=[.B2]*2", "-999");
  return package;
}
} // namespace
int main(int argc, char **argv) {
  TempDir directory;
  auto original = directory.path() + "/matrix.ods",
       saved = directory.path() + "/saved.ods";
  for (bool caches : {true, false}) {
    auto package = fixture(caches);
    package.save(original);
    if (caches && argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
      assert(File::copy(original, Text::fromUtf8(argv[2])));
    Workbook book;
    book.load(original);
    assert(book.display(0, 1, 1) == "19" && book.display(0, 2, 1) == "22");
    assert(book.display(0, 1, 2) == "43" && book.display(0, 2, 2) == "50");
    assert(book.display(0, 3, 1) == "134" && book.display(0, 3, 2) == "100");
    rejects([&] { book.set(0, 2, 2, "0"); });
    rejects([&] { book.set(0, 1, 1, "0"); });
    assert(book.display(0, 2, 2) == "50");
    book.set(0, 1, 1, "={5,6;7,8}");
    assert(book.display(0, 2, 2) == "8" && book.display(0, 3, 1) == "26" &&
           book.display(0, 3, 2) == "16");
    book.save(saved);
    OdfPackage result(Kind::Sheets);
    result.load(saved);
    auto table = children(result.body(), TABLE, "table").at(0);
    auto rows = children(table, TABLE, "table-row");
    auto first = children(rows[0], TABLE, "table-cell"),
         second = children(rows[1], TABLE, "table-cell");
    assert(first[0].attributeNS(TABLE, "number-matrix-columns-spanned") == "2");
    assert(first[0].attributeNS(OFFICE, "value") == "5" &&
           first[1].attributeNS(OFFICE, "value") == "6");
    assert(second[0].attributeNS(OFFICE, "value") == "7" &&
           second[1].attributeNS(OFFICE, "value") == "8");
    assert(first[1].attributeNS(TABLE, "formula").empty() &&
           second[1].attributeNS(TABLE, "formula").empty());
    Workbook reopened;
    reopened.load(saved);
    assert(reopened.display(0, 3, 1) == "26" &&
           reopened.display(0, 3, 2) == "16");
    // Reject a conflicting block without destroying the already loaded model.
    auto broken = fixture();
    auto badcells = broken.xml.elementsByTagNameNS(TABLE, "table-cell");
    badcells[1].setAttributeNS(TABLE, "table:formula", "of:={1;2}");
    badcells[1].setAttributeNS(TABLE, "table:number-matrix-columns-spanned",
                               "2");
    badcells[1].setAttributeNS(TABLE, "table:number-matrix-rows-spanned", "1");
    broken.save(original);
    rejects([&] { reopened.load(original); });
    assert(reopened.display(0, 3, 1) == "26" &&
           reopened.display(0, 2, 2) == "8");
  }
  // Uncomputable, unchanged matrices preserve all their imported caches on
  // save.
  auto unknown = fixture();
  auto unknown_cells = unknown.xml.elementsByTagNameNS(TABLE, "table-cell");
  unknown_cells[0].setAttributeNS(TABLE, "table:formula",
                                  "of:=UNAVAILABLE_NATIVE_MATRIX()");
  unknown.save(original);
  Workbook preserve;
  preserve.load(original);
  assert(preserve.display(0, 1, 1) == "#NAME?");
  preserve.save(saved);
  OdfPackage checked(Kind::Sheets);
  checked.load(saved);
  auto unknown_saved = checked.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(unknown_saved[0].attributeNS(OFFICE, "value") == "999" &&
         unknown_saved[1].attributeNS(OFFICE, "value") == "-999");
  unknown_cells[2].setAttributeNS(TABLE, "table:formula",
                                  "of:=IFERROR([.A1];7)");
  unknown_cells[2].setAttributeNS(OFFICE, "office:value", "314");
  unknown_cells[5].setAttributeNS(TABLE, "table:formula", "of:=[.C1]+1");
  unknown_cells[5].setAttributeNS(OFFICE, "office:value", "271");
  unknown.save(original);
  preserve.load(original);
  assert(preserve.display(0, 3, 1) == "7" &&
         preserve.value(0, 3, 1).incomplete);
  assert(preserve.display(0, 3, 2) == "8" &&
         preserve.value(0, 3, 2).incomplete);
  preserve.save(saved);
  checked.load(saved);
  unknown_saved = checked.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(unknown_saved[2].attributeNS(OFFICE, "value") == "314");
  assert(unknown_saved[5].attributeNS(OFFICE, "value") == "271");
  // Runtime errors are calculated results, unlike unsupported computations.
  auto runtime = fixture();
  auto runtime_cells = runtime.xml.elementsByTagNameNS(TABLE, "table-cell");
  runtime_cells[0].setAttributeNS(TABLE, "table:formula", "of:=1/0");
  runtime_cells[0].setAttributeNS(
      "urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0",
      "calcext:value-type", "float");
  runtime.save(original);
  preserve.load(original);
  assert(!preserve.value(0, 1, 1).incomplete);
  preserve.save(saved);
  checked.load(saved);
  runtime_cells = checked.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(runtime_cells[0].attributeNS(OFFICE, "string-value") == "#DIV/0!");
  assert(runtime_cells[1].attributeNS(OFFICE, "string-value") == "#DIV/0!");
  assert(!runtime_cells[0].hasAttributeNS(
      "urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0",
      "value-type"));
  // ODF defaults an omitted matrix row span to one.
  auto vector = fixture();
  auto vector_cells = vector.xml.elementsByTagNameNS(TABLE, "table-cell");
  vector_cells[0].setAttributeNS(TABLE, "table:formula", "of:={4;5}");
  vector_cells[0].removeAttributeNS(TABLE, "number-matrix-rows-spanned");
  vector.save(original);
  Workbook line;
  line.load(original);
  assert(line.display(0, 1, 1) == "4" && line.display(0, 2, 1) == "5");
  // Direct engine references see calculated matrix cells, including missing
  // caches.
  SheetEngine engine;
  engine.addNewSheet("Sheet1");
  engine.set(0, 1, 1, "={1,2;3,4}");
  engine.define_matrix(0, 1, 1, 2, 2);
  engine.set(0, 3, 1, "=SUM(A1:B2)");
  assert(engine.value(0, 3, 1).number == 10);
  engine.set(0, 1, 1, "=B2");
  assert(engine.value(0, 2, 2).string == "#CYCLE!");
  rejects([&] { engine.define_matrix(0, 1, 1, 2, 2); });
  OdfPackage primitives(Kind::Sheets);
  auto table = primitives.xml.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name", "Primitives");
  primitives.body().appendChild(table);
  auto row = primitives.xml.createElementNS(TABLE, "table:table-row");
  table.appendChild(row);
  auto empty = cell(primitives, row, "", "");
  empty.setAttributeNS(OFFICE, "office:value-type", "string");
  empty.removeAttributeNS(OFFICE, "value");
  empty.setAttributeNS(OFFICE, "office:string-value", "");
  auto boolean = cell(primitives, row, "", "0");
  boolean.setAttributeNS(OFFICE, "office:value-type", "boolean");
  boolean.removeAttributeNS(OFFICE, "value");
  boolean.setAttributeNS(OFFICE, "office:boolean-value", "1");
  cell(primitives, row, "of:=COUNTA([.A1])", "0");
  cell(primitives, row, "of:=IF([.B1];7;9)", "0");
  primitives.save(original);
  Workbook typed;
  typed.load(original);
  assert(typed.value(0, 1, 1).kind == NativeValue::String &&
         typed.display(0, 1, 1).empty());
  assert(typed.value(0, 2, 1).isBoolean() && typed.value(0, 2, 1).boolean);
  assert(typed.display(0, 3, 1) == "1" && typed.display(0, 4, 1) == "7");
  boolean.setAttributeNS(OFFICE, "office:boolean-value", "maybe");
  primitives.save(original);
  rejects([&] { typed.load(original); });
  assert(typed.display(0, 4, 1) == "7");
  std::cout
      << "PASS fixed ODF matrix recalculation, absent/stale caches, dependent "
         "cells, edits, saved caches, bounds/cycles and failed-load rollback"
      << std::endl;
}
