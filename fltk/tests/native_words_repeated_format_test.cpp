// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
template <class F> void rejects(F fn) {
  bool failed = false;
  try {
    fn();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
struct State {
  Bytes content, styles, extra;
  bool operator==(const State &) const = default;
};
State state(const TextDocument &d) {
  auto p = d.style_snapshot();
  return {p.xml.toByteArray(), p.entry("styles.xml"),
          p.entry("Extras/opaque.bin")};
}
OdfPackage fixture(bool nested = false, bool repeated = true,
                   bool empty = false) {
  OdfPackage p(Kind::Words);
  auto body = p.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  size_t id = 0;
  auto paragraph = [&](XmlElement parent, const Text &text) {
    auto node = p.xml.createElementNS(TEXT, "text:p");
    node.setAttributeNS(XML, "xml:id", "paragraph" + Text::number(++id));
    set_plain_text(node, text);
    parent.appendChild(node);
  };
  std::function<XmlElement(const Text &, size_t, bool)> table =
      [&](const Text &name, size_t count, bool child) {
        auto t = p.xml.createElementNS(TABLE, "table:table");
        t.setAttributeNS(TABLE, "table:name", name);
        auto column = p.xml.createElementNS(TABLE, "table:table-column");
        column.setAttributeNS(TABLE, "table:number-columns-repeated",
                              Text::number(count));
        t.appendChild(column);
        for (size_t r = 0; r < (repeated ? 1 : count); ++r) {
          auto row = p.xml.createElementNS(TABLE, "table:table-row");
          if (repeated)
            row.setAttributeNS(TABLE, "table:number-rows-repeated",
                               Text::number(count));
          for (size_t c = 0; c < (repeated ? 1 : count); ++c) {
            auto cell = p.xml.createElementNS(TABLE, "table:table-cell");
            if (repeated)
              cell.setAttributeNS(TABLE, "table:number-columns-repeated",
                                  Text::number(count));
            if (child) {
              paragraph(cell, "Outer");
              cell.appendChild(table("Inner", 2, false));
            } else {
              paragraph(cell, empty ? Text() : Text("Český link"));
              paragraph(cell, "Tail");
            }
            row.appendChild(cell);
          }
          t.appendChild(row);
        }
        return t;
      };
  paragraph(body, "Before");
  body.appendChild(table("Outer", 3, nested));
  paragraph(body, "After");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
size_t table_id(const TextFlow &f, const Text &name) {
  for (size_t i = 0; i < f.tables.size(); ++i)
    if (f.tables[i].name == name)
      return i;
  throw std::runtime_error("Missing table");
}
const TextTableCell &at(const TextTable &t, size_t r, size_t c) {
  for (const auto &cell : t.cells)
    if (cell.row == r && cell.column == c)
      return cell;
  throw std::runtime_error("Missing cell");
}
const TextParagraph &first(const TextFlow &f, const TextTableCell &cell) {
  for (const auto &b : cell.blocks)
    if (b.kind == TextBlock::Paragraph)
      return b.formatted ? *b.formatted : f.paragraphs.at(b.index);
  throw std::runtime_error("Missing paragraph");
}
TextColor color(const TextParagraph &p, size_t offset = 1) {
  for (const auto &s : p.spans)
    if (s.begin <= offset && offset < s.end)
      return s.color.value_or(p.color.value_or(TextColor{}));
  return p.color.value_or(TextColor{});
}
TextSelection selection(const TextDocument &d, bool nested = false,
                        bool range = true, bool empty = false) {
  auto f = d.flow();
  size_t position = empty ? 7 : d.text().find(Text("Český"));
  std::vector<TextCellAddress> path{{table_id(f, "Outer"), 1, 2, 1, 1}};
  if (nested)
    path.push_back({table_id(f, "Inner"), 1, 1, 1, 1});
  return {position, position + (range && !empty ? 5 : 0), path.back(), path};
}
void check_cells(const TextDocument &d,
                 const std::function<void(const TextParagraph &, bool)> &check,
                 bool nested = false) {
  auto f = d.flow();
  const auto &outer = f.tables[table_id(f, "Outer")];
  for (const auto &cell : outer.cells) {
    const bool selected = cell.row == 1 && cell.column == 2;
    if (!nested)
      check(first(f, cell), selected);
    else
      for (const auto &block : cell.blocks)
        if (block.kind == TextBlock::Table) {
          const auto &inner =
              block.table ? *block.table : f.tables.at(block.index);
          for (const auto &child : inner.cells)
            check(first(f, child),
                  selected && child.row == 1 && child.column == 1);
        }
  }
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-repeated-format-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto load = [&](TextDocument &d, bool nested = false, bool repeated = true,
                  bool empty = false) {
    auto p = fixture(nested, repeated, empty);
    auto path = Text((dir / "source.odt").string());
    p.save(path);
    d.load(path);
  };
  auto save = [&](const TextDocument &d, const char *name) {
    auto p = d.style_snapshot();
    p.save(Text((dir / (std::string(name) + ".odt")).string()));
  };
  const TextColor blue_color{22, 90, 167};
  WordsCharacterFormat blue;
  blue.color = "#165aa7";
  WordsParagraphFormat centered;
  centered.alignment = "center";
  TextDocument d;
  load(d);
  auto original = state(d);
  auto selected = selection(d), resolved = selected;
  assert(d.format_characters(selected, blue, &resolved));
  assert(resolved.cursor > selected.cursor);
  check_cells(d, [&](const auto &p, bool target) {
    assert(color(p) == (target ? blue_color : TextColor{}));
  });
  save(d, "range-blue");
  auto formatted = state(d);
  auto revision = d.revision();
  assert(!d.format_characters(resolved, blue, &resolved) &&
         d.revision() == revision);
  assert(d.undo() == selected && state(d) == original && !d.modified());
  assert(d.redo() == resolved && state(d) == formatted);
  d.undo();
  // A nonempty caret format stays transient; paragraph isolation remaps it.
  auto caret = selection(d, false, false);
  caret.anchor = caret.cursor += 2;
  assert(!d.format_characters(caret, blue, &resolved));
  assert(state(d) == original && resolved == caret && !d.can_undo());
  assert(d.format_paragraphs(caret, centered, &resolved));
  assert(d.format_info(resolved).character.color == blue.color);
  check_cells(d, [](const auto &p, bool target) {
    assert((p.alignment == TextParagraph::Center) == target);
  });
  save(d, "paragraph-centered");
  assert(d.undo() == caret && state(d) == original);
  // Empty paragraph character defaults are persisted, but only in that cell.
  load(d, false, true, true);
  original = state(d);
  caret = selection(d, false, false, true);
  assert(d.format_characters(caret, blue, &resolved));
  check_cells(d, [&](const auto &p, bool target) {
    assert(color(p) == (target ? blue_color : TextColor{}));
  });
  save(d, "empty-blue");
  assert(d.undo() == caret && state(d) == original);
  load(d);
  original = state(d);
  caret = selection(d, false, false);
  WordsTabsFormat tabs;
  WordsTabSet set;
  set.stops.push_back(words_new_tab_stop(80));
  tabs.stops = set;
  assert(d.format_tabs(caret, tabs, d.revision(), &resolved));
  check_cells(d, [](const auto &p, bool target) {
    assert((p.tabs.size() == 1) == target);
    if (target)
      assert(std::abs(p.tabs[0].position - 80) < 1e-7);
  });
  save(d, "tabs-local");
  assert(d.undo() == caret && state(d) == original);
  WordsTabsFormat defaults;
  defaults.default_distance = 60;
  auto old_revision = d.revision();
  assert(d.format_tabs(caret, defaults, old_revision, &resolved));
  assert(state(d).content == original.content && resolved == caret);
  check_cells(d, [](const auto &p, bool) {
    assert(std::abs(p.default_tab - 60) < 1e-7);
  });
  auto global = state(d);
  rejects([&] { d.format_tabs(caret, tabs, old_revision); });
  assert(state(d) == global);
  save(d, "tabs-global-default");
  d.undo();
  assert(state(d) == original);
  // Adding a named style is global; assigning it targets one displayed copy.
  auto definitions = d.style_snapshot();
  WordsCharacterStyleChange change;
  change.label = "Blue named";
  change.character = blue;
  auto name = define_words_character_style(definitions, change);
  assert(d.commit_styles(definitions, caret, d.revision(), {}, {}, &resolved));
  assert(state(d).content == original.content && resolved == caret);
  d.undo();
  assert(state(d) == original);
  selected = selection(d);
  assert(d.commit_styles(definitions, selected, d.revision(), {}, name,
                         &resolved));
  check_cells(d, [&](const auto &p, bool target) {
    assert(color(p) == (target ? blue_color : TextColor{}));
  });
  save(d, "named-character");
  assert(d.undo() == selected && state(d) == original);
  definitions = d.style_snapshot();
  WordsParagraphStyleChange paragraph;
  paragraph.label = "Centered named";
  paragraph.paragraph = centered;
  name = define_words_paragraph_style(definitions, paragraph);
  assert(
      d.commit_styles(definitions, caret, d.revision(), name, {}, &resolved));
  check_cells(d, [](const auto &p, bool target) {
    assert((p.alignment == TextParagraph::Center) == target);
  });
  save(d, "named-paragraph");
  assert(d.undo() == caret && state(d) == original);
  // An existing style definition remains global even with a repeated-cell
  // caret. Editing its properties must not expand the document body.
  definitions = d.style_snapshot();
  change.name = {};
  change.label = "Global blue";
  name = define_words_character_style(definitions, change);
  selected = selection(d);
  TextSelection raw{selected.anchor, selected.cursor};
  assert(d.commit_styles(definitions, raw, d.revision(), {}, name));
  auto shared = state(d);
  caret = selection(d, false, false);
  definitions = d.style_snapshot();
  WordsCharacterStyleChange update;
  update.name = name;
  update.character.color = "#cc2200";
  define_words_character_style(definitions, update);
  assert(d.commit_styles(definitions, caret, d.revision(), {}, {}, &resolved));
  assert(state(d).content == shared.content && resolved == caret);
  check_cells(d, [](const auto &p, bool) {
    assert(color(p) == TextColor({204, 34, 0}));
  });
  save(d, "named-global-definition");
  assert(d.undo() == caret && state(d) == shared);
  load(d);
  original = state(d);
  caret = selection(d, false, false);
  definitions = d.style_snapshot();
  change.name = {};
  change.label = "Caret blue";
  name = define_words_character_style(definitions, change);
  assert(
      d.commit_styles(definitions, caret, d.revision(), {}, name, &resolved));
  assert(state(d).content == original.content && resolved == caret &&
         d.current_character_style(caret) == name);
  auto named_caret = state(d);
  revision = d.revision();
  assert(!d.commit_styles(d.style_snapshot(), caret, revision, {}, name,
                          &resolved));
  assert(state(d) == named_caret && d.revision() == revision);
  load(d, true);
  original = state(d);
  selected = selection(d, true);
  save(d, "nested-source");
  assert(d.format_characters(selected, blue, &resolved));
  size_t colored = 0;
  check_cells(
      d,
      [&](const auto &p, bool target) {
        assert(color(p) == (target ? blue_color : TextColor{}));
        colored += target;
      },
      true);
  assert(colored == 1);
  save(d, "nested-blue");
  assert(d.undo() == selected && state(d) == original);
  // Ordinary unique-cell ranges remain legal across body/table boundaries.
  load(d, false, false);
  original = state(d);
  auto f = d.flow();
  auto start = d.text().find(Text("Český"));
  TextCellAddress cell{table_id(f, "Outer"), 0, 0, 1, 1};
  TextSelection across{0, start + 5, cell, {cell}};
  assert(d.format_characters(across, blue, &resolved) && resolved == across);
  assert(d.text().startsWith("Before\nČeský"));
  assert(d.undo() == across && state(d) == original);
  std::cout << "PASS repeated character/paragraph/tab/named formatting, global "
               "defaults, empty carets, nested cells and exact history\n";
}
