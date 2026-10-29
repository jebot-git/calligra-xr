// SPDX-License-Identifier: GPL-2.0-or-later
#include "format_dialog.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Return_Button.H>
namespace DomeOffice {
namespace {
class Panel : public Fl_Group {
  Fl_Window &window;
  Fl_Group *previous_group;
  Fl_Widget *previous_focus;

public:
  bool done = false, accepted = false;
  Panel(Fl_Window &w, Fl_Group *previous)
      : Fl_Group(0, 0, w.w(), w.h()), window(w), previous_group(previous),
        previous_focus(Fl::focus()) {
    w.add(this);
    begin();
  }
  ~Panel() {
    hide();
    window.redraw();
    Fl_Group::current(previous_group);
    if (previous_focus)
      previous_focus->take_focus();
  }
  int handle(int event) override {
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
      done = true;
      return 1;
    }
    if (Fl_Group::handle(event))
      return 1;
    return event == FL_PUSH || event == FL_DRAG || event == FL_RELEASE ||
           event == FL_MOUSEWHEEL || event == FL_KEYDOWN ||
           event == FL_SHORTCUT;
  }
  bool run(Fl_Widget &focus) {
    end();
    window.end();
    Fl_Group::current(previous_group);
    for (int i = 0; i < children(); ++i) {
      child(i)->labelfont(dx::font);
      child(i)->labelsize(dx::size);
      if (auto *input = dynamic_cast<Fl_Input *>(child(i))) {
        input->textfont(dx::font);
        input->textsize(dx::size);
      }
      if (auto *choice = dynamic_cast<Fl_Choice *>(child(i))) {
        choice->textfont(dx::font);
        choice->textsize(dx::size);
      }
    }
    show();
    focus.take_focus();
    window.redraw();
    while (!done && window.shown())
      Fl::wait();
    return accepted && window.shown();
  }
};
struct Background {
  Fl_Box backdrop, border, title, hint;
  Background(Fl_Window &w, int x, int y, int width, int height,
             const char *label)
      : backdrop(0, 0, w.w(), w.h()), border(x, y, width, height),
        title(x + 18, y + 10, width - 36, 28, label),
        hint(x + 18, y + height - 92, width - 36, 28,
             "Blank or mixed fields stay unchanged") {
    backdrop.box(FL_FLAT_BOX);
    backdrop.color(FL_BACKGROUND_COLOR);
    border.box(FL_BORDER_BOX);
    border.color(FL_BACKGROUND_COLOR);
    title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }
};
struct Buttons {
  Fl_Button cancel;
  Fl_Return_Button apply;
  Buttons(Panel &panel, int x, int y)
      : cancel(x, y, 90, 30, "Cancel"), apply(x + 105, y, 90, 30, "Apply") {
    cancel.callback(
        [](Fl_Widget *, void *p) { static_cast<Panel *>(p)->done = true; },
        &panel);
    apply.callback(
        [](Fl_Widget *, void *p) {
          auto *panel = static_cast<Panel *>(p);
          panel->accepted = panel->done = true;
        },
        &panel);
  }
};
template <class T> void number(Fl_Input &input, const std::optional<T> &value) {
  if (value)
    input.value(Text::number(*value).toUtf8().c_str());
}
void changed_number(Fl_Input &input, const std::optional<double> &initial,
                    std::optional<double> &output) {
  auto value = Text(input.value()).trimmed();
  if (value.empty() || (initial && value == Text::number(*initial)))
    return;
  bool valid = false;
  double parsed = value.toDouble(&valid);
  if (!valid || !std::isfinite(parsed))
    throw std::runtime_error("Enter a finite number, or leave the field blank");
  if (!initial || parsed != *initial)
    output = parsed;
}
void changed_integer(Fl_Input &input, const std::optional<int> &initial,
                     std::optional<int> &output) {
  auto value = Text(input.value()).trimmed();
  if (value.empty())
    return;
  bool valid = false;
  int parsed = value.toInt(&valid);
  if (!valid)
    throw std::runtime_error("Enter a whole number, or leave the field blank");
  if (!initial || parsed != *initial)
    output = parsed;
}
void boolean(Fl_Choice &choice, const std::optional<bool> &initial) {
  choice.add("Mixed or unchanged|No|Yes");
  choice.value(initial ? (*initial ? 2 : 1) : 0);
}
void changed_boolean(Fl_Choice &choice, const std::optional<bool> &initial,
                     std::optional<bool> &output) {
  if (choice.value() && (!initial || *initial != (choice.value() == 2)))
    output = choice.value() == 2;
}
} // namespace
std::optional<WordsCharacterFormat>
character_format_dialog(Fl_Window &window, const WordsCharacterFormat &initial,
                        std::optional<bool> initial_bold) {
  int width = 560, height = 350, x = std::max(0, (window.w() - width) / 2),
      y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Character format");
  Fl_Input family(x + 155, y + 50, 375, 30, "Font family:");
  Fl_Input size(x + 155, y + 90, 100, 30, "Size (pt):");
  Fl_Choice bold(x + 155, y + 130, 180, 30, "Bold:");
  Fl_Choice italic(x + 155, y + 170, 180, 30, "Italic:");
  Fl_Input color(x + 155, y + 210, 180, 30, "Color:");
  color.tooltip("#RRGGBB or auto");
  if (initial.family)
    family.value(initial.family->toUtf8().c_str());
  number(size, initial.size);
  boolean(bold, initial_bold);
  boolean(italic, initial.italic);
  if (initial.color)
    color.value(initial.color->toUtf8().c_str());
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  if (!panel.run(family))
    return {};
  WordsCharacterFormat result;
  auto name = Text(family.value()).trimmed(),
       ink = Text(color.value()).trimmed();
  if (!name.empty() && (!initial.family || name != *initial.family))
    result.family = name;
  if (!ink.empty() && (!initial.color || ink != *initial.color))
    result.color = ink;
  changed_number(size, initial.size, result.size);
  if (bold.value() && (!initial_bold || *initial_bold != (bold.value() == 2)))
    result.weight = bold.value() == 2 ? 700 : 400;
  changed_boolean(italic, initial.italic, result.italic);
  validate_words_format(result);
  return result;
}
std::optional<WordsParagraphFormat>
paragraph_format_dialog(Fl_Window &window,
                        const WordsParagraphFormat &initial) {
  int width = 700, height = 470, x = std::max(0, (window.w() - width) / 2),
      y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height,
                        "Paragraph format — spacing in points");
  Fl_Choice align(x + 145, y + 50, 180, 30, "Alignment:");
  align.add("Mixed or unchanged|Left|Center|Right|Justify|Start|End");
  const TextList names{"",        "left",  "center", "right",
                       "justify", "start", "end"};
  if (initial.alignment)
    for (size_t i = 1; i < names.size(); ++i)
      if (*initial.alignment == names[i])
        align.value(i);
  Fl_Input left(x + 145, y + 95, 95, 30, "Left margin:");
  number(left, initial.left);
  Fl_Input right(x + 535, y + 95, 95, 30, "Right margin:");
  number(right, initial.right);
  Fl_Input indent(x + 145, y + 140, 95, 30, "First indent:");
  number(indent, initial.indent);
  Fl_Input lines(x + 535, y + 140, 95, 30, "Line multiplier:");
  number(lines, initial.line_factor);
  Fl_Input before(x + 145, y + 185, 95, 30, "Space before:");
  number(before, initial.before);
  Fl_Input after(x + 535, y + 185, 95, 30, "Space after:");
  number(after, initial.after);
  Fl_Input orphans(x + 145, y + 230, 95, 30, "Orphan lines:");
  number(orphans, initial.orphans);
  Fl_Input widows(x + 535, y + 230, 95, 30, "Widow lines:");
  number(widows, initial.widows);
  Fl_Choice keep(x + 145, y + 275, 180, 30, "Keep together:");
  boolean(keep, initial.keep_together);
  Fl_Choice next(x + 535, y + 275, 145, 30, "Keep with next:");
  boolean(next, initial.keep_next);
  Fl_Choice page(x + 145, y + 320, 180, 30, "Page before:");
  boolean(page, initial.break_before);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  if (!panel.run(align))
    return {};
  WordsParagraphFormat result;
  if (align.value() &&
      (!initial.alignment || *initial.alignment != names.at(align.value())))
    result.alignment = names.at(align.value());
  changed_number(left, initial.left, result.left);
  changed_number(right, initial.right, result.right);
  changed_number(indent, initial.indent, result.indent);
  changed_number(lines, initial.line_factor, result.line_factor);
  changed_number(before, initial.before, result.before);
  changed_number(after, initial.after, result.after);
  changed_integer(orphans, initial.orphans, result.orphans);
  changed_integer(widows, initial.widows, result.widows);
  changed_boolean(keep, initial.keep_together, result.keep_together);
  changed_boolean(next, initial.keep_next, result.keep_next);
  changed_boolean(page, initial.break_before, result.break_before);
  validate_words_format(result);
  return result;
}
} // namespace DomeOffice
