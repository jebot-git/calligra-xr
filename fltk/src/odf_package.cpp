// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <zip.h>
namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const Text& error) { throw std::runtime_error(error.toStdString()); }
    Text local(Kind kind)
    {
        return kind == Kind::Words ? "text" : kind == Kind::Sheets ? "spreadsheet" : "presentation";
    }
    void validate_xml(const Bytes& data)
    {
        XmlDocument validation;
        if(!validation.setContent(data))fail("Malformed XML, entity declaration or excessive document depth");
    }

} // namespace
Text mime(Kind kind) { return "application/vnd.oasis.opendocument." + local(kind); }
Text suffix(Kind kind) { return kind == Kind::Words ? "odt" : kind == Kind::Sheets ? "ods" : "odp"; }
XmlElement child(const XmlElement& parent, const Text& ns, const Text& name)
{
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        auto e = n.toElement();
        if (e.namespaceURI() == ns && e.localName() == name)
            return e;
    }
    return { };
}
List<XmlElement> children(const XmlElement& parent, const Text& ns, const Text& name)
{
    List<XmlElement> result;
    for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        auto e = n.toElement();
        if (e.namespaceURI() == ns && (name.isEmpty() || e.localName() == name))
            result << e;
    }
    return result;
}
Text plain_text(const XmlElement& element)
{
    Text result;
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
                result += Text(count, ' ');
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
void set_plain_text(XmlElement element, const Text& value)
{
    while (!element.firstChild().isNull())
        element.removeChild(element.firstChild());
    auto doc = element.ownerDocument();
    Text run;
    auto flush = [&] {
        if (!run.isEmpty()) {
            element.appendChild(doc.createTextNode(run));
            run.clear();
        }
    };
    for (int i = 0; i < value.size(); ++i) {
        Char c = value[i];
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
    const Bytes start = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><office:document-content "
                              "xmlns:office=\""
        + OFFICE + "\" xmlns:text=\"" + TEXT + "\" xmlns:table=\"" + TABLE + "\" xmlns:draw=\"" + DRAW
        + "\" xmlns:svg=\"" + SVG + "\" xmlns:xlink=\"" + XLINK
        + "\" "
          "xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" "
          "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
          "office:version=\"1.3\">"
          "<office:automatic-styles/><office:body><office:"
        + local(kind) + "/></office:body></office:document-content>")
                                 .toUtf8();
    if (!xml.setContent(start, XmlDocument::ParseOption::UseNamespaceProcessing))
        fail("Cannot create document");
    entries_["mimetype"] = mime(kind).toUtf8();
    const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                  FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
    entries_["styles.xml"] = ("<?xml version=\"1.0\"?><office:document-styles xmlns:office=\"" + OFFICE
        + "\" xmlns:style=\"" + STYLE + "\" xmlns:fo=\"" + FO
        + "\" "
          "office:version=\"1.3\"><office:styles/"
          "><office:automatic-styles><style:page-layout "
          "style:name=\"pm1\"><style:page-layout-properties fo:page-width=\""
        + (kind == Kind::Stage ? Text("28cm") : Text("21cm")) + "\" fo:page-height=\""
        + (kind == Kind::Stage ? Text("15.75cm") : Text("29.7cm"))
        + "\"/></style:page-layout></"
          "office:automatic-styles><office:master-styles><style:master-page "
          "style:name=\"Default\" "
          "style:page-layout-name=\"pm1\"/></office:master-styles></"
          "office:document-styles>")
                                 .toUtf8();

    entries_["META-INF/manifest.xml"] = ("<?xml version=\"1.0\"?><manifest:manifest "
                                         "xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
                                         "manifest:version=\"1.3\"><manifest:file-entry manifest:full-path=\"/\" "
                                         "manifest:media-type=\""
        + mime(kind)
        + "\"/><manifest:file-entry manifest:full-path=\"content.xml\" "
          "manifest:media-type=\"text/xml\"/><manifest:file-entry "
          "manifest:full-path=\"styles.xml\" "
          "manifest:media-type=\"text/xml\"/></manifest:manifest>")
                                            .toUtf8();
}
XmlElement OdfPackage::body() const
{
    return child(child(xml.documentElement(), OFFICE, "body"), OFFICE, local(kind_));
}
Map<Text, Bytes> read_document_archive(const Text& path)
{
    int error = 0;
    std::unique_ptr<zip_t, decltype(&zip_discard)> zip(
        zip_open(File::encodeName(path).constData(), ZIP_RDONLY, &error), zip_discard);
    if (!zip)
        fail("Cannot open document archive");
    Map<Text, Bytes> contents;
    UInt64 total = 0;
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
        Text name = Text::fromUtf8(info.name);
        if (name.startsWith('/') || name.split('/').contains("..") || contents.contains(name))
            fail("Invalid or duplicate document archive name");
        if (info.size > 64 * 1024 * 1024 || (total += info.size) > 256 * 1024 * 1024)
            fail("Document exceeds the current import size limit");
        Bytes data;
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
    return contents;
}
XmlDocument read_document_xml(const Bytes& data)
{
    validate_xml(data);
    XmlDocument result;
    if (!result.setContent(data, XmlDocument::ParseOption::UseNamespaceProcessing))
        fail("Malformed document XML");
    return result;
}
void OdfPackage::load(const Text& path)
{
    auto contents = read_document_archive(path);
    if (contents.value("mimetype") != mime(kind_).toUtf8())
        fail("Open this file with the matching Words, Sheets or Stage application");
    const auto content = contents.value("content.xml");
    validate_xml(content);
    if (contents.contains("styles.xml"))
        validate_xml(contents.value("styles.xml"));
    XmlDocument parsed;
    auto result = parsed.setContent(content, XmlDocument::ParseOption::UseNamespaceProcessing);
    if (!result)
        fail("Malformed document XML");
    auto b = child(child(parsed.documentElement(), OFFICE, "body"), OFFICE, local(kind_));
    if (b.isNull())
        fail("Document has no expected content body");
    xml = parsed;
    entries_ = contents;
    read_only_ = false;
    for (const auto& name : entries_.keys())
        if (name.startsWith("META-INF/") && name.contains("signature", CaseInsensitive))
            read_only_ = true;
}
void OdfPackage::set_entry(const Text& name, const Bytes& data, const Text& mediaType)
{
    const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
    auto manifest = read_document_xml(entries_.value("META-INF/manifest.xml"));
    auto root = manifest.documentElement();
    auto entry = XmlElement();
    for (auto e : children(root, MANIFEST, "file-entry"))
        if (e.attributeNS(MANIFEST, "full-path") == name) entry = e;
    if (entry.isNull()) {
        entry = manifest.createElementNS(MANIFEST, "manifest:file-entry");
        root.appendChild(entry);
    }
    entry.setAttributeNS(MANIFEST, "manifest:full-path", name);
    entry.setAttributeNS(MANIFEST, "manifest:media-type", mediaType);
    entries_[name] = data;
    entries_["META-INF/manifest.xml"] = manifest.toByteArray(-1);
}
void OdfPackage::save(const Text& path)
{
    if (read_only_)
        fail("Signed documents are read-only in this port; signatures must not be "
             "silently invalidated");
    auto contents = entries_;
    contents["content.xml"] = xml.toByteArray(-1);
    TempFile temporary(FileInfo(path).absolutePath() + "/.calligra-XXXXXX");
    if (!temporary.open())
        fail("Cannot create document in the selected directory");
    Text temp = temporary.fileName();
    temporary.close();
    int error = 0;
    zip_t* zip = zip_open(File::encodeName(temp).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!zip)
        fail("Cannot create OpenDocument archive");
    auto add = [&](const Text& name, const Bytes& value, bool stored) {
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
    File synced(temp);
    if (!synced.open(IO::ReadOnly) || fsync(synced.handle()))
        fail("Cannot synchronize document");
    synced.close();
    if (::rename(File::encodeName(temp).constData(), File::encodeName(path).constData()))
        fail("Cannot replace the selected document");
    int directory
        = open(File::encodeName(FileInfo(path).absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
    entries_ = contents;
}
} // namespace DomeOffice
