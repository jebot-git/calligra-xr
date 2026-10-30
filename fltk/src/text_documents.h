// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include "odf_package.h"
#include "native_presentation.h"
#include "native_words_edit.h"
#include "native_words_format.h"
namespace DomeOffice {
struct TextSelection {
    size_t anchor = 0, cursor = 0;
    bool operator==(const TextSelection&) const = default;
};
class TextDocument {
public:
    TextDocument();
    void load(const Text&);
    void save(const Text&);
    Text text() const;
    void set_text(const Text&);
    // One frontend event can contain several buffer replacements. Defer history
    // changes until it succeeds; any failed mutation cancels the entire event.
    void begin_edit(TextSelection);
    std::optional<TextSelection> end_edit(std::optional<TextSelection> next = { });
    void cancel_edit();
    bool editing() const { return bool(edit_); }
    bool edit_cancelled() const { return edit_ && edit_->cancelled; }
    // Offsets count Unicode scalars in text(), including paragraph separators.
    void replace(size_t begin, size_t count, const Text& inserted,
        bool paragraph_breaks = true,
        std::optional<TextSelection> previous = { },
        std::optional<TextSelection> next = { });
    // Changes whole list items (including their descendants) without changing
    // paragraph text. Keyboard Tab applies only at paragraph/range boundaries.
    bool list_action(TextSelection, WordsListAction, bool at_start_only = false);
    WordsFormatInfo format_info(TextSelection) const;
    bool format_characters(TextSelection, const WordsCharacterFormat&);
    bool format_paragraphs(TextSelection, const WordsParagraphFormat&);
    OdfPackage style_snapshot() const;
    bool commit_styles(OdfPackage, TextSelection, uint64_t expected_revision,
        std::optional<Text> apply_paragraph = { });
    void clear_typing_format() { typing_format_.reset(); }
    bool has_typing_format() const { return bool(typing_format_); }
    std::optional<TextSelection> undo();
    std::optional<TextSelection> redo();
    bool can_undo() const { return history_index_ != 0; }
    bool can_redo() const { return history_index_ < history_.size(); }
    bool modified() const { return state_ != saved_state_ || (edit_ && edit_->changed && !edit_->cancelled); }
    uint64_t revision() const { return revision_; }
    TextFlow flow() const { return read_text_flow(package_); }
    TextPagination paginate(const MeasureText& measure) const { return paginate_text(flow(), measure); }
    bool imported_office() const { return imported_office_; }
    bool read_only() const { return package_.read_only(); }

private:
    OdfPackage package_ { Kind::Words };
    bool imported_office_ = false;
    struct Edit {
        size_t offset;
        Bytes before, after;
        TextSelection previous, next;
        uint64_t before_state, after_state;
        std::vector<ArchivePartChange> parts;
        size_t bytes() const
        {
            size_t result = before.size() + after.size();
            for (const auto& part : parts)
                result += part.before.size() + part.after.size();
            return result;
        }
    };
    std::vector<Edit> history_;
    size_t history_index_ = 0, history_bytes_ = 0;
    uint64_t state_ = 0, saved_state_ = 0, next_state_ = 1, revision_ = 0;
    struct TypingFormat {
        size_t position;
        WordsCharacterFormat format;
    };
    std::optional<TypingFormat> typing_format_;
    struct EditGroup {
        OdfPackage original;
        TextSelection previous, next;
        std::optional<TypingFormat> typing;
        bool changed = false, cancelled = false;
    };
    std::optional<EditGroup> edit_;
    void apply(OdfPackage, TextSelection, TextSelection);
    std::optional<TextSelection> travel(bool redo);
    void clear_history();
};
struct SlideBox {
    double x = 1, y = 1, width = 24, height = 3;
    Text text, image;
};
struct Slide {
    Text name;
    List<SlideBox> boxes;
    double width = 28, height = 15.75;
    int unsupported = 0;
};
struct PresentationPosition {
    int slide = 0, box = 0;
    int shape = -1;
    bool operator==(const PresentationPosition&) const = default;
};
class Presentation {
public:
    Presentation();
    void load(const Text&);
    void save(const Text&);
    int count() const;
    Slide slide(int index) const;
    const SlideScene& scene(int index) const;
    uint64_t revision() const { return revision_; }
    void add_slide(const Text& title = "New slide", PresentationPosition previous = {});
    void remove_slide(int index, int selectedBox = 0, int selectedShape = -1);
    void set_box_text(int slide, int box, const Text& text, bool merge = false);
    void end_text_edit() { merge_text_ = false; }
    void begin_handle_drag(int slide, int shape, int handle);
    void move_handle(int slide, int shape, int handle, ScenePoint target);
    void finish_handle_drag(bool cancel = false);
    bool dragging_handle() const { return bool(drag_); }
    bool can_undo() const { return history_index_ != 0 || (drag_ && drag_->changed); }
    bool can_redo() const { return history_index_ < history_.size() && !(drag_ && drag_->changed); }
    std::optional<PresentationPosition> undo();
    std::optional<PresentationPosition> redo();
    bool modified() const { return state_ != saved_state_; }
    Bytes image(const Text& path) const { return package_.entry(path); }
    bool imported_office() const { return imported_office_; }
    bool read_only() const { return package_.read_only(); }

private:
    OdfPackage package_ { Kind::Stage };
    bool imported_office_ = false;
    mutable std::vector<SlideScene> scenes_;
    mutable std::shared_ptr<OdfTextReader> text_reader_;
    uint64_t revision_ = 0;
    // Byte deltas retain the exact XML (including styled runs and unknown nodes)
    // without retaining a complete document snapshot for each keystroke.
    struct Edit {
        size_t offset;
        Bytes before, after;
        PresentationPosition previous, next;
        uint64_t before_state, after_state;
    };
    std::vector<Edit> history_;
    size_t history_index_ = 0, history_bytes_ = 0;
    uint64_t state_ = 0, next_state_ = 1, saved_state_ = 0;
    bool merge_text_ = false;
    struct HandleDrag {
        Bytes before;
        PresentationPosition position;
        int handle;
        uint64_t state;
        bool changed = false;
    };
    std::optional<HandleDrag> drag_;
    void clear_history();
    void remember(Bytes, PresentationPosition, PresentationPosition, bool merge = false);
    std::optional<PresentationPosition> travel(bool redo);
    void restore_content(const Bytes&);
};
}
