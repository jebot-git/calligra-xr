// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include "xlsx_import.h"
#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
#include <zip.h>
using namespace DomeOffice;
namespace {
void write_zip(const QString& file, const QMap<QString, QByteArray>& entries)
{
    int error;
    auto z = zip_open(QFile::encodeName(file).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    assert(z);
    for (auto i = entries.cbegin(); i != entries.cend(); ++i) {
        auto source = zip_source_buffer(z, i.value().data(), i.value().size(), 0);
        assert(source && zip_file_add(z, i.key().toUtf8().constData(), source, 0) >= 0);
    }
    assert(zip_close(z) == 0);
}
template <class F> void rejects(F f)
{
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    QTemporaryDir temp;
    if (argc == 3 && QString::fromUtf8(argv[1]) == "--external") {
        Workbook external;
        external.load(QString::fromUtf8(argv[2]));
        assert(external.sheet_count() == 2);
        assert(external.display(0, 2, 1) == "42");
        assert(external.display(1, 1, 1) == "84");
        assert(external.input(0, 3, 1) == QString::fromUtf8("Český 日本語"));
        external.set(0, 1, 1, "20");
        assert(external.display(1, 1, 1) == "100");
        auto ods = temp.path() + "/external.ods";
        external.save(ods);
        Workbook again;
        again.load(ods);
        assert(again.display(1, 1, 1) == "100");
        std::cout << "PASS independently generated workbook import/edit/ODS round trip\n";
        return 0;
    }
    auto path = temp.path() + "/source.xlsx";
    QMap<QString, QByteArray>
        files {
            { "[Content_Types].xml",
                R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Override PartName="/xl/book.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/></Types>)" },
            { "_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="r" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/book.xml"/></Relationships>)" },
            { "xl/book.xml",
                R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="First" sheetId="1" r:id="one"/><sheet name="Other sheet" sheetId="2" r:id="two"/></sheets><definedNames><definedName name="Total">First!$A$1:$A$2</definedName></definedNames></workbook>)" },
            { "xl/_rels/book.xml.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="one" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/one.xml"/><Relationship Id="two" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/two.xml"/><Relationship Id="ss" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="strings.xml"/><Relationship Id="sty" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>)" },
            { "xl/strings.xml",
                QString::fromUtf8(
                    R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><r><t>Český </t></r><r><t>日本語</t></r></si><si><t>=1+2</t></si></sst>)")
                    .toUtf8() },
            { "xl/styles.xml", R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><cellXfs><xf numFmtId="0"/><xf numFmtId="14"/></cellXfs></styleSheet>)" }, { "xl/worksheets/one.xml", R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><v>12</v></c><c r="B1"><f t="shared" si="0" ref="B1:B2">SUM(A1,$A$2)</f><v>42</v></c><c r="C1" t="s"><v>0</v></c><c r="D1" t="inlineStr"><is><t xml:space="preserve"> two  spaces </t></is></c><c r="E1" t="b"><v>1</v></c><c r="F1" s="1"><v>45292</v></c><c r="G1" t="s"><v>1</v></c></row><row r="2"><c r="A2"><v>30</v></c><c r="B2"><f t="shared" si="0"/><v>60</v></c></row><row r="7"><c r="D7"><v>1.25</v></c></row></sheetData></worksheet>)" },
            { "xl/worksheets/two.xml",
                R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f>First!B1*2</f><v>84</v></c><c r="B1"><f>SUM(Total)</f><v>42</v></c></row></sheetData></worksheet>)" }
        };
    write_zip(path, files);
    QFile source(path);
    assert(source.open(QIODevice::ReadOnly));
    auto original = source.readAll();
    source.close();
    Workbook workbook;
    workbook.load(path);
    assert(workbook.imported_xlsx() && workbook.sheet_count() == 2);
    assert(workbook.display(0, 2, 1) == "42");
    assert(workbook.display(0, 2, 2) == "60");
    assert(workbook.display(1, 1, 1) == "84");
    assert(workbook.display(1, 2, 1) == "42");
    assert(workbook.input(0, 3, 1) == QString::fromUtf8("Český 日本語"));
    assert(workbook.input(0, 4, 1) == " two  spaces ");
    assert(workbook.display(0, 7, 1) == "=1+2");
    assert(workbook.display(0, 4, 7) == "1.25");
    auto package = import_xlsx(path);
    auto date = package.xml.elementsByTagNameNS(TABLE, "table-cell");
    bool foundDate = false;
    for (int i = 0; i < date.size(); ++i)
        if (date.at(i).toElement().attributeNS(OFFICE, "date-value").startsWith("2024-01-01"))
            foundDate = true;
    assert(foundDate);
    workbook.set(0, 1, 1, "20");
    assert(workbook.display(1, 1, 1) == "100");
    rejects([&] { workbook.save(path); });
    assert(source.open(QIODevice::ReadOnly));
    assert(source.readAll() == original);
    source.close();
    auto ods = temp.path() + "/converted.ods";
    workbook.save(ods);
    Workbook reopened;
    reopened.load(ods);
    assert(!reopened.imported_xlsx());
    assert(reopened.display(1, 1, 1) == "100");
    assert(reopened.input(0, 3, 1) == QString::fromUtf8("Český 日本語"));
    std::cout << "PASS XLSX formulas, shared refs, names, Unicode, dates, sparse "
                 "cells, edit/ODS round trip and source protection\n";
    if (argc == 3 && QString::fromUtf8(argv[1]) == "--fixture")
        QFile::copy(path, QString::fromUtf8(argv[2]));
    auto dated = files;
    dated["xl/book.xml"].replace("<sheets>", "<workbookPr date1904=\"1\"/><sheets>");
    dated["xl/worksheets/one.xml"].replace("<v>45292</v>", "<v>0</v>");
    write_zip(path, dated);
    auto epoch = import_xlsx(path);
    assert(epoch.xml.toString().contains("1904-01-01"));
    auto timed = files;
    timed["xl/styles.xml"].replace("numFmtId=\"14\"", "numFmtId=\"46\"");
    timed["xl/worksheets/one.xml"].replace("<v>45292</v>", "<v>1.5</v>");
    write_zip(path, timed);
    auto duration = import_xlsx(path);
    assert(duration.xml.toString().contains("PT129600S"));
    std::cout << "PASS 1904 date epoch and elapsed time conversion\n";
    auto bad = files;
    bad["xl/worksheets/one.xml"] = "<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><x>&x;</x>";
    write_zip(path, bad);
    rejects([&] { workbook.load(path); });
    bad = files;
    bad["xl/_rels/book.xml.rels"].replace("Target=\"worksheets/one.xml\"", "Target=\"../../outside.xml\"");
    write_zip(path, bad);
    rejects([&] { workbook.load(path); });
    bad = files;
    bad["xl/worksheets/one.xml"].replace("<v>0</v>", "<v>999</v>");
    write_zip(path, bad);
    rejects([&] { workbook.load(path); });
    bad = files;
    bad["xl/worksheets/one.xml"].replace("t=\"shared\"", "t=\"array\"");
    write_zip(path, bad);
    rejects([&] { workbook.load(path); });
    bad = files;
    bad["_xmlsignatures/sig1.xml"] = "signed";
    write_zip(path, bad);
    rejects([&] { workbook.load(path); });
    // Strict namespace dialect, alternate workbook path and relationship targets.
    bad = files;
    for (auto i = bad.begin(); i != bad.end(); ++i) {
        i.value().replace("http://schemas.openxmlformats.org/spreadsheetml/2006/main",
            "http://purl.oclc.org/ooxml/spreadsheetml/main");
        i.value().replace("http://schemas.openxmlformats.org/officeDocument/2006/relationships",
            "http://purl.oclc.org/ooxml/officeDocument/relationships");
    }
    write_zip(path, bad);
    Workbook strict;
    strict.load(path);
    assert(strict.display(0, 2, 1) == "42");
    std::cout << "PASS Strict XLSX and malformed XML, escaping targets, invalid "
                 "indices, arrays and signatures rejection\n";
}
