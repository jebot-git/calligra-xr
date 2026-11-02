// SPDX-License-Identifier: GPL-2.0-or-later
#include "format_dialog.h"
#include "native_text_shaper.h"
#include "native_words_geometry.h"
#include "suite/fltk_appearance.h"
#include <charconv>
#include <numeric>
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
std::vector<WordsParagraphStyle> dialog_styles(const OdfPackage &package,
                                               bool text_styles) {
  if (!text_styles)
    return words_paragraph_styles(package);
  std::vector<WordsParagraphStyle> result;
  for (const auto &style : words_character_styles(package))
    result.push_back(
        {style.name, style.label, style.parent, {}, {}, style.hidden});
  return result;
}
Text define_dialog_style(OdfPackage &package,
                         const WordsParagraphStyleChange &change,
                         bool text_styles) {
  if (!text_styles)
    return define_words_paragraph_style(package, change);
  return define_words_character_style(
      package, {change.name, change.label, change.parent, change.character});
}
WordsFormatInfo dialog_style_format(const OdfPackage &package, const Text &name,
                                    bool text_styles) {
  return text_styles ? words_character_style_format(package, name)
                     : words_paragraph_style_format(package, name);
}
std::optional<WordsParagraphStyleChange>
edit_named_style(Fl_Window &window, const OdfPackage &working, const Text &name,
                 const Text &base, bool text_styles) {
  auto styles = dialog_styles(working, text_styles);
  WordsParagraphStyle initial;
  initial.label = "New style";
  initial.parent = base;
  if (!name.empty()) {
    auto found =
        std::find_if(styles.begin(), styles.end(),
                     [&](const auto &style) { return style.name == name; });
    if (found == styles.end())
      throw std::runtime_error("The named style no longer exists");
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
  const int width = 650, height = text_styles ? 340 : 460,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(
      window, x, y, width, height,
      text_styles
          ? (name.empty() ? "New character style" : "Edit character style")
          : (name.empty() ? "New paragraph style" : "Edit paragraph style"));
  background.hint.copy_label(
      "Save the definition, then Apply in the style chooser");
  Fl_Input label(x + 165, y + 50, 450, 30, "Name:");
  label.value(initial.label.toUtf8().c_str());
  Fl_Choice parent(x + 165, y + 90, 450, 30, "Based on:");
  parents.attach(parent, initial.parent);
  Fl_Choice next(x + 165, y + 130, 450, 30, "Next paragraph:");
  nexts.attach(next, initial_next);
  Fl_Choice outline(x + 165, y + 170, 450, 30, "Outline:");
  outlines.attach(outline, initial_outline);
  if (text_styles) {
    next.hide();
    outline.hide();
  }
  int action = 0;
  ActionButton text(panel, action, 1, x + 165, y + (text_styles ? 140 : 220),
                    180, "Text properties…");
  ActionButton paragraph(panel, action, 2, x + 355, y + 220, 180,
                         "Paragraph properties…");
  if (text_styles)
    paragraph.hide();
  Fl_Box summary(x + 20, y + (text_styles ? 190 : 270), width - 40, 40,
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
    if (!text_styles && nexts.selected(next) != initial_next)
      change.next = nexts.selected(next);
    if (!text_styles && outlines.selected(outline) != initial_outline)
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
      auto id = define_dialog_style(preview, change, text_styles);
      auto info = dialog_style_format(preview, id, text_styles);
      if (!action)
        return change;
      if (action == 1) {
        if (auto format =
                character_format_dialog(window, info.character, info.bold))
          character.overlay(*format);
      } else if (auto format = paragraph_format_dialog(window, info.paragraph))
        paragraph_format.overlay(*format);
      if (!character.empty() || !paragraph_format.empty())
        summary.label(text_styles
                          ? "Text property changes are staged"
                          : "Text and paragraph property changes are staged");
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
styles_dialog(Fl_Window &window, OdfPackage working,
              std::optional<Text> selected, bool text_styles) {
  const int width = 680, height = 480,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height,
                        text_styles ? "Character styles" : "Paragraph styles");
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
  Fl_Check_Button apply(x + 20, y + height - 65, text_styles ? 380 : 280, 30,
                        text_styles ? "Apply to selected text / future typing"
                                    : "Apply to selected paragraphs");
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
              ? (text_styles
                     ? "Default character properties from the paragraph/cell"
                     : "Default paragraph properties; character formatting is "
                       "retained")
              : "Choose a named style");
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
      if (text_styles)
        caption = "Parent: " + display(style.parent, "Default");
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
    styles = dialog_styles(working, text_styles);
    names.clear();
    browser.clear();
    names.push_back({});
    browser.add(text_styles ? "(Default character style)"
                            : "(Default paragraph)");
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
          throw std::runtime_error("Choose a named style");
        // Validate on a temporary document before allowing the outer
        // transaction.
        if (apply.value())
          (void)dialog_style_format(working, *chosen, text_styles);
        return WordsStylesDialogResult{std::move(working),
                                       apply.value() ? chosen : std::nullopt};
      }
      if (action == 1 || action == 2) {
        if (action == 2 && (!chosen || chosen->empty()))
          throw std::runtime_error("Choose a named style to edit");
        auto change =
            edit_named_style(window, working, action == 2 ? *chosen : Text{},
                             chosen.value_or(Text{}), text_styles);
        if (change) {
          auto prospective = style_copy(working);
          auto id = define_dialog_style(prospective, *change, text_styles);
          (void)dialog_style_format(prospective, id, text_styles);
          working = std::move(prospective);
          refresh(id);
        }
      } else {
        if (!chosen || chosen->empty())
          throw std::runtime_error("Choose a named style to delete");
        auto prospective = style_copy(working);
        if (text_styles)
          delete_words_character_style(prospective, *chosen);
        else
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
std::optional<WordsStylesDialogResult>
paragraph_styles_dialog(Fl_Window &window, OdfPackage working,
                        std::optional<Text> selected) {
  return styles_dialog(window, std::move(working), selected, false);
}
std::optional<WordsStylesDialogResult>
character_styles_dialog(Fl_Window &window, OdfPackage working,
                        std::optional<Text> selected) {
  return styles_dialog(window, std::move(working), selected, true);
}
std::optional<OdfPackage> page_setup_dialog(Fl_Window &window,
                                            OdfPackage working,
                                            const Text &page_style) {
  auto initial = words_page_style(working, page_style);
  constexpr double mm = 72 / 25.4;
  const int width = 620, height = 420,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Page setup");
  background.hint.copy_label("Blank fields keep their current value");
  auto caption = ("Page style: " + initial.label).toUtf8().toStdString();
  std::string escaped;
  for (auto c : caption) {
    escaped += c;
    if (c == '@')
      escaped += c;
  }
  Fl_Box label(x + 20, y + 45, width - 40, 25);
  label.copy_label(escaped.c_str());
  label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Fl_Choice paper(x + 155, y + 85, 180, 30, "Paper:");
  paper.add("Custom|A4|A5|Letter|Legal");
  paper.value(0);
  Fl_Choice orientation(x + 435, y + 85, 160, 30, "Orientation:");
  orientation.add("Portrait|Landscape");
  orientation.value(initial.geometry.width > initial.geometry.height ? 1 : 0);
  Fl_Input page_width(x + 155, y + 130, 120, 30, "Width (mm):"),
      page_height(x + 435, y + 130, 120, 30, "Height (mm):");
  Fl_Input left(x + 155, y + 180, 120, 30, "Left (mm):"),
      right(x + 435, y + 180, 120, 30, "Right (mm):");
  Fl_Input top(x + 155, y + 225, 120, 30, "Top (mm):"),
      bottom(x + 435, y + 225, 120, 30, "Bottom (mm):");
  std::array<Fl_Input *, 6> fields{&page_width, &page_height, &left,
                                   &right,      &top,         &bottom};
  std::array<double, 6> values{initial.geometry.width, initial.geometry.height,
                               initial.geometry.left,  initial.geometry.right,
                               initial.geometry.top,   initial.geometry.bottom};
  std::array<Text, 6> originals;
  auto display = [](double value) { return Text::number(value, 'g', 9); };
  for (size_t i = 0; i < fields.size(); ++i) {
    originals[i] = display(values[i] / mm);
    fields[i]->value(originals[i].toUtf8().c_str());
  }
  const std::array<std::pair<double, double>, 5> sizes{
      {{0, 0}, {210, 297}, {148, 210}, {215.9, 279.4}, {215.9, 355.6}}};
  for (size_t i = 1; i < sizes.size(); ++i) {
    auto [w, h] = sizes[i];
    if (orientation.value())
      std::swap(w, h);
    if (std::abs(values[0] / mm - w) < 1e-6 &&
        std::abs(values[1] / mm - h) < 1e-6)
      paper.value(int(i));
  }
  int action = 0;
  struct Queue {
    Panel &panel;
    int &action;
    int value;
  };
  Queue paper_action{panel, action, 1}, orientation_action{panel, action, 2};
  auto queue = [](Fl_Widget *, void *data) {
    auto &q = *static_cast<Queue *>(data);
    q.action = q.value;
    q.panel.done = q.panel.accepted = true;
  };
  paper.callback(queue, &paper_action);
  orientation.callback(queue, &orientation_action);
  int last_orientation = orientation.value();
  std::function<void()> clear_hint = [&] {
    background.hint.copy_label("Blank fields keep their current value");
    background.hint.labelcolor(FL_FOREGROUND_COLOR);
    background.hint.redraw();
  };
  std::function<void()> custom = [&] {
    clear_hint();
    paper.value(0);
    bool a = false, b = false;
    auto w = Text(page_width.value()).toDouble(&a),
         h = Text(page_height.value()).toDouble(&b);
    if (a && b && std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0) {
      orientation.value(w > h ? 1 : 0);
      last_orientation = orientation.value();
    }
  };
  auto custom_callback = [](Fl_Widget *, void *data) {
    (*static_cast<std::function<void()> *>(data))();
  };
  page_width.when(FL_WHEN_CHANGED);
  page_height.when(FL_WHEN_CHANGED);
  page_width.callback(custom_callback, &custom);
  page_height.callback(custom_callback, &custom);
  for (auto *field : {&left, &right, &top, &bottom}) {
    field->when(FL_WHEN_CHANGED);
    field->callback(custom_callback, &clear_hint);
  }
  Fl_Box scope(x + 20, y + 280, width - 40, 28,
               "Changes apply to every page using this style");
  scope.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  while (true) {
    action = 0;
    if (!panel.run(page_width))
      return {};
    try {
      if (action == 1) {
        if (paper.value() > 0) {
          auto [w, h] = sizes.at(size_t(paper.value()));
          if (orientation.value())
            std::swap(w, h);
          page_width.value(display(w).toUtf8().c_str());
          page_height.value(display(h).toUtf8().c_str());
        }
        continue;
      }
      if (action == 2) {
        bool a = false, b = false;
        auto w = Text(page_width.value()).toDouble(&a),
             h = Text(page_height.value()).toDouble(&b);
        if (!a || !b || !std::isfinite(w) || !std::isfinite(h) || w <= 0 ||
            h <= 0) {
          orientation.value(last_orientation);
          throw std::runtime_error(
              "Enter positive width and height before changing orientation");
        }
        const bool landscape = orientation.value() != 0;
        if ((landscape && w < h) || (!landscape && w > h))
          std::swap(w, h);
        page_width.value(display(w).toUtf8().c_str());
        page_height.value(display(h).toUtf8().c_str());
        last_orientation = orientation.value();
        continue;
      }
      WordsPageFormat format;
      std::array<std::optional<double> *, 6> patch{
          &format.width, &format.height, &format.left,
          &format.right, &format.top,    &format.bottom};
      for (size_t i = 0; i < fields.size(); ++i) {
        auto raw = Text(fields[i]->value()).trimmed();
        if (raw.empty() || raw == originals[i])
          continue;
        bool ok = false;
        auto value = raw.toDouble(&ok);
        if (!ok || !std::isfinite(value))
          throw std::runtime_error("Enter a valid dimension in millimetres");
        *patch[i] = value * mm;
      }
      auto candidate = style_copy(working);
      if (format_words_page(candidate, page_style, format)) {
        // Page dimensions alone do not prove that paragraph/table constraints
        // can lay out. Reject a failed native pagination before committing.
        NativeTextShaper shaper(
            Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
        (void)paginate_text(read_text_flow(candidate), shaper);
      }
      return candidate;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
}
namespace {
Text tab_value(const WordsTabStopSpec &stop, const Text &key,
               const Text &fallback = {}) {
  auto found = stop.properties.find(key);
  return found == stop.properties.end() ? fallback : found->second;
}
Text tab_pattern(const WordsTabStopSpec &stop) {
  if (!tab_value(stop, "leader-text").empty())
    return "text";
  auto type = tab_value(stop, "leader-type");
  if (type == "none")
    return "none";
  return tab_value(stop, "leader-style",
                   type == "single" || type == "double" ? Text("solid")
                                                        : Text("none"));
}
std::optional<WordsTabStopSpec> edit_tab_stop(Fl_Window &window,
                                              const OdfPackage &package,
                                              WordsTabStopSpec initial,
                                              const WordsTabSet &existing,
                                              std::optional<size_t> edited) {
  constexpr double mm = 72 / 25.4;
  const int width = 640, height = 400,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height,
                        edited ? "Edit tab stop" : "Add tab stop");
  background.hint.copy_label("Unchanged fields retain their source formatting");
  Fl_Input position(x + 170, y + 50, 120, 30, "Position (mm):");
  const auto initial_position =
      Text::number(words_tab_position(initial) / mm, 'g', 9);
  position.value(initial_position.toUtf8().c_str());
  StyleChoice alignments;
  for (const auto &value : TextList{"left", "right", "center", "char"})
    alignments.add(value, value == "char"    ? "Character"
                          : value == "left"  ? "Left"
                          : value == "right" ? "Right"
                                             : "Center");
  auto initial_type = tab_value(initial, "type", "left");
  if (alignments.find(initial_type) < 0)
    alignments.add(initial_type, "Current alignment");
  Fl_Choice alignment(x + 425, y + 50, 190, 30, "Alignment:");
  alignments.attach(alignment, initial_type);
  Fl_Input delimiter(x + 170, y + 95, 120, 30, "Delimiter:");
  auto initial_delimiter = tab_value(initial, "char", ".");
  delimiter.value(initial_delimiter.toUtf8().c_str());
  StyleChoice patterns;
  for (const auto &value :
       TextList{"none", "solid", "dotted", "dash", "long-dash", "dot-dash",
                "dot-dot-dash", "wave", "text"})
    patterns.add(value, value == "none"           ? "None"
                        : value == "solid"        ? "Solid"
                        : value == "dotted"       ? "Dotted"
                        : value == "dash"         ? "Dash"
                        : value == "long-dash"    ? "Long dash"
                        : value == "dot-dash"     ? "Dot dash"
                        : value == "dot-dot-dash" ? "Dot dot dash"
                        : value == "wave"         ? "Wave"
                                                  : "Text");
  const auto initial_pattern = tab_pattern(initial);
  if (patterns.find(initial_pattern) < 0)
    patterns.add(initial_pattern, "Current leader");
  Fl_Choice pattern(x + 425, y + 95, 190, 30, "Leader:");
  patterns.attach(pattern, initial_pattern);
  Fl_Input thickness(x + 170, y + 140, 120, 30, "Width (pt / %):");
  auto initial_width = tab_value(initial, "leader-width", "auto");
  thickness.value(initial_width.toUtf8().c_str());
  thickness.tooltip("A number in points, a length, a percentage, or "
                    "auto/normal/bold/thin/medium/thick");
  Fl_Check_Button doubled(x + 425, y + 140, 180, 30, "Double line");
  const bool initial_double = tab_value(initial, "leader-type") == "double";
  doubled.value(initial_double);
  Fl_Input color(x + 170, y + 185, 165, 30, "Color:");
  auto initial_color = tab_value(initial, "leader-color", "font-color");
  color.value(initial_color.toUtf8().c_str());
  color.tooltip("#RRGGBB or font-color");
  Fl_Input text(x + 425, y + 185, 190, 30, "Text:");
  auto initial_text = tab_value(initial, "leader-text", "·");
  text.value(initial_text.toUtf8().c_str());
  StyleChoice fonts;
  fonts.add({}, "(Tab text font)");
  auto named = words_character_styles(package);
  for (const auto &style : named) {
    auto label = style.label;
    if (std::count_if(named.begin(), named.end(), [&](const auto &other) {
          return other.label == style.label;
        }) > 1)
      label += " (" + style.name + ")";
    fonts.add(style.name, label);
  }
  auto initial_font = tab_value(initial, "leader-text-style");
  Text initial_font_choice = initial_font;
  if (!initial_font.empty() &&
      (initial.leader_scope != WordsTabStopSpec::Common ||
       fonts.find(initial_font) < 0)) {
    fonts.add(":current-source", "Current source style");
    initial_font_choice = ":current-source";
  }
  Fl_Choice font(x + 170, y + 230, 445, 30, "Leader text style:");
  fonts.attach(font, initial_font_choice);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  std::function<void()> enable = [&] {
    if (alignments.selected(alignment) == "char")
      delimiter.activate();
    else
      delimiter.deactivate();
    auto mode = patterns.selected(pattern);
    const bool line = mode != "none" && mode != "text";
    if (line) {
      doubled.activate();
      thickness.activate();
    } else {
      doubled.deactivate();
      thickness.deactivate();
    }
    if (mode == "text") {
      text.activate();
      font.activate();
    } else {
      text.deactivate();
      font.deactivate();
    }
    if (mode != "none")
      color.activate();
    else
      color.deactivate();
  };
  auto changed = [](Fl_Widget *, void *data) {
    (*static_cast<std::function<void()> *>(data))();
  };
  alignment.callback(changed, &enable);
  pattern.callback(changed, &enable);
  enable();
  while (panel.run(position)) {
    try {
      auto result = initial;
      auto raw = Text(position.value());
      if (raw != initial_position) {
        bool ok = false;
        auto points = raw.trimmed().toDouble(&ok) * mm;
        if (!ok || !std::isfinite(points))
          throw std::runtime_error("Enter a valid tab position in millimetres");
        result.properties["position"] =
            words_new_tab_stop(points).properties.at("position");
      }
      auto type = alignments.selected(alignment);
      if (type != initial_type)
        result.properties["type"] = type;
      if (type == "char") {
        auto value = Text(delimiter.value());
        if (type != initial_type || value != initial_delimiter)
          result.properties["char"] = value;
      } else
        result.properties.erase("char");
      const auto mode = patterns.selected(pattern);
      if (mode != initial_pattern) {
        result.properties["leader-style"] =
            mode == "text" ? Text("solid") : mode;
        result.properties["leader-type"] = mode == "none"    ? "none"
                                           : mode == "text"  ? "single"
                                           : doubled.value() ? "double"
                                                             : "single";
        if (mode != "text")
          result.properties.erase("leader-text");
      } else if (mode != "none" && mode != "text" &&
                 bool(doubled.value()) != initial_double)
        result.properties["leader-type"] =
            doubled.value() ? "double" : "single";
      if (mode == "text" &&
          (mode != initial_pattern || Text(text.value()) != initial_text))
        result.properties["leader-text"] = Text(text.value());
      if (Text(thickness.value()) != initial_width) {
        auto value = Text(thickness.value()).trimmed();
        bool numeric = false;
        auto points = value.toDouble(&numeric);
        result.properties["leader-width"] =
            numeric ? words_new_tab_stop(points).properties.at("position")
                    : value;
      }
      if (Text(color.value()) != initial_color) {
        auto value = Text(color.value()).trimmed();
        if (value.empty())
          result.properties.erase("leader-color");
        else
          result.properties["leader-color"] = value;
      }
      auto selected_font = fonts.selected(font);
      auto ref =
          selected_font == ":current-source" ? initial_font : selected_font;
      auto ref_scope = selected_font == ":current-source"
                           ? initial.leader_scope
                           : WordsTabStopSpec::Common;
      if (ref != initial_font || ref_scope != initial.leader_scope) {
        if (ref.empty())
          result.properties.erase("leader-text-style");
        else
          result.properties["leader-text-style"] = ref;
        result.leader_scope = ref_scope;
      }
      auto check = existing;
      if (edited)
        check.stops.at(*edited) = result;
      else
        check.stops.push_back(result);
      WordsTabsFormat format;
      format.stops = std::move(check);
      validate_words_tabs(format);
      return result;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
}
} // namespace
std::optional<WordsTabsFormat>
tab_stops_dialog(Fl_Window &window, OdfPackage original, size_t begin,
                 size_t end, const std::vector<TextCellAddress> &cells,
                 const std::vector<std::pair<size_t, size_t>> &ranges) {
  constexpr double mm = 72 / 25.4;
  const auto initial = read_words_tabs(original, begin, end, ranges);
  auto current = initial.stops;
  std::optional<WordsTabSet> replacement;
  bool inherit = false;
  const int width = 680, height = 500,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Tab stops");
  background.hint.copy_label(
      "Changes are staged until Apply; Cancel discards them");
  Fl_Hold_Browser browser(x + 20, y + 50, width - 40, 210);
  browser.format_char(0);
  browser.textfont(dx::font);
  browser.textsize(dx::size);
  Fl_Box scope(x + 20, y + 270, width - 40, 28);
  scope.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int action = 0;
  ActionButton add(panel, action, 1, x + 20, y + 310, 90, "Add…"),
      edit(panel, action, 2, x + 120, y + 310, 90, "Edit…"),
      remove(panel, action, 3, x + 220, y + 310, 90, "Delete"),
      clear(panel, action, 4, x + 320, y + 310, 100, "Clear all"),
      parent(panel, action, 5, x + 430, y + 310, 230, "Use inherited");
  Fl_Box global(x + 20, y + 345, width - 40, 20,
                "Document defaults (affect all paragraphs)");
  global.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Fl_Input distance(x + 175, y + 370, 110, 30, "Interval (mm):");
  const auto initial_distance =
      Text::number(initial.default_distance / mm, 'g', 9);
  distance.value(initial_distance.toUtf8().c_str());
  distance.tooltip("0 uses the default 12.5mm interval");
  Fl_Choice origin(x + 430, y + 370, 230, 30, "Origin:");
  origin.add("Paragraph indent|Page margin");
  origin.value(initial.relative ? 0 : 1);
  if (!initial.origin_editable) {
    origin.deactivate();
    origin.tooltip("Changing the origin requires ODF 1.4; the current document "
                   "version is preserved");
  }
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  std::function<void()> availability = [&] {
    auto chosen = browser.value();
    if (current && chosen > 0 && size_t(chosen) <= current->stops.size()) {
      edit.activate();
      remove.activate();
    } else {
      edit.deactivate();
      remove.deactivate();
    }
  };
  browser.callback(
      [](Fl_Widget *, void *data) {
        (*static_cast<std::function<void()> *>(data))();
      },
      &availability);
  auto refresh = [&](std::optional<double> selected = {}) {
    browser.clear();
    add.label(current ? "Add…" : "Replace…");
    if (!current) {
      browser.add("(Mixed tab stops)");
      scope.copy_label(
          "Mixed selection: Replace or Clear all assigns one tab list");
    } else {
      scope.copy_label(current->stops.empty()
                           ? "No explicit stops; the default interval is used"
                           : "Tab stops apply to the selected paragraphs");
      std::stable_sort(current->stops.begin(), current->stops.end(),
                       [](const auto &a, const auto &b) {
                         return words_tab_position(a) < words_tab_position(b);
                       });
      if (replacement)
        replacement = current;
      for (size_t i = 0; i < current->stops.size(); ++i) {
        auto &stop = current->stops[i];
        auto type = tab_value(stop, "type", "left");
        auto caption = Text::number(words_tab_position(stop) / mm, 'g', 9) +
                       " mm    " + type + "    " + tab_pattern(stop);
        browser.add(caption.toUtf8().c_str());
        if (selected && std::abs(words_tab_position(stop) - *selected) < 1e-7)
          browser.value(int(i + 1));
      }
    }
    availability();
  };
  refresh();
  while (true) {
    action = 0;
    if (!panel.run(browser))
      return {};
    try {
      std::optional<size_t> chosen;
      if (current && browser.value() > 0 &&
          size_t(browser.value()) <= current->stops.size())
        chosen = size_t(browser.value() - 1);
      if (action == 1 || action == 2) {
        if (action == 2 && !chosen)
          throw std::runtime_error("Choose a tab stop to edit");
        WordsTabSet base = current.value_or(WordsTabSet{});
        double next = base.stops.empty()
                          ? initial.default_distance
                          : words_tab_position(base.stops.back()) +
                                initial.default_distance;
        if (std::abs(next) > 100000)
          next = 0;
        auto spec =
            action == 2 ? base.stops[*chosen] : words_new_tab_stop(next);
        auto changed = edit_tab_stop(window, original, spec, base,
                                     action == 2 ? chosen : std::nullopt);
        if (changed) {
          if (action == 2)
            base.stops[*chosen] = *changed;
          else
            base.stops.push_back(*changed);
          // Keep source-scoped specs rather than reading generated aliases
          // back into the form. The final candidate is compiled only once.
          if (action != 2 || changed->properties != spec.properties ||
              changed->leader_scope != spec.leader_scope) {
            current = std::move(base);
            replacement = current;
            inherit = false;
          }
          refresh(words_tab_position(*changed));
        }
        continue;
      }
      if (action == 3) {
        if (!chosen)
          throw std::runtime_error("Choose a tab stop to delete");
        current->stops.erase(current->stops.begin() + *chosen);
        replacement = current;
        inherit = false;
        refresh();
        continue;
      }
      if (action == 4) {
        current = WordsTabSet{};
        replacement = current;
        inherit = false;
        refresh();
        continue;
      }
      if (action == 5) {
        auto preview = style_copy(original);
        WordsTabsFormat format;
        format.inherit = true;
        format_words_tabs(preview, begin, end, format);
        current = read_words_tabs(preview, begin, end, ranges).stops;
        replacement.reset();
        inherit = true;
        refresh();
        continue;
      }
      WordsTabsFormat format;
      format.stops = replacement;
      format.inherit = inherit;
      auto raw = Text(distance.value());
      if (raw != initial_distance && !raw.trimmed().empty()) {
        bool ok = false;
        auto value = raw.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value))
          throw std::runtime_error(
              "Enter a valid default interval in millimetres");
        format.default_distance = value * mm;
      }
      if (initial.origin_editable &&
          bool(origin.value() == 0) != initial.relative)
        format.relative = origin.value() == 0;
      auto candidate = style_copy(original);
      auto preview_begin = begin, preview_end = end;
      if (format.stops || format.inherit)
        if (auto range =
                materialize_words_table_range(original, cells, begin, end)) {
          candidate = std::move(range->document);
          preview_begin = range->begin;
          preview_end = range->end;
        }
      auto changed =
          format_words_tabs(candidate, preview_begin, preview_end, format);
      if (changed) {
        NativeTextShaper shaper(
            Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
        (void)paginate_text(read_text_flow(candidate), shaper);
      }
      return format;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
}
std::optional<WordsTableEdit>
insert_table_dialog(Fl_Window &window, OdfPackage original, size_t position) {
  const int width = 520, height = 300,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Insert table");
  background.hint.copy_label(
      "Insert at the caret; text on either side is retained");
  background.hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Fl_Input rows(x + 100, y + 65, 100, 30, "Rows:");
  rows.value("2");
  Fl_Input columns(x + 350, y + 65, 100, 30, "Columns:");
  columns.value("2");
  Fl_Check_Button header(x + 20, y + 115, width - 40, 30,
                         "Repeat first row on following pages");
  Buttons buttons(panel, x + width - 225, y + height - 50);
  buttons.apply.label("Insert");
  Fl_Group::current(previous);
  while (panel.run(rows)) {
    try {
      bool rows_ok = false, columns_ok = false;
      auto r = Text(rows.value()).trimmed().toInt(&rows_ok);
      auto c = Text(columns.value()).trimmed().toInt(&columns_ok);
      if (!rows_ok || !columns_ok || r <= 0 || c <= 0)
        throw std::runtime_error(
            "Enter positive whole numbers for rows and columns");
      auto candidate = insert_words_table(
          original, position, {size_t(r), size_t(c), bool(header.value())});
      NativeTextShaper shaper(
          Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
      (void)paginate_text(read_text_flow(candidate.document), shaper);
      return candidate;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
}

std::optional<WordsTableEdit>
cell_format_dialog(Fl_Window &window, OdfPackage original,
                   const TextCellAddress &address,
                   const std::vector<TextCellAddress> &path, size_t position) {
  const auto initial = words_table_cell_format(original, address, path);
  const int width = 760, height = 450,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Cell format");
  Fl_Choice vertical(x + 160, y + 55, 180, 30, "Vertical alignment:");
  vertical.add("Top|Middle|Bottom");
  vertical.value(initial.vertical);
  Fl_Input fill(x + 510, y + 55, 215, 30, "Background:");
  auto color_text = [](TextColor c) {
    if (c.alpha == 0)
      return Text("transparent");
    const char *digits = "0123456789abcdef";
    std::string result = "#";
    for (unsigned char v : {c.red, c.green, c.blue}) {
      result += digits[v >> 4];
      result += digits[v & 15];
    }
    return Text(result);
  };
  auto parse_color = [](Text value) {
    if (value == "transparent")
      return TextColor{0, 0, 0, 0};
    auto s = value.toStdString();
    unsigned rgb = 0;
    if (s.size() != 7 || s.front() != '#')
      throw std::runtime_error("Use #RRGGBB or transparent for cell colors");
    auto result = std::from_chars(s.data() + 1, s.data() + 7, rgb, 16);
    if (result.ec != std::errc() || result.ptr != s.data() + 7)
      throw std::runtime_error("Use #RRGGBB or transparent for cell colors");
    return TextColor{static_cast<unsigned char>(rgb >> 16),
                     static_cast<unsigned char>(rgb >> 8),
                     static_cast<unsigned char>(rgb)};
  };
  auto initial_fill =
      color_text(initial.background.value_or(TextColor{0, 0, 0, 0}));
  fill.value(initial_fill.toUtf8().c_str());
  fill.tooltip("#RRGGBB; transparent reveals the row or table background");
  Fl_Box pad_label(x + 125, y + 100, 110, 25, "Padding (pt)");
  Fl_Box style_label(x + 285, y + 100, 140, 25, "Border style");
  Fl_Box width_label(x + 460, y + 100, 110, 25, "Width (pt)");
  Fl_Box ink_label(x + 610, y + 100, 115, 25, "Border color");
  const char *sides[] = {"Top:", "Right:", "Bottom:", "Left:"};
  std::array<std::unique_ptr<Fl_Input>, 4> padding, widths, colors;
  std::array<std::unique_ptr<Fl_Choice>, 4> borders;
  for (size_t i = 0; i < 4; ++i) {
    int row = y + 135 + int(i) * 45;
    padding[i] = std::make_unique<Fl_Input>(x + 125, row, 110, 30, sides[i]);
    borders[i] = std::make_unique<Fl_Choice>(x + 285, row, 140, 30);
    borders[i]->add(
        "None|Solid|Double|Dashed|Dotted|Groove|Ridge|Inset|Outset");
    widths[i] = std::make_unique<Fl_Input>(x + 460, row, 110, 30);
    colors[i] = std::make_unique<Fl_Input>(x + 610, row, 115, 30);
    padding[i]->value(Text::number(initial.padding[i]).toUtf8().c_str());
    borders[i]->value(initial.borders[i].style);
    widths[i]->value(Text::number(initial.borders[i].width).toUtf8().c_str());
    colors[i]->value(color_text(initial.borders[i].color).toUtf8().c_str());
  }
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  while (panel.run(*padding[0])) {
    try {
      WordsCellFormat format;
      if (vertical.value() != initial.vertical)
        format.vertical = TextCellStyle::Vertical(vertical.value());
      auto value = Text(fill.value()).trimmed();
      if (!value.empty() && value != initial_fill)
        format.background = parse_color(value);
      for (size_t i = 0; i < 4; ++i) {
        changed_number(*padding[i], initial.padding[i], format.padding[i]);
        auto edge = initial.borders[i];
        auto original_edge = edge;
        std::optional<double> changed;
        changed_number(*widths[i], edge.width, changed);
        if (changed) {
          edge.width = *changed;
          edge.double_widths.reset();
        }
        if (borders[i]->value() != edge.style) {
          edge.style = TextTableBorder::Style(borders[i]->value());
          if (edge.style != TextTableBorder::None && edge.width == 0 &&
              !changed) {
            edge.width = .75;
            widths[i]->value("0.75");
          }
          edge.double_widths.reset();
        }
        value = Text(colors[i]->value()).trimmed();
        if (!value.empty() && value != color_text(edge.color))
          edge.color = parse_color(value);
        if (edge != original_edge)
          format.borders[i] = edge;
      }
      auto candidate =
          format_words_table_cell(original, address, format, path, position);
      NativeTextShaper shaper(
          Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
      (void)paginate_text(read_text_flow(candidate.document), shaper);
      return candidate;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
}

std::optional<WordsTableEdit>
table_sizing_dialog(Fl_Window &window, OdfPackage original,
                    const TextCellAddress &address,
                    const std::vector<TextCellAddress> &path, size_t position,
                    double body_width) {
  const auto initial = words_table_sizing(original, address, path);
  NativeTextShaper shaper(
      Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
  auto flow = read_text_flow(original);
  auto measured =
      measure_words_table_geometry(flow, path, body_width, shaper);
  const auto table_alignment = flow.tables.at(address.table).alignment;
  const double mm = 25.4 / 72.;
  const int width = 740, height = 390,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Table sizing");
  Fl_Choice table_mode(x + 180, y + 55, 180, 30, "Table width:");
  table_mode.add("Available width|Fixed|Percentage");
  Fl_Input table_value(x + 470, y + 55, 130, 30, "Size:");
  Fl_Box table_unit(x + 610, y + 55, 70, 30);
  Fl_Choice align(x + 180, y + 100, 180, 30, "Alignment:");
  align.add("Left|Center|Right");
  Fl_Choice row_mode(x + 180, y + 155, 180, 30, "Row height:");
  row_mode.add("Automatic|Minimum|Exact");
  Fl_Input row_value(x + 470, y + 155, 130, 30, "Height (mm):");
  Fl_Choice column_mode(x + 180, y + 205, 180, 30, "Column width:");
  column_mode.add("Relative weight|Fixed");
  Fl_Input column_value(x + 470, y + 205, 130, 30, "Size:");
  Fl_Box column_unit(x + 610, y + 205, 70, 30);
  background.hint.resize(x + 18, y + height - 132, width - 36, 64);
  background.hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  auto hint = "Anchor row " + Text::number(address.row + 1) + ", column " +
              Text::number(address.column + 1) +
              ". Exact height clips text. Fixed columns retain their widths; "
              "relative columns share the remaining table width.";
  background.hint.copy_label(hint.toUtf8().c_str());
  table_mode.value(initial.table.mode);
  row_mode.value(initial.row.mode);
  column_mode.value(initial.column.mode);
  align.value(table_alignment == TextTable::Center  ? 1
              : table_alignment == TextTable::Right ? 2
                                                   : 0);
  auto display = [](double n) { return Text::number(n, 'g', 9); };
  const double current_width =
      std::accumulate(measured.columns.begin(), measured.columns.end(), 0.);
  const double initial_table_value =
      initial.table.mode == WordsTableSize::Stretch ? current_width * mm
      : initial.table.mode == WordsTableSize::Fixed ? initial.table.value * mm
                                                    : initial.table.value;
  const double initial_row_value = initial.row.mode == WordsRowSize::Automatic
                                       ? measured.rows.at(address.row) * mm
                                       : initial.row.value * mm;
  const double initial_column_value =
      initial.column.mode == WordsColumnSize::Fixed ? initial.column.value * mm
                                                    : initial.column.value;
  table_value.value(display(initial_table_value).toUtf8().c_str());
  row_value.value(display(initial_row_value).toUtf8().c_str());
  column_value.value(display(initial_column_value).toUtf8().c_str());
  std::function<void(Fl_Widget *)> update = [&](Fl_Widget *changed) {
    if (changed == &table_mode)
      table_value.value(
          display(table_mode.value() == WordsTableSize::Percentage
                      ? (initial.table.mode == WordsTableSize::Percentage
                             ? initial.table.value
                             : 100)
                      : current_width * mm)
              .toUtf8()
              .c_str());
    if (changed == &column_mode)
      column_value.value(
          display(column_mode.value() == WordsColumnSize::Fixed
                      ? measured.columns.at(address.column) * mm
                      : (initial.column.mode == WordsColumnSize::Weight
                             ? initial.column.value
                             : 1))
              .toUtf8()
              .c_str());
    table_unit.copy_label(
        table_mode.value() == WordsTableSize::Percentage ? "%" : "mm");
    column_unit.copy_label(
        column_mode.value() == WordsColumnSize::Fixed ? "mm" : "weight");
    if (table_mode.value() == WordsTableSize::Stretch) {
      table_value.deactivate();
      align.deactivate();
    } else {
      table_value.activate();
      align.activate();
    }
    if (row_mode.value() == WordsRowSize::Automatic)
      row_value.deactivate();
    else
      row_value.activate();
    panel.redraw();
  };
  for (auto *choice : {&table_mode, &row_mode, &column_mode})
    choice->callback(
        [](Fl_Widget *w, void *p) {
          (*static_cast<std::function<void(Fl_Widget *)> *>(p))(w);
        },
        &update);
  update(nullptr);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  auto read = [&](Fl_Input &input, double shown, double exact,
                  bool unchanged_mode, double scale) {
    auto text = Text(input.value()).trimmed();
    if (unchanged_mode && (text.empty() || text == display(shown)))
      return exact;
    bool valid = false;
    double n = text.toDouble(&valid);
    if (!valid || !std::isfinite(n))
      throw std::runtime_error(
          "Enter a finite size for the selected sizing mode");
    return n * scale;
  };
  while (panel.run(table_mode)) {
    try {
      WordsTableSizingChange change;
      WordsTableSize table;
      table.mode = WordsTableSize::Mode(table_mode.value());
      if (table.mode != WordsTableSize::Stretch) {
        table.value =
            read(table_value, initial_table_value, initial.table.value,
                 table.mode == initial.table.mode,
                 table.mode == WordsTableSize::Fixed ? 1 / mm : 1);
        table.alignment = align.value() == 1   ? TextTable::Center
                          : align.value() == 2 ? TextTable::Right
                                               : TextTable::Left;
      }
      if (table != initial.table)
        change.table = table;
      WordsRowSize row;
      row.mode = WordsRowSize::Mode(row_mode.value());
      if (row.mode != WordsRowSize::Automatic)
        row.value = read(row_value, initial_row_value, initial.row.value,
                         row.mode == initial.row.mode, 1 / mm);
      if (row != initial.row)
        change.row = row;
      WordsColumnSize column;
      column.mode = WordsColumnSize::Mode(column_mode.value());
      column.value =
          read(column_value, initial_column_value, initial.column.value,
               column.mode == initial.column.mode,
               column.mode == WordsColumnSize::Fixed ? 1 / mm : 1);
      if (column != initial.column)
        change.column = column;
      auto candidate =
          size_words_table(original, address, change, path, position);
      (void)paginate_text(read_text_flow(candidate.document), shaper);
      return candidate;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
  return {};
}

static std::optional<WordsTableEdit>
cell_dimensions_dialog(Fl_Window &window, OdfPackage original,
                       const TextCellAddress &address,
                       const std::vector<TextCellAddress> &path, bool subdivide,
                       std::optional<double> body_width = {}) {
  const int width = 520, height = 260,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height,
                        subdivide ? "Subdivide cell" : "Merge cells");
  background.hint.copy_label(
      subdivide ? "Divide this cell into row and column parts; keep text in "
                  "the first part"
                : "Merge a rectangle starting at this cell; keep all contents");
  background.hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Fl_Input rows(x + 100, y + 70, 100, 30, "Rows:");
  rows.value(Text::number(subdivide ? 1 : address.rows).toUtf8().constData());
  Fl_Input columns(x + 350, y + 70, 100, 30, "Columns:");
  columns.value(
      Text::number(subdivide ? 2 : std::max<size_t>(2, address.columns))
          .toUtf8()
          .constData());
  Fl_Check_Button equal(x + 20, y + 115, width - 40, 30,
                        "Equal measured sizes");
  if (!subdivide)
    equal.hide();
  Buttons buttons(panel, x + width - 225, y + height - 50);
  buttons.apply.label(subdivide ? "Subdivide" : "Merge");
  Fl_Group::current(previous);
  while (panel.run(rows))
    try {
      bool rok = false, cok = false;
      auto r = Text(rows.value()).trimmed().toInt(&rok),
           c = Text(columns.value()).trimmed().toInt(&cok);
      if (!rok || !cok || r <= 0 || c <= 0)
        throw std::runtime_error(
            "Enter positive whole numbers for rows and columns");
      NativeTextShaper shaper(
          Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
      auto result =
          subdivide ? subdivide_words_table_cell(original, address, size_t(r),
                                                 size_t(c), path, body_width,
                                                 bool(equal.value()), &shaper)
                    : merge_words_table_cells(original, address, size_t(r),
                                              size_t(c), path);
      (void)paginate_text(read_text_flow(result.document), shaper);
      return result;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  return {};
}
std::optional<WordsTableEdit>
merge_cells_dialog(Fl_Window &window, OdfPackage original,
                   const TextCellAddress &address,
                   const std::vector<TextCellAddress> &path) {
  return cell_dimensions_dialog(window, std::move(original), address, path,
                                false);
}
std::optional<WordsTableEdit> subdivide_cell_dialog(
    Fl_Window &window, OdfPackage original, const TextCellAddress &address,
    const std::vector<TextCellAddress> &path, double body_width) {
  return cell_dimensions_dialog(window, std::move(original), address, path,
                                true, body_width);
}
} // namespace DomeOffice
