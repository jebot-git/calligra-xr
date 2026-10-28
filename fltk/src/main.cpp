// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_limits.h"
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
    std::string file, report, capture;
    int duration = 0;
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
    PageLayout
};
class Grid : public Fl_Table {
public:
    std::function<std::string(int, int)> text;
    std::function<void(int, int)> select;
    std::function<void(const std::string&)> paste;
    std::function<void()> edit;
    int selected_row = 0, selected_col = 0;
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
            = header ? (context == CONTEXT_ROW_HEADER ? std::to_string(row + 1) : column_name(col)) : text(row, col);
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
    uint64_t text_font_revision = 0;
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
    Options options;
    std::vector<Fl_Widget*> presentation_hidden;
    int canvas_x = 0, canvas_y = 0, canvas_w = 0, canvas_h = 0;
    std::vector<std::string> history;
    size_t history_index = 0;
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
            menu.add(
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
        add("&File/&Quit", FL_CTRL + 'q', Quit);
        add("&Edit/&Undo", FL_CTRL + 'z', Undo);
        add("&Edit/&Redo", FL_CTRL + FL_SHIFT + 'z', Redo);
        add("&Edit/Cu&t", FL_CTRL + 'x', Cut);
        add("&Edit/&Copy", FL_CTRL + 'c', Copy);
        add("&Edit/&Paste", FL_CTRL + 'v', Paste);
        add("&Edit/Select &all", FL_CTRL + 'a', SelectAll);
        if (kind == Kind::Words)
            add("&View/&Page layout", 0, PageLayout);
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
            grid->select = [this](int r, int c) { guard([&] { select_cell(r, c); }); };
            grid->edit = [this] {
                formula->take_focus();
                formula->insert_position(0, formula->size());
            };
            grid->paste = [this](const std::string& text) { guard([&] { paste_cells(qs(text.c_str())); }); };
            content->resizable(grid);
        } else if (kind == Kind::Words) {
            editor = new Fl_Text_Editor(28, 66, 1044, 668);
            editor->buffer(&buffer);
            editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
            text_pages = new TextPageCanvas(28, 66, 1044, 668);
            text_pages->hide();
            text_pages->edit_paragraph = [this](size_t paragraph) {
                guard([&] {
                    auto flow = words->flow();
                    size_t offset = 0;
                    for (size_t i = 0; i < paragraph; ++i)
                        offset += flow.paragraphs.at(i).text.toUtf8().size() + 1;
                    command(PageLayout);
                    editor->insert_position(int(offset));
                    editor->show_insert_position();
                    editor->take_focus();
                });
            };
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
            canvas->image = [this](const Text& path) { return stage->image(path); };
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
            editor->user_data(this);
            auto undo_key = [](int key, Fl_Text_Editor* widget) -> int {
                auto* office = static_cast<Office*>(widget->user_data());
                office->command(key == 'y' || (Fl::event_state() & FL_SHIFT) ? Redo : Undo);
                return 1;
            };
            editor->add_key_binding('z', FL_CTRL, undo_key);
            editor->add_key_binding('z', FL_CTRL | FL_SHIFT, undo_key);
            editor->add_key_binding('y', FL_CTRL, undo_key);
            editor->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
            content->resizable(canvas);
            auto* present = new Fl_Button(568, 10, 130, 32, "Present");
            present->callback([](Fl_Widget*, void* p) { static_cast<Office*>(p)->command(Present); }, this);
        }
        content->end();
        if (kind == Kind::Words) {
            page_button = new Fl_Button(560, 10, 100, 32, "Pages");
            page_button->callback([](Fl_Widget*, void* p) { static_cast<Office*>(p)->command(PageLayout); }, this);
        }
        status = new Fl_Box(12, 750, 1076, 24);
        status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        window.end();
        window.resizable(content);
        window.size_range(780, 560);
        window.xclass(kind == Kind::Words ? "calligra-words-fltk"
                : kind == Kind::Sheets    ? "calligra-sheets-fltk"
                                          : "calligra-stage-fltk");
        buffer.add_modify_callback(
            [](int, int inserted, int deleted, int, const char*, void* p) {
                auto* a = static_cast<Office*>(p);
                if (!a->updating && (inserted || deleted)) {
                    a->modified = true;
                    if (a->kind != Kind::Stage) a->remember_text();
                    a->title();
                    if (a->kind == Kind::Stage) {
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
        desktop_menu.reset();
        if (editor)
            editor->buffer(nullptr);
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
        history.clear();
        if (kind != Kind::Stage) history.push_back(buffer_text());
        history_index = 0;
        if (editor) {
            if (readonly())
                editor->deactivate();
            else
                editor->activate();
        }
    }
    void remember_text()
    {
        auto text = buffer_text();
        if (!history.empty() && history[history_index] == text)
            return;
        history.resize(history_index + 1);
        history.push_back(text);
        size_t bytes = 0;
        for (const auto& state : history)
            bytes += state.size();
        while (history.size() > 1 && (history.size() > 100 || bytes > 32 * 1024 * 1024)) {
            bytes -= history.front().size();
            history.erase(history.begin());
        }
        history_index = history.size() - 1;
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
            words = std::make_unique<TextDocument>();
            fill_buffer(words->text());
            if (page_view)
                refresh_pages();
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
        if (!editor)
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
        commit_text();
        text_pages->set_flow(words->flow());
        text_font_revision = dx::font_revision;
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
            words = std::move(fresh);
            fill_buffer(words->text());
            if (page_view)
                refresh_pages();
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
        guard([&] {
            if (canvas && canvas->dragging()) canvas->finish_drag();
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
            } else if (c == PageLayout && kind == Kind::Words) {
                if (!page_view)
                    refresh_pages();
                page_view = !page_view;
                if (page_view) {
                    editor->hide();
                    text_pages->show();
                    text_pages->take_focus();
                    content->resizable(text_pages);
                } else {
                    text_pages->hide();
                    editor->show();
                    editor->take_focus();
                    content->resizable(editor);
                }
                page_button->label(page_view ? "Edit" : "Pages");
                window.redraw();
            } else if (c == Save || c == SaveAs)
                save(c == SaveAs);
            else if (c == Quit) {
                if (discard())
                    window.hide();
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
                    if (!position) return;
                    slide = position->slide;
                    box = position->box;
                    refresh_stage(position->shape);
                    editor->insert_position(buffer.length());
                    editor->show_insert_position();
                } else {
                    if (c == Undo && history_index)
                        --history_index;
                    else if (c == Redo && history_index + 1 < history.size())
                        ++history_index;
                    else
                        return;
                    updating = true;
                    buffer.text(history[history_index].c_str());
                    updating = false;
                    if (page_view)
                        refresh_pages();
                }
                modified = kind == Kind::Stage ? stage->modified() : true;
                title();
            } else if (c == Copy) {
                if (editor)
                    Fl_Text_Editor::kf_copy(0, editor);
                else {
                    auto text = utf8(workbook->display(sheet, grid->selected_col + 1, grid->selected_row + 1));
                    Fl::copy(text.data(), text.size(), 1);
                }
            } else if (c == Paste) {
                if (editor && editor->active_r())
                    Fl_Text_Editor::kf_paste(0, editor);
                else if (!editor)
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
                if (editor)
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
                    window.fullscreen();
                } else {
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
        if (kind == Kind::Sheets)
            data["cell"] = { { "sheet", sheet }, { "row", grid->selected_row + 1 },
                { "column", grid->selected_col + 1 }, { "input", formula->value() },
                { "value", utf8(workbook->display(sheet, grid->selected_col + 1, grid->selected_row + 1)) } };
        else
            data["text"] = buffer_text();
        if (kind == Kind::Words) {
            size_t shaped_lines = 0, glyphs = 0, missing = 0;
            for (const auto& page : text_pages->pagination.pages)
                for (const auto& line : page.lines)
                    if (line.shaped) {
                        ++shaped_lines;
                        glyphs += line.shaped->glyphs.size();
                        missing += line.shaped->missing_glyphs;
                    }
            data["page_layout"]
                = { { "visible", page_view }, { "pages", text_pages->pagination.pages.size() },
                      { "page", text_pages->current_page() + 1 },
                      { "render_error", text_pages->render_error() },
                      { "rasterizations", text_pages->rasterizations() },
                      { "shaped_lines", shaped_lines }, { "glyphs", glyphs },
                      { "missing_glyphs", missing } };
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
            Json adjustments = Json::array();
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
            data["scene"] = { { "page", slide + 1 }, { "width", model.width },
                { "height", model.height }, { "shapes", model.shapes.size() },
                { "master_shapes", masters }, { "editable_frames", model.editable_sources.size() },
                { "selected_frame", box }, { "can_undo", stage->can_undo() }, { "can_redo", stage->can_redo() },
                { "selected_shape", canvas->selected_shape }, { "adjustments", adjustments },
                { "revision", stage->revision() }, { "slides", stage->count() },
                { "rendered_shapes", canvas->rendered_shapes() },
                { "geometry_points", canvas->geometry_points() },
                { "render_error", canvas->render_error() }, { "unsupported", unsupported },
                { "shaped_lines", text.lines }, { "glyphs", text.glyphs },
                { "missing_glyphs", text.missing_glyphs }, { "overflow_frames", text.overflow_frames },
                { "viewport", {canvas->x(), canvas->y(), canvas->w(), canvas->h()} } };
        }
        std::ofstream(options.report) << data.dump(2) << '\n';
    }
    void capture()
    {
        if (options.capture.empty())
            return;
        window.make_current();
        window.draw();
        glFinish();
        int w = window.pixel_w(), h = window.pixel_h();
        std::vector<unsigned char> pixels(size_t(w) * h * 3);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream out(options.capture, std::ios::binary);
        out << "P6\n" << w << ' ' << h << "\n255\n";
        for (int y = h - 1; y >= 0; --y)
            out.write(reinterpret_cast<char*>(pixels.data() + size_t(y) * w * 3), w * 3);
        Json metadata = { { "renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER)) },
            { "gpu_widgets", true }, { "universal_menu", desktop_menu->attached() } };
#ifdef FLTK_USE_WAYLAND
        metadata["wayland"] = bool(fl_wl_display());
#endif
        std::ofstream(options.capture + ".json") << metadata.dump(2) << '\n';
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
            else if (arg == "--quit-after-ms")
                options.duration = std::stoi(value());
            else if (arg == "--capture")
                options.capture = value();
            else if (arg == "--report")
                options.report = value();
            else if (arg == "--help") {
                std::cout << "Calligra FLTK [FILE] [--report JSON] [--capture PPM] "
                             "[--quit-after-ms N] [--page-layout (Words)]\n";
                return 0;
            } else if (arg.starts_with("--"))
                throw std::runtime_error("Unknown argument " + arg);
            else
                options.file = arg;
        }

        if (!getenv("FLTK_BACKEND"))
            setenv("FLTK_BACKEND", "wayland", 0);
        dx::init();
        Fl_RGB_Image::max_size(64 * 1024 * 1024);
        Fl::lock();
        Office office(kind, options);
        if (options.present)
            office.command(Present);
        if (options.pages)
            office.command(PageLayout);
        if (!options.capture.empty())
            Fl::add_timeout(1.5, [](void* p) { static_cast<Office*>(p)->capture(); }, &office);
        if (options.duration)
            Fl::add_timeout(options.duration / 1000., [](void* p) { static_cast<Office*>(p)->command(Quit); }, &office);
        return Fl::run();
    } catch (const std::exception& e) {
        std::cerr << "Calligra FLTK: " << e.what() << '\n';
        return 1;
    }
}
