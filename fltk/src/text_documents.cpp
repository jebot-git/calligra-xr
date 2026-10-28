// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "ooxml_text_import.h"
#include <algorithm>
#include <stdexcept>
namespace DomeOffice {
namespace {
    [[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
    List<XmlElement> paragraphs(const XmlElement& parent)
    {
        List<XmlElement> result;
        auto walk = [&](auto&& self, const XmlElement& e) -> void {
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
    bool inline_supported(const XmlElement& parent)
    {
        for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling())
            if (n.isElement()) {
                auto e = n.toElement();
                if (e.namespaceURI() != TEXT
                    || !TextList { "span", "a", "s", "tab", "line-break" }.contains(e.localName())
                    || !inline_supported(e))
                    return false;
            }
        return true;
    }
    // Change only the affected leaf text, retaining spans, links and their styling.
    void replace_text(XmlElement paragraph, const Text& text)
    {
        if (!inline_supported(paragraph))
            fail("This paragraph contains an embedded object that cannot be edited yet");
        Text before = plain_text(paragraph);
        if (before == text)
            return;
        int prefix = 0, suffix = 0;
        while (prefix < before.size() && prefix < text.size() && before[prefix] == text[prefix])
            ++prefix;
        while (suffix < before.size() - prefix && suffix < text.size() - prefix
            && before[before.size() - 1 - suffix] == text[text.size() - 1 - suffix])
            ++suffix;
        struct Leaf {
            XmlNode node;
            Text value;
        };
        List<Leaf> leaves;
        auto collect = [&](auto&& self, const XmlElement& e) -> void {
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
        Text inserted = text.mid(prefix, text.size() - prefix - suffix);
        bool used = false;
        int offset = 0;
        for (auto leaf : leaves) {
            int n = leaf.value.size();
            int keepBefore = std::clamp(prefix - offset, 0, n);
            int keepAfter = std::clamp(int(before.size()) - suffix - offset, 0, n);
            Text value = leaf.value.left(keepBefore);
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
    void set_paragraphs(XmlElement parent, const Text& text, bool allowStructural)
    {
        auto existing = paragraphs(parent);
        TextList parts = text.split('\n');
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
    Text joined(const XmlElement& parent)
    {
        TextList parts;
        for (auto p : paragraphs(parent))
            parts << plain_text(p);
        return parts.join('\n');
    }
}
TextDocument::TextDocument()
{
    auto p = package_.xml.createElementNS(TEXT, "text:p");
    package_.body().appendChild(p);
}
void TextDocument::load(const Text& p)
{
    bool office = FileInfo(p).suffix().compare("docx", CaseInsensitive) == 0;
    OdfPackage candidate(Kind::Words);
    if (office) candidate = import_docx(p);
    else candidate.load(p);
    package_ = std::move(candidate);
    imported_office_ = office;
}
void TextDocument::save(const Text& p)
{
    if (FileInfo(p).suffix().compare("odt", CaseInsensitive) != 0)
        fail("Words saves only ODT; choose an .odt destination");
    package_.save(p);
    imported_office_ = false;
}
Text TextDocument::text() const { return joined(package_.body()); }
void TextDocument::set_text(const Text& value)
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
void Presentation::load(const Text& p)
{
    bool office = FileInfo(p).suffix().compare("pptx", CaseInsensitive) == 0;
    OdfPackage candidate(Kind::Stage);
    if (office) candidate = import_pptx(p);
    else candidate.load(p);
    if (children(candidate.body(), DRAW, "page").isEmpty())
        fail("The presentation has no slides");
    auto scenes = read_presentation_scenes(candidate);
    package_ = std::move(candidate);
    scenes_ = std::move(scenes);
    ++revision_;
    imported_office_ = office;
}
void Presentation::save(const Text& p)
{
    if (FileInfo(p).suffix().compare("odp", CaseInsensitive) != 0)
        fail("Stage saves only ODP; choose an .odp destination");
    package_.save(p);
    imported_office_ = false;
}
const SlideScene& Presentation::scene(int index) const
{
    if (index < 0 || index >= count()) fail("Invalid slide");
    if (scenes_.empty()) scenes_ = read_presentation_scenes(package_);
    return scenes_.at(index);
}
Slide Presentation::slide(int index) const
{
    const auto& native = scene(index);
    Slide result;
    result.name = native.name;
    constexpr double points_per_cm = 72/2.54;
    result.width = native.width/points_per_cm;
    result.height = native.height/points_per_cm;
    result.unsupported = native.unsupported.size();
    for (size_t i = 0; i < native.editable_sources.size(); ++i) {
        const auto& shape = native.shapes.at(native.editor_shapes[i]);
        auto bounds = shape.bounds();
        SlideBox box;
        box.x = bounds.x/points_per_cm; box.y = bounds.y/points_per_cm;
        box.width = bounds.width/points_per_cm; box.height = bounds.height/points_per_cm;
        box.text = shape.text; box.image = shape.image;
        result.boxes.push_back(std::move(box));
    }
    return result;
}
void Presentation::add_slide(const Text& title)
{
    if (read_only())
        fail("This presentation is read-only");
    auto doc = package_.xml;
    auto page = doc.createElementNS(DRAW, "draw:page");
    page.setAttributeNS(DRAW, "draw:name", Text("Slide %1").arg(count() + 1));
    page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
    for (int i = 0; i < 2; ++i) {
        auto frame = doc.createElementNS(DRAW, "draw:frame");
        frame.setAttributeNS(SVG, "svg:x", "1cm");
        frame.setAttributeNS(SVG, "svg:y", i == 0 ? "1cm" : "5cm");
        frame.setAttributeNS(SVG, "svg:width", "24cm");
        frame.setAttributeNS(SVG, "svg:height", i == 0 ? "3cm" : "8cm");
        auto box = doc.createElementNS(DRAW, "draw:text-box");
        auto p = doc.createElementNS(TEXT, "text:p");
        set_plain_text(p, i == 0 ? title : Text());
        box.appendChild(p);
        frame.appendChild(box);
        page.appendChild(frame);
    }
    package_.body().appendChild(page);
    scenes_.clear(); ++revision_;
}
void Presentation::remove_slide(int index)
{
    if (read_only())
        fail("This presentation is read-only");
    auto pages = children(package_.body(), DRAW, "page");
    if (pages.size() < 2)
        fail("Keep at least one slide");
    package_.body().removeChild(pages.at(index));
    scenes_.clear(); ++revision_;
}
void Presentation::set_box_text(int slideIndex, int boxIndex, const Text& text)
{
    if (read_only())
        fail("This presentation is read-only");
    const auto& current = scene(slideIndex);
    auto frame = current.editable_sources.at(boxIndex);
    auto box = child(frame, DRAW, "text-box");
    if (box.isNull())
        fail("This frame is not an editable text box");
    if (joined(box) == text)
        return;
    set_paragraphs(box, text, true);
    scenes_.at(slideIndex).shapes.at(current.editor_shapes.at(boxIndex)).text = joined(frame);
    ++revision_;
}
}
