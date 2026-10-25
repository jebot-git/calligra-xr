// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "workbook.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
#include <zip.h>
using namespace DomeOffice;
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
void add_entry(const QString& file, const char* name, const QByteArray& bytes)
{
    int e;
    auto* z = zip_open(QFile::encodeName(file).data(), 0, &e);
    assert(z);
    auto* s = zip_source_buffer(z, bytes.data(), bytes.size(), 0);
    assert(zip_file_add(z, name, s, ZIP_FL_OVERWRITE) >= 0);
    assert(!zip_close(z));
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    QTemporaryDir dir;
    assert(dir.isValid());
    auto path = [&](const char* s) { return dir.path() + '/' + s; };
    Workbook w;
    w.set(0, 1, 1, "12");
    w.set(0, 1, 2, "30");
    w.set(0, 2, 1, "=SUM(A1:A2)");
    assert(w.display(0, 2, 1) == "42");
    w.set(0, 1, 1, "20");
    assert(w.display(0, 2, 1) == "50");
    w.set(0, 3, 1, QString::fromUtf8("Český 日本語"));
    w.add_sheet("Other");
    w.set(1, 1, 1, "=Sheet1!B1*2");
    assert(w.display(1, 1, 1) == "100");
    w.save(path("book.ods"));
    add_entry(path("book.ods"), "Pictures/opaque.bin", QByteArray("\0binary\xff", 8));
    Workbook loaded;
    loaded.load(path("book.ods"));
    assert(loaded.sheet_count() == 2);
    assert(loaded.display(0, 2, 1) == "50");
    assert(loaded.display(1, 1, 1) == "100");
    assert(loaded.input(0, 3, 1) == QString::fromUtf8("Český 日本語"));
    loaded.set(0, 1, 1, "22");
    assert(loaded.display(1, 1, 1) == "104");
    loaded.save(path("saved.ods"));
    OdfPackage preserved(Kind::Sheets);
    preserved.load(path("saved.ods"));
    assert(preserved.entry("Pictures/opaque.bin") == QByteArray("\0binary\xff", 8));
    OdfPackage repeated(Kind::Sheets);
    auto table = repeated.xml.createElementNS(TABLE, "table:table");
    table.setAttributeNS(TABLE, "table:name", "Repeated");
    auto row = repeated.xml.createElementNS(TABLE, "table:table-row");
    row.setAttributeNS(TABLE, "table:number-rows-repeated", 3);
    auto cell = repeated.xml.createElementNS(TABLE, "table:table-cell");
    cell.setAttributeNS(TABLE, "table:number-columns-repeated", 4);
    cell.setAttributeNS(OFFICE, "office:value-type", "float");
    cell.setAttributeNS(OFFICE, "office:value", 7);
    row.appendChild(cell);
    table.appendChild(row);
    repeated.body().appendChild(table);
    repeated.save(path("repeated.ods"));
    Workbook r;
    r.load(path("repeated.ods"));
    r.set(0, 2, 2, "99");
    r.save(path("split.ods"));
    Workbook split;
    split.load(path("split.ods"));
    assert(split.display(0, 2, 2) == "99");
    assert(split.display(0, 1, 1) == "7" && split.display(0, 3, 2) == "7" && split.display(0, 2, 3) == "7");
    std::cout << "PASS: upstream formula engine, dependencies, cross-sheet refs, ODS round trips and repeated-cell "
                 "splitting\n";
    TextDocument words;
    words.set_text(QString::fromUtf8("Hello  svět\nSecond\tparagraph"));
    words.save(path("words.odt"));
    TextDocument words2;
    words2.load(path("words.odt"));
    assert(words2.text() == words.text());
    words2.set_text(QString::fromUtf8("Hello  日本語\nSecond\tparagraph\nThird"));
    words2.save(path("words2.odt"));
    TextDocument words3;
    words3.load(path("words2.odt"));
    assert(words3.text() == words2.text());
    OdfPackage rich(Kind::Words);
    auto para = rich.xml.createElementNS(TEXT, "text:p");
    auto span = rich.xml.createElementNS(TEXT, "text:span");
    span.setAttributeNS(TEXT, "text:style-name", "Bold");
    span.appendChild(rich.xml.createTextNode("bold"));
    para.appendChild(rich.xml.createTextNode("Hello "));
    para.appendChild(span);
    rich.body().appendChild(para);
    rich.save(path("rich.odt"));
    TextDocument richtext;
    richtext.load(path("rich.odt"));
    richtext.set_text("Hello bold world");
    richtext.save(path("rich-saved.odt"));
    OdfPackage richresult(Kind::Words);
    richresult.load(path("rich-saved.odt"));
    assert(
        richresult.xml.elementsByTagNameNS(TEXT, "span").at(0).toElement().attributeNS(TEXT, "style-name") == "Bold");
    assert(richtext.text() == "Hello bold world");
    std::cout << "PASS: Words Unicode, whitespace, paragraphs and retained character-style spans\n";
    Presentation stage;
    stage.set_box_text(0, 0, QString::fromUtf8("Úvod 日本語"));
    stage.set_box_text(0, 1, "First\nSecond");
    stage.add_slide("Conclusion");
    stage.save(path("slides.odp"));
    Presentation stage2;
    stage2.load(path("slides.odp"));
    assert(stage2.count() == 2);
    assert(stage2.slide(0).boxes[0].text == QString::fromUtf8("Úvod 日本語"));
    assert(stage2.slide(0).boxes[1].text == "First\nSecond");
    stage2.remove_slide(0);
    assert(stage2.count() == 1);
    rejects([&] { stage2.remove_slide(0); });
    stage2.save(path("slides2.odp"));
    OdfPackage illustrated(Kind::Stage);
    illustrated.load(path("slides.odp"));
    auto page = children(illustrated.body(), DRAW, "page").first();
    auto frame = illustrated.xml.createElementNS(DRAW, "draw:frame");
    for (auto attribute : { "x", "y", "width", "height" })
        frame.setAttributeNS(SVG, "svg:" + QString(attribute),
            QString(attribute) == "x"       ? "24cm"
                : QString(attribute) == "y" ? "1cm"
                                            : "2cm");
    auto image = illustrated.xml.createElementNS(DRAW, "draw:image");
    image.setAttributeNS(XLINK, "xlink:href", "Pictures/test.png");
    frame.appendChild(image);
    page.appendChild(frame);
    illustrated.save(path("illustrated.odp"));
    QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
    add_entry(path("illustrated.odp"), "Pictures/test.png", png);
    Presentation embedded;
    embedded.load(path("illustrated.odp"));
    assert(embedded.slide(0).boxes.last().image == "Pictures/test.png");
    assert(embedded.image("Pictures/test.png") == png);
    std::cout << "PASS: Stage slides, text frames, Unicode, insert/delete and ODP round trip\n";
    add_entry(path("words.odt"), "META-INF/documentsignatures.xml", "signed");
    TextDocument signedDoc;
    signedDoc.load(path("words.odt"));
    assert(signedDoc.read_only());
    rejects([&] { signedDoc.set_text("changed"); });
    rejects([&] { signedDoc.save(path("signed-copy.odt")); });
    add_entry(
        path("words2.odt"), "content.xml", "<!DOCTYPE test [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><test>&x;</test>");
    rejects([&] {
        TextDocument malformed;
        malformed.load(path("words2.odt"));
    });
    QString declaration = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?><!DOCTYPE test [<!ENTITY x \"entity\">]><test>&x;</test>");
    QByteArray utf16("\xff\xfe", 2);
    for (QChar ch : declaration) {
        utf16.append(char(ch.unicode() & 255));
        utf16.append(char(ch.unicode() >> 8));
    }
    add_entry(path("words2.odt"), "content.xml", utf16);
    rejects([&] {
        TextDocument encoded;
        encoded.load(path("words2.odt"));
    });
    rejects([&] {
        TextDocument wrong;
        wrong.load(path("saved.ods"));
    });
    std::cout << "PASS: signed-document protection, entity rejection and file-type validation\n";
    if (argc == 3 && QString::fromUtf8(argv[1]) == "--fixtures") {
        QDir().mkpath(QString::fromUtf8(argv[2]));
        auto destination = QString::fromUtf8(argv[2]);
        QFile::remove(destination + "/sheets.ods");
        QFile::copy(path("saved.ods"), destination + "/sheets.ods");
        QFile::remove(destination + "/words.odt");
        words3.save(destination + "/words.odt");
        QFile::remove(destination + "/stage.odp");
        QFile::copy(path("illustrated.odp"), destination + "/stage.odp");
    }
}
