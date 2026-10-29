// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_calendar.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
using namespace std::chrono;
namespace {
using V = NativeValue;
void near(const V &got, double want) {
  if (!got.isNumber() ||
      std::abs(got.number - want) > 1e-11 * std::max(1., std::abs(want)))
    std::cerr << got.display().toStdString() << " != " << want << '\n';
  assert(got.isNumber() && !got.incomplete);
  assert(std::abs(got.number - want) <= 1e-11 * std::max(1., std::abs(want)));
}
V eval(SheetEngine &engine, const Text &expression) {
  engine.set(0, 20, 1, "=" + expression);
  return engine.value(0, 20, 1);
}
OdfPackage fixture(const Text &epoch, bool declared = true) {
  OdfPackage p(Kind::Sheets);
  if (declared) {
    auto settings = p.xml.createElementNS(TABLE, "table:calculation-settings");
    settings.setAttributeNS(TABLE, "table:null-year", "1930");
    settings.setAttributeNS(TABLE, "table:use-wildcards", "true");
    auto date = p.xml.createElementNS(TABLE, "table:null-date");
    date.setAttributeNS(TABLE, "table:date-value", epoch);
    settings.appendChild(date);
    p.body().appendChild(settings);
  }
  auto table = p.xml.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name", "Dates");
  p.body().appendChild(table);
  auto row = p.xml.createElementNS(TABLE, "table:table-row");
  table.appendChild(row);
  for (auto formula : {"", "of:=DATE(2000;1;1)", "of:=YEAR([.A1])",
                       "of:=COUPNCD([.A1];DATE(2001;1;1);2)",
                       "of:=UNKNOWN_CONTEXT_FUNCTION()"}) {
    auto cell = p.xml.createElementNS(TABLE, "table:table-cell");
    if (Text(formula).empty()) {
      cell.setAttributeNS(OFFICE, "office:value-type", "date");
      cell.setAttributeNS(OFFICE, "office:date-value", "2000-01-01");
    } else {
      cell.setAttributeNS(TABLE, "table:formula", formula);
      cell.setAttributeNS(OFFICE, "office:value-type", "float");
      cell.setAttributeNS(OFFICE, "office:value", "901");
    }
    row.appendChild(cell);
  }
  return p;
}
} // namespace
int main() {
  SheetEngine baseline;
  baseline.addNewSheet("Dates");
  const std::vector<Text> formulas = {
      "YEAR(A1)",
      "MONTH(A1)",
      "DAY(A1)",
      "DATE(2024,2,1)",
      "YEARFRAC(A1,B1,1)",
      "DAYS360(A1,B1)",
      "ACCRINTM(A1,\"2024-05-01\",.1,1000,1)",
      "DISC(A1,B1,95,100,1)",
      "INTRATE(A1,B1,95,100,1)",
      "PRICEDISC(A1,B1,.1,100,1)",
      "RECEIVED(A1,B1,100,.1,1)",
      "YIELDDISC(A1,B1,95,100,1)",
      "COUPDAYBS(A1,C1,2,1)",
      "COUPDAYS(A1,C1,2,1)",
      "COUPDAYSNC(A1,C1,2,1)",
      "COUPNUM(A1,C1,2,1)",
      "COUPNCD(A1,C1,2,1)",
      "COUPPCD(A1,C1,2,1)",
      "PRICE(A1,C1,.1,.08,100,2,1)",
      "YIELD(A1,C1,.1,102,100,2,1)",
      "DURATION(A1,C1,.1,.08,2,1)",
      "DURATION_ADD(A1,C1,.1,.08,2,1)",
      "MDURATION(A1,C1,.1,.08,2,1)",
      "ACCRINT(D1,B1,\"2024-02-01\",.1,1000,2,1)",
      "ODDLPRICE(A1,C1,D1,.1,.08,100,2,1)",
      "ODDLYIELD(A1,C1,D1,.1,102,100,2,1)",
      "PRICEMAT(A1,B1,D1,.1,.08,1)",
      "YIELDMAT(A1,B1,D1,.1,102,1)",
      "TBILLEQ(A1,B1,.05)",
      "TBILLPRICE(A1,B1,.05)",
      "TBILLYIELD(A1,B1,99)",
      "AMORLINC(1000,D1,\"2024-05-01\",100,0,.1,1)",
      "AMORDEGRC(1000,D1,\"2024-05-01\",100,1,.15,1)",
      "XNPV(.1,{-100,110},{A1,\"2025-02-01\"})",
      "XIRR({-100,110},{A1,\"2025-02-01\"})"};
  std::vector<Text> dates = {"2024-02-01", "2024-05-01", "2025-02-28",
                             "2023-08-01"};
  for (size_t i = 0; i < dates.size(); ++i)
    baseline.set(0, int(i + 1), 1, dates[i]);
  std::vector<V> expected;
  for (const auto &formula : formulas) {
    auto v = eval(baseline, formula);
    assert(v.isNumber() && !v.incomplete);
    expected.push_back(v);
  }
  for (auto epoch : {year(1) / 1 / 1, year(1900) / 1 / 1, year(1904) / 1 / 1,
                     year(2000) / 1 / 1, year(9999) / 12 / 31}) {
    NativeCalculationSettings settings(epoch);
    SheetEngine engine(settings);
    engine.addNewSheet("Dates");
    auto offset = (settings.reference_date() -
                   baseline.calculation_settings().reference_date())
                      .count();
    for (size_t i = 0; i < dates.size(); ++i)
      engine.set(0, int(i + 1), 1, dates[i]);
    near(engine.value(0, 1, 1), baseline.value(0, 1, 1).number - offset);
    for (size_t i = 0; i < formulas.size(); ++i) {
      auto v = eval(engine, formulas[i]);
      bool date =
          expected[i].format == V::Date || formulas[i] == "DATE(2024,2,1)";
      near(v, expected[i].number - (date ? offset : 0));
      assert(v.format == expected[i].format);
      if (v.format == V::Date)
        assert(v.string == expected[i].string);
      // Revisit an already evaluated default-epoch workbook between calls.
      near(eval(baseline, formulas[i]), expected[i].number);
    }
    // Plain serials and civil strings can be mixed in either direction.
    auto serial = engine.value(0, 1, 1).number;
    near(eval(engine, "YEARFRAC(" + Text::number(serial, 'g', 17) +
                          ",\"2024-05-01\",1)"),
         expected[4].number);
    near(eval(engine, "YEAR(0)"), int(epoch.year()));
    near(eval(engine, "MONTH(0)"), unsigned(epoch.month()));
    near(eval(engine, "DAY(0)"), unsigned(epoch.day()));
    near(eval(engine, "YEAR(-.9)"), int(epoch.year()));
    near(eval(engine, "YEAR(\"2024-02-01\")"), 2024);
    auto minimum =
        (sys_days(year(1) / 1 / 1) - settings.reference_date()).count();
    auto maximum =
        (sys_days(year(9999) / 12 / 31) - settings.reference_date()).count();
    for (auto bad : {minimum - 1, maximum + 1})
      assert(eval(engine, "YEAR(" + Text::number(bad) + ")").string == "#NUM!");
  }
  for (auto invalid :
       {year(0) / 1 / 1, year(10000) / 1 / 1, year(2023) / 2 / 29}) {
    bool rejected = false;
    try {
      NativeCalculationSettings settings(invalid);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  TempDir directory;
  auto input = directory.path() + "/dates.ods",
       saved = directory.path() + "/saved.ods";
  Workbook custom, other;
  fixture("2000-01-01").save(input);
  custom.load(input);
  near(custom.value(0, 1, 1), 0);
  near(custom.value(0, 2, 1), 0);
  near(custom.value(0, 3, 1), 2000);
  assert(custom.value(0, 4, 1).format == V::Date);
  assert(custom.display(0, 4, 1) == "2000-07-01");
  near(custom.value(0, 4, 1), 182);
  fixture("1904-01-01").save(input);
  other.load(input);
  near(other.value(0, 1, 1), 35064);
  near(custom.value(0, 1, 1), 0);
  custom.set(0, 1, 1, "2000-01-02T12:00:00");
  near(custom.value(0, 1, 1), 1.5);
  custom.save(saved);
  OdfPackage package(Kind::Sheets);
  package.load(saved);
  auto settings = children(package.body(), TABLE, "calculation-settings").at(0);
  assert(settings.attributeNS(TABLE, "null-year") == "1930");
  assert(settings.attributeNS(TABLE, "use-wildcards") == "true");
  assert(children(settings, TABLE, "null-date")
             .at(0)
             .attributeNS(TABLE, "date-value") == "2000-01-01");
  auto cells = package.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(cells[0].attributeNS(OFFICE, "date-value") == "2000-01-02T12:00:00");
  assert(cells[1].attributeNS(OFFICE, "value") == "0");
  assert(cells[3].attributeNS(OFFICE, "date-value") == "2000-07-01");
  assert(cells[4].attributeNS(OFFICE, "value") == "901");
  Workbook reopened;
  reopened.load(saved);
  near(reopened.value(0, 1, 1), 1.5);
  near(reopened.value(0, 4, 1), 182);
  for (auto bad :
       {"2000-02-30", "0000-01-01", "garbage", "2000-01-01T00:00:00"}) {
    fixture(bad).save(input);
    other.load(input);
    near(other.value(0, 1, 1), 36526);
  }
  auto wrong_type = fixture("2000-01-01");
  wrong_type.xml.elementsByTagNameNS(TABLE, "null-date")[0].setAttributeNS(
      TABLE, "table:value-type", "float");
  wrong_type.save(input);
  other.load(input);
  near(other.value(0, 1, 1), 36526);
  fixture("", false).save(input);
  custom.load(input);
  near(custom.value(0, 1, 1), 36526);
  std::cout << "PASS workbook-owned date context across date/financial "
               "families, epochs, saved caches and reload\n";
}
