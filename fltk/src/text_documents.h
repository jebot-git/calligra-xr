// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include "odf_package.h"
#include "native_presentation.h"
namespace DomeOffice {
class TextDocument {
public:
    TextDocument();
    void load(const Text&);
    void save(const Text&);
    Text text() const;
    void set_text(const Text&);
    TextFlow flow() const { return read_text_flow(package_); }
    TextPagination paginate(const MeasureText& measure) const { return paginate_text(flow(), measure); }
    bool imported_office() const { return imported_office_; }
    bool read_only() const { return package_.read_only(); }

private:
    OdfPackage package_ { Kind::Words };
    bool imported_office_ = false;
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
