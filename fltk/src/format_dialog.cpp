// SPDX-License-Identifier: GPL-2.0-or-later
#include "format_dialog.h"
#include <array>
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
#include <FL/Fl_Multiline_Input.H>
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
paragraph_format_dialog(Fl_Window &window, const WordsParagraphFormat &initial,
                        bool paginated) {
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
  if (!paginated)
    for (Fl_Widget *control :
         std::array<Fl_Widget *, 5>{&orphans, &widows, &keep, &next, &page})
      control->deactivate();
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
      if (paginated) {
        changed_integer(orphans, initial.orphans, result.orphans);
        changed_integer(widows, initial.widows, result.widows);
        changed_boolean(keep, initial.keep_together, result.keep_together);
        changed_boolean(next, initial.keep_next, result.keep_next);
        changed_boolean(page, initial.break_before, result.break_before);
      }
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
  browser.column_char(0);
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
std::optional<WordsStoryDialogResult>
page_story_dialog(Fl_Window &window, const OdfPackage &package,
                  WordsStoryAddress selected) {
  auto pages = words_page_styles(package);
  const int width = 590, height = 400,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Header and footer");
  background.hint.copy_label(
      "Edit text and formatting, then choose Finish to return");
  Fl_Choice page(x + 155, y + 50, 400, 30, "Page style:");
  page.value(0);
  for (size_t i = 0; i < pages.size(); ++i) {
    page.add(" ");
    page.replace(int(i), pages[i].label.toUtf8().c_str());
    if (pages[i].name == selected.master)
      page.value(int(i));
  }
  Fl_Choice kind(x + 155, y + 95, 240, 30, "Content:");
  kind.add("Header|Footer");
  kind.value(selected.kind == TextPageStory::Footer ? 1 : 0);
  Fl_Choice variant(x + 155, y + 140, 240, 30, "Pages:");
  variant.add("Default|First|Left (even numbered)");
  variant.value(int(selected.variant));
  Fl_Choice region(x + 155, y + 185, 240, 30, "Region:");
  region.add("Full width|Left|Center|Right");
  region.value(int(selected.region));
  Fl_Check_Button displayed(x + 155, y + 230, 340, 28, "Show on pages");
  Fl_Box scope(x + 20, y + 265, width - 40, 28,
               "Changes apply to all pages using this page style and variant");
  scope.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  buttons.apply.label("Edit");
  bool changed = false;
  struct State {
    Panel &panel;
    bool &changed;
  } state{panel, changed};
  for (auto *choice : {&page, &kind, &variant, &region})
    choice->callback(
        [](Fl_Widget *, void *data) {
          auto &s = *static_cast<State *>(data);
          s.changed = s.panel.done = s.panel.accepted = true;
        },
        &state);
  auto address = [&] {
    return WordsStoryAddress{pages.at(size_t(std::max(0, page.value()))).name,
                             kind.value() ? TextPageStory::Footer
                                          : TextPageStory::Header,
                             TextPageStory::Variant(variant.value()),
                             WordsStoryAddress::Region(region.value())};
  };
  auto update = [&] {
    auto a = address();
    auto found = read_text_flow(package).masters.at(a.master).stories;
    auto story =
        found ? (a.kind == TextPageStory::Header ? found->headers
                                                 : found->footers)[a.variant]
              : nullptr;
    displayed.value(!story || story->displayed);
  };
  update();
  Fl_Group::current(previous);
  while (true) {
    changed = false;
    if (!panel.run(page))
      return {};
    if (changed) {
      update();
      continue;
    }
    try {
      WordsStoryEdit check(package, address());
      return WordsStoryDialogResult{address(), bool(displayed.value())};
    } catch (const std::exception &e) {
      background.hint.copy_label(e.what());
      background.hint.labelcolor(FL_RED);
      background.hint.redraw();
    }
  }
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
  browser.column_char(0);
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
  auto measured = measure_words_table_geometry(flow, path, body_width, shaper);
  const auto table_alignment = flow.tables.at(address.table).alignment;
  const double mm = 25.4 / 72.;
  const int width = 740, height = 440,
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
  Fl_Check_Button proportional(x + 180, y + 245, 500, 30,
                               "Scale fixed columns proportionally");
  background.hint.resize(x + 18, y + height - 132, width - 36, 64);
  background.hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  auto hint = "Anchor row " + Text::number(address.row + 1) + ", column " +
              Text::number(address.column + 1) +
              ". Exact height clips text. Fixed columns keep their widths "
              "unless proportional scaling is selected. Relative columns "
              "share the remaining table width.";
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
    if (proportional.value()) {
      column_mode.deactivate();
      column_value.deactivate();
    } else {
      column_mode.activate();
      column_value.activate();
    }
    panel.redraw();
  };
  for (auto *choice : {&table_mode, &row_mode, &column_mode})
    choice->callback(
        [](Fl_Widget *w, void *p) {
          (*static_cast<std::function<void(Fl_Widget *)> *>(p))(w);
        },
        &update);
  proportional.callback(
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
      change.proportional_columns = proportional.value();
      if (!change.proportional_columns) {
        WordsColumnSize column;
        column.mode = WordsColumnSize::Mode(column_mode.value());
        column.value =
            read(column_value, initial_column_value, initial.column.value,
                 column.mode == initial.column.mode,
                 column.mode == WordsColumnSize::Fixed ? 1 / mm : 1);
        if (column != initial.column)
          change.column = column;
      }
      auto candidate = size_words_table(original, address, change, path,
                                        position, body_width, &shaper);
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

namespace DomeOffice {
bool shape_geometry_dialog(
    Fl_Window &window, const NativeShapeGeometry &initial,
    const std::function<void(const NativeShapeGeometryEdit &)> &apply) {
  constexpr double mm = 72 / 25.4;
  const int width = 620, height = 370,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  struct GeometryPanel : Panel {
    using Panel::Panel;
    int handle(int event) override {
      if (event == FL_KEYDOWN &&
          (Fl::event_key() == FL_Enter || Fl::event_key() == FL_KP_Enter) &&
          dynamic_cast<Fl_Input *>(Fl::focus())) {
        accepted = done = true;
        return 1;
      }
      return Panel::handle(event);
    }
  };
  GeometryPanel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Position and size");
  background.hint.copy_label("Blank fields keep their current value");
  Fl_Input px(x + 155, y + 70, 125, 30, "X (mm):"),
      py(x + 435, y + 70, 125, 30, "Y (mm):");
  Fl_Input w(x + 155, y + 120, 125, 30,
             initial.line ? "Length (mm):" : "Width (mm):"),
      h(x + 435, y + 120, 125, 30, "Height (mm):"),
      angle(x + 155, y + 170, 125, 30, "Rotation (°):");
  if (!initial.position_enabled) {
    px.deactivate();
    py.deactivate();
  }
  if (!initial.width_enabled)
    w.deactivate();
  if (!initial.height_enabled)
    h.deactivate();
  if (initial.line)
    h.hide();
  if (!initial.rotation_enabled)
    angle.deactivate();
  std::array<Fl_Input *, 5> fields{&px, &py, &w, &h, &angle};
  std::array<std::optional<double>, 5> values{
      initial.origin.x / mm, initial.origin.y / mm, initial.width / mm,
      initial.height / mm, initial.rotation};
  std::array<Text, 5> originals;
  for (size_t i = 0; i < fields.size(); ++i)
    if (values[i]) {
      originals[i] = Text::number(*values[i], 'g', 10);
      fields[i]->value(originals[i].toUtf8().c_str());
    }
  Fl_Box hint(x + 20, y + 220, width - 40, 45,
              initial.line ? "X/Y locate the start of the line. Rotation is "
                             "measured on the slide."
                           : "X/Y locate the shape origin on the slide.\nSize "
                             "is measured before rotation and scaling.");
  hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  Fl_Widget *focus = &buttons.cancel;
  for (auto *field : fields)
    if (field->active() && field->visible()) {
      focus = field;
      break;
    }
  while (panel.run(*focus)) {
    try {
      NativeShapeGeometryEdit edit;
      std::array<std::optional<double> *, 5> output{
          &edit.x, &edit.y, &edit.width, &edit.height, &edit.rotation};
      for (size_t i = 0; i < fields.size(); ++i) {
        if (!fields[i]->active() || !fields[i]->visible())
          continue;
        auto text = Text(fields[i]->value()).trimmed();
        if (text.empty() || text == originals[i])
          continue;
        bool ok = false;
        double value = text.toDouble(&ok);
        if (!ok || !std::isfinite(value))
          throw std::runtime_error(
              "Enter a finite number, or leave the field unchanged");
        bool old_ok = false;
        double shown = originals[i].toDouble(&old_ok);
        if (old_ok && shown == value)
          continue;
        *output[i] = i == 4 ? value : value * mm;
      }
      apply(edit);
      return true;
    } catch (const std::exception &error) {
      background.hint.copy_label(error.what());
      background.hint.labelcolor(FL_RED);
      window.redraw();
    }
  }
  return false;
}

std::optional<NativeStageLayerEdit>
stage_layers_dialog(Fl_Window &window, NativeStageLayerEdit draft) {
  const int width = 760, height = 520,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Layers");
  background.hint.copy_label(
      "Changes apply together. Cancel keeps the document unchanged.");
  background.hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Fl_Choice scope(x + 85, y + 48, 150, 28, "Scope:");
  scope.add("Document");
  scope.add("Slide");
  scope.add("Master");
  scope.value(1);
  Fl_Box origin(x + 250, y + 48, 170, 28);
  origin.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Fl_Button local(x + 430, y + 48, 145, 28, "Make local set"),
      inherit(x + 590, y + 48, 145, 28, "Use document set");
  Fl_Hold_Browser browser(x + 20, y + 92, 230, 225);
  browser.format_char(0);
  browser.column_char(0);
  browser.textfont(dx::font);
  browser.textsize(dx::size);
  Fl_Button create(x + 20, y + 327, 105, 28, "New layer"),
      remove(x + 140, y + 327, 110, 28, "Delete layer");
  Fl_Input name(x + 350, y + 95, 385, 28, "Name:");
  Fl_Choice display(x + 350, y + 132, 255, 28, "Visible:");
  for (auto text : {"Screen and print", "Screen only", "Print only", "Hidden"})
    display.add(text);
  Fl_Check_Button locked(x + 345, y + 168, 390, 28, "Protect objects");
  Fl_Input title(x + 350, y + 205, 385, 28, "Title:");
  Fl_Multiline_Input description(x + 350, y + 242, 385, 48, "Description:");
  Fl_Button update(x + 350, y + 300, 145, 28, "Update layer");
  Fl_Box uses(x + 510, y + 300, 225, 28);
  uses.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Fl_Choice missing(x + 20, y + 355, 230, 28);
  Fl_Button define(x + 20, y + 390, 230, 28, "Define missing layer");
  Fl_Choice assignment(x + 350, y + 355, 250, 28, "Selection:");
  Fl_Button assign(x + 615, y + 355, 120, 28, "Assign objects");
  Fl_Box assignment_hint(x + 285, y + 392, 450, 24,
                         "Assignment uses the current slide's layers");
  assignment_hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  NativeLayerScope current = NativeLayerScope::Slide;
  std::optional<Text> selected;
  std::optional<NativeLayerInfo> selected_info;
  std::vector<NativeLayerInfo> items, assignable;
  std::vector<Text> unresolved;
  bool refreshing = false;
  auto enabled = [](Fl_Widget &widget, bool value) {
    if (value)
      widget.activate();
    else
      widget.deactivate();
  };
  auto status = [&](const char *text, bool error = false) {
    background.hint.copy_label(text);
    background.hint.labelcolor(error ? FL_RED : FL_FOREGROUND_COLOR);
    window.redraw();
  };
  auto save_current = [&] {
    if (!selected || !draft.own_set(current) || draft.read_only())
      return;
    NativeLayerPatch patch;
    auto value = *selected_info;
    if (Text(name.value()) != value.name)
      patch.name = Text(name.value());
    if (Text(title.value()) != value.title)
      patch.title = Text(title.value());
    if (Text(description.value()) != value.description)
      patch.description = Text(description.value());
    if (display.value() != int(value.properties.display))
      patch.display = SceneLayer::Display(display.value());
    if (bool(locked.value()) != value.properties.protected_objects)
      patch.protected_objects = bool(locked.value());
    draft.update(current, *selected, patch);
    if (patch.name) {
      selected = *patch.name;
      value.name = *patch.name;
    }
    if (patch.title)
      value.title = *patch.title;
    if (patch.description)
      value.description = *patch.description;
    if (patch.display)
      value.properties.display = *patch.display;
    if (patch.protected_objects)
      value.properties.protected_objects = *patch.protected_objects;
    selected_info = std::move(value);
  };
  std::function<void(std::optional<Text>)> refresh =
      [&](std::optional<Text> wanted) {
        refreshing = true;
        items = draft.layers(current);
        browser.clear();
        selected.reset();
        selected_info.reset();
        for (size_t i = 0; i < items.size(); ++i) {
          auto label =
              items[i].name.empty() ? Text("(empty name)") : items[i].name;
          browser.add(label.toUtf8().c_str());
          if (wanted && items[i].name == *wanted)
            browser.value(int(i) + 1);
        }
        if (browser.value() == 0 && !items.empty())
          browser.value(1);
        const bool own = draft.own_set(current), writable = !draft.read_only();
        origin.copy_label(own ? (current == NativeLayerScope::Document
                                     ? "Document layers"
                                     : "Local layer set")
                              : "Using document layers");
        if (browser.value() > 0) {
          const auto &info = items.at(browser.value() - 1);
          selected = info.name;
          selected_info = info;
          name.value(info.name.toUtf8().c_str());
          title.value(info.title.toUtf8().c_str());
          description.value(info.description.toUtf8().c_str());
          display.value(int(info.properties.display));
          locked.value(info.properties.protected_objects);
          uses.copy_label((Text::number(info.uses) + " objects in this scope")
                              .toUtf8()
                              .c_str());
        } else {
          name.value("");
          title.value("");
          description.value("");
          display.value(0);
          locked.value(0);
          uses.copy_label("");
        }
        for (Fl_Widget *widget :
             std::array<Fl_Widget *, 7>{&name, &title, &description, &display,
                                        &locked, &update, &remove})
          enabled(*widget, writable && own && selected.has_value());
        enabled(create, writable);
        enabled(local, writable && !own);
        enabled(inherit,
                writable && own && current != NativeLayerScope::Document);
        enabled(buttons.apply, writable);
        unresolved = draft.missing_layers(current);
        missing.clear();
        for (size_t i = 0; i < unresolved.size(); ++i) {
          missing.add(" ");
          missing.replace(int(i), unresolved[i].toUtf8().c_str());
        }
        if (!unresolved.empty())
          missing.value(0);
        enabled(missing, !unresolved.empty());
        enabled(define, writable && !unresolved.empty());
        assignable = draft.layers(NativeLayerScope::Slide);
        assignment.clear();
        assignment.add("Keep current assignment");
        assignment.add("No layer");
        for (size_t i = 0; i < assignable.size(); ++i) {
          assignment.add(" ");
          assignment.replace(int(i) + 2, assignable[i].name.toUtf8().c_str());
        }
        assignment.value(0);
        enabled(assignment, writable && draft.has_selection());
        enabled(assign, writable && draft.has_selection());
        refreshing = false;
        window.redraw();
      };
  std::vector<std::unique_ptr<std::function<void()>>> callbacks;
  auto on = [&](Fl_Widget &widget, std::function<void()> action) {
    auto callback = std::make_unique<std::function<void()>>(
        [&, action = std::move(action)] {
          if (refreshing)
            return;
          try {
            action();
            status(
                "Changes apply together. Cancel keeps the document unchanged.");
          } catch (const std::exception &error) {
            status(error.what(), true);
          }
        });
    widget.callback(
        [](Fl_Widget *, void *p) {
          (*static_cast<std::function<void()> *>(p))();
        },
        callback.get());
    callbacks.push_back(std::move(callback));
  };
  on(browser, [&] {
    if (browser.value() < 1)
      return;
    auto wanted = items.at(browser.value() - 1).name;
    auto old = selected;
    save_current();
    if (old && wanted == *old)
      wanted = *selected;
    refresh(wanted);
  });
  on(scope, [&] {
    auto next = NativeLayerScope(scope.value());
    if (!draft.available(next)) {
      scope.value(int(current));
      throw std::runtime_error("This slide has no resolved master page");
    }
    try {
      save_current();
    } catch (...) {
      scope.value(int(current));
      throw;
    }
    current = next;
    refresh({});
  });
  on(local, [&] {
    save_current();
    draft.make_local(current);
    refresh(selected);
  });
  on(inherit, [&] {
    save_current();
    draft.inherit(current);
    refresh(selected);
  });
  on(create, [&] {
    save_current();
    auto name = draft.new_name(current);
    draft.create(current, name);
    refresh(name);
  });
  on(update, [&] {
    save_current();
    refresh(selected);
  });
  on(remove, [&] {
    save_current();
    if (selected)
      draft.remove(current, *selected);
    refresh({});
  });
  on(define, [&] {
    save_current();
    if (missing.value() < 0 || size_t(missing.value()) >= unresolved.size())
      return;
    auto value = unresolved[missing.value()];
    draft.create(current, value, true);
    refresh(value);
  });
  on(assign, [&] {
    auto index = assignment.value();
    std::optional<Text> value;
    if (index >= 2)
      value = assignable.at(index - 2).name;
    auto old = selected;
    const bool slide_definition = current == NativeLayerScope::Slide ||
                                  (current == NativeLayerScope::Document &&
                                   !draft.own_set(NativeLayerScope::Slide));
    save_current();
    if (slide_definition && value && old && selected && *value == *old)
      value = *selected;
    if (index > 0)
      draft.assign(value);
    refresh(selected);
  });
  refresh({});
  while (panel.run(browser)) {
    try {
      save_current();
      draft.validate();
      return draft;
    } catch (const std::exception &error) {
      status(error.what(), true);
    }
  }
  return {};
}
} // namespace DomeOffice

namespace DomeOffice {
namespace {
std::optional<NativePaintFormat>
dash_format_dialog(Fl_Window &window, const NativePaintInfo &initial) {
  const int width = 660, height = 520,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Dash pattern");
  background.hint.copy_label(
      "Blank lengths use one stroke width; outer Apply commits the changes");
  Fl_Check_Button use(x + 20, y + 50, 600, 30,
                      "Apply this pattern to the stroke");
  use.value(initial.values.stroke == "dash" &&
            initial.values.dash_pattern.has_value());
  Fl_Input count1(x + 170, y + 100, 100, 30, "First count:"),
      length1(x + 450, y + 100, 170, 30, "First length:"),
      count2(x + 170, y + 145, 100, 30, "Second count:"),
      length2(x + 450, y + 145, 170, 30, "Second length:"),
      distance(x + 170, y + 190, 140, 30, "Gap:");
  Fl_Choice shape(x + 450, y + 190, 170, 30, "Dash shape:");
  shape.add("Rectangular|Round");
  shape.tooltip(
      "Round dash shapes use round ends; rectangular shapes use the line cap");
  SceneDashPattern default_pattern;
  default_pattern.first = 1;
  default_pattern.first_length = SceneDashLength{4, true};
  default_pattern.distance = SceneDashLength{2, true};
  auto pattern = initial.values.dash_pattern.value_or(default_pattern);
  count1.value(Text::number(pattern.first).toUtf8().c_str());
  count2.value(Text::number(pattern.second).toUtf8().c_str());
  auto show = [](Fl_Input &field, const std::optional<SceneDashLength> &value) {
    if (value)
      field.value(
          (Text::number(value->value * (value->relative ? 100 : 1), 'g', 12) +
           (value->relative ? "%" : "pt"))
              .toUtf8()
              .c_str());
  };
  show(length1, pattern.first_length);
  show(length2, pattern.second_length);
  show(distance, pattern.distance);
  shape.value(pattern.round ? 1 : 0);
  Fl_Box units(
      x + 20, y + 235, 600, 30,
      "Lengths: pt, mm, cm or % of stroke width; blank uses one stroke width.");
  units.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Fl_Check_Button replace_overlays(x + 20, y + 275, 600, 30,
                                   "Replace overlay styles (blank clears)");
  replace_overlays.value(0);
  Fl_Input overlays(x + 170, y + 315, 450, 30, "Overlay styles:");
  overlays.when(FL_WHEN_CHANGED);
  overlays.callback(
      [](Fl_Widget *, void *data) {
        static_cast<Fl_Check_Button *>(data)->value(1);
      },
      &replace_overlays);
  if (initial.values.dash_names)
    overlays.value(initial.values.dash_names->join(' ').toUtf8().c_str());
  overlays.tooltip("Space-separated existing dash style names, in paint order; "
                   "empty clears overlays");
  Fl_Box available(x + 20, y + 355, 600, 50);
  available.copy_label(
      ("Available styles: " + initial.available_dashes.join(' '))
          .toUtf8()
          .c_str());
  available.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  Fl_Group::current(previous);
  while (panel.run(use)) {
    try {
      NativePaintFormat result;
      if (use.value()) {
        auto doc = read_document_xml(
            ("<draw:stroke-dash xmlns:draw=\"" + DRAW + "\"/>").toUtf8());
        auto node = doc.documentElement();
        for (auto [key, field] :
             {std::pair{"dots1", &count1}, std::pair{"dots2", &count2},
              std::pair{"dots1-length", &length1},
              std::pair{"dots2-length", &length2},
              std::pair{"distance", &distance}}) {
          auto value = Text(field->value()).trimmed();
          if (!value.empty())
            node.setAttributeNS(DRAW, Text("draw:") + key, value);
        }
        node.setAttributeNS(DRAW, "draw:style",
                            shape.value() ? "round" : "rect");
        result.dash_pattern = read_scene_dash_pattern(node);
        result.stroke = "dash";
      }
      if (replace_overlays.value()) {
        TextList names;
        std::istringstream input(Text(overlays.value()).toStdString());
        for (std::string name; input >> name;) {
          if (!initial.available_dashes.contains(Text(name)) ||
              names.size() >= 63)
            throw std::runtime_error(
                "Choose available dash style names for overlays");
          names.push_back(Text(name));
        }
        if (initial.values.dash_names != names)
          result.dash_names = names;
      }
      return result;
    } catch (const std::exception &e) {
      background.hint.copy_label(e.what());
      background.hint.labelcolor(FL_RED);
      window.redraw();
    }
  }
  return {};
}
} // namespace
bool object_paint_dialog(
    Fl_Window &window, const NativePaintInfo &initial,
    const std::function<void(const NativePaintFormat &)> &apply) {
  const int width = 660, height = 540,
            x = std::max(0, (window.w() - width) / 2),
            y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  window.begin();
  Panel panel(window, previous);
  Fl_Group::current(&panel);
  Background background(window, x, y, width, height, "Fill and stroke");
  Fl_Choice fill(x + 160, y + 65, 160, 30, "Fill:"),
      stroke(x + 460, y + 65, 160, 30, "Stroke:");
  Fl_Input fill_color(x + 160, y + 115, 160, 30, "Fill color:"),
      stroke_color(x + 460, y + 115, 160, 30, "Stroke color:");
  Fl_Input fill_opacity(x + 160, y + 165, 125, 30, "Opacity (%):"),
      stroke_opacity(x + 460, y + 165, 125, 30, "Opacity (%):");
  Fl_Input stroke_width(x + 460, y + 215, 125, 30, "Width (pt):");
  Fl_Choice rule(x + 160, y + 215, 160, 30, "Fill rule:");
  rule.add("Keep current|Nonzero|Even-odd");
  if (initial.values.even_odd)
    rule.value(*initial.values.even_odd ? 2 : 1);
  else
    rule.value(0);
  auto mode = [](Fl_Choice &field, const std::optional<Text> &value) {
    field.add("Keep current|None|Solid");
    field.value(value == "none" ? 1 : value == "solid" ? 2 : 0);
  };
  mode(fill, initial.values.fill);
  mode(stroke, initial.values.stroke);
  stroke.add("Dash");
  if (initial.values.stroke == "dash")
    stroke.value(3);
  if (initial.values.fill_color)
    fill_color.value(initial.values.fill_color->toUtf8().c_str());
  if (initial.values.stroke_color)
    stroke_color.value(initial.values.stroke_color->toUtf8().c_str());
  fill_color.tooltip("#RRGGBB; blank keeps each object's current color");
  stroke_color.tooltip(fill_color.tooltip());
  auto percent = [](std::optional<double> value) {
    return value ? std::optional<double>(*value * 100) : std::nullopt;
  };
  number(fill_opacity, percent(initial.values.fill_opacity));
  number(stroke_opacity, percent(initial.values.stroke_opacity));
  number(stroke_width, initial.values.stroke_width);
  Fl_Input image_opacity(x + 160, y + 265, 125, 30, "Image opacity (%):");
  number(image_opacity, percent(initial.values.image_opacity));
  image_opacity.tooltip(
      "Opacity of selected image frames, independent of their background fill");
  if (!initial.image_objects)
    image_opacity.deactivate();
  Fl_Choice cap(x + 460, y + 265, 160, 30, "Line cap:"),
      join(x + 460, y + 315, 160, 30, "Line join:");
  const TextList caps{"butt", "round", "square"},
      joins{"miter", "round", "bevel", "none"};
  auto choice = [](Fl_Choice &field, const char *labels, const TextList &values,
                   const std::optional<Text> &initial) {
    field.add(labels);
    field.value(0);
    for (int i = 0; i < int(values.size()); ++i)
      if (initial == values[i])
        field.value(i + 1);
  };
  choice(cap, "Keep current|Butt|Round|Square", caps,
         initial.values.stroke_cap);
  choice(join, "Keep current|Miter|Round|Bevel|None", joins,
         initial.values.stroke_join);
  int action = 0;
  ActionButton dash_button(panel, action, 1, x + 160, y + 315, 160,
                           "Dash pattern...");
  NativePaintFormat dash_patch;
  Fl_Box summary(x + 20, y + 365, width - 40, 50);
  auto label = Text::number(initial.objects) +
               (initial.objects == 1 ? " object. " : " objects. ") +
               "Selected groups include hidden members.\nShared styles and "
               "other objects keep their formatting.";
  summary.copy_label(label.toUtf8().c_str());
  summary.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  Buttons buttons(panel, x + width - 225, y + height - 50);
  if (!initial.editable) {
    for (auto *widget : std::initializer_list<Fl_Widget *>{
             &fill, &stroke, &fill_color, &stroke_color, &fill_opacity,
             &stroke_opacity, &stroke_width, &rule, &image_opacity, &cap, &join,
             &dash_button, &buttons.apply})
      widget->deactivate();
    background.hint.copy_label(
        "This selection is read-only or contains protected objects");
  }
  Fl_Group::current(previous);
  while (panel.run(initial.editable
                       ? static_cast<Fl_Widget &>(fill)
                       : static_cast<Fl_Widget &>(buttons.cancel))) {
    try {
      if (!initial.editable)
        continue;
      if (action == 1) {
        action = 0;
        auto info = initial;
        if (dash_patch.dash_pattern)
          info.values.dash_pattern = dash_patch.dash_pattern;
        if (dash_patch.dash_names)
          info.values.dash_names = dash_patch.dash_names;
        if (dash_patch.stroke)
          info.values.stroke = dash_patch.stroke;
        if (auto patch = dash_format_dialog(window, info)) {
          if (patch->dash_pattern) {
            dash_patch.dash_pattern = patch->dash_pattern;
            dash_patch.stroke = "dash";
            stroke.value(3);
          }
          if (patch->dash_names)
            dash_patch.dash_names = patch->dash_names;
        }
        continue;
      }
      NativePaintFormat result = dash_patch;
      auto changed_mode = [](Fl_Choice &field, const std::optional<Text> &old,
                             std::optional<Text> &out) {
        if (field.value()) {
          Text value = field.value() == 1   ? "none"
                       : field.value() == 2 ? "solid"
                                            : "dash";
          if (!old || *old != value)
            out = value;
        }
      };
      changed_mode(fill, initial.values.fill, result.fill);
      changed_mode(stroke, initial.values.stroke, result.stroke);
      if (stroke.value() != 3) {
        result.dash_pattern.reset();
        if (dash_patch.stroke && stroke.value())
          result.stroke = stroke.value() == 1 ? "none" : "solid";
      } else if (!result.dash_pattern && !initial.values.dash_pattern &&
                 initial.values.stroke != "dash") {
        SceneDashPattern pattern;
        pattern.first = 1;
        pattern.first_length = SceneDashLength{4, true};
        pattern.distance = SceneDashLength{2, true};
        result.dash_pattern = pattern;
      }
      auto changed_choice = [](Fl_Choice &field, const TextList &values,
                               const std::optional<Text> &old,
                               std::optional<Text> &out) {
        if (field.value() > 0) {
          const auto &value = values.at(field.value() - 1);
          if (old != value)
            out = value;
        }
      };
      changed_choice(cap, caps, initial.values.stroke_cap, result.stroke_cap);
      changed_choice(join, joins, initial.values.stroke_join,
                     result.stroke_join);
      auto changed_color = [](Fl_Input &field, const std::optional<Text> &old,
                              std::optional<Text> &out) {
        auto value = Text(field.value()).trimmed();
        if (!value.empty() && (!old || *old != value))
          out = value;
      };
      changed_color(fill_color, initial.values.fill_color, result.fill_color);
      changed_color(stroke_color, initial.values.stroke_color,
                    result.stroke_color);
      changed_number(fill_opacity, percent(initial.values.fill_opacity),
                     result.fill_opacity);
      if (result.fill_opacity)
        *result.fill_opacity /= 100;
      changed_number(stroke_opacity, percent(initial.values.stroke_opacity),
                     result.stroke_opacity);
      if (result.stroke_opacity)
        *result.stroke_opacity /= 100;
      changed_number(stroke_width, initial.values.stroke_width,
                     result.stroke_width);
      if (initial.image_objects) {
        changed_number(image_opacity, percent(initial.values.image_opacity),
                       result.image_opacity);
        if (result.image_opacity)
          *result.image_opacity /= 100;
      }
      if (rule.value() && (!initial.values.even_odd ||
                           *initial.values.even_odd != (rule.value() == 2)))
        result.even_odd = rule.value() == 2;
      apply(result);
      return true;
    } catch (const std::exception &e) {
      background.hint.copy_label(e.what());
      background.hint.labelcolor(FL_RED);
      window.redraw();
    }
  }
  return false;
}
} // namespace DomeOffice
