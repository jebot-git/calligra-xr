// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
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
OdfPackage fixture(bool nested = false, bool repeated = true) {
  OdfPackage p(Kind::Words);
  auto body = p.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  auto styles = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  auto list_style = p.xml.createElementNS(TEXT, "text:list-style");
  list_style.setAttributeNS(STYLE, "style:name", "Numbers");
  for (size_t i = 1; i <= 2; ++i) {
    auto level = p.xml.createElementNS(TEXT, "text:list-level-style-number");
    level.setAttributeNS(TEXT, "text:level", Text::number(i));
    level.setAttributeNS(STYLE, "style:num-format", i == 1 ? "1" : "a");
    level.setAttributeNS(STYLE, "style:num-suffix", ".");
    list_style.appendChild(level);
  }
  styles.appendChild(list_style);
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
              auto list = p.xml.createElementNS(TEXT, "text:list");
              list.setAttributeNS(TEXT, "text:style-name", "Numbers");
              list.setAttributeNS(XML, "xml:id", "list" + Text::number(++id));
              for (const Text &value :
                   {Text("Alpha"), Text("Český Beta"), Text("Gamma"), Text()}) {
                auto item = p.xml.createElementNS(TEXT, "text:list-item");
                item.setAttributeNS(XML, "xml:id", "item" + Text::number(++id));
                paragraph(item, value);
                list.appendChild(item);
              }
              cell.appendChild(list);
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

TextSelection selected(const TextDocument &d, bool nested, const Text &word,
                       bool range = false) {
  auto f = d.flow();
  auto start = d.text().find(word);
  assert(start != Text::npos);
  std::vector<TextCellAddress> path{{table_id(f, "Outer"), 1, 2, 1, 1}};
  if (nested)
    path.push_back({table_id(f, "Inner"), 1, 1, 1, 1});
  return {start, start + (range ? word.size() + 3 : 0), path.back(), path};
}
std::vector<TextParagraph> cell_paragraphs(const TextFlow &f,
                                           const TextTableCell &cell) {
  std::vector<TextParagraph> result;
  for (const auto &b : cell.blocks)
    if (b.kind == TextBlock::Paragraph)
      result.push_back(b.formatted ? *b.formatted : f.paragraphs.at(b.index));
  return result;
}
void check(const TextDocument &d, bool nested, WordsListAction action,
           bool range = false) {
  auto f = d.flow();
  const auto &outer = f.tables[table_id(f, "Outer")];
  size_t total = 0, changed = 0;
  auto leaf = [&](const TextTableCell &cell, bool target) {
    auto ps = cell_paragraphs(f, cell);
    assert(ps.size() == 4);
    assert(ps[0].text == "Alpha" && ps[1].text == "Český Beta" &&
           ps[2].text == "Gamma" && ps[3].text.empty());
    for (size_t i = 0; i < ps.size(); ++i) {
      bool exited = target && action == WordsListAction::ExitEmpty && i == 3;
      assert(bool(ps[i].list) == !exited);
      if (!exited) {
        size_t expected = target && action == WordsListAction::Indent &&
                                  (i == 1 || (range && i == 2))
                              ? 2
                              : 1;
        assert(ps[i].list->level == expected);
      }
    }
    ++total;
    if ((ps[1].list && ps[1].list->level == 2) || !ps[3].list)
      ++changed;
  };
  for (const auto &cell : outer.cells) {
    bool target = cell.row == 1 && cell.column == 2;
    if (!nested)
      leaf(cell, target);
    else
      for (const auto &b : cell.blocks)
        if (b.kind == TextBlock::Table) {
          const auto &inner = b.table ? *b.table : f.tables.at(b.index);
          for (const auto &c : inner.cells)
            leaf(c, target && c.row == 1 && c.column == 1);
        }
  }
  assert(total == (nested ? 36 : 9));
  assert(changed == 1);
  std::set<Text> ids;
  std::function<void(XmlElement)> visit = [&](XmlElement e) {
    auto key = e.attributeNS(XML, "id");
    assert(key.empty() || ids.insert(key).second);
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        visit(n);
  };
  visit(d.style_snapshot().body());
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-repeated-lists-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto save = [&](const TextDocument &d, const std::string &name) {
    auto path = Text((dir / (name + ".odt")).string());
    d.style_snapshot().save(path);
    OdfPackage reopened(Kind::Words);
    reopened.load(path);
    assert(reopened.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
    assert(read_text_flow(reopened).tables.size() == d.flow().tables.size());
  };
  for (bool nested : {false, true}) {
    const std::string prefix = nested ? "nested-" : "flat-";
    auto source = dir / (prefix + "source.odt");
    fixture(nested).save(Text(source.string()));
    for (auto action : {WordsListAction::Indent, WordsListAction::ExitEmpty})
      for (bool reverse : {false, true}) {
        TextDocument d;
        d.load(Text(source.string()));
        auto before = state(d);
        auto selection = selected(d, nested, "Český Beta",
                                  reverse && action == WordsListAction::Indent);
        if (action == WordsListAction::ExitEmpty)
          selection.anchor = selection.cursor =
              d.text().find(Text("Gamma")) + 6;
        if (reverse)
          std::swap(selection.anchor, selection.cursor);
        auto next = selection;
        assert(d.list_action(selection, action, true, &next));
        check(d, nested, action, reverse && action == WordsListAction::Indent);
        assert(next.cells.size() == (nested ? 2 : 1) &&
               next.cell == next.cells.back());
        assert(next.cells.front().row == 1 && next.cells.front().column == 2);
        assert((next.anchor > next.cursor) ==
               (selection.anchor > selection.cursor));
        const auto changed = state(d);
        assert(changed.extra == before.extra &&
               changed.styles == before.styles);
        save(d, prefix +
                    (action == WordsListAction::Indent ? "indent" : "exit") +
                    (reverse ? "-range" : ""));
        assert(d.undo() == selection);
        assert(state(d) == before && !d.modified() && !d.can_undo() &&
               d.can_redo());
        assert(d.redo() == next && state(d) == changed);
        if (action == WordsListAction::Indent) {
          auto out = next;
          assert(d.list_action(next, WordsListAction::Outdent, false, &out));
          auto f = d.flow();
          size_t beta = 0;
          for (const auto &p : f.paragraphs)
            if (p.text == "Český Beta") {
              assert(p.list && p.list->level == 1);
              ++beta;
            }
          assert(beta > 0);
          save(d, prefix + "outdent" + (reverse ? "-range" : ""));
          assert(d.undo() == next && state(d) == changed);
        }
      }
    TextDocument d;
    d.load(Text(source.string()));
    auto initial = state(d);
    auto revision = d.revision();
    auto caret = selected(d, nested, "Český Beta");
    auto resolved = caret;
    assert(d.list_action(caret, WordsListAction::Outdent, true, &resolved));
    assert(resolved == caret && state(d) == initial &&
           d.revision() == revision && !d.can_undo());
    ++caret.anchor;
    ++caret.cursor;
    assert(!d.list_action(caret, WordsListAction::Indent, true, &resolved));
    assert(state(d) == initial && d.revision() == revision);
    assert(!d.list_action(caret, WordsListAction::ExitEmpty));
    assert(!d.list_action({0, 0}, WordsListAction::Indent));
    auto invalid = selected(d, nested, "Český Beta");
    invalid.cursor = d.text().size();
    rejects([&] { d.list_action(invalid, WordsListAction::Indent); });
    assert(state(d) == initial && d.revision() == revision && !d.can_undo());
    if (nested) {
      invalid = selected(d, nested, "Český Beta");
      invalid.cells.erase(invalid.cells.begin());
      rejects([&] { d.list_action(invalid, WordsListAction::Indent); });
      assert(state(d) == initial && !d.can_undo());
    }
    // A pending caret format follows source expansion; text typed afterwards
    // stays in that same displayed list item and has its original blue color.
    caret = selected(d, nested, "Český Beta");
    WordsCharacterFormat blue;
    blue.color = "#165aa7";
    assert(!d.format_characters(caret, blue));
    assert(d.list_action(caret, WordsListAction::Indent, false, &resolved));
    assert(d.has_typing_format() &&
           d.format_info(resolved).character.color == "#165aa7");
    d.replace(resolved.cursor, 0, "X", true, resolved, resolved);
    save(d, prefix + "typed-blue");
    size_t marked = 0;
    for (const auto &p : d.flow().paragraphs)
      if (p.text == "XČeský Beta")
        ++marked;
    assert(marked == 1);
    assert(d.undo());
    assert(d.undo() == caret && state(d) == initial);
  }
  std::cout << "PASS local repeated/nested list levels and empty exit, "
               "orientation, no-op, pending format and exact history\n";
}
