// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "ooxml_text_import.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>
namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
    QList<QDomElement> paragraphs(const QDomElement& parent)
    {
        QList<QDomElement> result;
        auto walk = [&](auto&& self, const QDomElement& e) -> void {
            for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
                if (n.isElement()) {
                    auto p = n.toElement();
                    if (p.namespaceURI() == TEXT && (p.localName() == "p" || p.localName() == "h"))
                        result << p;
                    else
                        self(self, p);
                }
        };
        walk(walk, parent);
        return result;
    }
    bool inline_supported(const QDomElement& parent)
    {
        for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling())
            if (n.isElement()) {
                auto e = n.toElement();
                if (e.namespaceURI() != TEXT
                    || !QStringList { "span", "a", "s", "tab", "line-break" }.contains(e.localName())
                    || !inline_supported(e))
                    return false;
            }
        return true;
    }
    // Change only the affected leaf text, retaining spans, links and their styling.
    void replace_text(QDomElement paragraph, const QString& text)
    {
        if (!inline_supported(paragraph))
            fail("This paragraph contains an embedded object that cannot be edited yet");
        QString before = plain_text(paragraph);
        if (before == text)
            return;
        int prefix = 0, suffix = 0;
        while (prefix < before.size() && prefix < text.size() && before[prefix] == text[prefix])
            ++prefix;
        while (suffix < before.size() - prefix && suffix < text.size() - prefix
            && before[before.size() - 1 - suffix] == text[text.size() - 1 - suffix])
            ++suffix;
        struct Leaf {
            QDomNode node;
            QString value;
        };
        QList<Leaf> leaves;
        auto collect = [&](auto&& self, const QDomElement& e) -> void {
            for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling()) {
                if (n.isText())
                    leaves.push_back({ n, n.nodeValue() });
                else if (n.isElement()) {
                    auto item = n.toElement();
                    if (item.namespaceURI() == TEXT
                        && (item.localName() == "s" || item.localName() == "tab" || item.localName() == "line-break")) {
                        auto holder = paragraph.ownerDocument().createElement("holder");
                        holder.appendChild(item.cloneNode(true));
                        leaves.push_back({ n, plain_text(holder) });
                    } else
                        self(self, item);
                }
            }
        };
        collect(collect, paragraph);
        if (leaves.isEmpty()) {
            set_plain_text(paragraph, text);
            return;
        }
        QString inserted = text.mid(prefix, text.size() - prefix - suffix);
        bool used = false;
        int offset = 0;
        for (auto leaf : leaves) {
            int n = leaf.value.size();
            int keepBefore = std::clamp(prefix - offset, 0, n);
            int keepAfter = std::clamp(int(before.size()) - suffix - offset, 0, n);
            QString value = leaf.value.left(keepBefore);
            if (!used && offset <= prefix && offset + n >= prefix) {
                value += inserted;
                used = true;
            }
            value += leaf.value.mid(keepAfter);
            if (value != leaf.value) {
                auto parent = leaf.node.parentNode();
                auto holder = paragraph.ownerDocument().createElement("holder");
                set_plain_text(holder, value);
                while (!holder.firstChild().isNull())
                    parent.insertBefore(holder.firstChild(), leaf.node);
                parent.removeChild(leaf.node);
            }
            offset += n;
        }
    }
    void set_paragraphs(QDomElement parent, const QString& text, bool allowStructural)
    {
        auto existing = paragraphs(parent);
        QStringList parts = text.split('\n');
        if (existing.size() != parts.size() && !allowStructural)
            fail("Adding or removing paragraphs in this structure is not supported yet");
        // Validate before changing any node.
        for (int i = 0; i < existing.size(); ++i)
            if (i < parts.size() && plain_text(existing[i]) != parts[i] && !inline_supported(existing[i]))
                fail("This paragraph contains an embedded object that cannot be edited yet");
        for (int i = 0; i < std::min(existing.size(), parts.size()); ++i)
            replace_text(existing[i], parts[i]);
        for (int i = parts.size(); i < existing.size(); ++i)
            parent.removeChild(existing[i]);
        for (int i = existing.size(); i < parts.size(); ++i) {
            auto p = parent.ownerDocument().createElementNS(TEXT, "text:p");
            set_plain_text(p, parts[i]);
            parent.appendChild(p);
        }
    }
    QString joined(const QDomElement& parent)
    {
        QStringList parts;
        for (auto p : paragraphs(parent))
            parts << plain_text(p);
        return parts.join('\n');
    }
    double length(const QString& text, double fallback)
    {
        static QRegularExpression re("^(-?[0-9]+(?:\\.[0-9]+)?)(cm|mm|in|pt|px)$");
        auto m = re.match(text);
        if (!m.hasMatch())
            return fallback;
        double scale = m.captured(2) == "cm" ? 1
            : m.captured(2) == "mm"          ? .1
            : m.captured(2) == "in"          ? 2.54
            : m.captured(2) == "pt"          ? 2.54 / 72
                                             : 2.54 / 96;
        return m.captured(1).toDouble() * scale;
    }
    QList<QDomElement> frames(const QDomElement& page) { return children(page, DRAW, "frame"); }
}
TextDocument::TextDocument()
{
    auto p = package_.xml.createElementNS(TEXT, "text:p");
    package_.body().appendChild(p);
}
void TextDocument::load(const QString& p)
{
    bool office = QFileInfo(p).suffix().compare("docx", Qt::CaseInsensitive) == 0;
    OdfPackage candidate(Kind::Words);
    if (office) candidate = import_docx(p);
    else candidate.load(p);
    package_ = std::move(candidate);
    imported_office_ = office;
}
void TextDocument::save(const QString& p)
{
    if (QFileInfo(p).suffix().compare("odt", Qt::CaseInsensitive) != 0)
        fail("Words saves only ODT; choose an .odt destination");
    package_.save(p);
    imported_office_ = false;
}
QString TextDocument::text() const { return joined(package_.body()); }
void TextDocument::set_text(const QString& value)
{
    if (read_only())
        fail("This document is read-only");
    if (value == text())
        return;
    bool flat = true;
    for (auto e : children(package_.body(), TEXT))
        if (e.localName() != "p" && e.localName() != "h" && e.localName() != "sequence-decls")
            flat = false;
    // Table/drawing bodies also prohibit structural paragraph edits.
    for (auto n = package_.body().firstChild(); !n.isNull(); n = n.nextSibling())
        if (n.isElement() && n.toElement().namespaceURI() != TEXT)
            flat = false;
    set_paragraphs(package_.body(), value, flat);
}
Presentation::Presentation() { add_slide("Title"); }
int Presentation::count() const { return children(package_.body(), DRAW, "page").size(); }
void Presentation::load(const QString& p)
{
    bool office = QFileInfo(p).suffix().compare("pptx", Qt::CaseInsensitive) == 0;
    OdfPackage candidate(Kind::Stage);
    if (office) candidate = import_pptx(p);
    else candidate.load(p);
    if (children(candidate.body(), DRAW, "page").isEmpty())
        fail("The presentation has no slides");
    package_ = std::move(candidate);
    imported_office_ = office;
}
void Presentation::save(const QString& p)
{
    if (QFileInfo(p).suffix().compare("odp", Qt::CaseInsensitive) != 0)
        fail("Stage saves only ODP; choose an .odp destination");
    package_.save(p);
    imported_office_ = false;
}
Slide Presentation::slide(int index) const
{
    auto pages = children(package_.body(), DRAW, "page");
    if (index < 0 || index >= pages.size())
        fail("Invalid slide");
    auto page = pages[index];
    Slide result;
    result.name = page.attributeNS(DRAW, "name", QString("Slide %1").arg(index + 1));
    for (auto node = page.firstChild(); !node.isNull(); node = node.nextSibling())
        if (node.isElement() && node.toElement().namespaceURI() == DRAW && node.toElement().localName() != "frame")
            ++result.unsupported;
    auto styles = package_.entry("styles.xml");
    if (!styles.isEmpty() && !styles.toUpper().contains("<!DOCTYPE")) {
        QDomDocument doc;
        if (doc.setContent(styles, QDomDocument::ParseOption::UseNamespaceProcessing)) {
            const QString STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                          FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
            auto properties = doc.elementsByTagNameNS(STYLE, "page-layout-properties");
            if (!properties.isEmpty()) {
                auto e = properties.at(0).toElement();
                result.width = length(e.attributeNS(FO, "page-width"), 28);
                result.height = length(e.attributeNS(FO, "page-height"), 15.75);
            }
        }
    }

    for (auto frame : frames(page)) {
        SlideBox box;
        box.x = length(frame.attributeNS(SVG, "x"), 1);
        box.y = length(frame.attributeNS(SVG, "y"), 1);
        box.width = length(frame.attributeNS(SVG, "width"), 24);
        box.height = length(frame.attributeNS(SVG, "height"), 3);
        box.text = joined(frame);
        auto image = child(frame, DRAW, "image");
        box.image = image.attributeNS(XLINK, "href");
        result.boxes << box;
    }
    return result;
}
void Presentation::add_slide(const QString& title)
{
    if (read_only())
        fail("This presentation is read-only");
    auto doc = package_.xml;
    auto page = doc.createElementNS(DRAW, "draw:page");
    page.setAttributeNS(DRAW, "draw:name", QString("Slide %1").arg(count() + 1));
    page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
    for (int i = 0; i < 2; ++i) {
        auto frame = doc.createElementNS(DRAW, "draw:frame");
        frame.setAttributeNS(SVG, "svg:x", "1cm");
        frame.setAttributeNS(SVG, "svg:y", i == 0 ? "1cm" : "5cm");
        frame.setAttributeNS(SVG, "svg:width", "24cm");
        frame.setAttributeNS(SVG, "svg:height", i == 0 ? "3cm" : "8cm");
        auto box = doc.createElementNS(DRAW, "draw:text-box");
        auto p = doc.createElementNS(TEXT, "text:p");
        set_plain_text(p, i == 0 ? title : QString());
        box.appendChild(p);
        frame.appendChild(box);
        page.appendChild(frame);
    }
    package_.body().appendChild(page);
}
void Presentation::remove_slide(int index)
{
    if (read_only())
        fail("This presentation is read-only");
    auto pages = children(package_.body(), DRAW, "page");
    if (pages.size() < 2)
        fail("Keep at least one slide");
    package_.body().removeChild(pages.at(index));
}
void Presentation::set_box_text(int slideIndex, int boxIndex, const QString& text)
{
    if (read_only())
        fail("This presentation is read-only");
    auto page = children(package_.body(), DRAW, "page").at(slideIndex);
    auto frame = frames(page).at(boxIndex);
    auto box = child(frame, DRAW, "text-box");
    if (box.isNull())
        fail("This frame is not an editable text box");
    if (joined(box) == text)
        return;
    set_paragraphs(box, text, true);
}
}
