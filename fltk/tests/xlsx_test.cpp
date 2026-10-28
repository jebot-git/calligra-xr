// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include "xlsx_import.h"
#include <cassert>
#include <iostream>
#include <zip.h>
using namespace DomeOffice;
namespace {
void write_zip(const Text& file, const Map<Text, Bytes>& entries)
{
    int error;
    auto z = zip_open(File::encodeName(file).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
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

    TempDir temp;
    if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
        Workbook external;
        external.load(Text::fromUtf8(argv[2]));
        assert(external.sheet_count() == 2);
        assert(external.display(0, 2, 1) == "42");
        assert(external.display(1, 1, 1) == "84");
        assert(external.input(0, 3, 1) == Text::fromUtf8("Český 日本語"));
        assert(external.display(0, 8, 1) == "19" && external.display(0, 9, 2) == "50");
        assert(external.display(0, 11, 1) == "134" && external.display(0, 12, 1) == "114");
        rejects([&] { external.set(0, 9, 2, "0"); });
        external.set(0, 1, 1, "20");
        assert(external.display(1, 1, 1) == "100");
        assert(external.display(0, 12, 1) == "130");
        auto ods = temp.path() + "/external.ods";
        external.save(ods);
        Workbook again;
        again.load(ods);
        assert(again.display(1, 1, 1) == "100");
        assert(again.display(0, 9, 2) == "50" && again.display(0, 12, 1) == "130");
        std::cout << "PASS independently generated workbook import/edit/ODS round trip\n";
        return 0;
    }
    auto path = temp.path() + "/source.xlsx";
    Map<Text, Bytes>
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
                Text::fromUtf8(
                    R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><r><t>Český </t></r><r><t>日本語</t></r></si><si><t>=1+2</t></si></sst>)")
                    .toUtf8() },
            { "xl/styles.xml", R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><cellXfs><xf numFmtId="0"/><xf numFmtId="14"/></cellXfs></styleSheet>)" }, { "xl/worksheets/one.xml", R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><v>12</v></c><c r="B1"><f t="shared" si="0" ref="B1:B2">SUM(A1,$A$2)</f><v>42</v></c><c r="C1" t="s"><v>0</v></c><c r="D1" t="inlineStr"><is><t xml:space="preserve"> two  spaces </t></is></c><c r="E1" t="b"><v>1</v></c><c r="F1" s="1"><v>45292</v></c><c r="G1" t="s"><v>1</v></c></row><row r="2"><c r="A2"><v>30</v></c><c r="B2"><f t="shared" si="0"/><v>60</v></c></row><row r="7"><c r="D7"><v>1.25</v></c></row></sheetData></worksheet>)" },
            { "xl/worksheets/two.xml",
                R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f>First!B1*2</f><v>84</v></c><c r="B1"><f>SUM(Total)</f><v>42</v></c></row></sheetData></worksheet>)" }
        };
    write_zip(path, files);
    File source(path);
    assert(source.open(IO::ReadOnly));
    auto original = source.readAll();
    source.close();
    Workbook workbook;
    workbook.load(path);
    assert(workbook.imported_xlsx() && workbook.sheet_count() == 2);
    assert(workbook.display(0, 2, 1) == "42");
    assert(workbook.display(0, 2, 2) == "60");
    assert(workbook.display(1, 1, 1) == "84");
    assert(workbook.display(1, 2, 1) == "42");
    assert(workbook.input(0, 3, 1) == Text::fromUtf8("Český 日本語"));
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
    assert(source.open(IO::ReadOnly));
    assert(source.readAll() == original);
    source.close();
    auto ods = temp.path() + "/converted.ods";
    workbook.save(ods);
    Workbook reopened;
    reopened.load(ods);
    assert(!reopened.imported_xlsx());
    assert(reopened.display(1, 1, 1) == "100");
    assert(reopened.input(0, 3, 1) == Text::fromUtf8("Český 日本語"));
    std::cout << "PASS XLSX formulas, shared refs, names, Unicode, dates, sparse "
                 "cells, edit/ODS round trip and source protection\n";
    if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
        File::copy(path, Text::fromUtf8(argv[2]));
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
    auto arrays = files;
    const auto matrixXml
        = Text::fromUtf8(R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>
<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c><c r="D1"><f t="array" ref="D1:E2">MMULT(A1:B2,{5,6;7,8})</f><v>999</v></c><c r="E1"><v>-999</v></c><c r="G1"><f>SUM(D1:E2)</f><v>-999</v></c><c r="H1" t="str"><f t="array" ref="H1:I2">TRANSPOSE({"Český",TRUE;#N/A,4})</f><v>stale</v></c><c r="I1" t="e"><v>#VALUE!</v></c></row>
<row r="2"><c r="A2"><v>3</v></c><c r="B2"><v>4</v></c><c r="D2"><f t="array"/><v>-999</v></c><c r="E2"><v>-999</v></c><c r="H2" t="b"><v>0</v></c><c r="I2"><v>-999</v></c></row>
<row r="5"><c r="A5"><f t="array" ref="A5:B6">IFERROR(UNPORTED(),7)</f><v>901</v></c><c r="B5"><v>902</v></c><c r="C5"><f>A5+1</f><v>905</v></c></row>
<row r="6"><c r="A6"><v>903</v></c><c r="B6"><v>904</v></c></row>
<row r="9"><c r="D9"><f t="array" ref="D9">SUM(A1:A2*B1:B2)</f></c></row>
<row r="11"><c r="D11"><f t="array" ref="D11:E12">TRANSPOSE(A1:B2)</f></c><c r="G11"><f>SUM(D11:E12)</f></c></row>
</sheetData></worksheet>)")
              .toUtf8();
    arrays["xl/worksheets/one.xml"] = matrixXml;
    write_zip(path, arrays);
    if (argc == 3 && Text::fromUtf8(argv[1]) == "--array-fixture")
        assert(File::copy(path, Text::fromUtf8(argv[2])));
    assert(source.open(IO::ReadOnly));
    const auto arraySource = source.readAll();
    source.close();
    Workbook matrix;
    matrix.load(path);
    assert(matrix.display(0, 4, 1) == "19" && matrix.display(0, 5, 1) == "22");
    assert(matrix.display(0, 4, 2) == "43" && matrix.display(0, 5, 2) == "50");
    assert(matrix.display(0, 7, 1) == "134");
    assert(matrix.display(0, 8, 1) == Text::fromUtf8("Český"));
    assert(matrix.value(0, 9, 1).isError() && matrix.display(0, 9, 1) == "#N/A");
    assert(matrix.value(0, 8, 2).isBoolean() && matrix.value(0, 8, 2).asBoolean());
    assert(matrix.display(0, 9, 2) == "4");
    assert(matrix.display(0, 4, 9) == "14");
    assert(matrix.display(0, 5, 11) == "3" && matrix.display(0, 4, 12) == "2");
    assert(matrix.display(0, 5, 12) == "4" && matrix.display(0, 7, 11) == "10");
    assert(matrix.value(0, 1, 5).incomplete && matrix.value(0, 2, 6).incomplete && matrix.value(0, 3, 5).incomplete);
    rejects([&] { matrix.set(0, 5, 2, "0"); });
    rejects([&] { matrix.set(0, 4, 1, "0"); });
    matrix.set(0, 1, 1, "10");
    assert(matrix.display(0, 4, 1) == "64" && matrix.display(0, 5, 1) == "76");
    assert(matrix.display(0, 7, 1) == "233" && matrix.display(0, 4, 9) == "32");
    assert(matrix.display(0, 7, 11) == "19");
    matrix.save(ods);
    OdfPackage matrixSaved(Kind::Sheets);
    matrixSaved.load(ods);
    int matrixCount = 0;
    for (auto cell : children(matrixSaved.body(), TABLE, "table")[0].elementsByTagNameNS(TABLE, "table-cell")) {
        if (cell.hasAttributeNS(TABLE, "number-matrix-columns-spanned"))
            ++matrixCount;
    }
    assert(matrixCount == 5);
    for (int cache = 901; cache <= 905; ++cache)
        assert(matrixSaved.xml.toString().contains("office:value=\"" + Text::number(cache) + "\""));
    Workbook matrixAgain;
    matrixAgain.load(ods);
    assert(matrixAgain.display(0, 7, 1) == "233" && matrixAgain.display(0, 7, 11) == "19");
    assert(matrixAgain.display(0, 4, 9) == "32" && matrixAgain.value(0, 9, 1).isError());
    assert(source.open(IO::ReadOnly));
    assert(source.readAll() == arraySource);
    source.close();
    auto arrayRejects = [&](const Bytes& from, const Bytes& to) {
        auto malformed = arrays;
        malformed["xl/worksheets/one.xml"].replace(from, to);
        write_zip(path, malformed);
        rejects([&] { matrixAgain.load(path); });
        assert(matrixAgain.display(0, 7, 1) == "233"); // Failed-load rollback.
    };
    for (const auto& range : { "", "E1:F2", "D1:C2", "D1:E0", "D1:XFE2", "D1:E2:F3", "D1:XFD1048576" })
        arrayRejects("ref=\"D1:E2\"", (Text("ref=\"") + range + '"').toUtf8());
    arrayRejects("ref=\"D1:E2\"", "");
    arrayRejects("ref=\"H1:I2\"", "ref=\"D1:I2\"");
    arrayRejects("ref=\"D9\"", "ref=\"D9:E11\"");
    arrayRejects("<c r=\"E2\"><v>", "<c r=\"E2\"><f>1+2</f><v>");
    arrayRejects("<f t=\"array\"/>", "<f t=\"array\">1+2</f>");
    arrayRejects("t=\"array\" ref=\"D9\"", "t=\"dataTable\" ref=\"D9\"");
    arrayRejects("SUM(A1:A2*B1:B2)", "");
    arrayRejects("<f t=\"array\" ref=\"D9\">SUM(A1:A2*B1:B2)</f>", "<f>1</f><f>2</f>");
    auto boundary = arrays;
    boundary["xl/worksheets/one.xml"] = R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1048576"><c r="XFD1048576"><f t="array" ref="XFD1048576">1+2</f></c></row></sheetData></worksheet>)";
    write_zip(path, boundary);
    Workbook edge;
    edge.load(path);
    assert(edge.display(0, 16384, 1048576) == "3");
    edge.save(ods);
    Workbook edgeAgain;
    edgeAgain.load(ods);
    assert(edgeAgain.display(0, 16384, 1048576) == "3");
    std::cout << "PASS XLSX arrays, typed and sparse results, recalculation, ODS spans/caches, edit protection and "
                 "malformed ranges\n";
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
    bad["xl/worksheets/one.xml"].replace("<f t=\"shared\" si=\"0\"/>", "<f t=\"array\"/>");
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
    for (auto i = arrays.begin(); i != arrays.end(); ++i) {
        i.value().replace("http://schemas.openxmlformats.org/spreadsheetml/2006/main",
            "http://purl.oclc.org/ooxml/spreadsheetml/main");
        i.value().replace("http://schemas.openxmlformats.org/officeDocument/2006/relationships",
            "http://purl.oclc.org/ooxml/officeDocument/relationships");
    }
    write_zip(path, arrays);
    strict.load(path);
    assert(strict.display(0, 5, 2) == "50" && strict.display(0, 4, 9) == "14");
    std::cout << "PASS Strict XLSX and malformed XML, escaping targets, invalid "
                 "indices, orphan arrays and signatures rejection\n";
}
