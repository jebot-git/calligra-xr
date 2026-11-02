// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <unistd.h>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F fn) {
  bool failed = false;
  try {
    fn();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
OdfPackage fixture(bool note = false) {
  OdfPackage p(Kind::Words);
  p.xml = read_document_xml(Bytes(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles/><office:body><office:text><text:p>Before</text:p><table:table table:name="Outer"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3" xml:id="outer-row"><table:table-cell table:number-columns-repeated="3" xml:id="outer-cell"><text:p>Outer</text:p><table:table table:name="Inner"><table:table-column table:number-columns-repeated="2"/><table:table-row table:number-rows-repeated="2" xml:id="inner-row"><table:table-cell table:number-columns-repeated="2" xml:id="inner-cell"><text:p xml:id="first-p">Český <text:a xlink:type="simple" xlink:href="https://example.invalid/retained">link</text:a></text:p><text:p>Tail</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table><text:p>After</text:p></office:text></office:body></office:document-content>)XML"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  if (note) {
    auto item = p.xml.createElementNS(TEXT, "text:note");
    item.setAttributeNS(TEXT, "text:id", "note-1");
    item.setAttributeNS(TEXT, "text:note-class", "footnote");
    auto citation = p.xml.createElementNS(TEXT, "text:note-citation");
    citation.appendChild(p.xml.createTextNode("1"));
    item.appendChild(citation);
    auto body = p.xml.createElementNS(TEXT, "text:note-body");
    auto paragraph = p.xml.createElementNS(TEXT, "text:p");
    set_plain_text(paragraph, "Preserved note");
    body.appendChild(paragraph);
    item.appendChild(body);
    child(p.body(), TEXT, "p").appendChild(item);
  }
  return p;
}
size_t visible_count(XmlElement node, const Text &text, size_t factor = 1) {
  if (node.namespaceURI() == TABLE) {
    if (node.localName() == "table-row")
      factor *=
          node.attributeNS(TABLE, "number-rows-repeated", "1").toLongLong();
    if (node.localName() == "table-cell")
      factor *=
          node.attributeNS(TABLE, "number-columns-repeated", "1").toLongLong();
  }
  if (node.namespaceURI() == TEXT && node.localName() == "p")
    return plain_text(node) == text ? factor : 0;
  size_t count = 0;
  for (auto child = node.firstChild(); !child.isNull();
       child = child.nextSibling())
    if (child.isElement())
      count += visible_count(child, text, factor);
  return count;
}
void unique(const OdfPackage &p) {
  std::set<Text> ids, tables;
  for (auto e : p.xml.elementsByTagNameNS(TABLE, "table"))
    assert(tables.insert(e.attributeNS(TABLE, "name")).second);
  std::function<void(XmlElement)> visit = [&](XmlElement node) {
    auto id = node.attributeNS("http://www.w3.org/XML/1998/namespace", "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto c = node.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        visit(c);
  };
  visit(p.xml.documentElement());
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-repeated-edit-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto source = fixture();
  const auto path = Text((dir / "source.odt").string());
  source.save(path);
  const auto original = source.xml.toByteArray();
  assert(visible_count(source.body(), "Český link") == 36);
  TextDocument document;
  document.load(path);
  auto position = document.text().find(Text("Český")) + 3;
  auto flow = document.flow();
  auto table_id = [&](const Text &name) {
    for (size_t i = 0; i < flow.tables.size(); ++i)
      if (flow.tables[i].name == name)
        return i;
    throw std::runtime_error("Missing test table");
  };
  std::vector<TextCellAddress> cells{{table_id("Outer"), 1, 2, 1, 1},
                                     {table_id("Inner"), 1, 1, 1, 1}};
  TextSelection previous{position, position, cells.back(), cells};
  auto prepared = document.prepare_cell_edit(previous);
  assert(prepared && document.editing());
  assert(prepared->cursor != previous.cursor && prepared->cells.size() == 2);
  assert(visible_count(document.style_snapshot().body(), "Český link") == 36);
  // Preparation alone is a no-op, including source, saved state and history.
  assert(document.end_edit(*prepared) == previous);
  assert(!document.modified() && !document.can_undo() && !document.editing());
  assert(document.style_snapshot().xml.toByteArray() == original);
  prepared = document.prepare_cell_edit(previous);
  const auto cursor = prepared->cursor;
  auto next = *prepared;
  next.anchor = next.cursor = cursor + 1;
  document.replace(cursor, 0, "x", true, *prepared, next);
  assert(!document.end_edit(next));
  auto typed = document.style_snapshot();
  assert(visible_count(typed.body(), "Česxký link") == 1);
  assert(visible_count(typed.body(), "Český link") == 35);
  assert(typed.entry("Extras/opaque.bin") == source.entry("Extras/opaque.bin"));
  unique(typed);
  typed.save(Text((dir / "typed.odt").string()));
  assert(document.undo() == previous);
  assert(!document.modified() && !document.can_undo() && document.can_redo());
  assert(document.style_snapshot().xml.toByteArray() == original);
  assert(document.redo() == next);
  assert(document.style_snapshot().xml.toByteArray() ==
         typed.xml.toByteArray());
  assert(
      !document.prepare_cell_edit(next)); // The edited instance is now local.
  document.undo();
  prepared = document.prepare_cell_edit(previous);
  document.replace(prepared->cursor, 0, "temporary", true, *prepared,
                   *prepared);
  rejects([&] { document.replace(document.text().size() + 1, 0, "bad"); });
  assert(document.end_edit() == previous);
  assert(document.style_snapshot().xml.toByteArray() == original);
  assert(!document.modified() && !document.can_undo() && document.can_redo());
  // Reverse selections map both scalar endpoints, including Unicode text.
  auto range = previous;
  range.anchor += 2;
  range.cursor -= 1;
  prepared = document.prepare_cell_edit(range);
  assert(prepared && prepared->anchor > prepared->cursor);
  next = *prepared;
  next.anchor = next.cursor = prepared->cursor + 1;
  document.replace(prepared->cursor, prepared->anchor - prepared->cursor, "X",
                   true, *prepared, next);
  document.end_edit(next);
  auto replaced = document.style_snapshot();
  assert(visible_count(replaced.body(), "ČeX link") == 1);
  assert(visible_count(replaced.body(), "Český link") == 35);
  unique(replaced);
  replaced.save(Text((dir / "range-replaced.odt").string()));
  assert(document.undo() == range);
  assert(document.style_snapshot().xml.toByteArray() == original);
  // Pending caret formatting follows the mapped source; a cancelled event
  // restores that transient format without adding source or history changes.
  WordsCharacterFormat blue;
  blue.color = "#165aa7";
  document.format_characters(previous, blue);
  assert(document.has_typing_format() && !document.modified());
  prepared = document.prepare_cell_edit(previous);
  assert(document.end_edit(*prepared) == previous);
  assert(document.has_typing_format());
  assert(document.format_info(previous).character.color == blue.color);
  prepared = document.prepare_cell_edit(previous);
  next = *prepared;
  next.anchor = next.cursor = prepared->cursor + 1;
  document.replace(prepared->cursor, 0, "Y", true, *prepared, next);
  document.end_edit(next);
  bool colored = false;
  for (const auto &paragraph : document.flow().paragraphs)
    if (paragraph.text == "ČesYký link")
      for (const auto &span : paragraph.spans)
        colored = colored || (span.begin <= 3 && span.end > 3 &&
                              span.color == TextColor{22, 90, 167});
  assert(colored);
  auto styled = document.style_snapshot();
  assert(visible_count(styled.body(), "ČesYký link") == 1);
  assert(visible_count(styled.body(), "Český link") == 35);
  unique(styled);
  styled.save(Text((dir / "typed-blue.odt").string()));
  assert(document.undo() == previous);
  assert(document.style_snapshot().xml.toByteArray() == original);
  auto incomplete = previous;
  incomplete.cells.erase(incomplete.cells.begin());
  rejects([&] { document.prepare_cell_edit(incomplete); });
  assert(document.style_snapshot().xml.toByteArray() == original);
  auto outside = previous;
  outside.anchor = 0;
  rejects([&] { document.prepare_cell_edit(outside); });
  assert(document.style_snapshot().xml.toByteArray() == original);
  std::cout << "PASS local repeated/nested cell text preparation, scalar "
               "ranges, atomic event history, no-op and rollback\n";
  // A paragraph inside a preserved note is not a second source paragraph in
  // TextDocument::text(). It must not shift a later table's edit coordinates.
  auto noted_source = fixture(true);
  auto noted_path = Text((dir / "note-source.odt").string());
  noted_source.save(noted_path);
  TextDocument noted;
  noted.load(noted_path);
  auto noted_position = noted.text().find(Text("Český")) + 3;
  TextSelection noted_previous{noted_position, noted_position, cells.back(),
                               cells};
  prepared = noted.prepare_cell_edit(noted_previous);
  assert(prepared);
  next = *prepared;
  next.anchor = next.cursor = prepared->cursor + 1;
  noted.replace(prepared->cursor, 0, "z", true, *prepared, next);
  noted.end_edit(next);
  auto noted_typed = noted.style_snapshot();
  assert(visible_count(noted_typed.body(), "Česzký link") == 1);
  assert(visible_count(noted_typed.body(), "Český link") == 35);
  auto notes = noted_typed.xml.elementsByTagNameNS(TEXT, "note");
  assert(notes.size() == 1 && notes[0].attributeNS(TEXT, "id") == "note-1");
  assert(plain_text(child(notes[0], TEXT, "note-body")) == "Preserved note");
  unique(noted_typed);
  noted_typed.save(Text((dir / "note-typed.odt").string()));
  assert(noted.undo() == noted_previous);
  assert(noted.style_snapshot().xml.toByteArray() ==
         noted_source.xml.toByteArray());
  std::cout << "PASS table offsets after preserved embedded paragraphs\n";
}
