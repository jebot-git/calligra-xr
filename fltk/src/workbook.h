// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <QSet>
#include <engine/MapBase.h>
#include <map>
#include <memory>
namespace DomeOffice {
class Workbook {
public:
    Workbook();
    void load(const QString& path);
    void save(const QString& path);
    QString input(int sheet, int col, int row) const;
    QString display(int sheet, int col, int row) const;
    void validate_cell(int sheet, int col, int row);
    void set(int sheet, int col, int row, const QString& value);
    int sheet_count() const;
    QString sheet_name(int sheet) const;
    void add_sheet(const QString& name);
    int rows(int sheet) const;
    int columns(int sheet) const;
    bool read_only() const { return package_.read_only() || protected_; }

private:
    OdfPackage package_ { Kind::Sheets };
    std::unique_ptr<Calligra::Sheets::MapBase> map_;
    QList<QDomElement> tables_;
    QMap<QString, QString> changed_;
    QSet<QString> formulas_;
    bool protected_ = false;
    void initialize();
    void recalculate();
};
}
