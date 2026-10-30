// SPDX-License-Identifier: GPL-2.0-or-later
#include "format_dialog.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Hold_Browser.H>
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
    done = accepted = false;
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
void boolean(Fl_Choice &choice, const std::optional<bool> &initial,
             bool compact = false) {
  choice.add(compact ? "Unchanged|No|Yes" : "Mixed or unchanged|No|Yes");
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
  int width = 560, height = 390, x = std::max(0, (window.w() - width) / 2),
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
  Fl_Choice underline(x + 430, y + 130, 110, 30, "Underline:");
  Fl_Choice strike(x + 430, y + 170, 110, 30, "Strike:");
  Fl_Input color(x + 155, y + 210, 180, 30, "Color:");
  color.tooltip("#RRGGBB or auto");
  Fl_Input highlight(x + 155, y + 250, 180, 30, "Highlight:");
  highlight.tooltip("#RRGGBB or transparent to clear highlighting");
  if (initial.highlight)
    highlight.value(initial.highlight->toUtf8().c_str());
  if (initial.family)
    family.value(initial.family->toUtf8().c_str());
  number(size, initial.size);
  boolean(bold, initial_bold);
  boolean(italic, initial.italic);
  boolean(underline, initial.underline, true);
  boolean(strike, initial.strike, true);
  if (initial.color)
    color.value(initial.color->toUtf8().c_str());
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  while (panel.run(family)) {
    try {
      WordsCharacterFormat result;
      auto name = Text(family.value()).trimmed(),
           ink = Text(color.value()).trimmed();
      if (!name.empty() && (!initial.family || name != *initial.family))
        result.family = name;
      if (!ink.empty() && (!initial.color || ink != *initial.color))
        result.color = ink;
      auto background = Text(highlight.value()).trimmed();
      if (!background.empty() &&
          (!initial.highlight || background != *initial.highlight))
        result.highlight = background;
      changed_number(size, initial.size, result.size);
      if (bold.value() &&
          (!initial_bold || *initial_bold != (bold.value() == 2)))
        result.weight = bold.value() == 2 ? 700 : 400;
      changed_boolean(italic, initial.italic, result.italic);
      changed_boolean(underline, initial.underline, result.underline);
      changed_boolean(strike, initial.strike, result.strike);
      validate_words_format(result);
      return result;
    } catch (const std::exception &e) {
      background.hint.copy_label(e.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
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
  while (panel.run(align)) {
    try {
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
    } catch (const std::exception &e) {
      background.hint.copy_label(e.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
}
namespace {
class ActionButton : public Fl_Button {
  Panel &panel;
  int &action, value;

public:
  ActionButton(Panel &p, int &a, int v, int x, int y, int w, const char *label)
      : Fl_Button(x, y, w, 30, label), panel(p), action(a), value(v) {
    callback([](Fl_Widget *w, void *) {
      auto &button = *static_cast<ActionButton *>(w);
      button.action = button.value;
      button.panel.accepted = button.panel.done = true;
    });
  }
};
OdfPackage style_copy(const OdfPackage &package) {
  auto result = package;
  result.xml = read_document_xml(package.xml.toByteArray());
  return result;
}
struct StyleChoice {
  std::vector<Text> values;
  std::vector<std::string> labels;
  std::vector<Fl_Menu_Item> menu;
  void add(Text value, const Text &label) {
    values.push_back(value);
    auto raw = label.toUtf8().toStdString();
    std::string escaped;
    for (char c : raw) {
      escaped += c;
      if (c == '&' || c == '@')
        escaped += c;
    }
    labels.push_back(std::move(escaped));
  }
  int find(const Text &value) const {
    for (size_t i = 0; i < values.size(); ++i)
      if (values[i] == value)
        return int(i);
    return -1;
  }
  void attach(Fl_Choice &choice, const Text &value) {
    menu.resize(values.size() + 1);
    for (size_t i = 0; i < values.size(); ++i)
      menu[i].text = labels[i].c_str();
    choice.menu(menu.data());
    choice.value(std::max(0, find(value)));
  }
  Text selected(const Fl_Choice &choice) const {
    return values.at(size_t(choice.value()));
  }
};
Text style_label(const std::vector<WordsParagraphStyle> &styles,
                 const WordsParagraphStyle &style) {
  size_t matches = 0;
  for (const auto &other : styles)
    matches += other.label == style.label;
  return matches > 1 ? style.label + " (" + style.name + ")" : style.label;
}
std::optional<WordsParagraphStyleChange>
edit_paragraph_style(Fl_Window &window, const OdfPackage &working,
                     const Text &name, const Text &base) {
  auto styles = words_paragraph_styles(working);
  WordsParagraphStyle initial;
  initial.label = "New style";
  initial.parent = base;
  if (!name.empty()) {
    auto found =
        std::find_if(styles.begin(), styles.end(),
                     [&](const auto &style) { return style.name == name; });
    if (found == styles.end())
      throw std::runtime_error("The paragraph style no longer exists");
    initial = *found;
  }
  StyleChoice parents, nexts, outlines;
  parents.add({}, "(Default properties)");
  nexts.add({}, "(Inherit next style)");
  nexts.add(":self", "Same style");
  outlines.add("inherit", "Inherit outline level");
  outlines.add({}, "Body text");
  for (int level = 1; level <= 10; ++level)
    outlines.add(Text::number(level), "Heading " + Text::number(level));
  for (const auto &style : styles) {
    if (style.name != name)
      parents.add(style.name, style_label(styles, style));
    if (style.name != name)
      nexts.add(style.name, style_label(styles, style));
  }
  if (!initial.parent.empty() && parents.find(initial.parent) < 0)
    parents.add(initial.parent, "Current parent: " + initial.parent);
  auto initial_next =
      initial.next == name && !name.empty() ? Text(":self") : initial.next;
  if (!initial_next.empty() && nexts.find(initial_next) < 0)
    nexts.add(initial_next, "Current next style: " + initial_next);
  auto initial_outline = initial.outline.value_or("inherit");
  if (outlines.find(initial_outline) < 0)
    outlines.add(initial_outline, "Current outline: " + initial_outline);
  const int width = 650, height = 460,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height,
                        name.empty() ? "New paragraph style"
                                     : "Edit paragraph style");
  background.hint.copy_label(
      "Save the definition, then Apply in Paragraph styles");
  Fl_Input label(x + 165, y + 50, 450, 30, "Name:");
  label.value(initial.label.toUtf8().c_str());
  Fl_Choice parent(x + 165, y + 90, 450, 30, "Based on:");
  parents.attach(parent, initial.parent);
  Fl_Choice next(x + 165, y + 130, 450, 30, "Next paragraph:");
  nexts.attach(next, initial_next);
  Fl_Choice outline(x + 165, y + 170, 450, 30, "Outline:");
  outlines.attach(outline, initial_outline);
  int action = 0;
  ActionButton text(panel, action, 1, x + 165, y + 220, 180,
                    "Text properties…");
  ActionButton paragraph(panel, action, 2, x + 355, y + 220, 180,
                         "Paragraph properties…");
  Fl_Box summary(x + 20, y + 270, width - 40, 40,
                 "Property fields inherit until explicitly changed");
  summary.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  buttons.apply.label(name.empty() ? "Create" : "Save style");
  Fl_Group::current(previous);
  WordsCharacterFormat character;
  WordsParagraphFormat paragraph_format;
  auto collect = [&] {
    WordsParagraphStyleChange change;
    change.name = name;
    const auto display = Text(label.value()).trimmed();
    if (name.empty() || display != initial.label)
      change.label = display;
    if (name.empty() || parents.selected(parent) != initial.parent)
      change.parent = parents.selected(parent);
    if (nexts.selected(next) != initial_next)
      change.next = nexts.selected(next);
    if (outlines.selected(outline) != initial_outline)
      change.outline = outlines.selected(outline);
    change.character = character;
    change.paragraph = paragraph_format;
    return change;
  };
  while (true) {
    action = 0;
    if (!panel.run(label))
      return {};
    try {
      auto change = collect();
      auto preview = style_copy(working);
      auto id = define_words_paragraph_style(preview, change);
      auto info = words_paragraph_style_format(preview, id);
      if (!action)
        return change;
      if (action == 1) {
        if (auto format =
                character_format_dialog(window, info.character, info.bold))
          character.overlay(*format);
      } else if (auto format = paragraph_format_dialog(window, info.paragraph))
        paragraph_format.overlay(*format);
      if (!character.empty() || !paragraph_format.empty())
        summary.label("Text and paragraph property changes are staged");
      summary.redraw();
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
}
} // namespace
std::optional<WordsStylesDialogResult>
paragraph_styles_dialog(Fl_Window &window, OdfPackage working,
                        std::optional<Text> selected) {
  const int width = 680, height = 480,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Paragraph styles");
  background.hint.resize(x + 20, y + height - 180, width - 40, 28);
  background.hint.copy_label("Choose a style, or create and edit definitions");
  Fl_Hold_Browser browser(x + 20, y + 50, width - 40, height - 240);
  browser.format_char(0);
  browser.textfont(dx::font);
  browser.textsize(dx::size);
  Fl_Box description(x + 20, y + height - 142, width - 40, 36);
  description.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int action = 0;
  ActionButton create(panel, action, 1, x + 20, y + height - 105, 90, "New…");
  ActionButton edit(panel, action, 2, x + 120, y + height - 105, 90, "Edit…");
  ActionButton remove(panel, action, 3, x + 220, y + height - 105, 130,
                      "Delete unused");
  Fl_Check_Button apply(x + 20, y + height - 65, 280, 30,
                        "Apply to selected paragraphs");
  apply.value(1);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  std::vector<WordsParagraphStyle> styles;
  std::vector<Text> names;
  std::function<void()> describe = [&] {
    auto index = browser.value();
    if (index <= 1) {
      edit.deactivate();
      remove.deactivate();
      description.copy_label(
          index == 1
              ? "Default paragraph properties; character formatting is retained"
              : "Choose a paragraph style");
    } else {
      edit.activate();
      remove.activate();
      const auto &style = styles.at(size_t(index - 2));
      auto outline =
          style.outline ? (style.outline->empty() ? Text("Body text")
                                                  : "Heading " + *style.outline)
                        : Text("Inherited");
      auto display = [&](const Text &name, const Text &empty) {
        if (name.empty())
          return empty;
        auto found =
            std::find_if(styles.begin(), styles.end(),
                         [&](const auto &item) { return item.name == name; });
        return found == styles.end() ? "Missing: " + name
                                     : style_label(styles, *found);
      };
      auto caption =
          "Parent: " + display(style.parent, "Default") + "   Next: " +
          (style.next == style.name ? Text("Same style")
                                    : display(style.next, "Inherited")) +
          "   Outline: " + outline;
      description.copy_label(caption.toUtf8().c_str());
    }
    description.redraw();
  };
  browser.callback(
      [](Fl_Widget *, void *data) {
        (*static_cast<std::function<void()> *>(data))();
      },
      &describe);
  auto refresh = [&](std::optional<Text> choice) {
    styles = words_paragraph_styles(working);
    names.clear();
    browser.clear();
    names.push_back({});
    browser.add("(Default paragraph)");
    for (const auto &style : styles) {
      names.push_back(style.name);
      browser.add(style_label(styles, style).toUtf8().c_str());
    }
    browser.value(0);
    if (choice)
      for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == *choice)
          browser.value(int(i + 1));
    describe();
  };
  refresh(selected);
  while (true) {
    action = 0;
    if (!panel.run(browser))
      return {};
    try {
      std::optional<Text> chosen;
      if (browser.value() > 0)
        chosen = names.at(size_t(browser.value() - 1));
      if (!action) {
        if (apply.value() && !chosen)
          throw std::runtime_error("Choose a paragraph style");
        // Validate on a temporary document before allowing the outer
        // transaction.
        if (apply.value())
          (void)words_paragraph_style_format(working, *chosen);
        return WordsStylesDialogResult{std::move(working),
                                       apply.value() ? chosen : std::nullopt};
      }
      if (action == 1 || action == 2) {
        if (action == 2 && (!chosen || chosen->empty()))
          throw std::runtime_error("Choose a named paragraph style to edit");
        auto change = edit_paragraph_style(window, working,
                                           action == 2 ? *chosen : Text{},
                                           chosen.value_or(Text{}));
        if (change) {
          auto prospective = style_copy(working);
          auto id = define_words_paragraph_style(prospective, *change);
          (void)words_paragraph_style_format(prospective, id);
          working = std::move(prospective);
          refresh(id);
        }
      } else {
        if (!chosen || chosen->empty())
          throw std::runtime_error("Choose a named paragraph style to delete");
        auto prospective = style_copy(working);
        delete_words_paragraph_style(prospective, *chosen);
        working = std::move(prospective);
        refresh(Text{});
      }
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
}
} // namespace DomeOffice
