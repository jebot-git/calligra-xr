// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <zip.h>
namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const QString& error) { throw std::runtime_error(error.toStdString()); }
    QString local(Kind kind)
    {
        return kind == Kind::Words ? "text" : kind == Kind::Sheets ? "spreadsheet" : "presentation";
    }
    void validate_xml(const QByteArray& data)
    {
        QXmlStreamReader reader(data);
        int depth = 0;
        while (!reader.atEnd()) {
            auto token = reader.readNext();
            if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
                fail("Document entity declarations are not supported");
            if (token == QXmlStreamReader::StartElement && ++depth > 256)
                fail("Document XML nesting is too deep");
            if (token == QXmlStreamReader::EndElement)
                --depth;
        }
        if (reader.hasError())
            fail("Malformed document XML: " + reader.errorString());
    }
}
QString mime(Kind kind) { return "application/vnd.oasis.opendocument." + local(kind); }
QString suffix(Kind kind) { return kind == Kind::Words ? "odt" : kind == Kind::Sheets ? "ods" : "odp"; }
QDomElement child(const QDomElement& parent, const QString& ns, const QString& name)
{
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        auto e = n.toElement();
        if (e.namespaceURI() == ns && e.localName() == name)
            return e;
    }
    return { };
}
QList<QDomElement> children(const QDomElement& parent, const QString& ns, const QString& name)
{
    QList<QDomElement> result;
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        auto e = n.toElement();
        if (e.namespaceURI() == ns && (name.isEmpty() || e.localName() == name))
            result << e;
    }
    return result;
}
QString plain_text(const QDomElement& element)
{
    QString result;
    for (auto node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isText() || node.isCDATASection())
            result += node.nodeValue();
        else if (node.isElement()) {
            auto e = node.toElement();
            if (e.namespaceURI() == TEXT && e.localName() == "s") {
                bool ok = false;
                int count = e.attributeNS(TEXT, "c", "1").toInt(&ok);
                if (!ok || count < 1 || count > 65536)
                    fail("Invalid repeated spaces in document");
                result += QString(count, ' ');
            } else if (e.namespaceURI() == TEXT && e.localName() == "tab")
                result += '\t';
            else if (e.namespaceURI() == TEXT && e.localName() == "line-break")
                result += '\n';
            else
                result += plain_text(e);
        }
    }
    return result;
}
void set_plain_text(QDomElement element, const QString& value)
{
    while (!element.firstChild().isNull())
        element.removeChild(element.firstChild());
    auto doc = element.ownerDocument();
    QString run;
    auto flush = [&] {
        if (!run.isEmpty()) {
            element.appendChild(doc.createTextNode(run));
            run.clear();
        }
    };
    for (int i = 0; i < value.size(); ++i) {
        QChar c = value[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            flush();
            auto node = doc.createElementNS(TEXT, c == ' ' ? "text:s" : c == '\t' ? "text:tab" : "text:line-break");
            if (c == ' ') {
                int count = 1;
                while (i + 1 < value.size() && value[i + 1] == ' ') {
                    ++i;
                    ++count;
                }
                node.setAttributeNS(TEXT, "text:c", count);
            }
            element.appendChild(node);
        } else
            run += c;
    }
    flush();
}
OdfPackage::OdfPackage(Kind kind)
    : kind_(kind)
{
    const QByteArray start = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><office:document-content "
                              "xmlns:office=\""
        + OFFICE + "\" xmlns:text=\"" + TEXT + "\" xmlns:table=\"" + TABLE + "\" xmlns:draw=\"" + DRAW
        + "\" xmlns:svg=\"" + SVG + "\" xmlns:xlink=\"" + XLINK
        + "\" "
          "xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" "
          "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" office:version=\"1.3\">"
          "<office:automatic-styles/><office:body><office:"
        + local(kind) + "/></office:body></office:document-content>")
                                 .toUtf8();
    if (!xml.setContent(start, QDomDocument::ParseOption::UseNamespaceProcessing))
        fail("Cannot create document");
    entries_["mimetype"] = mime(kind).toUtf8();
    const QString STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                  FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
    entries_["styles.xml"] = ("<?xml version=\"1.0\"?><office:document-styles xmlns:office=\"" + OFFICE
        + "\" xmlns:style=\"" + STYLE + "\" xmlns:fo=\"" + FO
        + "\" office:version=\"1.3\"><office:styles/><office:automatic-styles><style:page-layout "
          "style:name=\"pm1\"><style:page-layout-properties fo:page-width=\""
        + (kind == Kind::Stage ? QString("28cm") : QString("21cm")) + "\" fo:page-height=\""
        + (kind == Kind::Stage ? QString("15.75cm") : QString("29.7cm"))
        + "\"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page "
          "style:name=\"Default\" style:page-layout-name=\"pm1\"/></office:master-styles></office:document-styles>")
                                 .toUtf8();

    entries_["META-INF/manifest.xml"]
        = ("<?xml version=\"1.0\"?><manifest:manifest "
           "xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
           "manifest:version=\"1.3\"><manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\""
            + mime(kind)
            + "\"/><manifest:file-entry manifest:full-path=\"content.xml\" "
              "manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"styles.xml\" "
              "manifest:media-type=\"text/xml\"/></manifest:manifest>")
              .toUtf8();
}
QDomElement OdfPackage::body() const
{
    return child(child(xml.documentElement(), OFFICE, "body"), OFFICE, local(kind_));
}
void OdfPackage::load(const QString& path)
{
    int error = 0;
    std::unique_ptr<zip_t, decltype(&zip_discard)> zip(
        zip_open(QFile::encodeName(path).constData(), ZIP_RDONLY, &error), zip_discard);
    if (!zip)
        fail("Cannot open OpenDocument archive");
    QMap<QString, QByteArray> contents;
    quint64 total = 0;
    auto count = zip_get_num_entries(zip.get(), 0);
    if (count < 0 || count > 10000)
        fail("Too many document archive entries");
    for (zip_int64_t i = 0; i < count; ++i) {
        zip_stat_t info;
        zip_stat_init(&info);
        if (zip_stat_index(zip.get(), i, 0, &info))
            fail("Cannot inspect archive entry");
        if (info.encryption_method != ZIP_EM_NONE)
            fail("Encrypted documents are not supported yet");
        QString name = QString::fromUtf8(info.name);
        if (name.startsWith('/') || name.split('/').contains("..") || contents.contains(name))
            fail("Invalid or duplicate document archive name");
        if (info.size > 64 * 1024 * 1024 || (total += info.size) > 256 * 1024 * 1024)
            fail("Document exceeds the current import size limit");
        QByteArray data;
        data.resize(info.size);
        zip_file_t* file = zip_fopen_index(zip.get(), i, 0);
        if (!file)
            fail("Cannot read document archive entry");
        zip_int64_t offset = 0;
        while (offset < data.size()) {
            auto n = zip_fread(file, data.data() + offset, data.size() - offset);
            if (n <= 0) {
                zip_fclose(file);
                fail("Truncated document archive entry");
            }
            offset += n;
        }
        if (zip_fclose(file))
            fail("Corrupt document archive entry");
        contents[name] = data;
    }
    if (contents.value("mimetype") != mime(kind_).toUtf8())
        fail("Open this file with the matching Words, Sheets or Stage application");
    const auto content = contents.value("content.xml");
    validate_xml(content);
    if (contents.contains("styles.xml"))
        validate_xml(contents.value("styles.xml"));
    QDomDocument parsed;
    auto result = parsed.setContent(content, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!result)
        fail("Malformed document XML: " + result.errorMessage);
    auto b = child(child(parsed.documentElement(), OFFICE, "body"), OFFICE, local(kind_));
    if (b.isNull())
        fail("Document has no expected content body");
    xml = parsed;
    entries_ = contents;
    read_only_ = false;
    for (const auto& name : entries_.keys())
        if (name.startsWith("META-INF/") && name.contains("signature", Qt::CaseInsensitive))
            read_only_ = true;
}
void OdfPackage::save(const QString& path)
{
    if (read_only_)
        fail("Signed documents are read-only in this port; signatures must not be silently invalidated");
    auto contents = entries_;
    contents["content.xml"] = xml.toByteArray(-1);
    QTemporaryFile temporary(QFileInfo(path).absolutePath() + "/.calligra-XXXXXX");
    if (!temporary.open())
        fail("Cannot create document in the selected directory");
    QString temp = temporary.fileName();
    temporary.close();
    int error = 0;
    zip_t* zip = zip_open(QFile::encodeName(temp).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!zip)
        fail("Cannot create OpenDocument archive");
    auto add = [&](const QString& name, const QByteArray& value, bool stored) {
        auto* source = zip_source_buffer(zip, value.constData(), value.size(), 0);
        if (!source)
            fail("Cannot prepare document archive entry");
        auto index = zip_file_add(zip, name.toUtf8().constData(), source, ZIP_FL_ENC_UTF_8);
        if (index < 0) {
            zip_source_free(source);
            fail("Cannot add document archive entry");
        }
        if (stored && zip_set_file_compression(zip, index, ZIP_CM_STORE, 0))
            fail("Cannot store document media type");
    };
    try {
        add("mimetype", contents["mimetype"], true);
        for (auto it = contents.cbegin(); it != contents.cend(); ++it)
            if (it.key() != "mimetype")
                add(it.key(), it.value(), false);
    } catch (...) {
        zip_discard(zip);
        throw;
    }
    if (zip_close(zip)) {
        zip_discard(zip);
        fail("Cannot finish document archive");
    }
    QFile synced(temp);
    if (!synced.open(QIODevice::ReadOnly) || fsync(synced.handle()))
        fail("Cannot synchronize document");
    synced.close();
    if (::rename(QFile::encodeName(temp).constData(), QFile::encodeName(path).constData()))
        fail("Cannot replace the selected document");
    int directory
        = open(QFile::encodeName(QFileInfo(path).absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
    entries_ = contents;
}
}
