// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_limits.h"
#include "native_print.h"
#include "print_dialog.h"
#include "format_dialog.h"
#include "suite/desktop-menu.h"
#include "suite/gpu-window.h"
#include "text_documents.h"
#include "text_page_canvas.h"
#include "slide_canvas.h"
#include "workbook.h"
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
using namespace DomeOffice;
namespace {
std::string utf8(const Text& s) { return s.toUtf8().toStdString(); }
Text qs(const char* s) { return Text::fromUtf8(s ? s : ""); }
std::string column_name(int column)
{
    std::string s;
    do {
        s.insert(s.begin(), char('A' + column % 26));
        column = column / 26 - 1;
    } while (column >= 0);
    return s;
}
struct Options {
    std::string file, report, capture, export_odf, print_pdf, printer;
    int duration = 0, capture_delay = 1500;
    bool present = false, pages = false;
};
class Office;
class Window : public dx::GpuWindow {
public:
    std::function<void(int)> command;
    bool presentation = false, stage_app = false;
    using dx::GpuWindow::GpuWindow;
    int handle(int event) override;
};
enum Command {
    New = 1,
    Open,
    Save,
    SaveAs,
    Print,
    Quit,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    AddSheet,
    AddSlide,
    DeleteSlide,
    Present,
    Next,
    Previous,
    About,
    PageLayout,
    TextView,
    ListIndent,
    ListOutdent,
    CharacterFormat,
    ParagraphFormat,
    Bold,
    Italic,
    AlignLeft,
    AlignCenter,
    AlignRight,
    AlignJustify
};
class Grid : public Fl_Table {
    std::map<std::pair<int, int>, std::string> painted;

public:
    std::function<std::string(int, int)> text;
    std::function<NativeValue(int, int)> value;
    std::function<void(int, int)> select;
    std::function<void(const std::string&)> paste;
    std::function<void()> edit;
    int selected_row = 0, selected_col = 0;
    std::optional<std::string> painted_value(int row, int col) const
    {
        auto found = painted.find({ row, col });
        return found == painted.end() ? std::nullopt : std::optional(found->second);
    }
    std::string fitted_text(int row, int col, int width)
    {
        auto full = text(row, col);
        auto cell = value(row, col).scalar();
        if (cell.kind != NativeValue::Number || cell.format != NativeValue::General || fl_width(full.c_str()) <= width)
            return full;
        for (int precision = 14; precision >= 1; --precision) {
            auto candidate = utf8(Text::number(cell.number, 'g', precision));
            if (fl_width(candidate.c_str()) <= width)
                return candidate;
        }
        // A clipped exponent changes the apparent magnitude. Indicate an
        // insufficient width while retaining the full value for copying/editing.
        std::string overflow;
        while (overflow.size() < 64 && fl_width((overflow + '#').c_str()) <= width)
            overflow += '#';
        return overflow;
    }
    Grid(int x, int y, int w, int h)
        : Fl_Table(x, y, w, h)
    {
        end();
        rows(1000);
        cols(100);
        row_header(1);
        col_header(1);
        row_header_width(54);
        col_header_height(28);
        row_height_all(28);
        col_width_all(120);
        row_resize(1);
        col_resize(1);
        when(FL_WHEN_RELEASE);
        callback([](Fl_Widget* w, void*) {
            auto* g = static_cast<Grid*>(w);
            if (g->callback_context() == CONTEXT_CELL) {
                g->selected_row = g->callback_row();
                g->selected_col = g->callback_col();
                g->take_focus();
                g->select(g->selected_row, g->selected_col);
                g->redraw();
                if (Fl::event_clicks() && g->edit)
                    g->edit();
            }
        });
    }
    void draw_cell(TableContext context, int row, int col, int x, int y, int w, int h) override
    {
        if (context == CONTEXT_STARTPAGE) {
            painted.clear();
            fl_font(dx::font, dx::size);
            return;
        }
        if (context != CONTEXT_CELL && context != CONTEXT_ROW_HEADER && context != CONTEXT_COL_HEADER)
            return;
        fl_push_clip(x, y, w, h);
        bool header = context != CONTEXT_CELL;
        bool selected = !header && row == selected_row && col == selected_col;
        fl_color(selected ? FL_SELECTION_COLOR : header ? FL_BACKGROUND_COLOR : FL_BACKGROUND2_COLOR);
        fl_rectf(x, y, w, h);
        fl_color(fl_color_average(FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR, .22f));
        fl_rect(x, y, w, h);
        fl_color(selected ? fl_contrast(FL_FOREGROUND_COLOR, FL_SELECTION_COLOR) : FL_FOREGROUND_COLOR);
        std::string value
            = header ? (context == CONTEXT_ROW_HEADER ? std::to_string(row + 1) : column_name(col)) : fitted_text(row, col, w - 12);
        if (!header)
            painted[{ row, col }] = value;
        fl_draw(value.c_str(), x + 6, y + 2, w - 12, h - 4,
            header ? FL_ALIGN_CENTER : FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        fl_pop_clip();
    }
    int handle(int e) override
    {
        if (e == FL_PASTE) {
            if (paste)
                paste(Fl::event_text());
            return 1;
        }
        if (e == FL_KEYDOWN) {
            int key = Fl::event_key();
            int state = Fl::event_state();
            if ((state & FL_CTRL) && key == 'c') {
                auto value = text(selected_row, selected_col);
                Fl::copy(value.data(), value.size(), 1);
                return 1;
            }
            if ((state & FL_CTRL) && key == 'v') {
                Fl::paste(*this, 1);
                return 1;
            }
            int dr = key == FL_Down ? 1 : key == FL_Up ? -1 : 0, dc = key == FL_Right ? 1 : key == FL_Left ? -1 : 0;
            if (dr || dc) {
                selected_row = std::clamp(selected_row + dr, 0, rows() - 1);
                selected_col = std::clamp(selected_col + dc, 0, cols() - 1);
                select(selected_row, selected_col);
                row_position(std::max(0, selected_row - 10));
                col_position(std::max(0, selected_col - 5));
                redraw();
                return 1;
            }
            if (key == FL_F + 2 || key == FL_Enter || key == FL_KP_Enter) {
                if (edit)
                    edit();
                return 1;
            }
        }
        return Fl_Table::handle(e);
    }
};
class WordsEditor : public Fl_Text_Editor {
public:
    using Fl_Text_Editor::Fl_Text_Editor;
    unsigned handling = 0;
    bool composing = false, cancel_marked = false;
    std::function<TextSelection()> selection;
    std::optional<TextSelection> before_event;
    std::function<void()> finish_event;
    std::function<void(int)> start_event;
    int handle(int event) override
    {
        if (!handling) {
            if (start_event)
                start_event(event);
            if (selection)
                before_event = selection();
        }
        ++handling;
        bool was_composing = composing;
        if (event == FL_UNFOCUS && composing) {
            composing = false;
            cancel_marked = true;
            Fl::compose_reset();
            fl_reset_spot();
        }
        int result;
        if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
            cancel_marked = composing;
            composing = false;
            Fl::compose_reset();
            fl_reset_spot();
            if (!cancel_marked)
                buffer()->unselect();
            result = 1;
        } else if (event == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
            Fl::focus(this);
            // Keep the standard FLTK context menu, but defer paste deletion
            // until clipboard delivery so it belongs to the insertion event.
            switch (handle_rmb(0)) {
            case 1:
                kf_cut(0, this);
                break;
            case 2:
                kf_copy(0, this);
                break;
            case 3:
                Fl::paste(*this, 1);
                break;
            }
            result = 1;
        } else if (event == FL_PASTE && !Fl::event_length()) {
            result = 1;
        } else {
            result = Fl_Text_Editor::handle(event);
        }
        if (!--handling) {
            if (event == FL_KEYDOWN) {
                composing = Fl::compose_state != 0;
                cancel_marked = cancel_marked || (was_composing && !composing && !Fl::event_length());
            }
            if (finish_event)
                finish_event();
            cancel_marked = false;
            before_event.reset();
        }
        return result;
    }
};
class Office {
public:
    Kind kind;
    Window window { 1100, 780 };
    Fl_Menu_Bar menu { 0, 0, 1, 1 };
    std::unique_ptr<DesktopMenu> desktop_menu;
    std::unique_ptr<Workbook> workbook;
    std::unique_ptr<TextDocument> words;
    std::unique_ptr<Presentation> stage;
    Fl_Text_Buffer buffer;
    Fl_Text_Editor* editor = nullptr;
    TextPageCanvas* text_pages = nullptr;
    Fl_Button* page_button = nullptr;
    bool page_view = false;
    int page_menu = -1, text_menu = -1;
    uint64_t text_font_revision = 0, page_revision = 0;
    Grid* grid = nullptr;
    Fl_Input* formula = nullptr;
    Fl_Choice* sheets = nullptr;
    Fl_Box* address = nullptr;
    SlideCanvas* canvas = nullptr;
    Fl_Hold_Browser* slides = nullptr;
    Fl_Choice* boxes = nullptr;
    Fl_Box* status = nullptr;
    Fl_Group* content = nullptr;
    std::string file;
    bool modified = false, updating = false, presenting = false;
    int sheet = 0, slide = 0, box = 0, edit_row = 0, edit_col = 0;
    bool cell_pending = false;
    bool printing = false;
    Options options;
    std::vector<Fl_Widget*> presentation_hidden;
    int canvas_x = 0, canvas_y = 0, canvas_w = 0, canvas_h = 0;
    std::optional<TextSelection> words_edit_previous;
    bool soft_break = false;
    bool formatting_dialog = false;
    int queued_format = 0;
    struct CellEdit {
        int sheet, col, row;
        Text old_value, new_value;
    };
    std::vector<std::vector<CellEdit>> cell_history;
    size_t cell_history_index = 0;
    explicit Office(Kind k, Options opts)
        : kind(k)
        , options(std::move(opts))
    {
        window.stage_app = kind == Kind::Stage;
        window.command = [this](int c) { command(c); };
        window.callback([](Fl_Widget*, void* p) { static_cast<Office*>(p)->command(Quit); }, this);
        menu.user_data(this);
        menu.hide();
        auto add = [&](const char* label, int shortcut, Command command) {
            return menu.add(
                label, shortcut,
                [](Fl_Widget* w, void* p) {
                    static_cast<Office*>(w->user_data())->command(int(reinterpret_cast<intptr_t>(p)));
                },
                reinterpret_cast<void*>(command));
        };
        add("&File/&New", FL_CTRL + 'n', New);
        add("&File/&Open…", FL_CTRL + 'o', Open);
        add("&File/&Save", FL_CTRL + 's', Save);
        add("&File/Save &as…", FL_CTRL + FL_SHIFT + 's', SaveAs);
        add("&File/&Print…", FL_CTRL + 'p', Print);
        add("&File/&Quit", FL_CTRL + 'q', Quit);
        add("&Edit/&Undo", FL_CTRL + 'z', Undo);
        add("&Edit/&Redo", FL_CTRL + FL_SHIFT + 'z', Redo);
        add("&Edit/Cu&t", FL_CTRL + 'x', Cut);
        add("&Edit/&Copy", FL_CTRL + 'c', Copy);
        add("&Edit/&Paste", FL_CTRL + 'v', Paste);
        add("&Edit/Select &all", FL_CTRL + 'a', SelectAll);
        if (kind == Kind::Words) {
            page_menu = add("&View/&Page layout", 0, PageLayout);
            text_menu = add("&View/&Text view", 0, TextView);
            menu.mode(page_menu, FL_MENU_RADIO);
            menu.mode(text_menu, FL_MENU_RADIO | FL_MENU_VALUE);
            add("&Format/Character/&Font…", FL_CTRL + 'd', CharacterFormat);
            add("&Format/Character/&Bold", FL_CTRL + 'b', Bold);
            add("&Format/Character/&Italic", FL_CTRL + 'i', Italic);
            add("&Format/&Paragraph…", 0, ParagraphFormat);
            add("&Format/Alignment/&Left", FL_CTRL + 'l', AlignLeft);
            add("&Format/Alignment/&Center", FL_CTRL + 'e', AlignCenter);
            add("&Format/Alignment/&Right", FL_CTRL + 'r', AlignRight);
            add("&Format/Alignment/&Justify", FL_CTRL + 'j', AlignJustify);
            add("&Format/List/&Indent", 0, ListIndent);
            add("&Format/List/&Outdent", 0, ListOutdent);
        }
        if (kind == Kind::Sheets)
            add("&Sheet/&Add worksheet…", 0, AddSheet);
        if (kind == Kind::Stage) {
            add("&Slide/&Add slide", FL_CTRL + 'm', AddSlide);
            add("&Slide/&Delete slide", 0, DeleteSlide);
            add("&Presentation/&Present", FL_F + 5, Present);
            add("&Presentation/&Next slide", FL_Page_Down, Next);
            add("&Presentation/&Previous slide", FL_Page_Up, Previous);
        }
        add("&Help/&About", 0, About);
        auto* popup = new Fl_Menu_Button(12, 10, 90, 32, "Menu");
        popup->copy(menu.menu());
        popup->user_data(this);
        int x = 112;
        for (auto item : std::vector<std::pair<const char*, Command>> {
                 { "New", New }, { "Open", Open }, { "Save", Save }, { "Undo", Undo }, { "Redo", Redo } }) {
            auto* b = new Fl_Button(x, 10, 80, 32, item.first);
            b->callback(
                [](Fl_Widget* w, void* p) {
                    static_cast<Window*>(w->window())->command(int(reinterpret_cast<intptr_t>(p)));
                },
                reinterpret_cast<void*>(item.second));
            x += 88;
        }
        content = new Fl_Group(0, 52, 1100, 696);
        content->begin();
        if (kind == Kind::Sheets) {
            sheets = new Fl_Choice(12, 58, 190, 30);
            sheets->callback(
                [](Fl_Widget*, void* p) {
                    auto* a = static_cast<Office*>(p);
                    a->guard([&] {
                        a->commit_cell();
                        a->sheet = a->sheets->value();
                        a->select_cell(a->grid->selected_row, a->grid->selected_col);
                        a->grid->redraw();
                    });
                },
                this);
            address = new Fl_Box(208, 58, 88, 30, "A1");
            formula = new Fl_Input(304, 58, 784, 30);
            formula->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY_ALWAYS);
            formula->callback(
                [](Fl_Widget*, void* p) {
                    auto* a = static_cast<Office*>(p);
                    a->cell_pending = true;
                    a->modified = true;
                    a->title();
                    if (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter)
                        a->guard([&] {
                            a->commit_cell();
                            a->grid->take_focus();
                        });
                },
                this);
            grid = new Grid(12, 98, 1076, 638);
            grid->text = [this](int r, int c) { return utf8(workbook->display(sheet, c + 1, r + 1)); };
            grid->value = [this](int r, int c) { return workbook->value(sheet, c + 1, r + 1); };
            grid->select = [this](int r, int c) { guard([&] { select_cell(r, c); }); };
            grid->edit = [this] {
                formula->take_focus();
                formula->insert_position(0, formula->size());
            };
            grid->paste = [this](const std::string& text) { guard([&] { paste_cells(qs(text.c_str())); }); };
            content->resizable(grid);
        } else if (kind == Kind::Words) {
            editor = new WordsEditor(28, 66, 1044, 668);
            static_cast<WordsEditor*>(editor)->selection = [this] { return word_selection(); };
            editor->buffer(&buffer);
            editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
            text_pages = new TextPageCanvas(28, 66, 1044, 668);
            text_pages->hide();
            text_pages->selection = [this] { return word_selection(); };
            text_pages->select = [this](TextSelection range) {
                words->clear_typing_format();
                restore_word_selection(range);
            };
            text_pages->input = [this](int event) { return page_input(event); };
            text_pages->erase = [this](size_t begin, size_t end) { erase_words(begin, end); };
            text_pages->cancel_composition = [this] { cancel_word_composition(); };
            content->resizable(editor);
        } else {
            slides = new Fl_Hold_Browser(12, 62, 170, 674);
            slides->callback(
                [](Fl_Widget*, void* p) {
                    auto* a = static_cast<Office*>(p);
                    a->guard([&] {
                        a->commit_text();
                        a->slide = std::max(0, a->slides->value() - 1);
                        a->box = 0;
                        a->refresh_stage();
                    });
                },
                this);
            canvas = new SlideCanvas(194, 62, 894, 418);
            canvas->scene = [this]() -> const SlideScene& { return stage->scene(slide); };
            canvas->revision = [this] { return stage->revision(); };
            canvas->selected = [this](int n) {
                if (presenting) {
                    command(Next);
                    return;
                }
                guard([&] {
                    commit_text();
                    refresh_stage(n);
                });
            };
            canvas->adjustment = [this](SlideCanvas::Adjustment event, int shape, int handle, ScenePoint point) {
                try {
                    if (event == SlideCanvas::Begin) {
                        commit_text(); stage->begin_handle_drag(slide, shape, handle);
                    } else if (event == SlideCanvas::Move) {
                        stage->move_handle(slide, shape, handle, point);
                    } else {
                        stage->finish_handle_drag(event == SlideCanvas::Cancel);
                        refresh_stage(shape);
                    }
                    modified = stage->modified(); title(); canvas->redraw();
                    return true;
                } catch (const std::exception& e) {
                    stage->finish_handle_drag(true);
                    refresh_stage(shape);
                    modified = stage->modified(); title();
                    message(e.what()); return false;
                }
            };
            boxes = new Fl_Choice(194, 490, 894, 30);
            boxes->callback(
                [](Fl_Widget*, void* p) {
                    auto* a = static_cast<Office*>(p);
                    a->guard([&] {
                        a->commit_text();
                        a->box = a->boxes->value();
                        a->refresh_stage();
                    });
                },
                this);
            editor = new Fl_Text_Editor(194, 530, 894, 206);
            editor->buffer(&buffer);
            editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
            content->resizable(canvas);
        }
        content->end();
        if (editor) {
            editor->user_data(this);
            auto undo_key = [](int key, Fl_Text_Editor* widget) -> int {
                auto* office = static_cast<Office*>(widget->user_data());
                office->command(key == 'y' || (Fl::event_state() & FL_SHIFT) ? Redo : Undo);
                return 1;
            };
            editor->add_key_binding('z', FL_CTRL, undo_key);
            editor->add_key_binding('z', FL_CTRL | FL_SHIFT, undo_key);
            editor->add_key_binding('y', FL_CTRL, undo_key);
            if (kind == Kind::Words) {
                static_cast<WordsEditor*>(editor)->finish_event = [this] { finish_word_edit(); };
                static_cast<WordsEditor*>(editor)->start_event = [this](int event) {
                    if (!words)
                        return;
                    int key = Fl::event_key();
                    if (event == FL_PUSH || (event == FL_KEYDOWN && (key == FL_Left || key == FL_Right || key == FL_Up || key == FL_Down || key == FL_Home || key == FL_End || key == FL_Page_Up || key == FL_Page_Down)))
                        words->clear_typing_format();
                };
                for (int key : { 'b', 'i', 'd', 'l', 'e', 'r', 'j' }) {
                    editor->add_key_binding(key, FL_CTRL, [](int key, Fl_Text_Editor* widget) -> int {
                        auto* office = static_cast<Office*>(widget->user_data());
                        office->command(key == 'b' ? Bold : key == 'i' ? Italic
                                : key == 'd'                           ? CharacterFormat
                                : key == 'l'                           ? AlignLeft
                                : key == 'e'                           ? AlignCenter
                                : key == 'r'                           ? AlignRight
                                                                       : AlignJustify);
                        return 1;
                    });
                }
                auto paste_key = [](int, Fl_Text_Editor* widget) -> int {
                    auto* office = static_cast<Office*>(widget->user_data());
                    Fl::paste(office->page_view ? static_cast<Fl_Widget&>(*office->text_pages) : static_cast<Fl_Widget&>(*widget), 1);
                    return 1;
                };
                editor->add_key_binding('a', FL_CTRL, [](int, Fl_Text_Editor* widget) -> int {
                    static_cast<Office*>(widget->user_data())->command(SelectAll);
                    return 1;
                });
                editor->add_key_binding('v', FL_CTRL, paste_key);
                editor->add_key_binding(FL_Insert, FL_SHIFT, paste_key);
                for (int key : { FL_Enter, FL_KP_Enter })
                    editor->add_key_binding(key, 0, [](int key, Fl_Text_Editor* widget) -> int {
                        auto* office = static_cast<Office*>(widget->user_data());
                        if (office->word_list_action(WordsListAction::ExitEmpty, true))
                            return 1;
                        return Fl_Text_Editor::kf_enter(key, widget);
                    });
                auto list_tab = [](int, Fl_Text_Editor* widget) -> int {
                    auto* office = static_cast<Office*>(widget->user_data());
                    bool reverse = Fl::event_state() & FL_SHIFT;
                    if (office->word_list_action(reverse ? WordsListAction::Outdent : WordsListAction::Indent, true))
                        return 1;
                    return reverse ? 0 : Fl_Text_Editor::kf_default('\t', widget);
                };
                editor->add_key_binding(FL_Tab, 0, list_tab);
                editor->add_key_binding(FL_Tab, FL_SHIFT, list_tab);
                for (int key : { FL_Enter, FL_KP_Enter })
                    editor->add_key_binding(key, FL_SHIFT, [](int key, Fl_Text_Editor* widget) -> int {
                        auto* office = static_cast<Office*>(widget->user_data());
                        office->soft_break = true;
                        auto result = Fl_Text_Editor::kf_enter(key, widget);
                        office->soft_break = false;
                        return result;
                    });
            }
        }
        if (kind == Kind::Stage) {
            auto* present = new Fl_Button(568, 10, 130, 32, "Present");
            present->callback([](Fl_Widget*, void* p) { static_cast<Office*>(p)->command(Present); }, this);
        }
        if (kind == Kind::Words) {
            page_button = new Fl_Button(560, 10, 100, 32, "Pages");
            page_button->callback([](Fl_Widget*, void* p) {
                auto* office = static_cast<Office*>(p);
                office->command(office->page_view ? TextView : PageLayout);
            },
                this);
        }
        status = new Fl_Box(12, 750, 1076, 24);
        status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        window.end();
        window.resizable(content);
        window.size_range(780, 560);
        window.xclass(kind == Kind::Words ? "calligra-words-fltk"
                : kind == Kind::Sheets    ? "calligra-sheets-fltk"
                                          : "calligra-stage-fltk");
        buffer.add_predelete_callback(
            [](int, int, void* p) {
                auto* a = static_cast<Office*>(p);
                if (a->kind == Kind::Words && !a->updating && a->words) {
                    auto* widget = static_cast<WordsEditor*>(a->editor);
                    a->words_edit_previous = a->word_selection();
                    if (widget->handling && !a->words->editing())
                        a->words->begin_edit(widget->before_event.value_or(*a->words_edit_previous));
                }
            },
            this);
        buffer.add_modify_callback(
            [](int position, int inserted, int deleted, int, const char* deleted_text, void* p) {
                auto* a = static_cast<Office*>(p);
                if (!a->updating && (inserted || deleted)) {
                    a->modified = true;
                    a->title();
                    if (a->kind == Kind::Words) {
                        if (a->words->edit_cancelled())
                            return;
                        try {
                            auto bytes = a->buffer_text();
                            auto begin = Text(bytes.substr(0, position)).size();
                            auto added = Text(bytes.substr(position, inserted));
                            auto removed = deleted_text ? Text(std::string(deleted_text, deleted)).size() : 0;
                            a->words->replace(begin, removed, added, !a->soft_break,
                                a->words_edit_previous, TextSelection { begin + added.size(), begin + added.size() });
                            a->words_edit_previous.reset();
                            a->modified = a->words->modified();
                            a->title();
                        } catch (const std::exception& e) {
                            // A failed compound event is repaired after FLTK finishes
                            // all of its callbacks and its final caret update.
                            a->words->cancel_edit();
                            if (!a->words->editing()) {
                                auto selection = a->words_edit_previous.value_or(TextSelection { });
                                a->fill_buffer(a->words->text());
                                a->restore_word_selection(selection);
                            }
                            a->words_edit_previous.reset();
                            a->modified = a->words->modified();
                            a->title();
                            a->message(e.what());
                        }
                    } else if (a->kind == Kind::Stage) {
                        try {
                            a->commit_text(true);
                            a->modified = a->stage->modified();
                            a->title();
                            a->canvas->redraw();
                        } catch (const std::exception& e) {
                            a->fill_buffer(a->stage->slide(a->slide).boxes.at(a->box).text);
                            a->modified = a->stage->modified();
                            a->title();
                            a->message(e.what());
                        }
                    }
                }
            },
            this);
        reset();
        if (!options.file.empty())
            load(options.file);
        dx::style(&window);
        window.show();
        desktop_menu = std::make_unique<DesktopMenu>(window, menu);
        Fl::add_timeout(.04, tick, this);
    }
    ~Office()
    {
        Fl::remove_timeout(tick, this);
        Fl::remove_timeout(format_timeout, this);
        desktop_menu.reset();
        if (text_pages) {
            // Unfocus callbacks must not access an editor already destroyed
            // while the window clears its children.
            text_pages->cancel_composition = {};
            text_pages->selection = {};
            text_pages->select = {};
            text_pages->input = {};
            text_pages->erase = {};
        }
        if (editor) {
            if (kind == Kind::Words) {
                auto* widget = static_cast<WordsEditor*>(editor);
                widget->selection = {};
                widget->start_event = {};
                widget->finish_event = {};
            }
            editor->buffer(nullptr);
        }
        window.remove(menu);
        window.clear();
    }
    template <class F> void guard(F f)
    {
        try {
            f();
        } catch (const std::exception& e) {
            fl_alert("%s", e.what());
        }
    }
    bool readonly() const
    {
        return kind == Kind::Sheets ? workbook->read_only()
            : kind == Kind::Words   ? words->read_only()
                                    : stage->read_only();
    }
    void message(const std::string& text) { status->copy_label(text.c_str()); }
    void title()
    {
        std::string app = kind == Kind::Words ? "Words" : kind == Kind::Sheets ? "Sheets" : "Stage";
        window.copy_label(((file.empty() ? "Untitled" : FileInfo(qs(file.c_str())).fileName().toStdString())
            + (modified ? " *" : "") + " — Calligra " + app)
                .c_str());
    }
    std::string buffer_text()
    {
        char* text = buffer.text();
        std::string result = text;
        free(text);
        return result;
    }
    void fill_buffer(const Text& text)
    {
        updating = true;
        buffer.text(utf8(text).c_str());
        updating = false;
        words_edit_previous.reset();
        if (editor) {
            if (readonly())
                editor->deactivate();
            else
                editor->activate();
        }
    }
    void finish_word_edit()
    {
        if (!words || !words->editing())
            return;
        auto* widget = static_cast<WordsEditor*>(editor);
        if (widget->cancel_marked)
            words->cancel_edit();
        if (widget->composing && !words->edit_cancelled()) {
            modified = words->modified();
            title();
            sync_pages();
            return;
        }
        if (words->edit_cancelled() && widget->composing) {
            widget->composing = false;
            Fl::compose_reset();
            fl_reset_spot();
        }
        widget->cancel_marked = false;
        std::optional<TextSelection> cancelled;
        try {
            cancelled = words->end_edit(words->edit_cancelled() ? std::nullopt : std::optional(word_selection()));
        } catch (const std::exception& e) {
            words->cancel_edit();
            cancelled = words->end_edit();
            message(e.what());
        }
        if (cancelled) {
            fill_buffer(words->text());
            restore_word_selection(*cancelled);
        }
        words_edit_previous.reset();
        modified = words->modified();
        title();
        sync_pages();
    }
    bool cancel_word_composition()
    {
        if (!words || !editor)
            return false;
        auto* widget = static_cast<WordsEditor*>(editor);
        if (!widget->composing)
            return false;
        widget->composing = false;
        widget->cancel_marked = true;
        Fl::compose_reset();
        fl_reset_spot();
        finish_word_edit();
        widget->cancel_marked = false;
        return true;
    }
    int page_input(int event)
    {
        if (event == FL_PUSH) {
            Fl_Menu_Item items[4] { };
            const char* labels[] = { "Cut", "Copy", "Paste" };
            const int actions[] = { Cut, Copy, Paste };
            for (int i = 0; i < 3; ++i) {
                items[i].label(labels[i]);
                items[i].user_data(reinterpret_cast<void*>(intptr_t(actions[i])));
                items[i].labelfont(dx::font);
                items[i].labelsize(dx::size);
                if (readonly() && actions[i] != Copy)
                    items[i].flags = FL_MENU_INACTIVE;
            }
            if (auto* item = items->popup(Fl::event_x_root(), Fl::event_y_root()))
                command(int(reinterpret_cast<intptr_t>(item->user_data())));
            return 1;
        }
        if (readonly()) {
            if (event == FL_KEYDOWN && (Fl::event_state() & FL_CTRL)) {
                if (Fl::event_key() == 'c' || Fl::event_key() == FL_Insert)
                    command(Copy);
                if (Fl::event_key() == 'a')
                    command(SelectAll);
                if (Fl::event_key() == 'n' || Fl::event_key() == 'o' || Fl::event_key() == 's' || Fl::event_key() == 'p' || Fl::event_key() == 'q' || Fl::event_key() == 'z' || Fl::event_key() == 'y')
                    return 0;
            }
            return 1;
        }
        int result = 1;
        guard([&] { result = editor->handle(event); sync_pages(); });
        return result;
    }
    void erase_words(size_t begin, size_t end)
    {
        if (readonly() || begin == end)
            return;
        auto previous = word_selection();
        try {
            words->begin_edit(previous);
            try {
                words->replace(begin, end - begin, { }, true, previous, TextSelection { begin, begin });
                words->end_edit(TextSelection { begin, begin });
            } catch (...) {
                words->cancel_edit();
                words->end_edit();
                throw;
            }
            fill_buffer(words->text());
            restore_word_selection({ begin, begin });
        } catch (const std::exception& e) {
            restore_word_selection(previous);
            message(e.what());
        }
        modified = words->modified();
        title();
        sync_pages();
    }
    static void format_timeout(void* p)
    {
        auto* office = static_cast<Office*>(p);
        auto action = Command(office->queued_format);
        office->queued_format = 0;
        if (office->window.shown())
            office->guard([&] { office->word_format(action); });
    }
    void word_format(Command action)
    {
        if (kind != Kind::Words || !words)
            return;
        auto selection = word_selection();
        auto info = words->format_info(selection);
        WordsCharacterFormat character;
        WordsParagraphFormat paragraph;
        if (action == Bold)
            character.weight = info.bold.value_or(false) ? 400 : 700;
        else if (action == Italic)
            character.italic = !info.character.italic.value_or(false);
        else if (action >= AlignLeft && action <= AlignJustify)
            paragraph.alignment = action == AlignLeft ? "left" : action == AlignCenter ? "center"
                : action == AlignRight                                                 ? "right"
                                                                                       : "justify";
        else {
            formatting_dialog = true;
            try {
                if (action == CharacterFormat) {
                    auto value = character_format_dialog(window, info.character, info.bold);
                    if (value)
                        character = *value;
                } else {
                    auto value = paragraph_format_dialog(window, info.paragraph);
                    if (value)
                        paragraph = *value;
                }
            } catch (...) {
                formatting_dialog = false;
                throw;
            }
            formatting_dialog = false;
        }
        if (!character.empty())
            words->format_characters(selection, character);
        if (!paragraph.empty())
            words->format_paragraphs(selection, paragraph);
        restore_word_selection(selection);
        modified = words->modified();
        title();
        sync_pages();
        if (!character.empty() || !paragraph.empty())
            message(selection.anchor == selection.cursor && !character.empty() ? "Typing format updated; Pages shows document formatting" : "Formatting updated; Pages shows document formatting");
    }
    bool word_list_action(WordsListAction action, bool key = false)
    {
        if (kind != Kind::Words)
            return false;
        auto selection = word_selection();
        try {
            if (!words->list_action(selection, action, key))
                return false;
            words_edit_previous.reset();
            modified = words->modified();
            restore_word_selection(selection);
            title();
            sync_pages();
        } catch (const std::exception& e) {
            restore_word_selection(selection);
            message(e.what());
        }
        return true;
    }
    TextSelection word_selection()
    {
        auto bytes = buffer_text();
        int cursor = editor ? std::clamp(editor->insert_position(), 0, int(bytes.size())) : 0;
        int first = 0, last = 0;
        int anchor = cursor;
        if (buffer.selection_position(&first, &last)) {
            // FLTK permits an insertion caret inside a separately selected
            // range (Select All and the context menu both do this). Native
            // selections represent the full range through its two endpoints.
            if (cursor != first && cursor != last)
                cursor = last;
            anchor = cursor == first ? last : first;
        }
        return { Text(bytes.substr(0, std::clamp(anchor, 0, int(bytes.size())))).size(), Text(bytes.substr(0, cursor)).size() };
    }
    void restore_word_selection(TextSelection selection)
    {
        auto text = words->text();
        int anchor = text.left(std::min(selection.anchor, text.size())).toUtf8().size();
        int cursor = text.left(std::min(selection.cursor, text.size())).toUtf8().size();
        editor->insert_position(cursor);
        if (anchor != cursor)
            buffer.select(std::min(anchor, cursor), std::max(anchor, cursor));
        else
            buffer.unselect();
        editor->show_insert_position();
    }
    void reset()
    {
        workbook.reset();
        words.reset();
        stage.reset();
        sheet = slide = box = 0;
        cell_pending = false;
        file.clear();
        modified = false;
        cell_history.clear();
        cell_history_index = 0;
        if (kind == Kind::Sheets) {
            workbook = std::make_unique<Workbook>();
            refresh_sheets();
        } else if (kind == Kind::Words) {
            text_pages->images.clear();
            text_pages->reset_editing();
            words = std::make_unique<TextDocument>();
            fill_buffer(words->text());
            restore_word_selection({ 0, 0 });
            if (page_view)
                refresh_pages();
            if (window.shown())
                (page_view ? static_cast<Fl_Widget*>(text_pages) : editor)->take_focus();
        } else {
            stage = std::make_unique<Presentation>();
            refresh_stage();
        }
        message("Ready");
        title();
    }
    void refresh_sheets()
    {
        sheets->clear();
        for (int i = 0; i < workbook->sheet_count(); ++i) {
            sheets->add(" ");
            sheets->replace(i, utf8(workbook->sheet_name(i)).c_str());
        }
        sheets->value(sheet);
        grid->rows(std::min(KS_rowMax, std::max(1000, workbook->rows(sheet) + 100)));
        grid->cols(std::min(KS_colMax, std::max(100, workbook->columns(sheet) + 10)));
        select_cell(0, 0);
        grid->redraw();
    }
    void select_cell(int r, int c)
    {
        commit_cell();
        edit_row = r;
        edit_col = c;
        grid->selected_row = r;
        grid->selected_col = c;
        address->copy_label((column_name(c) + std::to_string(r + 1)).c_str());
        formula->value(utf8(workbook->input(sheet, c + 1, r + 1)).c_str());
        if (readonly())
            formula->deactivate();
        else
            formula->activate();
        message("Worksheet: " + utf8(workbook->sheet_name(sheet)));
    }
    void commit_cell()
    {
        if (!grid || !cell_pending)
            return;
        auto old = workbook->input(sheet, edit_col + 1, edit_row + 1);
        auto text = qs(formula->value());
        if (old == text) {
            cell_pending = false;
            return;
        }
        std::vector<CellEdit> changes { { sheet, edit_col + 1, edit_row + 1, old, text } };
        workbook->set(sheet, edit_col + 1, edit_row + 1, text);
        cell_pending = false;
        cell_history.resize(cell_history_index);
        cell_history.push_back(changes);
        cell_history_index = cell_history.size();
        modified = true;
        title();
        grid->redraw();
    }
    void paste_cells(const Text& text)
    {
        std::vector<CellEdit> changes;
        auto rows = text.split('\n');
        if (rows.size() > 1 && rows.last().isEmpty())
            rows.removeLast();
        int r = grid->selected_row + 1;
        for (auto row : rows) {
            if (row.endsWith('\r'))
                row.chop(1);
            int c = grid->selected_col + 1;
            for (auto value : row.split('\t')) {
                workbook->validate_cell(sheet, c, r);
                changes.push_back({ sheet, c, r, workbook->input(sheet, c, r), value });
                ++c;
                if (changes.size() > 10000)
                    throw std::runtime_error("Paste up to 10,000 cells at a time");
            }
            ++r;
        }
        for (const auto& change : changes)
            workbook->set(change.sheet, change.col, change.row, change.new_value);
        cell_history.resize(cell_history_index);
        cell_history.push_back(changes);
        cell_history_index = cell_history.size();
        modified = true;
        title();
        select_cell(grid->selected_row, grid->selected_col);
        grid->redraw();
    }
    void commit_text(bool merge = false)
    {
        if (!editor || readonly())
            return;
        auto text = qs(buffer_text().c_str());
        if (kind == Kind::Words)
            words->set_text(text);
        else {
            const auto current = stage->slide(slide);
            if (box >= 0 && box < int(current.boxes.size()) && current.boxes[box].image.isEmpty())
                stage->set_box_text(slide, box, text, merge);
        }
        if (kind == Kind::Stage && !merge) stage->end_text_edit();
    }
    void refresh_pages()
    {
        text_pages->set_flow(words->flow());
        text_font_revision = dx::font_revision;
        page_revision = words->revision();
    }
    void sync_pages()
    {
        if (!page_view || !words)
            return;
        if (page_revision != words->revision() || text_font_revision != dx::font_revision)
            refresh_pages();
        text_pages->selection_changed();
    }
    void refresh_stage(int selected_shape = -1)
    {
        auto current = stage->slide(slide);
        slides->clear();
        for (int i = 0; i < stage->count(); ++i) {
            auto label = std::to_string(i + 1) + "  " + utf8(stage->slide(i).name);
            slides->add(label.c_str());
        }
        slides->value(slide + 1);
        boxes->clear();
        for (int i = 0; i < int(current.boxes.size()); ++i) {
            boxes->add(" ");
            boxes->replace(i,
                ((current.boxes[i].image.isEmpty() ? std::string("Text frame ") : std::string("Image frame "))
                    + std::to_string(i + 1))
                    .c_str());
        }
        box = std::clamp(box, 0, std::max(0, int(current.boxes.size()) - 1));
        const auto& scene = stage->scene(slide);
        if (selected_shape >= 0 && selected_shape < int(scene.shapes.size()) && !scene.shapes[selected_shape].master)
            box = scene.shapes[selected_shape].editor_index;
        else
            selected_shape = current.boxes.empty() ? -1 : int(scene.editor_shapes.at(box));
        boxes->value(box);
        fill_buffer(current.boxes.isEmpty() || box < 0 ? Text() : current.boxes[box].text);
        if (box < 0 || current.boxes.isEmpty() || !current.boxes[box].image.isEmpty())
            editor->deactivate();
        canvas->box = box;
        canvas->selected_shape = selected_shape;
        canvas->selection_visible = !presenting;
        canvas->adjustable = !presenting && !readonly();
        canvas->redraw();
        message("Slide " + std::to_string(slide + 1) + " of " + std::to_string(stage->count()));
    }
    void load(const std::string& path)
    {
        cell_pending = false;
        if (kind == Kind::Sheets) {
            auto fresh = std::make_unique<Workbook>();
            fresh->load(qs(path.c_str()));
            workbook = std::move(fresh);
            sheet = 0;
            refresh_sheets();
        } else if (kind == Kind::Words) {
            auto fresh = std::make_unique<TextDocument>();
            fresh->load(qs(path.c_str()));
            text_pages->images.clear();
            text_pages->reset_editing();
            words = std::move(fresh);
            fill_buffer(words->text());
            restore_word_selection({ 0, 0 });
            if (page_view)
                refresh_pages();
            if (window.shown())
                (page_view ? static_cast<Fl_Widget*>(text_pages) : editor)->take_focus();
        } else {
            auto fresh = std::make_unique<Presentation>();
            fresh->load(qs(path.c_str()));
            stage = std::move(fresh);
            slide = box = 0;
            canvas->reset();
            refresh_stage();
        }
        file = path;
        modified = false;
        cell_history.clear();
        cell_history_index = 0;
        title();
        if (readonly())
            message("Read-only document");
        else if (kind == Kind::Sheets && workbook->imported_xlsx())
            message("XLSX cell data imported; save as ODS. Formatting, charts and "
                    "layout are not imported.");
        else if (kind == Kind::Words && words->imported_office())
            message("DOCX text/tables imported; save as ODT. Formatting, images, lists and page layout omitted.");
        else if (kind == Kind::Stage && stage->imported_office())
            message("PPTX text/images imported; save as ODP. Themes, groups, charts, animations and formatting omitted.");
    }
    bool save(bool as = false)
    {
        commit_cell();
        commit_text();
        std::string target = file;
        bool imported = kind == Kind::Sheets
            ? FileInfo(qs(file.c_str())).suffix().compare("xlsx", CaseInsensitive) == 0
            : kind == Kind::Words ? words->imported_office() : stage->imported_office();
        if (as || target.empty() || imported) {
            if (imported)
                target = utf8(
                    FileInfo(qs(file.c_str())).path() + "/" + FileInfo(qs(file.c_str())).completeBaseName() + "." + suffix(kind));
            auto filter = "*." + utf8(suffix(kind));
            const char* chosen = fl_file_chooser(
                imported ? "Save imported document as OpenDocument (conversion may omit features)" : "Save document",
                filter.c_str(), target.c_str());
            if (!chosen)
                return false;
            target = chosen;
            if (FileInfo(qs(target.c_str())).suffix().isEmpty())
                target += '.' + utf8(suffix(kind));
            if (target != file && std::filesystem::exists(target)
                && fl_choice("Replace the existing file?", "Cancel", "Replace", nullptr) != 1)
                return false;
        }
        if (kind == Kind::Sheets)
            workbook->save(qs(target.c_str()));
        else if (kind == Kind::Words)
            words->save(qs(target.c_str()));
        else
            stage->save(qs(target.c_str()));
        file = target;
        modified = false;
        title();
        message("Saved " + target);
        return true;
    }
    bool discard()
    {
        if (!modified)
            return true;
        int result = fl_choice("Save changes before closing this document?", "Cancel", "Discard", "Save");
        return result == 1 || (result == 2 && save());
    }
    void command(int c)
    {
        if ((printing || formatting_dialog || queued_format) && c != Quit) return;
        guard([&] {
            if (kind == Kind::Words && cancel_word_composition() && (c == Undo || c == Redo))
                return;
            if (canvas && canvas->dragging())
                canvas->finish_drag();
            if (c == New) {
                if (discard())
                    reset();
            } else if (c == Open) {
                if (!discard())
                    return;
                auto filter = kind == Kind::Sheets ? std::string("*.{ods,xlsx,ODS,XLSX}")
                    : kind == Kind::Words ? std::string("*.{odt,docx,ODT,DOCX}") : std::string("*.{odp,pptx,ODP,PPTX}");
                if (auto* name = fl_file_chooser("Open document", filter.c_str(), file.c_str()))
                    load(name);
            } else if ((c == PageLayout || c == TextView) && kind == Kind::Words) {
                bool pages = c == PageLayout;
                if (pages && !page_view)
                    refresh_pages();
                page_view = pages;
                if (page_view) {
                    editor->hide();
                    text_pages->show();
                    text_pages->take_focus();
                    content->resizable(text_pages);
                    text_pages->selection_changed();
                } else {
                    text_pages->hide();
                    editor->show();
                    editor->take_focus();
                    content->resizable(editor);
                }
                page_button->label(page_view ? "Text view" : "Pages");
                menu.mode(page_menu, FL_MENU_RADIO | (page_view ? FL_MENU_VALUE : 0));
                menu.mode(text_menu, FL_MENU_RADIO | (page_view ? 0 : FL_MENU_VALUE));
                if (desktop_menu)
                    desktop_menu->refresh();
                window.redraw();
            } else if (c == Save || c == SaveAs)
                save(c == SaveAs);
            else if (c == Print) {
                if (!readonly()) {
                    commit_cell();
                    commit_text();
                }
                struct PrintingScope {
                    bool& active;
                    explicit PrintingScope(bool& value)
                        : active(value)
                    {
                        active = true;
                    }
                    ~PrintingScope() { active = false; }
                } printing_scope(printing);
                if (auto request = print_dialog(window, kind == Kind::Sheets, sheet)) {
                    TempDir directory;
                    auto path = directory.path() + "/print.pdf";
                    PrintResult result;
                    if (kind == Kind::Words)
                        result = DomeOffice::print_pdf(*words, path, request->settings);
                    else if (kind == Kind::Sheets)
                        result = DomeOffice::print_pdf(*workbook, path, request->settings);
                    else
                        result = DomeOffice::print_pdf(*stage, path, request->settings);
                    if ((result.missing_glyphs || !result.unsupported.empty()) && fl_choice("Some document features cannot be rendered by this build. Print the supported content?", "Cancel", "Print", nullptr) != 1)
                        return;
                    auto job = cups_submit(path, request->destination, file.empty() ? "Calligra document" : qs(file.c_str()), request->copies, request->pages, result.pages);
                    message(utf8(job));
                }
            } else if (c == Quit) {
                if (discard())
                    window.hide();
            } else if (c == CharacterFormat || c == ParagraphFormat) {
                // Return the D-Bus menu reply before entering the modal loop.
                queued_format = c;
                Fl::add_timeout(0, format_timeout, this);
            } else if (c >= Bold && c <= AlignJustify) {
                word_format(Command(c));
            } else if (c == ListIndent || c == ListOutdent) {
                if (!word_list_action(c == ListIndent ? WordsListAction::Indent : WordsListAction::Outdent))
                    message("Place the cursor in a list item");
            } else if (c == Undo || c == Redo) {
                if (kind == Kind::Sheets) {
                    bool redo = c == Redo;
                    if ((redo && cell_history_index >= cell_history.size()) || (!redo && !cell_history_index))
                        return;
                    auto& changes = cell_history[redo ? cell_history_index++ : --cell_history_index];
                    for (auto& change : changes)
                        workbook->set(change.sheet, change.col, change.row, redo ? change.new_value : change.old_value);
                    select_cell(grid->selected_row, grid->selected_col);
                    grid->redraw();
                } else if (kind == Kind::Stage) {
                    commit_text();
                    auto position = c == Undo ? stage->undo() : stage->redo();
                    if (!position)
                        return;
                    slide = position->slide;
                    box = position->box;
                    refresh_stage(position->shape);
                    editor->insert_position(buffer.length());
                    editor->show_insert_position();
                } else {
                    auto selection = c == Undo ? words->undo() : words->redo();
                    if (!selection)
                        return;
                    fill_buffer(words->text());
                    restore_word_selection(*selection);
                    if (page_view)
                        refresh_pages();
                }
                modified = kind == Kind::Stage ? stage->modified() : kind == Kind::Words ? words->modified()
                                                                                         : true;
                title();
            } else if (c == Copy) {
                if (editor)
                    Fl_Text_Editor::kf_copy(0, editor);
                else {
                    auto text = utf8(workbook->display(sheet, grid->selected_col + 1, grid->selected_row + 1));
                    Fl::copy(text.data(), text.size(), 1);
                }
            } else if (c == Paste) {
                if (editor && editor->active_r()) {
                    if (kind == Kind::Words)
                        Fl::paste(page_view ? static_cast<Fl_Widget&>(*text_pages) : static_cast<Fl_Widget&>(*editor), 1);
                    else
                        Fl_Text_Editor::kf_paste(0, editor);
                } else if (!editor)
                    Fl::paste(*grid, 1);
            } else if (c == Cut) {
                if (editor && editor->active_r())
                    Fl_Text_Editor::kf_cut(0, editor);
                else if (!editor) {
                    command(Copy);
                    formula->value("");
                    cell_pending = true;
                    commit_cell();
                }
            } else if (c == SelectAll) {
                if (editor && kind == Kind::Words) {
                    words->clear_typing_format();
                    editor->insert_position(buffer.length());
                    Fl_Text_Editor::kf_select_all(0, editor);
                } else if (editor)
                    buffer.select(0, buffer.length());
            } else if (c == AddSheet) {
                commit_cell();
                if (auto* name = fl_input("Worksheet name", "Sheet")) {
                    workbook->add_sheet(qs(name));
                    sheet = workbook->sheet_count() - 1;
                    refresh_sheets();
                    modified = true;
                    title();
                }
            } else if (c == AddSlide) {
                commit_text();
                stage->add_slide("New slide", { slide, box, canvas->selected_shape });
                slide = stage->count() - 1;
                box = 0;
                refresh_stage();
                modified = true;
                title();
            } else if (c == DeleteSlide) {
                commit_text();
                stage->remove_slide(slide, box, canvas->selected_shape);
                slide = std::min(slide, stage->count() - 1);
                box = 0;
                refresh_stage();
                modified = true;
                title();
            } else if (c == Present) {
                commit_text();
                presenting = !presenting;
                canvas->selection_visible = !presenting;
                canvas->adjustable = !presenting && !readonly();
                window.presentation = presenting;
                if (presenting) {
                    canvas_x = canvas->x();
                    canvas_y = canvas->y();
                    canvas_w = canvas->w();
                    canvas_h = canvas->h();
                    presentation_hidden.clear();
                    for (int i = 0; i < window.children(); ++i) {
                        auto* widget = window.child(i);
                        if (widget != content && widget->visible()) {
                            presentation_hidden.push_back(widget);
                            widget->hide();
                        }
                    }
                    for (int i = 0; i < content->children(); ++i) {
                        auto* widget = content->child(i);
                        if (widget != canvas && widget->visible()) {
                            presentation_hidden.push_back(widget);
                            widget->hide();
                        }
                    }
                    canvas->resize(0, 0, window.w(), window.h());
                    canvas->box = -1;
                    canvas->clear_images();
                    window.fullscreen();
                } else {
                    canvas->clear_images();
                    window.fullscreen_off();
                    for (auto* widget : presentation_hidden)
                        widget->show();
                    presentation_hidden.clear();
                    canvas->resize(canvas_x, canvas_y, canvas_w, canvas_h);
                    canvas->box = box;
                }
                window.redraw();
            } else if (c == Next || c == Previous) {
                commit_text();
                slide = std::clamp(slide + (c == Next ? 1 : -1), 0, stage->count() - 1);
                box = 0;
                refresh_stage();
                if (presenting) {
                    slides->hide();
                    boxes->hide();
                    editor->hide();
                    canvas->box = -1;
                }
            } else if (c == About)
                fl_message("Calligra %s for DomeD\nNative FLTK interface; OpenDocument "
                           "files.\nThis port currently provides "
                           "basic editing.\nSee the installed port-status "
                           "documentation for remaining features.",
                    kind == Kind::Words        ? "Words"
                        : kind == Kind::Sheets ? "Sheets"
                                               : "Stage");
            if (kind == Kind::Words)
                sync_pages();
        });
    }
    void report()
    {
        if (options.report.empty())
            return;
        Json data = { { "application",
                          kind == Kind::Words        ? "words"
                              : kind == Kind::Sheets ? "sheets"
                                                     : "stage" },
            { "file", file }, { "modified", modified }, { "qt_widgets", false },
            { "desktop_menu",
                { { "service", desktop_menu->serviceName() }, { "path", desktop_menu->objectPath() },
                    { "attached", desktop_menu->attached() } } } };
        data["print_dialog"] = printing;
        data["format_dialog"] = formatting_dialog;
        data["window_size"] = { window.w(), window.h() };
        if (kind == Kind::Sheets) {
            data["cell"] = { { "sheet", sheet }, { "row", grid->selected_row + 1 },
                { "column", grid->selected_col + 1 }, { "input", formula->value() },
                { "value", utf8(workbook->display(sheet, grid->selected_col + 1, grid->selected_row + 1)) } };
            if (auto painted = grid->painted_value(grid->selected_row, grid->selected_col))
                data["cell"]["painted_value"] = *painted;
        } else
            data["text"] = buffer_text();
        if (kind == Kind::Words) {
            auto selection = word_selection();
            data["editing"] = { { "anchor", selection.anchor }, { "cursor", selection.cursor },
                { "can_undo", words->can_undo() }, { "can_redo", words->can_redo() }, { "revision", words->revision() }, { "typing_format", words->has_typing_format() } };
            size_t shaped_lines = 0, glyphs = 0, missing = 0;
            Json labels = Json::array(), image_labels = Json::array(), tabs = Json::array(), cells = Json::array();
            size_t table_paints = 0;
            std::vector<std::string> unsupported;
            for (const auto& feature : text_pages->pagination.unsupported)
                unsupported.push_back(utf8(feature));
            size_t page_index = 0;
            for (const auto& page : text_pages->pagination.pages) {
                table_paints += page.paints.size();
                for (const auto& cell : page.cells)
                    cells.push_back({ { "table", cell.table }, { "row", cell.row }, { "column", cell.column }, { "rows", cell.rows }, { "columns", cell.columns }, { "header", cell.header }, { "continued", cell.continued }, { "page", page_index }, { "x", cell.rect.x }, { "y", cell.rect.y }, { "width", cell.rect.width }, { "height", cell.rect.height }, { "page_width", page.style.width }, { "page_height", page.style.height } });
                for (const auto& line : page.lines)
                    if (line.shaped) {
                        ++shaped_lines;
                        glyphs += line.shaped->glyphs.size();
                        missing += line.shaped->missing_glyphs;
                        for (const auto &tab : line.tabs)
                            tabs.push_back({{"source", tab.source},
                                            {"page", page_index},
                                            {"paragraph", line.paragraph},
                                            {"x1", line.x + tab.x1},
                                            {"x2", line.x + tab.x2},
                                            {"baseline", line.baseline},
                                            {"position", tab.position},
                                            {"strokes", tab.strokes.size()},
                                            {"glyphs", tab.leader ? tab.leader->glyphs.size() : 0},
                                            {"page_width", page.style.width},
                                            {"page_height", page.style.height}});
                        if (line.label && line.label->image)
                            image_labels.push_back({{"source", utf8(line.label->image->source)},
                                                    {"available", bool(line.label->image->asset)},
                                                    {"page", page_index},
                                                    {"paragraph", line.paragraph},
                                                    {"x", line.label->x},
                                                    {"y", line.baseline + line.label->y},
                                                    {"width", line.label->width},
                                                    {"height", line.label->height},
                                                    {"page_width", page.style.width},
                                                    {"page_height", page.style.height}});
                        if(line.label&&line.label->shaped) {
                            glyphs+=line.label->shaped->glyphs.size();
                            missing+=line.label->shaped->missing_glyphs;
                            labels.push_back({{"text",utf8(line.label->text)},{"page",page_index},{"paragraph",line.paragraph},
                                {"x",line.label->x},{"baseline",line.baseline},{"width",line.label->width},{"size",line.label->font.size},
                                {"page_width",page.style.width},{"page_height",page.style.height}});
                        }
                    }
                ++page_index;
            }
            data["page_layout"] = { { "visible", page_view },
                { "editing", text_pages->editing() }, { "scroll", text_pages->scroll_position() },
                { "preedit", static_cast<WordsEditor*>(editor)->composing },
                { "caret_page", text_pages->caret_page() ? *text_pages->caret_page() + 1 : 0 },
                { "pages", text_pages->pagination.pages.size() },
                { "page", text_pages->current_page() + 1 },
                { "render_error", text_pages->render_error() },
                { "rasterizations", text_pages->rasterizations() },
                { "shaped_lines", shaped_lines },
                { "glyphs", glyphs },
                { "missing_glyphs", missing },
                { "list_labels", labels },
                { "image_labels", image_labels },
                { "tabs", tabs },
                { "table_cells", cells },
                { "table_paints", table_paints },
                { "unsupported", unsupported },
                { "image_decodes", text_pages->images.decodes() },
                { "image_uploads", text_pages->images.uploads() },
                { "image_errors", text_pages->images.errors() },
                { "viewport", { text_pages->x(), text_pages->y(), text_pages->w(), text_pages->h() } } };
        }
        if (kind == Kind::Words) {
            auto caret = text_pages->caret_rect();
            if (caret)
                data["page_layout"]["caret_rect"] = { caret->x, caret->y, caret->width, caret->height };
            else
                data["page_layout"]["caret_rect"] = nullptr;
        }
        if (kind == Kind::Stage) {
            const auto& model = stage->scene(slide);
            size_t masters = 0;
            for (const auto& shape : model.shapes)
                masters += shape.master;
            std::vector<std::string> unsupported;
            for (const auto& feature : model.unsupported)
                unsupported.push_back(utf8(feature));
            const auto &text = canvas->text_stats();
            Json adjustments = Json::array(), tabs = Json::array();
            for (const auto &tab : text.tabs)
                tabs.push_back({{"source", tab.source},
                                {"x1", tab.start.x},
                                {"y1", tab.start.y},
                                {"x2", tab.end.x},
                                {"y2", tab.end.y},
                                {"position", tab.position},
                                {"strokes", tab.strokes},
                                {"glyphs", tab.glyphs}});
            for (size_t i = 0; i < model.shapes.size(); ++i) {
                const auto& shape = model.shapes[i];
                if (shape.master) continue;
                for (size_t j = 0; j < shape.handles.size(); ++j) {
                    const auto& handle = shape.handles[j];
                    auto point = shape.transform.map(handle.position);
                    adjustments.push_back({ { "shape", i }, { "handle", j }, { "x", point.x }, { "y", point.y }, { "editable", handle.editable } });
                }
            }
            for (const auto& feature : text.unsupported)
                unsupported.push_back(utf8(feature));
            Json cells = Json::array();
            for (const auto& cell : text.cells) {
                Json corners = Json::array();
                for (const auto& p : cell.corners)
                    corners.push_back({ p.x, p.y });
                const auto& c = cell.source;
                cells.push_back({ { "table", c.table }, { "row", c.row }, { "column", c.column }, { "rows", c.rows }, { "columns", c.columns }, { "header", c.header }, { "corners", corners } });
            }
            data["scene"] = { { "page", slide + 1 },
                { "width", model.width },
                { "height", model.height },
                { "shapes", model.shapes.size() },
                { "master_shapes", masters },
                { "editable_frames", model.editable_sources.size() },
                { "selected_frame", box },
                { "can_undo", stage->can_undo() },
                { "can_redo", stage->can_redo() },
                { "selected_shape", canvas->selected_shape },
                { "adjustments", adjustments },
                { "revision", stage->revision() },
                { "slides", stage->count() },
                { "rendered_shapes", canvas->rendered_shapes() },
                { "geometry_points", canvas->geometry_points() },
                { "render_error", canvas->render_error() },
                { "unsupported", unsupported },
                { "shaped_lines", text.lines },
                { "table_cells", cells },
                { "table_paints", text.table_paints },
                { "glyphs", text.glyphs },
                { "list_labels", text.labels },
                { "image_labels", text.image_labels },
                { "image_decodes", canvas->images().decodes() },
                { "image_uploads", canvas->images().uploads() },
                { "image_errors", canvas->images().errors() },
                { "tabs", tabs },
                { "missing_glyphs", text.missing_glyphs },
                { "overflow_frames", text.overflow_frames },
                { "viewport", { canvas->x(), canvas->y(), canvas->w(), canvas->h() } } };
        }
        // Readers must see one complete snapshot while the UI refreshes it.
        TempFile temporary(Text(options.report + ".XXXXXX"));
        if (!temporary.open())
            return;
        temporary.close();
        std::ofstream output(utf8(temporary.fileName()));
        output << data.dump(2) << '\n';
        output.close();
        if (output)
            ::rename(utf8(temporary.fileName()).c_str(), options.report.c_str());
    }
    void capture()
    {
        if (options.capture.empty())
            return;
        window.make_current();
        window.draw();
        glFinish();
        int w = window.pixel_w(), h = window.pixel_h();
        std::vector<unsigned char> pixels(size_t(w) * h * 4);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // Publish complete captures: readers may open them as soon as the
        // final pathname appears, including under a heavily loaded compositor.
        TempFile temporary(Text(options.capture + ".XXXXXX"));
        if (!temporary.open())
            return;
        temporary.close();
        std::ofstream out(utf8(temporary.fileName()), std::ios::binary);
        out << "P6\n" << w << ' ' << h << "\n255\n";
        std::vector<unsigned char> row(size_t(w) * 3);
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x)
                std::copy_n(pixels.data() + (size_t(y) * w + x) * 4, 3, row.data() + size_t(x) * 3);
            out.write(reinterpret_cast<char*>(row.data()), w * 3);
        }
        out.close();
        if (!out || ::rename(utf8(temporary.fileName()).c_str(), options.capture.c_str()) != 0)
            return;
        Json metadata = { { "renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER)) },
            { "gpu_widgets", true }, { "universal_menu", desktop_menu->attached() } };
#ifdef FLTK_USE_WAYLAND
        metadata["wayland"] = bool(fl_wl_display());
#endif
        if (kind == Kind::Words && page_view) {
            unsigned char minimum = 255;
            double sx = double(w) / window.w(), sy = double(h) / window.h();
            int x0 = std::clamp(int(std::ceil(text_pages->x() * sx)), 0, w);
            int x1 = std::clamp(int(std::floor((text_pages->x() + text_pages->w()) * sx)), 0, w);
            int y0 = std::clamp(int(std::ceil(text_pages->y() * sy)), 0, h);
            int y1 = std::clamp(int(std::floor((text_pages->y() + text_pages->h()) * sy)), 0, h);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    minimum = std::min(minimum, pixels[(size_t(h - 1 - y) * w + x) * 4 + 3]);
            auto range = word_selection();
            metadata["page_min_alpha"] = unsigned(minimum);
            metadata["page_selection"] = { range.anchor, range.cursor };
        }
        TempFile attributes(Text(options.capture + ".json.XXXXXX"));
        if (!attributes.open())
            return;
        attributes.close();
        std::ofstream detail(utf8(attributes.fileName()));
        detail << metadata.dump(2) << '\n';
        detail.close();
        if (detail)
            ::rename(utf8(attributes.fileName()).c_str(), (options.capture + ".json").c_str());
    }
    static void tick(void* data)
    {
        auto* self = static_cast<Office*>(data);
        if (self->page_view && self->text_font_revision != dx::font_revision) {
            self->text_font_revision = dx::font_revision;
            self->guard([&] { self->refresh_pages(); });
        }
        self->report();
        if (self->window.shown())
            Fl::repeat_timeout(.1, tick, data);
    }
};
int Window::handle(int event)
{
    if (event == FL_KEYDOWN) {
        auto key = Fl::event_key(), state = Fl::event_state();
        if (stage_app && key == FL_F + 5) {
            command(Present);
            return 1;
        }
        if (stage_app && key == 'm' && (state & FL_CTRL)) {
            command(AddSlide);
            return 1;
        }
        if (presentation) {
            int c = key == FL_Escape                                     ? Present
                : (key == FL_Right || key == FL_Page_Down || key == ' ') ? Next
                : (key == FL_Left || key == FL_Page_Up)                  ? Previous
                                                                         : 0;
            if (c) {
                command(c);
                return 1;
            }
        }
        if ((state & FL_CTRL) && !(state & (FL_ALT | FL_META))) {
            int c = key == 'n' ? New
                : key == 'o'   ? Open
                : key == 's'   ? ((state & FL_SHIFT) ? SaveAs : Save)
                : key == 'p'   ? Print
                : key == 'q'   ? Quit
                : key == 'z'   ? ((state & FL_SHIFT) ? Redo : Undo)
                : key == 'y'   ? Redo
                               : 0;
            if (c) {
                command(c);
                return 1;
            }
        }
    }
    return dx::GpuWindow::handle(event);
}
} // namespace
int runOffice(DomeOffice::Kind kind, int argc, char** argv)
{
    try {
        Options options;
        options.pages = kind == Kind::Words;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for " + arg);
                return std::string(argv[i]);
            };
            if (arg == "--present" && kind == Kind::Stage)
                options.present = true;
            else if (arg == "--page-layout" && kind == Kind::Words)
                options.pages = true;
            else if (arg == "--text-view" && kind == Kind::Words)
                options.pages = false;
            else if (arg == "--quit-after-ms")
                options.duration = std::stoi(value());
            else if (arg == "--capture")
                options.capture = value();
            else if (arg == "--capture-delay-ms") {
                options.capture_delay = std::stoi(value());
                if (options.capture_delay < 1 || options.capture_delay > 600000)
                    throw std::runtime_error("Capture delay must be between 1 and 600000 milliseconds");
            } else if (arg == "--export-odf")
                options.export_odf = value();
            else if (arg == "--print-pdf")
                options.print_pdf = value();
            else if (arg == "--print-to")
                options.printer = value();
            else if (arg == "--report")
                options.report = value();
            else if (arg == "--help") {
                std::cout << "Calligra FLTK [FILE] [--report JSON] [--capture PPM] "
                             "[--quit-after-ms N] [--capture-delay-ms N] [--page-layout|--text-view (Words; pages default)]\n"
                             "Batch: FILE [--export-odf OUTPUT] [--print-pdf OUTPUT.pdf] [--print-to CUPS_PRINTER]\n";
                return 0;
            } else if (arg.starts_with("--"))
                throw std::runtime_error("Unknown argument " + arg);
            else
                options.file = arg;
        }

        if (!options.export_odf.empty() || !options.print_pdf.empty() || !options.printer.empty()) {
            // FLTK's CPU image decoder initializes its default graphics driver.
            // Its X11 driver can decode without opening a display, whereas a
            // forced Wayland driver requires a connection even without windows.
            // This branch creates no windows; interactive launches stay Wayland.
            setenv("FLTK_BACKEND", "x11", 1);
            if (options.file.empty())
                throw std::runtime_error("Batch export/printing requires an input document");
            TempDir directory;
            auto pdf = options.print_pdf.empty() ? directory.path() + "/print.pdf" : qs(options.print_pdf.c_str());
            auto run = [&](auto& document) {
                document.load(qs(options.file.c_str()));
                if (!options.export_odf.empty())
                    document.save(qs(options.export_odf.c_str()));
                if (!options.print_pdf.empty() || !options.printer.empty()) {
                    auto result = DomeOffice::print_pdf(document, pdf);
                    std::cout << "Prepared " << result.pages << " print pages\n";
                    if (result.missing_glyphs || !result.unsupported.empty())
                        std::cerr << "Print rendering: " << result.missing_glyphs << " missing glyphs, " << result.unsupported.size() << " unsupported features\n";
                    if (!options.printer.empty())
                        std::cout << utf8(cups_submit(pdf, qs(options.printer.c_str()), qs(options.file.c_str()))) << '\n';
                }
            };
            if (kind == Kind::Words) {
                TextDocument document;
                run(document);
            } else if (kind == Kind::Sheets) {
                Workbook document;
                run(document);
            } else {
                Presentation document;
                run(document);
            }
            return 0;
        }

        if (!getenv("FLTK_BACKEND"))
            setenv("FLTK_BACKEND", "wayland", 0);
        dx::init();
        Fl_RGB_Image::max_size(64 * 1024 * 1024);
        Fl::lock();
        Office office(kind, options);
        if (options.present)
            office.command(Present);
        if (kind == Kind::Words)
            office.command(options.pages ? PageLayout : TextView);
        if (!options.capture.empty())
            Fl::add_timeout(options.capture_delay / 1000., [](void* p) { static_cast<Office*>(p)->capture(); }, &office);
        if (options.duration)
            Fl::add_timeout(options.duration / 1000., [](void* p) { static_cast<Office*>(p)->command(Quit); }, &office);
        return Fl::run();
    } catch (const std::exception& e) {
        std::cerr << "Calligra FLTK: " << e.what() << '\n';
        return 1;
    }
}
