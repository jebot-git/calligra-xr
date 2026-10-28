// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include "native_sheets.h"
#include <map>
#include <memory>
namespace DomeOffice {
class Workbook {
public:
    Workbook();
    void load(const Text& path);
    void save(const Text& path);
    Text input(int sheet, int col, int row) const;
    Text display(int sheet, int col, int row) const;
    void validate_cell(int sheet, int col, int row);
    void set(int sheet, int col, int row, const Text& value);
    int sheet_count() const;
    Text sheet_name(int sheet) const;
    void add_sheet(const Text& name);
    int rows(int sheet) const;
    int columns(int sheet) const;
    bool imported_xlsx() const { return imported_xlsx_; }
    bool read_only() const { return package_.read_only() || protected_; }

private:
    OdfPackage package_ { Kind::Sheets };
    std::unique_ptr<SheetEngine> map_;
    List<XmlElement> tables_;
    Map<Text, Text> changed_;
    Set<Text> formulas_;
    bool protected_ = false;
    bool imported_xlsx_ = false;
    void load_package(OdfPackage candidate);
    void initialize();
    void recalculate();
};
} // namespace DomeOffice
