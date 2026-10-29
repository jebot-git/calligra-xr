// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "ooxml_text_import.h"
#include "native_enhanced_geometry.h"
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
        // Preserve the distinction between original paragraph boundaries and
        // inline line breaks when a text-only edit leaves every newline intact.
        TextList original;
        bool soft_breaks = false;
        for (auto p : existing) {
            auto value = plain_text(p);
            soft_breaks = soft_breaks || value.contains('\n');
            original.push_back(value);
        }
        if (soft_breaks) {
            auto before = original.join('\n');
            size_t prefix = 0, suffix = 0;
            while (prefix < before.size() && prefix < text.size() && before[prefix] == text[prefix])
                ++prefix;
            while (suffix < before.size() - prefix && suffix < text.size() - prefix
                && before[before.size() - 1 - suffix] == text[text.size() - 1 - suffix])
                ++suffix;
            auto removed = before.mid(prefix, before.size() - prefix - suffix);
            auto inserted = text.mid(prefix, text.size() - prefix - suffix);
            if (!removed.contains('\n') && !inserted.contains('\n')) {
                parts.clear();
                size_t old_boundary = 0, start = 0;
                const auto delta = static_cast<int64_t>(text.size()) - static_cast<int64_t>(before.size());
                for (size_t i = 0; i + 1 < original.size(); ++i) {
                    old_boundary += original[i].size();
                    auto boundary = static_cast<int64_t>(old_boundary);
                    if (old_boundary >= before.size() - suffix)
                        boundary += delta;
                    if (boundary < static_cast<int64_t>(start) || boundary >= static_cast<int64_t>(text.size())
                        || text[size_t(boundary)] != '\n')
                        fail("Cannot retain the original paragraph boundaries");
                    parts.push_back(text.mid(start, size_t(boundary) - start));
                    start = size_t(boundary) + 1;
                    ++old_boundary;
                }
                parts.push_back(text.mid(start));
            }
        }
        if (existing.size() != parts.size() && !allowStructural)
            fail("Adding or removing paragraphs in this structure is not supported yet");
        if (existing.size() != parts.size())
            for (auto p : existing)
                if (p.parentNode() != parent)
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
Presentation::Presentation() { add_slide("Title"); clear_history(); }
void Presentation::clear_history()
{
    history_.clear();
    history_index_ = history_bytes_ = 0;
    state_ = saved_state_ = 0;
    next_state_ = 1;
    merge_text_ = false;
    drag_.reset();
}
void Presentation::remember(Bytes before, PresentationPosition previous, PresentationPosition next, bool merge)
{
    const auto after = package_.xml.toByteArray();
    if (before == after) return;
    if (merge && merge_text_ && history_index_ && history_index_ == history_.size()
        && history_.back().previous == previous && history_.back().next == next) {
        const auto& last = history_.back();
        before.std::string::replace(last.offset, last.after.size(), last.before);
        state_ = last.before_state;
        history_bytes_ -= last.before.size() + last.after.size();
        history_.pop_back();
        --history_index_;
    }
    merge_text_ = merge;
    if (before == after) { merge_text_ = false; return; }
    size_t prefix = 0, suffix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
        && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
    Edit edit { prefix, before.substr(prefix, before.size() - prefix - suffix),
        after.substr(prefix, after.size() - prefix - suffix), previous, next, state_, next_state_++ };
    for (size_t i = history_index_; i < history_.size(); ++i)
        history_bytes_ -= history_[i].before.size() + history_[i].after.size();
    history_.resize(history_index_);
    state_ = edit.after_state;
    history_bytes_ += edit.before.size() + edit.after.size();
    history_.push_back(std::move(edit));
    constexpr size_t budget = 32 * 1024 * 1024;
    while (!history_.empty() && (history_.size() > 100 || history_bytes_ > budget)) {
        history_bytes_ -= history_.front().before.size() + history_.front().after.size();
        history_.erase(history_.begin());
    }
    history_index_ = history_.size();
}
std::optional<PresentationPosition> Presentation::travel(bool redo)
{
    finish_handle_drag();
    end_text_edit();
    if (read_only()) fail("This presentation is read-only");
    if (redo ? !can_redo() : !can_undo()) return {};
    const auto& edit = history_[redo ? history_index_ : history_index_ - 1];
    const auto& from = redo ? edit.before : edit.after;
    const auto& to = redo ? edit.after : edit.before;
    auto bytes = package_.xml.toByteArray();
    if (edit.offset > bytes.size() || from.size() > bytes.size() - edit.offset
        || bytes.compare(edit.offset, from.size(), from) != 0)
        fail("Presentation undo history does not match the document");
    bytes.std::string::replace(edit.offset, from.size(), to);
    restore_content(bytes);
    history_index_ = redo ? history_index_ + 1 : history_index_ - 1;
    state_ = redo ? edit.after_state : edit.before_state;
    return redo ? edit.next : edit.previous;
}
void Presentation::restore_content(const Bytes& bytes)
{
    // Validate the restored document and its scenes before changing live state.
    OdfPackage candidate = package_;
    candidate.xml = read_document_xml(bytes);
    if (children(candidate.body(), DRAW, "page").isEmpty()) fail("Undo would remove every slide");
    auto reader = std::make_shared<OdfTextReader>(candidate);
    auto scenes = read_presentation_scenes(candidate, reader);
    package_ = std::move(candidate);
    scenes_ = std::move(scenes);
    text_reader_ = std::move(reader);
    ++revision_;
}
std::optional<PresentationPosition> Presentation::undo() { return travel(false); }
std::optional<PresentationPosition> Presentation::redo() { return travel(true); }
int Presentation::count() const { return children(package_.body(), DRAW, "page").size(); }
void Presentation::load(const Text& p)
{
    bool office = FileInfo(p).suffix().compare("pptx", CaseInsensitive) == 0;
    OdfPackage candidate(Kind::Stage);
    if (office) candidate = import_pptx(p);
    else candidate.load(p);
    if (children(candidate.body(), DRAW, "page").isEmpty())
        fail("The presentation has no slides");
    auto reader = std::make_shared<OdfTextReader>(candidate);
    auto scenes = read_presentation_scenes(candidate, reader);
    package_ = std::move(candidate);
    scenes_ = std::move(scenes);
    text_reader_ = std::move(reader);
    ++revision_;
    imported_office_ = office;
    clear_history();
}
void Presentation::save(const Text& p)
{
    finish_handle_drag();
    if (FileInfo(p).suffix().compare("odp", CaseInsensitive) != 0)
        fail("Stage saves only ODP; choose an .odp destination");
    package_.save(p);
    imported_office_ = false;
    saved_state_ = state_;
    end_text_edit();
}
const SlideScene& Presentation::scene(int index) const
{
    if (index < 0 || index >= count()) fail("Invalid slide");
    if (scenes_.empty()) {
        text_reader_ = std::make_shared<OdfTextReader>(package_);
        scenes_ = read_presentation_scenes(package_, text_reader_);
    }
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
void Presentation::add_slide(const Text& title, PresentationPosition previous)
{
    finish_handle_drag();
    if (read_only())
        fail("This presentation is read-only");
    const auto before = package_.xml.toByteArray();
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
    remember(before, previous, { count() - 1, 0 });
}
void Presentation::remove_slide(int index, int selectedBox, int selectedShape)
{
    finish_handle_drag();
    if (read_only())
        fail("This presentation is read-only");
    auto pages = children(package_.body(), DRAW, "page");
    if (pages.size() < 2)
        fail("Keep at least one slide");
    if (index < 0 || index >= pages.size()) fail("Invalid slide");
    const auto before = package_.xml.toByteArray();
    package_.body().removeChild(pages.at(index));
    scenes_.clear(); ++revision_;
    remember(before, { index, selectedBox, selectedShape }, { std::min(index, count() - 1), 0 });
}
void Presentation::set_box_text(int slideIndex, int boxIndex, const Text& text, bool merge)
{
    finish_handle_drag();
    if (read_only())
        fail("This presentation is read-only");
    if (text.size() > 4000000) fail("Text frame exceeds native layout text limit");
    size_t length = 0, paragraphs = 1;
    for (auto character : text) {
        if (character == '\n') { length = 0; if (++paragraphs > 100000) fail("Too many text paragraphs"); }
        else if (++length > 1000000) fail("Paragraph exceeds layout text limit");
    }
    const auto& current = scene(slideIndex);
    auto frame = current.editable_sources.at(boxIndex);
    auto box = child(frame, DRAW, "text-box");
    if(box.isNull()&&(!children(frame,TEXT,"p").empty()||!children(frame,TEXT,"h").empty()))box=frame;
    if (box.isNull())
        fail("This frame is not an editable text box");
    if (joined(box) == text)
        return;
    const auto before = package_.xml.toByteArray();
    set_paragraphs(box, text, true);
    auto& shape = scenes_.at(slideIndex).shapes.at(current.editor_shapes.at(boxIndex));
    shape.text = joined(frame);
    TextParagraph base; base.font = shape.paint.font;
    if (!shape.paint.use_window_text_color) base.color = shape.paint.text_color;
    shape.text_flow = text_reader_->frame(box,base,shape.paragraph_style);
    ++revision_;
    remember(before, { slideIndex, boxIndex }, { slideIndex, boxIndex }, merge);
}
void Presentation::begin_handle_drag(int slideIndex, int shapeIndex, int handleIndex)
{
    finish_handle_drag();
    end_text_edit();
    if (read_only()) fail("This presentation is read-only");
    const auto& shape = scene(slideIndex).shapes.at(shapeIndex);
    if (shape.master || shape.source.isNull() || !shape.handles.at(handleIndex).editable)
        fail("This adjustment handle is not editable");
    drag_ = HandleDrag { package_.xml.toByteArray(),
        { slideIndex, shape.editor_index, shapeIndex }, handleIndex, state_ };
}
void Presentation::finish_handle_drag(bool cancel)
{
    if (!drag_) return;
    auto drag = std::move(*drag_);
    drag_.reset();
    if (cancel) {
        if (drag.changed) restore_content(drag.before);
        state_ = drag.state;
    } else {
        state_ = drag.state;
        remember(std::move(drag.before), drag.position, drag.position);
    }
}
void Presentation::move_handle(int slideIndex, int shapeIndex, int handleIndex, ScenePoint target)
{
    if (read_only()) fail("This presentation is read-only");
    if (drag_ && (drag_->position.slide != slideIndex || drag_->position.shape != shapeIndex || drag_->handle != handleIndex))
        fail("Another adjustment handle is being dragged");
    const auto& shape = scene(slideIndex).shapes.at(shapeIndex);
    if (shape.master || shape.source.isNull() || shape.kind != SceneShape::Custom)
        fail("This shape is not adjustable");
    auto inverse = shape.transform.inverse();
    if (!inverse) fail("The shape transform is singular");
    auto geometry = child(shape.source, DRAW, "enhanced-geometry");
    auto values = adjusted_enhanced_modifiers(geometry, shape.width, shape.height, shape.paint,
        handleIndex, inverse->map(target), enhanced_legacy_polar_order(package_));
    TextList parts;
    for (double value : values) parts << Text::number(value, 'g', 17);
    auto modifiers = parts.join(' ');
    if (geometry.attributeNS(DRAW, "modifiers") == modifiers) return;
    const auto before = package_.xml.toByteArray();
    // Locate the corresponding source in an isolated candidate, including XML
    // whitespace siblings. A failed formula/geometry update leaves live state intact.
    std::vector<size_t> path;
    for (auto node = geometry; node != package_.xml.documentElement(); node = node.parentNode()) {
        if (node.isNull()) fail("Adjustment source is outside the presentation");
        size_t index = 0;
        for (auto previous = node.previousSibling(); !previous.isNull(); previous = previous.previousSibling()) ++index;
        path.push_back(index);
    }
    auto candidate = read_document_xml(before);
    auto node = candidate.documentElement();
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        node = node.firstChild();
        for (size_t i = 0; i < *it; ++i) node = node.nextSibling();
        if (node.isNull()) fail("Invalid adjustment source path");
    }
    node.setAttributeNS(DRAW, "draw:modifiers", modifiers);
    PresentationPosition position { slideIndex, shape.editor_index, shapeIndex };
    restore_content(candidate.toByteArray());
    if (drag_) {
        drag_->changed = package_.xml.toByteArray() != drag_->before;
        state_ = drag_->changed ? next_state_++ : drag_->state;
    } else {
        end_text_edit();
        remember(before, position, position);
    }
}
}
