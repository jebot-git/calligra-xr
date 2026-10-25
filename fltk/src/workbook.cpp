// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include <QCoreApplication>
#include <QDateTime>
#include <engine/CalculationSettings.h>
#include <engine/CellBase.h>
#include <engine/CellBaseStorage.h>
#include <engine/DependencyManager.h>
#include <engine/FunctionModuleRegistry.h>
#include <engine/FunctionRepository.h>
#include <engine/NamedAreaManager.h>
#include <engine/OdfFormula.h>
#include <engine/RecalcManager.h>
#include <engine/SheetBase.h>
#include <engine/Value.h>
#include <engine/ValueConverter.h>
#include <engine/calligra_sheets_limits.h>
#include <mutex>
#include <stdexcept>
using namespace Calligra::Sheets;
namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const char* error) { throw std::runtime_error(error); }
    int repeated(const QDomElement& e, const QString& name)
    {
        bool ok;
        auto v = e.attributeNS(TABLE, name, "1").toLongLong(&ok);
        if (!ok || v < 1 || v > KS_rowMax)
            fail("Invalid repeated rows or cells");
        return int(v);
    }
    QDomElement split(QDomElement e, int index, int count, const QString& repeat)
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
    QDomElement row_at(QDomElement table, int desired)
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
    QDomElement cell_at(QDomElement row, int desired)
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
    QString text_value(const QDomElement& cell)
    {
        QStringList parts;
        for (auto p : children(cell, TEXT))
            if (p.localName() == "p" || p.localName() == "h")
                parts << plain_text(p);
        return parts.join('\n');
    }
}
Workbook::Workbook()
{
    initialize();
    add_sheet("Sheet1");
}
void Workbook::initialize()
{
    static std::once_flag once;
    std::call_once(once, [] {
        FunctionRepository::self();
        FunctionModuleRegistry::instance()->loadFunctionModules();
    });
    map_ = std::make_unique<MapBase>();
}
int Workbook::sheet_count() const { return map_->count(); }
QString Workbook::sheet_name(int s) const { return map_->sheet(s)->sheetName(); }
void Workbook::add_sheet(const QString& name)
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
QString Workbook::input(int s, int c, int r) const { return CellBase(map_->sheet(s), c, r).userInput(); }
QString Workbook::display(int s, int c, int r) const
{
    auto v = CellBase(map_->sheet(s), c, r).value();
    return map_->converter()->asString(v).asString();
}
void Workbook::recalculate()
{
    map_->flushDamages();
    map_->recalcManager()->recalcMap();
    QCoreApplication::processEvents();
}
void Workbook::validate_cell(int s, int c, int r)
{
    if (read_only())
        fail("This document is read-only");
    if (s < 0 || s >= sheet_count() || c < 1 || c > KS_colMax || r < 1 || r > KS_rowMax)
        fail("Cell is outside the worksheet");
    // Validate covered-cell access before mutating the engine or document.
    cell_at(row_at(tables_.at(s), r), c);
}
void Workbook::set(int s, int c, int r, const QString& value)
{
    validate_cell(s, c, r);
    CellBase(map_->sheet(s), c, r).parseUserInput(value);
    auto key = QString("%1:%2:%3").arg(s).arg(c).arg(r);
    changed_[key] = value;
    if (value.startsWith('='))
        formulas_.insert(key);
    else
        formulas_.remove(key);
    recalculate();
}
void Workbook::load(const QString& path)
{
    OdfPackage candidate(Kind::Sheets);
    candidate.load(path);
    auto tables = children(candidate.body(), TABLE, "table");
    if (tables.isEmpty())
        fail("The document has no worksheets");
    initialize();
    tables_.clear();
    changed_.clear();
    formulas_.clear();
    protected_ = false;
    map_->setLoading(true);
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
    for (int s = 0; s < tables.size(); ++s) {
        qint64 row = 1;
        auto list = tables[s].elementsByTagNameNS(TABLE, "table-row");
        for (int i = 0; i < list.size(); ++i) {
            auto re = list.at(i).toElement();
            int nr = repeated(re, "number-rows-repeated");
            qint64 col = 1;
            for (auto ce : children(re, TABLE)) {
                if (ce.localName() != "table-cell" && ce.localName() != "covered-table-cell")
                    continue;
                int nc = repeated(ce, "number-columns-repeated");
                QString formula = ce.attributeNS(TABLE, "formula");
                QString value;
                auto type = ce.attributeNS(OFFICE, "value-type");
                if (!formula.isEmpty()) {
                    int colon = formula.indexOf(":=");
                    QString prefix = colon >= 0 ? formula.left(colon + 1) : QString();
                    value = Odf::decodeFormula(colon >= 0 ? formula.mid(colon + 1) : formula, nullptr, prefix);
                } else if (type == "float" || type == "currency" || type == "percentage")
                    value = ce.attributeNS(OFFICE, "value");
                else if (type == "boolean")
                    value = ce.attributeNS(OFFICE, "boolean-value") == "true" ? "TRUE" : "FALSE";
                else if (type == "date")
                    value = ce.attributeNS(OFFICE, "date-value");
                else if (type == "time")
                    value = ce.attributeNS(OFFICE, "time-value");
                else
                    value = text_value(ce);
                if (!value.isEmpty()) {
                    if (qint64(nr) * nc + loaded > 1000000 || qint64(row) + nr - 1 > KS_rowMax
                        || qint64(col) + nc - 1 > KS_colMax)
                        fail("Worksheet exceeds the current import cell limit");
                    for (int rr = 0; rr < nr; ++rr)
                        for (int cc = 0; cc < nc; ++cc) {
                            auto cell = CellBase(map_->sheet(s), col + cc, row + rr);
                            if (formula.isEmpty() && type == "string") {
                                cell.setCellValue(Value(value));
                            } else
                                cell.parseUserInput(value);
                            if (!formula.isEmpty())
                                formulas_.insert(QString("%1:%2:%3").arg(s).arg(col + cc).arg(row + rr));
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
        auto ref = Odf::loadRegion(e.attributeNS(TABLE, "cell-range-address"));
        map_->namedAreaManager()->insert(map_->regionFromName(ref), e.attributeNS(TABLE, "name"));
    }
    map_->setLoading(false);
    map_->dependencyManager()->updateAllDependencies(map_.get());
    package_ = candidate;
    tables_ = tables;
    recalculate();
}
void Workbook::save(const QString& path)
{
    if (read_only())
        fail("This workbook is read-only");
    auto values = changed_;
    for (const auto& key : formulas_) {
        auto indexes = key.split(':');
        int s = indexes[0].toInt(), c = indexes[1].toInt(), r = indexes[2].toInt();
        if (!CellBase(map_->sheet(s), c, r).value().isError())
            values[key] = input(s, c, r);
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        auto indexes = it.key().split(':');
        int s = indexes[0].toInt(), c = indexes[1].toInt(), r = indexes[2].toInt();
        auto element = cell_at(row_at(tables_.at(s), r), c);
        auto value = CellBase(map_->sheet(s), c, r).value();
        for (auto key : { "value-type", "value", "string-value", "boolean-value", "date-value", "time-value" })
            element.removeAttributeNS(OFFICE, key);
        element.removeAttributeNS(TABLE, "formula");
        // Preserve style and extension attributes and any non-text children.
        for (auto p : children(element, TEXT))
            if (p.localName() == "p" || p.localName() == "h")
                element.removeChild(p);
        if (it.value().startsWith('='))
            element.setAttributeNS(TABLE, "table:formula", "of:" + Odf::encodeFormula(it.value()));
        QString text = display(s, c, r);
        if (value.isNumber()) {
            element.setAttributeNS(OFFICE, "office:value-type", "float");
            element.setAttributeNS(
                OFFICE, "office:value", QString::number(double(numToDouble(value.asFloat())), 'g', 17));
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
}
