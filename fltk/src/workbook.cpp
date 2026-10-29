// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include "native_formula.h"
#include "native_time.h"
#include "xlsx_import.h"
#include <mutex>
#include <stdexcept>

namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const char* error) { throw std::runtime_error(error); }
    int repeated(const XmlElement& e, const Text& name)
    {
        bool ok;
        auto v = e.attributeNS(TABLE, name, "1").toLongLong(&ok);
        if (!ok || v < 1 || v > KS_rowMax)
            fail("Invalid repeated rows or cells");
        return int(v);
    }
    XmlElement split(XmlElement e, int index, int count, const Text& repeat)
    {
        auto parent = e.parentNode();
        if (index > 1) {
            auto before = e.cloneNode(true).toElement();
            before.setAttributeNS(TABLE, "table:" + repeat, index - 1);
            parent.insertBefore(before, e);
        }
        if (index < count) {
            auto after = e.cloneNode(true).toElement();
            after.setAttributeNS(TABLE, "table:" + repeat, count - index);
            parent.insertAfter(after, e);
        }
        e.removeAttributeNS(TABLE, repeat);
        return e;
    }
    XmlElement row_at(XmlElement table, int desired)
    {
        auto rows = table.elementsByTagNameNS(TABLE, "table-row");
        int at = 1;
        for (int i = 0; i < rows.size(); ++i) {
            auto e = rows.at(i).toElement();
            int count = repeated(e, "number-rows-repeated");
            if (desired < at + count)
                return split(e, desired - at + 1, count, "number-rows-repeated");
            at += count;
        }
        auto doc = table.ownerDocument();
        if (desired > at) {
            auto gap = doc.createElementNS(TABLE, "table:table-row");
            gap.setAttributeNS(TABLE, "table:number-rows-repeated", desired - at);
            table.appendChild(gap);
        }
        auto e = doc.createElementNS(TABLE, "table:table-row");
        table.appendChild(e);
        return e;
    }
    XmlElement cell_at(XmlElement row, int desired)
    {
        int at = 1;
        for (auto e : children(row, TABLE)) {
            if (e.localName() != "table-cell" && e.localName() != "covered-table-cell")
                continue;
            int count = repeated(e, "number-columns-repeated");
            if (desired < at + count) {
                if (e.localName() == "covered-table-cell")
                    fail("Edit the top-left cell of a merged range");
                return split(e, desired - at + 1, count, "number-columns-repeated");
            }
            at += count;
        }
        auto doc = row.ownerDocument();
        if (desired > at) {
            auto gap = doc.createElementNS(TABLE, "table:table-cell");
            gap.setAttributeNS(TABLE, "table:number-columns-repeated", desired - at);
            row.appendChild(gap);
        }
        auto e = doc.createElementNS(TABLE, "table:table-cell");
        row.appendChild(e);
        return e;
    }
    Text text_value(const XmlElement& cell)
    {
        TextList parts;
        for (auto p : children(cell, TEXT))
            if (p.localName() == "p" || p.localName() == "h")
                parts << plain_text(p);
        return parts.join('\n');
    }
} // namespace
Workbook::Workbook()
{
    initialize();
    add_sheet("Sheet1");
}
void Workbook::initialize(NativeCalculationSettings settings) {
  map_ = std::make_unique<SheetEngine>(settings);
}
int Workbook::sheet_count() const { return map_->count(); }
Text Workbook::sheet_name(int s) const { return map_->sheet(s)->sheetName(); }
void Workbook::add_sheet(const Text& name)
{
    if (read_only())
        fail("This document is read-only");
    if (name.isEmpty() || map_->findSheet(name))
        fail("Choose a unique worksheet name");
    map_->addNewSheet(name);
    auto e = package_.xml.createElementNS(TABLE, "table:table");
    e.setAttributeNS(TABLE, "table:name", name);
    package_.body().appendChild(e);
    tables_ << e;
}
int Workbook::rows(int s) const { return map_->sheet(s)->cellStorage()->rows(false); }
int Workbook::columns(int s) const { return map_->sheet(s)->cellStorage()->columns(false); }
Text Workbook::input(int s, int c, int r) const { return NativeCell(map_->sheet(s), c, r).userInput(); }
Text Workbook::display(int s, int c, int r) const { return value(s, c, r).display(); }
NativeValue Workbook::value(int s, int c, int r) const { return NativeCell(map_->sheet(s), c, r).value(); }
void Workbook::recalculate()
{
    map_->recalculate();

}
void Workbook::validate_cell(int s, int c, int r)
{
    if (read_only())
        fail("This document is read-only");
    if (s < 0 || s >= sheet_count() || c < 1 || c > KS_colMax || r < 1 || r > KS_rowMax)
        fail("Cell is outside the worksheet");
    if (map_->matrix_child(s, c, r))
        fail("Edit the top-left cell of the matrix formula");
    // Validate covered-cell access before mutating the engine or document.
    cell_at(row_at(tables_.at(s), r), c);
}
void Workbook::set(int s, int c, int r, const Text& value)
{
    validate_cell(s, c, r);
    NativeCell(map_->sheet(s), c, r).parseUserInput(value);
    auto key = Text("%1:%2:%3").arg(s).arg(c).arg(r);
    changed_[key] = value;
    if (value.startsWith('='))
        formulas_.insert(key);
    else
        formulas_.remove(key);
    recalculate();
}
void Workbook::load(const Text& path)
{
    OdfPackage candidate(Kind::Sheets);
    bool xlsx = FileInfo(path).suffix().compare("xlsx", CaseInsensitive) == 0;
    if (xlsx)
        candidate = import_xlsx(path);
    else
        candidate.load(path);
    Workbook next;
    next.load_package(candidate);
    next.imported_xlsx_ = xlsx;
    *this = std::move(next);
}
void Workbook::load_package(OdfPackage candidate)
{
    auto tables = children(candidate.body(), TABLE, "table");
    if (tables.isEmpty())
        fail("The document has no worksheets");
    NativeCalculationSettings settings;
    auto declarations =
        children(candidate.body(), TABLE, "calculation-settings");
    if (!declarations.isEmpty())
      for (auto element : children(declarations[0], TABLE, "null-date")) {
        // Match the retained loader: absent/invalid declarations fall back
        // to the default. Ignore other namespaces and nested lookalikes.
        settings = NativeCalculationSettings();
        if (element.attributeNS(TABLE, "value-type", "date") != "date")
          continue;
        auto value = element.attributeNS(TABLE, "date-value", "1899-12-30");
        auto date =
            value.size() <= 128 ? Date::fromString(value, ISODate) : Date();
        if (date.valid && int(date.day.year()) >= 1 &&
            int(date.day.year()) <= 9999)
          settings = NativeCalculationSettings(date.day);
      }
    initialize(settings);
    tables_.clear();
    changed_.clear();
    formulas_.clear();
    protected_ = false;

    // Make all sheet names available before loading cross-sheet references.
    for (auto table : tables) {
        if (table.attributeNS(TABLE, "protected") == "true")
            protected_ = true;
        auto name = table.attributeNS(TABLE, "name");
        if (name.isEmpty() || map_->findSheet(name))
            fail("Invalid or duplicate worksheet name");
        map_->addNewSheet(name);
    }
    int loaded = 0;
    std::vector<MatrixBlock> matrices;
    for (int s = 0; s < tables.size(); ++s) {
        Int64 row = 1;
        auto list = tables[s].elementsByTagNameNS(TABLE, "table-row");
        for (int i = 0; i < list.size(); ++i) {
            auto re = list.at(i).toElement();
            int nr = repeated(re, "number-rows-repeated");
            Int64 col = 1;
            for (auto ce : children(re, TABLE)) {
                if (ce.localName() != "table-cell" && ce.localName() != "covered-table-cell")
                    continue;
                int nc = repeated(ce, "number-columns-repeated");
                Text formula = ce.attributeNS(TABLE, "formula");
                if (ce.hasAttributeNS(TABLE, "number-matrix-columns-spanned") || ce.hasAttributeNS(TABLE, "number-matrix-rows-spanned")) {
                    bool valid = false;
                    int columns = ce.attributeNS(TABLE, "number-matrix-columns-spanned", "1").toInt(&valid);
                    if (!valid || columns < 1)
                        fail("Invalid matrix columns");
                    int rows = ce.attributeNS(TABLE, "number-matrix-rows-spanned", "1").toInt(&valid);
                    if (!valid || rows < 1)
                        fail("Invalid matrix rows");
                    if (formula.isEmpty() || Int64(nr) * nc + matrices.size() > 1'000'000)
                        fail("Invalid matrix formula masters");
                    for (int rr = 0; rr < nr; ++rr)
                        for (int cc = 0; cc < nc; ++cc) {
                            if (row + rr > KS_rowMax || col + cc > KS_colMax)
                                fail("Matrix master outside worksheet");
                            matrices.push_back({ s, int(col + cc), int(row + rr), columns, rows });
                        }
                }
                Text value;
                auto type = ce.attributeNS(OFFICE, "value-type");
                if (!formula.isEmpty()) {
                    value = Formula::decode(formula);
                } else if (type == "float" || type == "currency" || type == "percentage")
                    value = ce.attributeNS(OFFICE, "value");
                else if (type == "boolean") {
                    auto boolean = ce.attributeNS(OFFICE, "boolean-value");
                    if (boolean != "true" && boolean != "false" && boolean != "1" && boolean != "0")
                        fail("Invalid Boolean cell value");
                    value = (boolean == "true" || boolean == "1") ? "TRUE" : "FALSE";
                } else if (type == "date")
                    value = ce.attributeNS(OFFICE, "date-value");
                else if (type == "time")
                    value = ce.attributeNS(OFFICE, "time-value");
                else if (type == "string" && ce.hasAttributeNS(OFFICE, "string-value"))
                    value = ce.attributeNS(OFFICE, "string-value");
                else
                    value = text_value(ce);
                if (!value.isEmpty() || type == "string") {
                    if (Int64(nr) * nc + loaded > 1000000 || Int64(row) + nr - 1 > KS_rowMax
                        || Int64(col) + nc - 1 > KS_colMax)
                        fail("Worksheet exceeds the current import cell limit");
                    for (int rr = 0; rr < nr; ++rr)
                        for (int cc = 0; cc < nc; ++cc) {
                            auto cell = NativeCell(map_->sheet(s), col + cc, row + rr);
                            if (formula.isEmpty() && type == "string") {
                                cell.setCellValue(NativeValue(value));
                            } else
                                cell.parseUserInput(value);
                            if (!formula.isEmpty())
                                formulas_.insert(Text("%1:%2:%3").arg(s).arg(col + cc).arg(row + rr));
                            ++loaded;
                        }
                }
                col += nc;
            }
            row += nr;
            if (row > KS_rowMax + 1)
                fail("Too many worksheet rows");
        }
    }
    auto ranges = candidate.body().elementsByTagNameNS(TABLE, "named-range");
    for (int i = 0; i < ranges.size(); ++i) {
        auto e = ranges.at(i).toElement();
        auto ref = Formula::load_region(e.attributeNS(TABLE, "cell-range-address"));
        map_->addNamedRange(ref, e.attributeNS(TABLE, "name"));
    }
    for (const auto& matrix : matrices)
        map_->define_matrix(matrix.sheet, matrix.column, matrix.row, matrix.columns, matrix.rows);

    package_ = candidate;
    tables_ = tables;
    recalculate();
}
void Workbook::save(const Text& path)
{
    if (FileInfo(path).suffix().compare("ods", CaseInsensitive) != 0)
        fail("Sheets saves OpenDocument spreadsheets (.ods); choose an ODS "
             "filename");
    if (read_only())
        fail("This workbook is read-only");
    auto values = changed_;
    for (const auto& key : formulas_) {
        auto indexes = key.split(':');
        int s = indexes[0].toInt(), c = indexes[1].toInt(), r = indexes[2].toInt();
        if (!NativeCell(map_->sheet(s), c, r).value().incomplete)
            values[key] = input(s, c, r);
    }
    // Recompute every cached cell in an existing fixed matrix block. Dependent
    // formulas read those calculated cells, never the imported stale caches.
    for (const auto& matrix : map_->matrix_blocks()) {
        auto key = Text("%1:%2:%3").arg(matrix.sheet).arg(matrix.column).arg(matrix.row);
        if (!values.contains(key))
            continue;
        for (int r = 0; r < matrix.rows; ++r)
            for (int c = 0; c < matrix.columns; ++c)
                if (r || c)
                    values[Text("%1:%2:%3").arg(matrix.sheet).arg(matrix.column + c).arg(matrix.row + r)] = { };
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        auto indexes = it.key().split(':');
        int s = indexes[0].toInt(), c = indexes[1].toInt(), r = indexes[2].toInt();
        auto element = cell_at(row_at(tables_.at(s), r), c);
        auto value = NativeCell(map_->sheet(s), c, r).value().scalar();
        for (auto key : { "value-type", "value", "string-value", "boolean-value", "date-value", "time-value" })
            element.removeAttributeNS(OFFICE, key);
        element.removeAttributeNS(TABLE, "formula");
        // LibreOffice's extension mirrors the cached type; drop the old mirror
        // when replacing the authoritative standard ODF value attributes.
        element.removeAttributeNS("urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0", "value-type");
        // Preserve style and extension attributes and any non-text children.
        for (auto p : children(element, TEXT))
            if (p.localName() == "p" || p.localName() == "h")
                element.removeChild(p);
        if (it.value().startsWith('='))
            element.setAttributeNS(TABLE, "table:formula", "of:" + Formula::encode(it.value()));
        Text text = display(s, c, r);
        if (value.isComplex()) {
            element.setAttributeNS(OFFICE, "office:value-type", "string");
            element.setAttributeNS(OFFICE, "office:string-value", value.display());
        } else if (value.isNumber() && value.format != NativeValue::General) {
            bool date = value.format == NativeValue::Date;
            element.setAttributeNS(OFFICE, "office:value-type", date ? "date" : "time");
            element.setAttributeNS(OFFICE, date ? "office:date-value" : "office:time-value", value.string);
        } else if (value.isNumber()) {
            element.setAttributeNS(OFFICE, "office:value-type", "float");
            element.setAttributeNS(
                OFFICE, "office:value", Text::number(double(value.asFloat()), 'g', 17));
        } else if (value.isBoolean()) {
            element.setAttributeNS(OFFICE, "office:value-type", "boolean");
            element.setAttributeNS(OFFICE, "office:boolean-value", value.asBoolean() ? "true" : "false");
        } else {
            element.setAttributeNS(OFFICE, "office:value-type", "string");
            element.setAttributeNS(OFFICE, "office:string-value", text);
        }
        auto p = package_.xml.createElementNS(TEXT, "text:p");
        set_plain_text(p, text);
        element.appendChild(p);
    }
    package_.save(path);
    changed_.clear();
}
} // namespace DomeOffice
