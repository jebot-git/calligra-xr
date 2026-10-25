// SPDX-License-Identifier: GPL-2.0-or-later
#include "ooxml_text_import.h"
#include <QDir>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace DomeOffice {
namespace {
    const QString REL = "http://schemas.openxmlformats.org/package/2006/relationships";
    const QString DOCREL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const QString STRICTREL = "http://purl.oclc.org/ooxml/officeDocument/relationships";
    const QString WORD = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    const QString STRICTWORD = "http://purl.oclc.org/ooxml/wordprocessingml/main";
    const QString PPT = "http://schemas.openxmlformats.org/presentationml/2006/main";
    const QString STRICTPPT = "http://purl.oclc.org/ooxml/presentationml/main";
    const QString ART = "http://schemas.openxmlformats.org/drawingml/2006/main";
    const QString STRICTART = "http://purl.oclc.org/ooxml/drawingml/main";
    const QString STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
    const QString FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
    [[noreturn]] void fail(const QString& message)
    {
        throw std::runtime_error(("Office import: " + message).toStdString());
    }
    QDomElement append(QDomElement parent, const QString& ns, const QString& name)
    {
        auto e = parent.ownerDocument().createElementNS(ns, name);
        parent.appendChild(e);
        return e;
    }
    bool relation_type(const QString& type, const QString& kind)
    {
        return type == DOCREL + '/' + kind || type == STRICTREL + '/' + kind;
    }
    struct Link {
        QString type, target;
        bool external;
    };
    struct Archive {
        QMap<QString, QByteArray> entries;
        explicit Archive(const QString& path)
            : entries(read_document_archive(path))
        {
            for (auto it = entries.cbegin(); it != entries.cend(); ++it)
                if (it.key().startsWith("_xmlsignatures/", Qt::CaseInsensitive)
                    || it.key().endsWith("vbaProject.bin", Qt::CaseInsensitive))
                    fail("Signed or macro-enabled documents are not supported");
            auto types = xml("[Content_Types].xml");
            for (auto e :
                children(types.documentElement(), "http://schemas.openxmlformats.org/package/2006/content-types")) {
                auto type = e.attribute("ContentType");
                if (type.contains("macroEnabled", Qt::CaseInsensitive)
                    || type.contains("vbaProject", Qt::CaseInsensitive)
                    || type.contains("digital-signature", Qt::CaseInsensitive))
                    fail("Signed or macro-enabled documents are not supported");
            }
        }
        QDomDocument xml(const QString& path) const
        {
            if (!entries.contains(path))
                fail("Missing package part: " + path);
            return read_document_xml(entries.value(path));
        }
        QMap<QString, Link> links(const QString& part) const
        {
            int slash = part.lastIndexOf('/');
            auto name = part.isEmpty() ? QString("_rels/.rels")
                                       : part.left(slash + 1) + "_rels/" + part.mid(slash + 1) + ".rels";
            QMap<QString, Link> result;
            if (!entries.contains(name))
                return result;
            auto root = xml(name).documentElement();
            if (root.namespaceURI() != REL || root.localName() != "Relationships")
                fail("Invalid relationship part");
            for (auto e : children(root, REL, "Relationship")) {
                auto id = e.attribute("Id"), target = e.attribute("Target"), mode = e.attribute("TargetMode");
                if (id.isEmpty() || result.contains(id) || target.isEmpty()
                    || (!mode.isEmpty() && mode != "External" && mode != "Internal"))
                    fail("Invalid package relationship");
                bool external = mode == "External";
                if (!external) {
                    if (target.contains('\\') || target.contains('%') || target.contains(':') || target.contains('#')
                        || target.contains('?'))
                        fail("Unsupported package target");
                    target = QDir::cleanPath(target.startsWith('/') ? target.mid(1) : part.left(slash + 1) + target);
                    if (target == "." || target == ".." || target.startsWith("../") || target.startsWith('/'))
                        fail("Package target escapes archive");
                }
                auto type = e.attribute("Type");
                if (type.contains("digital-signature", Qt::CaseInsensitive)
                    || type.contains("vbaProject", Qt::CaseInsensitive))
                    fail("Signed or macro-enabled documents are not supported");
                result[id] = { type, target, external };
            }
            return result;
        }
        QString main_part(const QString& type) const
        {
            QString part;
            for (const auto& link : links({}))
                if (relation_type(link.type, "officeDocument")) {
                    if (link.external || !part.isEmpty())
                        fail("Invalid main document relationship");
                    part = link.target;
                }
            if (part.isEmpty())
                fail("Missing main document relationship");
            bool valid = false;
            for (auto e : children(xml("[Content_Types].xml").documentElement(),
                     "http://schemas.openxmlformats.org/package/2006/content-types", "Override"))
                if (e.attribute("PartName") == '/' + part) {
                    if (valid || e.attribute("ContentType") != type)
                        fail("Not a standard DOCX/PPTX package");
                    valid = true;
                }
            if (!valid)
                fail("Not a standard DOCX/PPTX package");
            return part;
        }
    };
    struct Budget {
        qint64 text = 0;
        int nodes = 0;
        void account(const QString& s = {})
        {
            text += s.size();
            if (++nodes > 100000 || text > 32 * 1024 * 1024)
                fail("Document exceeds the text/object import limit");
        }
    };
    QString rid(const QDomElement& e, const QString& name = "id")
    {
        return e.attributeNS(DOCREL, name, e.attributeNS(STRICTREL, name));
    }
    QString cm(double emu) { return QString::number(emu / 360000.0, 'g', 12) + "cm"; }
    double number(const QDomElement& e, const QString& name, bool positive = false)
    {
        bool ok = false;
        double n = e.attribute(name).toDouble(&ok);
        if (!ok || !std::isfinite(n) || std::abs(n) > 1e12 || (positive && n <= 0))
            fail("Invalid slide geometry");
        return n;
    }
    // Import only visible run text. Field instructions, deleted revisions and objects
    // are deliberately excluded. Recurse through hyperlinks/content controls, not drawings.
    QString word_text(const QDomElement& e, const QString& ns)
    {
        QString result;
        for (auto n : children(e, ns)) {
            auto name = n.localName();
            if (name == "t")
                result += n.text();
            else if (name == "tab")
                result += '\t';
            else if (name == "br" || name == "cr")
                result += '\n';
            else if (name == "noBreakHyphen")
                result += QChar(0x2011);
            else if (name == "softHyphen")
                result += QChar(0x00ad);
            else if (QStringList { "r", "hyperlink", "fldSimple", "ins", "sdt", "sdtContent", "smartTag", "customXml" }.contains(
                         name))
                result += word_text(n, ns);
        }
        return result;
    }
    void word_blocks(QDomElement out, const QDomElement& input, const QString& ns, Budget& budget)
    {
        for (auto e : children(input, ns)) {
            if (e.localName() == "p") {
                auto value = word_text(e, ns);
                budget.account(value);
                set_plain_text(append(out, TEXT, "text:p"), value);
            } else if (e.localName() == "tbl") {
                budget.account();
                auto table = append(out, TABLE, "table:table");
                table.setAttributeNS(TABLE, "table:name", QString("Table%1").arg(budget.nodes));
                for (auto row : children(e, ns, "tr")) {
                    budget.account();
                    auto r = append(table, TABLE, "table:table-row");
                    for (auto cell : children(row, ns, "tc")) {
                        budget.account();
                        auto c = append(r, TABLE, "table:table-cell");
                        word_blocks(c, cell, ns, budget);
                        if (c.firstChild().isNull())
                            append(c, TEXT, "text:p");
                    }
                }
            } else if (QStringList { "sdt", "sdtContent", "ins", "customXml" }.contains(e.localName()))
                word_blocks(out, e, ns, budget);
        }
    }
    void drawing_text(QDomElement out, const QDomElement& input, const QString& ns, Budget& budget)
    {
        for (auto p : children(input, ns, "p")) {
            QString text;
            for (auto e : children(p, ns)) {
                if (e.localName() == "r" || e.localName() == "fld")
                    text += child(e, ns, "t").text();
                else if (e.localName() == "br")
                    text += '\n';
            }
            budget.account(text);
            set_plain_text(append(out, TEXT, "text:p"), text);
        }
    }
    QDomElement related_root(const Archive& archive, const QMap<QString, Link>& links, const QString& kind,
        const QString& ns, const QString& rootName)
    {
        QDomElement result;
        for (const auto& link : links)
            if (relation_type(link.type, kind)) {
                if (link.external || !result.isNull())
                    fail("Invalid " + kind + " relationship");
                result = archive.xml(link.target).documentElement();
                if (result.namespaceURI() != ns || result.localName() != rootName)
                    fail("Invalid " + kind + " part");
            }
        return result;
    }
    QDomElement placeholder_transform(
        const QDomElement& shape, const QDomElement& layout, const QString& ns, const QString& art)
    {
        auto direct = child(child(shape, ns, "spPr"), art, "xfrm");
        if (!direct.isNull())
            return direct;
        auto ph = child(child(child(shape, ns, "nvSpPr"), ns, "nvPr"), ns, "ph");
        if (ph.isNull())
            return {};
        auto tree = child(child(layout, ns, "cSld"), ns, "spTree");
        for (auto candidate : children(tree, ns, "sp")) {
            auto other = child(child(child(candidate, ns, "nvSpPr"), ns, "nvPr"), ns, "ph");
            if (!other.isNull() && ph.attribute("idx", "0") == other.attribute("idx", "0"))
                return child(child(candidate, ns, "spPr"), art, "xfrm");
        }
        return {};
    }
    struct Rect {
        double x = 360000, y = 360000, w = 8640000, h = 1080000;
    };
    Rect rectangle(const QDomElement& transform, const QString& ns)
    {
        Rect r;
        if (!transform.isNull()) {
            auto off = child(transform, ns, "off"), ext = child(transform, ns, "ext");
            if (!off.isNull()) {
                r.x = number(off, "x");
                r.y = number(off, "y");
            }
            if (!ext.isNull()) {
                r.w = number(ext, "cx", true);
                r.h = number(ext, "cy", true);
            }
        }
        return r;
    }
    QDomElement frame(QDomElement page, const Rect& r, Budget& budget)
    {
        budget.account();
        auto f = append(page, DRAW, "draw:frame");
        f.setAttributeNS(SVG, "svg:x", cm(r.x));
        f.setAttributeNS(SVG, "svg:y", cm(r.y));
        f.setAttributeNS(SVG, "svg:width", cm(r.w));
        f.setAttributeNS(SVG, "svg:height", cm(r.h));
        return f;
    }
} // namespace
OdfPackage import_docx(const QString& path)
{
    Archive archive(path);
    auto part = archive.main_part("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml");
    auto root = archive.xml(part).documentElement();
    auto ns = root.namespaceURI();
    if ((ns != WORD && ns != STRICTWORD) || root.localName() != "document")
        fail("Unsupported Word document namespace");
    auto body = child(root, ns, "body");
    if (body.isNull())
        fail("Word document has no body");
    archive.links(part); // Validate relationships; external resources are never fetched.
    OdfPackage output(Kind::Words);
    Budget budget;
    word_blocks(output.body(), body, ns, budget);
    if (output.body().firstChild().isNull())
        append(output.body(), TEXT, "text:p");
    return output;
}
OdfPackage import_pptx(const QString& path)
{
    Archive archive(path);
    auto part = archive.main_part("application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml");
    auto root = archive.xml(part).documentElement();
    auto ns = root.namespaceURI();
    if ((ns != PPT && ns != STRICTPPT) || root.localName() != "presentation")
        fail("Unsupported presentation namespace");
    const auto art = ns == PPT ? ART : STRICTART;
    auto links = archive.links(part);
    auto slides = children(child(root, ns, "sldIdLst"), ns, "sldId");
    if (slides.isEmpty() || slides.size() > 4096)
        fail("Invalid slide count");
    OdfPackage output(Kind::Stage);
    auto size = child(root, ns, "sldSz");
    if (!size.isNull()) {
        auto styles = read_document_xml(output.entry("styles.xml"));
        auto props = styles.elementsByTagNameNS(STYLE, "page-layout-properties").at(0).toElement();
        props.setAttributeNS(FO, "fo:page-width", cm(number(size, "cx", true)));
        props.setAttributeNS(FO, "fo:page-height", cm(number(size, "cy", true)));
        output.set_entry("styles.xml", styles.toByteArray(-1), "text/xml");
    }
    Budget budget;
    QMap<QString, QString> media;
    QSet<QString> ids, parts;
    for (auto slide : slides) {
        auto id = rid(slide);
        auto link = links.value(id);
        if (id.isEmpty() || ids.contains(id) || parts.contains(link.target) || link.external
            || !relation_type(link.type, "slide"))
            fail("Invalid slide relationship");
        ids.insert(id);
        parts.insert(link.target);
        auto slideRoot = archive.xml(link.target).documentElement();
        if (slideRoot.namespaceURI() != ns || slideRoot.localName() != "sld")
            fail("Invalid slide part");
        auto slideLinks = archive.links(link.target);
        auto layout = related_root(archive, slideLinks, "slideLayout", ns, "sldLayout");
        auto common = child(slideRoot, ns, "cSld");
        auto tree = child(common, ns, "spTree");
        if (tree.isNull())
            fail("Slide has no shape tree");
        auto page = append(output.body(), DRAW, "draw:page");
        page.setAttributeNS(DRAW, "draw:name", QString("Slide %1").arg(ids.size()));
        page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
        // Direct text boxes and pictures, including layout placeholder geometry.
        // Full theme/master inheritance, groups, rotations, charts and other
        // shapes require the full Stage model.
        for (auto shape : children(tree, ns)) {
            if (shape.localName() == "sp") {
                auto text = child(shape, ns, "txBody");
                if (text.isNull())
                    continue;
                auto xfrm = placeholder_transform(shape, layout, ns, art);
                auto f = frame(page, rectangle(xfrm, art), budget);
                drawing_text(append(f, DRAW, "draw:text-box"), text, art, budget);
            } else if (shape.localName() == "pic") {
                auto blip = child(child(shape, ns, "blipFill"), art, "blip");
                auto embed = rid(blip, "embed");
                if (embed.isEmpty())
                    continue; // Linked images are deliberately omitted.
                auto imageLink = slideLinks.value(embed);
                if (imageLink.external || !relation_type(imageLink.type, "image")
                    || !archive.entries.contains(imageLink.target))
                    fail("Invalid image relationship");
                const auto bytes = archive.entries.value(imageLink.target);
                QString ext, type;
                if (bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
                    ext = "png";
                    type = "image/png";
                } else if (bytes.startsWith(QByteArray::fromHex("ffd8ff"))) {
                    ext = "jpg";
                    type = "image/jpeg";
                } else
                    continue;
                if (!media.contains(imageLink.target)) {
                    auto name = QString("Pictures/image%1.%2").arg(media.size() + 1).arg(ext);
                    output.set_entry(name, bytes, type);
                    media[imageLink.target] = name;
                }
                auto f = frame(page, rectangle(child(child(shape, ns, "spPr"), art, "xfrm"), art), budget);
                auto image = append(f, DRAW, "draw:image");
                image.setAttributeNS(XLINK, "xlink:href", media.value(imageLink.target));
                image.setAttributeNS(XLINK, "xlink:type", "simple");
                image.setAttributeNS(XLINK, "xlink:show", "embed");
                image.setAttributeNS(XLINK, "xlink:actuate", "onLoad");
            }
        }
    }
    return output;
}
} // namespace DomeOffice
