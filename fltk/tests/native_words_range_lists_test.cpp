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
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
OdfPackage fixture(bool nested) {
  OdfPackage p(Kind::Words);
  auto styles = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  for (auto name : {"Numbers", "Roman"}) {
    auto style = p.xml.createElementNS(TEXT, "text:list-style");
    style.setAttributeNS(STYLE, "style:name", name);
    for (int i = 1; i <= 3; ++i) {
      auto level = p.xml.createElementNS(TEXT, "text:list-level-style-number");
      level.setAttributeNS(TEXT, "text:level", Text::number(i));
      level.setAttributeNS(STYLE, "style:num-format",
                           Text(name) == "Roman" ? "I"
                           : i == 1              ? "1"
                           : i == 2              ? "a"
                                                 : "i");
      level.setAttributeNS(STYLE, "style:num-suffix", ".");
      auto props = p.xml.createElementNS(STYLE, "style:list-level-properties");
      props.setAttributeNS(TEXT, "text:space-before",
                           Text::number((i - 1) * 18) + "pt");
      props.setAttributeNS(TEXT, "text:min-label-width", "18pt");
      props.setAttributeNS(TEXT, "text:min-label-distance", "4pt");
      level.appendChild(props);
      style.appendChild(level);
    }
    styles.appendChild(style);
  }
  auto blue = p.xml.createElementNS(STYLE, "style:style");
  blue.setAttributeNS(STYLE, "style:name", "Blue");
  blue.setAttributeNS(STYLE, "style:family", "text");
  auto props = p.xml.createElementNS(STYLE, "style:text-properties");
  props.setAttributeNS(FO, "fo:color", "#165aa7");
  blue.appendChild(props);
  styles.appendChild(blue);
  auto cellstyle = p.xml.createElementNS(STYLE, "style:style");
  cellstyle.setAttributeNS(STYLE, "style:name", "Cell");
  cellstyle.setAttributeNS(STYLE, "style:family", "table-cell");
  props = p.xml.createElementNS(STYLE, "style:table-cell-properties");
  props.setAttributeNS(FO, "fo:border", "0.5pt solid #222222");
  props.setAttributeNS(FO, "fo:padding", "3pt");
  cellstyle.appendChild(props);
  styles.appendChild(cellstyle);
  size_t serial = 0;
  auto element = [&](XmlElement parent, const Text &tag) {
    auto node = p.xml.createElementNS(TEXT, "text:" + tag);
    if (tag != "span")
      node.setAttributeNS(XML, "xml:id", "item" + Text::number(++serial));
    parent.appendChild(node);
    return node;
  };
  auto paragraph = [&](XmlElement parent, const Text &value,
                       bool colored = false) {
    auto para = element(parent, "p");
    auto dest = para;
    if (colored) {
      dest = element(para, "span");
      dest.setAttributeNS(TEXT, "text:style-name", "Blue");
    }
    set_plain_text(dest, value);
    return para;
  };
  auto lists = [&](XmlElement cell) {
    auto list = element(cell, "list");
    list.setAttributeNS(TEXT, "text:style-name", "Numbers");
    auto a = element(list, "list-item");
    paragraph(a, "Alpha");
    paragraph(a, "A continuation");
    auto sub = element(a, "list");
    auto child = element(sub, "list-item");
    paragraph(child, "Child");
    auto b = element(list, "list-item");
    paragraph(b, "Český Beta", true);
    paragraph(b, "B continuation");
    auto c = element(list, "list-item");
    paragraph(c, "Gamma");
    auto roman = element(cell, "list");
    roman.setAttributeNS(TEXT, "text:style-name", "Roman");
    auto d = element(roman, "list-item");
    d.setAttributeNS(TEXT, "text:start-value", "7");
    paragraph(d, "Delta");
    paragraph(element(roman, "list-item"), "End");
  };
  std::function<XmlElement(bool)> table = [&](bool outer) {
    auto node = p.xml.createElementNS(TABLE, "table:table");
    node.setAttributeNS(TABLE, "table:name", outer ? "Outer" : "Inner");
    auto col = p.xml.createElementNS(TABLE, "table:table-column");
    col.setAttributeNS(TABLE, "table:number-columns-repeated",
                       outer ? "3" : "2");
    node.appendChild(col);
    auto row = p.xml.createElementNS(TABLE, "table:table-row");
    row.setAttributeNS(TABLE, "table:number-rows-repeated", outer ? "3" : "2");
    node.appendChild(row);
    auto cell = p.xml.createElementNS(TABLE, "table:table-cell");
    cell.setAttributeNS(TABLE, "table:number-columns-repeated",
                        outer ? "3" : "2");
    cell.setAttributeNS(TABLE, "table:style-name", "Cell");
    cell.setAttributeNS(OFFICE, "office:value-type", "string");
    cell.setAttributeNS(OFFICE, "office:string-value", "retained");
    row.appendChild(cell);
    if (outer && nested) {
      paragraph(cell, "Outer");
      cell.appendChild(table(false));
      paragraph(cell, "Tail");
    } else
      lists(cell);
    return node;
  };
  auto body = p.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  paragraph(body, "Before");
  body.appendChild(table(true));
  paragraph(body, "After");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
int role(const std::vector<TextCellAddress> &path, bool nested) {
  if (path.size() != (nested ? 2 : 1))
    return 0;
  if (nested) {
    if (path[0].row != 0 || path[0].column != 1)
      return 0;
    const auto &c = path.back();
    return c.row == 0 && c.column == 0   ? 1
           : c.row == 1 && c.column == 1 ? 3
                                         : 2;
  }
  const auto &c = path.back();
  return c.row == 1 && c.column == 1   ? 1
         : c.row == 1 && c.column == 2 ? 2
         : c.row == 2 && c.column == 0 ? 3
                                       : 0;
}
TextSelection selection(const TextDocument &doc, bool nested,
                        bool reverse = false) {
  auto f = doc.flow();
  WordsDisplayMap map(f);
  const WordsDisplayParagraph *a = nullptr, *b = nullptr;
  for (const auto &r : map.paragraphs())
    if (f.paragraphs[r.paragraph].text == "Český Beta") {
      if (role(r.cells, nested) == 1)
        a = &r;
      if (role(r.cells, nested) == 3)
        b = &r;
    }
  assert(a && b);
  TextSelection s{a->begin + 2, b->begin + 3};
  s.anchor_cells = a->cells;
  s.cells = b->cells;
  s.cell = s.cells.back();
  if (reverse) {
    std::swap(s.anchor, s.cursor);
    std::swap(*s.anchor_cells, s.cells);
    s.cell = s.cells.back();
  }
  return s;
}
void check(const TextDocument &doc, bool nested, bool indented) {
  auto flow = doc.flow();
  WordsDisplayMap map(flow);
  size_t changed = 0;
  for (const auto &r : map.paragraphs()) {
    const auto &p = flow.paragraphs[r.paragraph];
    if (!p.list)
      continue;
    if (p.text == "Delta")
      assert(p.list->text == "VII.");
    if (p.text == "End")
      assert(p.list->text == "VIII.");
    auto role_id = role(r.cells, nested);
    const bool affected =
        indented && (role_id == 2 ||
                     (role_id == 1 && p.text != "Alpha" &&
                      p.text != "A continuation" && p.text != "Child") ||
                     (role_id == 3 && p.text != "Gamma" && p.text != "Delta" &&
                      p.text != "End"));
    assert(p.list->level == (p.text == "Child" ? 2 : 1) + affected);
    if (affected)
      ++changed;
  }
  assert(changed == (indented ? (nested ? 26 : 18) : 0));
  for (const auto &t : flow.tables)
    for (const auto &c : t.cells)
      assert(c.source.attributeNS(OFFICE, "string-value") == "retained");
  std::set<Text> ids;
  auto walk = [&](auto &&self, XmlElement node) -> void {
    auto id = node.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        self(self, n);
  };
  walk(walk, doc.style_snapshot().xml.documentElement());
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("words-range-lists-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  for (bool nested : {false, true}) {
    const std::string prefix = nested ? "nested-" : "flat-";
    auto p = fixture(nested);
    auto path = Text((dir / (prefix + "source.odt")).string());
    p.save(path);
    for (bool reverse : {false, true}) {
      TextDocument doc;
      doc.load(path);
      const auto original = doc.style_snapshot().xml.toByteArray();
      auto old = selection(doc, nested, reverse);
      const auto selected = doc.selected_text(old);
      TextSelection next;
      assert(doc.list_action(old, WordsListAction::Indent, true, &next));
      check(doc, nested, true);
      assert(doc.selected_text(next) == selected && next.anchor_cells &&
             next.cells.size() == (nested ? 2 : 1));
      assert((next.anchor > next.cursor) == reverse);
      auto changed = doc.style_snapshot().xml.toByteArray();
      doc.save(Text((dir / (prefix + (reverse ? "reverse" : "indent") + ".odt"))
                        .string()));
      assert(doc.undo() == old &&
             doc.style_snapshot().xml.toByteArray() == original);
      assert(doc.redo() == next &&
             doc.style_snapshot().xml.toByteArray() == changed);
      TextSelection restored;
      assert(doc.list_action(next, WordsListAction::Outdent, true, &restored));
      check(doc, nested, false);
      assert(doc.selected_text(restored) == selected);
      doc.save(Text(
          (dir / (prefix + (reverse ? "reverse-outdent" : "outdent") + ".odt"))
              .string()));
      assert(doc.undo() == next &&
             doc.style_snapshot().xml.toByteArray() == changed);
      assert(doc.undo() == old &&
             doc.style_snapshot().xml.toByteArray() == original);
      assert(!doc.can_undo());
    }
    // Every selected item is at root: no-op outdent must discard localization.
    TextDocument doc;
    doc.load(path);
    auto s = selection(doc, nested);
    auto original = doc.style_snapshot().xml.toByteArray();
    // Child is selected together with its parent and travels with it.
    auto rev = doc.revision();
    auto next = s;
    assert(doc.list_action(s, WordsListAction::Outdent, true, &next));
    assert(next == s && doc.revision() == rev && !doc.can_undo() &&
           doc.style_snapshot().xml.toByteArray() == original);
    assert(!doc.list_action(s, WordsListAction::ExitEmpty));
    // A stale endpoint or a selection extending into ordinary text rejects all.
    auto invalid = s;
    invalid.cursor = doc.text().size();
    rejects([&] { doc.list_action(invalid, WordsListAction::Indent); });
    invalid = s;
    invalid.cursor = 0;
    invalid.cells.clear();
    invalid.cell.reset();
    rejects([&] { doc.list_action(invalid, WordsListAction::Indent); });
    assert(doc.style_snapshot().xml.toByteArray() == original &&
           !doc.can_undo());
    assert(doc.style_snapshot().entry("Extras/opaque.bin") ==
           Bytes("\0keep\xff", 6));
  }
  // Sparse/duplicate scalar ranges pick items once and retain unselected peers.
  auto p = fixture(false);
  auto path = Text((dir / "sparse-source.odt").string());
  p.save(path);
  TextDocument doc;
  doc.load(path);
  auto beta = doc.text().find(Text("Český Beta")),
       cont = doc.text().find(Text("B continuation")),
       delta = doc.text().find(Text("Delta"));
  auto candidate = doc.style_snapshot();
  candidate.xml = read_document_xml(candidate.xml.toByteArray());
  assert(change_words_lists(candidate,
                            {{beta, beta + 2},
                             {cont, cont + 2},
                             {beta, beta + 2},
                             {delta, delta + 2}},
                            WordsListAction::Indent));
  for (const auto &p : read_text_flow(candidate).paragraphs)
    if (p.text == "Český Beta" || p.text == "B continuation" ||
        p.text == "Delta")
      assert(p.list->level == 2);
  candidate.save(Text((dir / "sparse-indent.odt").string()));
  // Later groups can move before an earlier unsupported nested outdent fails.
  // Discard both those moves and the displayed-instance materialization.
  auto guarded = fixture(false);
  auto cell = guarded.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  auto main = child(cell, TEXT, "list");
  auto wrapper = guarded.xml.createElementNS(TEXT, "text:list");
  wrapper.setAttributeNS(TEXT, "text:style-name", "Numbers");
  cell.insertBefore(wrapper, main);
  auto carrier = guarded.xml.createElementNS(TEXT, "text:list-item");
  wrapper.appendChild(carrier);
  auto label = guarded.xml.createElementNS(TEXT, "text:p");
  set_plain_text(label, "Carrier");
  carrier.appendChild(label);
  carrier.appendChild(main);
  auto trailing = guarded.xml.createElementNS(TEXT, "text:list");
  child(main, TEXT, "list-item").appendChild(trailing);
  auto guarded_path = Text((dir / "guarded-source.odt").string());
  guarded.save(guarded_path);
  TextDocument blocked;
  blocked.load(guarded_path);
  auto guarded_selection = selection(blocked, false);
  auto guarded_flow = blocked.flow();
  WordsDisplayMap guarded_map(guarded_flow);
  for (const auto &record : guarded_map.paragraphs())
    if (role(record.cells, false) == 1 &&
        guarded_flow.paragraphs[record.paragraph].text == "Child")
      guarded_selection.anchor = record.begin + 1;
  const auto before = blocked.style_snapshot().xml.toByteArray();
  rejects([&] {
    blocked.list_action(guarded_selection, WordsListAction::Outdent);
  });
  assert(!blocked.can_undo() && !blocked.modified() &&
         blocked.style_snapshot().xml.toByteArray() == before);
  // An entirely non-list selection leaves Tab available for ordinary input.
  auto plain = fixture(false);
  auto plain_cell = plain.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  while (!plain_cell.firstChild().isNull())
    plain_cell.removeChild(plain_cell.firstChild());
  auto para = plain.xml.createElementNS(TEXT, "text:p");
  set_plain_text(para, "Plain");
  plain_cell.appendChild(para);
  auto plain_path = Text((dir / "plain-source.odt").string());
  plain.save(plain_path);
  TextDocument ordinary;
  ordinary.load(plain_path);
  WordsDisplayMap plain_map(ordinary.flow());
  const auto &a = plain_map.paragraphs()[5], &b = plain_map.paragraphs()[7];
  TextSelection plain_selection{a.begin + 1, b.begin + 2};
  plain_selection.anchor_cells = a.cells;
  plain_selection.cells = b.cells;
  plain_selection.cell = b.cells.back();
  const auto plain_xml = ordinary.style_snapshot().xml.toByteArray();
  assert(!ordinary.list_action(plain_selection, WordsListAction::Indent, true));
  assert(ordinary.style_snapshot().xml.toByteArray() == plain_xml &&
         !ordinary.can_undo() && !ordinary.modified());
  std::cout << "PASS displayed multi-list/cell ranges, item deduplication, "
               "numbering/style retention, no-op and exact history\n";
}
