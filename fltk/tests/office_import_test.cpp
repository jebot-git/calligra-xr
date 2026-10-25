// SPDX-License-Identifier: GPL-2.0-or-later
#include "ooxml_text_import.h"
#include "text_documents.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>
#include <zip.h>
using namespace DomeOffice;
namespace {
void write_zip(const QString& path, const QMap<QString, QByteArray>& entries)
{
    int error;
    auto z = zip_open(QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    assert(z);
    for (auto i = entries.cbegin(); i != entries.cend(); ++i) {
        auto source = zip_source_buffer(z, i.value().data(), i.value().size(), 0);
        assert(source && zip_file_add(z, i.key().toUtf8().constData(), source, 0) >= 0);
    }
    assert(zip_close(z) == 0);
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    return file.readAll();
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
QMap<QString, QByteArray> package(const QByteArray& part, const QByteArray& type)
{
    return { { "[Content_Types].xml",
                 "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Override PartName=\"/"
                     + part + "\" ContentType=\"application/vnd.openxmlformats-officedocument." + type
                     + "\"/></Types>" },
        { "_rels/.rels",
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship "
            "Id=\"main\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
            "Target=\""
                + part + "\"/></Relationships>" } };
}
QMap<QString, QByteArray> strict(QMap<QString, QByteArray> files)
{
    for (auto i = files.begin(); i != files.end(); ++i) {
        i.value().replace("http://schemas.openxmlformats.org/wordprocessingml/2006/main",
            "http://purl.oclc.org/ooxml/wordprocessingml/main");
        i.value().replace("http://schemas.openxmlformats.org/presentationml/2006/main",
            "http://purl.oclc.org/ooxml/presentationml/main");
        i.value().replace(
            "http://schemas.openxmlformats.org/drawingml/2006/main", "http://purl.oclc.org/ooxml/drawingml/main");
        i.value().replace("http://schemas.openxmlformats.org/officeDocument/2006/relationships",
            "http://purl.oclc.org/ooxml/officeDocument/relationships");
    }
    return files;
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    assert(temp.isValid());
    if (argc == 4 && QString::fromUtf8(argv[1]) == "--external") {
        TextDocument word;
        word.load(QString::fromUtf8(argv[2]));
        assert(word.text().contains(QString::fromUtf8("Český 日本語")));
        assert(word.text().contains("Table cell"));
        word.set_text(word.text().replace("Table cell", "Edited cell"));
        word.save(temp.path() + "/external.odt");
        TextDocument again;
        again.load(temp.path() + "/external.odt");
        assert(again.text().contains("Edited cell"));
        Presentation slides;
        slides.load(QString::fromUtf8(argv[3]));
        assert(slides.count() == 2);
        assert(slides.slide(0).boxes[0].text == QString::fromUtf8("Český 日本語"));
        assert(slides.slide(0).boxes.size() == 2);
        assert(std::abs(slides.slide(1).boxes[0].width - 21.59) < .001);
        assert(slides.image(slides.slide(1).boxes[2].image).startsWith(QByteArray::fromHex("ffd8ff")));
        assert(!slides.image(slides.slide(0).boxes[1].image).isEmpty());
        assert(std::abs(slides.slide(0).width - 25.4) < .001);
        slides.set_box_text(0, 0, "Edited title");
        slides.save(temp.path() + "/external.odp");
        Presentation reopened;
        reopened.load(temp.path() + "/external.odp");
        assert(reopened.slide(0).boxes[0].text == "Edited title");
        assert(reopened.image(reopened.slide(0).boxes[1].image) == slides.image(slides.slide(0).boxes[1].image));
        std::cout << "PASS independent DOCX/PPTX producer import, edits and ODF round trips\n";
        return 0;
    }
    auto docx = temp.path() + "/source.DOCX", pptx = temp.path() + "/source.pptx";
    auto odt = temp.path() + "/converted.odt", odp = temp.path() + "/converted.odp";
    auto wordFiles = package("word/main.xml", "wordprocessingml.document.main+xml");
    wordFiles["word/main.xml"] = QString::fromUtf8(
        R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t xml:space="preserve">Český 日本語  </w:t><w:tab/><w:t>tab</w:t><w:br/><w:t>line</w:t></w:r><w:del><w:r><w:delText>deleted</w:delText></w:r></w:del><w:ins><w:r><w:t> inserted</w:t></w:r></w:ins><w:hyperlink><w:r><w:t> link</w:t></w:r></w:hyperlink></w:p><w:tbl><w:tr><w:tc><w:p><w:r><w:t>Cell</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:p/><w:sdt><w:sdtContent><w:p><w:r><w:t>Content control</w:t></w:r></w:p></w:sdtContent></w:sdt></w:body></w:document>)")
                                     .toUtf8();
    write_zip(docx, wordFiles);
    auto originalWord = bytes(docx);
    TextDocument word;
    word.load(docx);
    assert(word.imported_office());
    assert(word.text() == QString::fromUtf8("Český 日本語  \ttab\nline inserted link\nCell\n\nContent control"));
    // Committing an unchanged editor must retain mixed inline/paragraph breaks.
    word.set_text(word.text());
    rejects([&] { word.save(docx); });
    rejects([&] { word.save(temp.path() + "/wrong.pptx"); });
    assert(bytes(docx) == originalWord && word.imported_office());
    word.save(odt);
    assert(!word.imported_office());
    TextDocument reopened;
    reopened.load(odt);
    assert(reopened.text() == word.text());
    auto odtEntries = read_document_archive(odt);
    assert(odtEntries["mimetype"] == mime(Kind::Words).toUtf8());
    assert(read_document_xml(odtEntries["content.xml"]).elementsByTagNameNS(TABLE, "table-cell").size() == 1);
    // Flat documents support structural paragraph edits after import.
    auto flat = wordFiles;
    flat["word/main.xml"]
        = R"(<document xmlns="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><body><p><r><t>First</t></r></p></body></document>)";
    write_zip(docx, flat);
    word.load(docx);
    word.set_text("First\nSecond");
    word.save(odt);
    reopened.load(odt);
    assert(reopened.text() == "First\nSecond");
    write_zip(docx, strict(wordFiles));
    word.load(docx);
    assert(word.text().contains("Content control"));

    auto slideFiles = package("deck/main.xml", "presentationml.presentation.main+xml");
    slideFiles["deck/main.xml"]
        = R"(<p:presentation xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><p:sldIdLst><p:sldId id="256" r:id="second"/><p:sldId id="257" r:id="first"/></p:sldIdLst><p:sldSz cx="9144000" cy="5143500"/></p:presentation>)";
    slideFiles["deck/_rels/main.xml.rels"]
        = R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="first" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/one.xml"/><Relationship Id="second" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="/deck/slides/two.xml"/></Relationships>)";
    slideFiles["deck/slides/one.xml"]
        = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"><p:cSld><p:spTree/></p:cSld></p:sld>)";
    slideFiles["deck/slides/two.xml"] = QString::fromUtf8(
        R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><p:cSld><p:spTree><p:sp><p:spPr><a:xfrm><a:off x="-360000" y="720000"/><a:ext cx="3600000" cy="1080000"/></a:xfrm></p:spPr><p:txBody><a:bodyPr/><a:p><a:r><a:t>Český 日本語</a:t></a:r><a:br/><a:fld><a:t>field</a:t></a:fld></a:p><a:p><a:r><a:t>Second paragraph</a:t></a:r></a:p></p:txBody></p:sp><p:pic><p:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="360000" cy="720000"/></a:xfrm></p:spPr><p:blipFill><a:blip r:embed="image"/></p:blipFill></p:pic><p:pic><p:blipFill><a:blip r:link="external"/></p:blipFill></p:pic></p:spTree></p:cSld></p:sld>)")
                                            .toUtf8();
    slideFiles["deck/slides/_rels/two.xml.rels"]
        = R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="image" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/pic.png"/><Relationship Id="external" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="https://invalid.example/no-network" TargetMode="External"/></Relationships>)";
    const auto png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAEklEQVR4nGNQaHig0PCAAUIBACcOBgEpwnGPAAAAAElFTkSuQmCC");
    slideFiles["deck/media/pic.png"] = png;
    write_zip(pptx, slideFiles);
    auto originalSlides = bytes(pptx);
    Presentation stage;
    stage.load(pptx);
    assert(stage.imported_office() && stage.count() == 2);
    auto s = stage.slide(0);
    assert(s.boxes.size() == 2 && stage.slide(1).boxes.isEmpty());
    assert(s.boxes[0].text == QString::fromUtf8("Český 日本語\nfield\nSecond paragraph"));
    assert(std::abs(s.width - 25.4) < .00001 && std::abs(s.height - 14.2875) < .00001);
    assert(s.boxes[0].x == -1 && s.boxes[0].y == 2 && s.boxes[0].width == 10);
    assert(stage.image(s.boxes[1].image) == png);
    stage.set_box_text(0, 0, "Edited title\nMore text");
    rejects([&] { stage.save(pptx); });
    rejects([&] { stage.save(temp.path() + "/wrong.docx"); });
    stage.save(odp);
    assert(bytes(pptx) == originalSlides && !stage.imported_office());
    Presentation again;
    again.load(odp);
    assert(again.slide(0).boxes[0].text == "Edited title\nMore text");
    assert(again.image(again.slide(0).boxes[1].image) == png);
    auto entries = read_document_archive(odp);
    assert(entries["mimetype"] == mime(Kind::Stage).toUtf8());
    assert(entries["META-INF/manifest.xml"].contains("image/png"));
    write_zip(pptx, strict(slideFiles));
    stage.load(pptx);
    assert(stage.slide(0).boxes[0].text == s.boxes[0].text);
    if (argc == 3 && QString::fromUtf8(argv[1]) == "--fixtures") {
        write_zip(QString::fromUtf8(argv[2]) + "/import.docx", flat);
        write_zip(QString::fromUtf8(argv[2]) + "/import.pptx", slideFiles);
    }
    // Failures must leave the live document intact.
    auto prior = stage.slide(0).boxes[0].text;
    auto bad = slideFiles;
    bad["deck/slides/_rels/two.xml.rels"].replace("../media/pic.png", "../../../escape.png");
    write_zip(pptx, bad);
    rejects([&] { stage.load(pptx); });
    assert(stage.slide(0).boxes[0].text == prior && stage.imported_office());
    bad = slideFiles;
    bad["deck/slides/two.xml"] = "<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><x>&x;</x>";
    write_zip(pptx, bad);
    rejects([&] { stage.load(pptx); });
    bad = slideFiles;
    bad["deck/slides/two.xml"].replace("cx=\"3600000\"", "cx=\"nan\"");
    write_zip(pptx, bad);
    rejects([&] { stage.load(pptx); });
    bad = slideFiles;
    bad["deck/_rels/main.xml.rels"].replace(
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\"",
        "Type=\"https://invalid/slide\"");
    write_zip(pptx, bad);
    rejects([&] { stage.load(pptx); });
    for (bool slides : { false, true }) {
        auto base = slides ? slideFiles : wordFiles;
        auto path = slides ? pptx : docx;
        auto load = [&] {
            if (slides)
                stage.load(path);
            else
                word.load(path);
        };
        bad = base;
        bad[slides ? "deck/slides/two.xml" : "word/main.xml"]
            = "<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><x>&x;</x>";
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["_xmlsignatures/sig.xml"] = "signature";
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["hidden/vbaProject.bin"] = "macro";
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["[Content_Types].xml"].replace("openxmlformats-officedocument", "ms-word.macroEnabled");
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["../escape.xml"] = "bad archive member";
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["_rels/.rels"].replace("Target=\"", "TargetMode=\"External\" Target=\"");
        write_zip(path, bad);
        rejects(load);
        bad = base;
        bad["[Content_Types].xml"].replace("main+xml", "wrong+xml");
        write_zip(path, bad);
        rejects(load);
    }
    std::cout << "PASS DOCX/PPTX Unicode, whitespace, tables, slide ordering, geometry, images, Strict namespaces, "
                 "edits/ODF round trips, source protection and invalid package rejection\n";
}
