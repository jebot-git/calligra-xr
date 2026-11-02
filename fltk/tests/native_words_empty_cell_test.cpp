// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_text_editing.h"
#include "native_text_shaper.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
OdfPackage fixture(bool repeated, bool nested, bool body = true) {
  OdfPackage p(Kind::Words);
  Text xml =
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="Cell" style:family="table-cell"><style:table-cell-properties fo:padding="3pt" fo:border="0.5pt solid #222222"/><style:text-properties fo:font-weight="700" fo:color="#165aa7"/></style:style></office:automatic-styles><office:body><office:text>)";
  if (body)
    xml += "<text:p>Before</text:p>";
  xml += "<table:table table:name=\"Outer\"><table:table-column "
         "table:number-columns-repeated=\"3\"/><table:table-row";
  if (repeated)
    xml += " table:number-rows-repeated=\"3\"";
  xml += " xml:id=\"row\"><table:table-cell table:style-name=\"Cell\"";
  if (repeated)
    xml += " table:number-columns-repeated=\"3\"";
  xml += " xml:id=\"cell\" office:value-type=\"string\" "
         "office:string-value=\"stale\">";
  if (nested)
    xml += "<text:p>Outer</text:p><table:table "
           "table:name=\"Inner\"><table:table-column "
           "table:number-columns-repeated=\"2\"/><table:table-row "
           "table:number-rows-repeated=\"2\"><table:table-cell "
           "table:number-columns-repeated=\"2\" "
           "xml:id=\"inner\"/></table:table-row></table:table><text:p>Tail</"
           "text:p>";
  xml += "</table:table-cell></table:table-row></table:table>";
  if (body)
    xml += "<text:p>After</text:p>";
  xml += "</office:text></office:body></office:document-content>";
  p.xml = read_document_xml(xml.toUtf8());
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
TextSelection caret(const WordsDisplayParagraph &p) {
  TextSelection s{p.begin, p.begin};
  s.cells = p.cells;
  s.anchor_cells = s.cells;
  s.cell = s.cells.back();
  return s;
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
void unique_ids(const OdfPackage &p) {
  std::set<Text> ids;
  auto walk = [&](auto &&self, XmlElement e) -> void {
    auto id = e.attributeNS("http://www.w3.org/XML/1998/namespace", "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        self(self, n);
  };
  walk(walk, p.xml.documentElement());
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("words-empty-cell-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  for (const auto &[name, repeated, nested, body] :
       {std::tuple{"independent", false, false, true},
        std::tuple{"repeated", true, false, true},
        std::tuple{"nested", true, true, true},
        std::tuple{"table-only", true, false, false},
        std::tuple{"distinct-sources", true, false, true},
        std::tuple{"after-last", false, false, true}}) {
    auto p = fixture(repeated, nested, body);
    if (Text(name) == "distinct-sources") {
      auto cell = p.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
      cell.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
      auto other = cell.cloneNode(true);
      other.removeAttributeNS(TABLE, "number-columns-repeated");
      other.setAttributeNS("http://www.w3.org/XML/1998/namespace", "xml:id",
                           "other");
      cell.parentNode().appendChild(other);
    }
    if (Text(name) == "after-last") {
      auto tail = children(p.body(), TEXT).back();
      auto table = children(p.body(), TABLE)[0];
      p.body().insertBefore(tail, table);
    }
    auto path = Text((dir / (std::string(name) + "-source.odt")).string());
    p.save(path);
    TextDocument doc;
    doc.load(path);
    const auto original = doc.style_snapshot().xml.toByteArray();
    const auto before = doc.text();
    auto flow = doc.flow();
    auto map = WordsDisplayMap(flow);
    std::vector<WordsDisplayParagraph> empty;
    for (const auto &record : map.paragraphs())
      if (record.paragraph >= flow.paragraphs.size())
        empty.push_back(record);
    const size_t count = nested ? 36 : repeated ? 9 : 1;
    assert(empty.size() == count);
    assert(flow.empty_cells.size() ==
           (Text(name) == "distinct-sources" ? 2 : 1));
    TextEditMap edit;
    edit.reset(flow);
    assert(edit.text() == before);
    for (const auto &record : empty) {
      assert(edit.virtual_paragraph(record.paragraph));
      assert(edit.global(record.paragraph, 0) == record.begin);
      assert(record.length == 0);
    }
    NativeTextShaper shaper("monospace", 12);
    auto pages = paginate_text(flow, shaper);
    std::set<std::vector<std::array<size_t, 5>>> displayed;
    for (const auto &page : pages.pages)
      for (const auto &line : page.lines)
        if (edit.virtual_paragraph(line.paragraph)) {
          assert(text_line_carets(line).size() >= 1);
          std::vector<TextCellAddress> path;
          for (auto c = line.cell_context; c; c = c->nested)
            path.push_back(c->address);
          auto record =
              std::find_if(empty.begin(), empty.end(),
                           [&](const auto &p) { return p.cells == path; });
          assert(record != empty.end() && record->paragraph == line.paragraph);
          std::vector<std::array<size_t, 5>> key;
          for (auto c = line.cell_context; c; c = c->nested)
            key.push_back({c->address.table, c->address.row, c->address.column,
                           c->address.rows, c->address.columns});
          displayed.insert(key);
        }
    assert(displayed.size() == count);
    auto s = caret(empty[count / 2]);
    assert(doc.format_info(s).bold == true);
    assert(doc.style_snapshot().xml.toByteArray() == original);
    auto prepared = doc.prepare_cell_edit(s);
    assert(prepared && prepared->collapsed() && doc.editing());
    assert(doc.end_edit(*prepared) == s);
    assert(doc.text() == before &&
           doc.style_snapshot().xml.toByteArray() == original);
    assert(!doc.can_undo() && !doc.modified());
    prepared = doc.prepare_cell_edit(s);
    doc.replace(prepared->cursor, 0, "preedit");
    doc.cancel_edit();
    assert(doc.end_edit() == s);
    assert(doc.style_snapshot().xml.toByteArray() == original &&
           !doc.can_undo());
    prepared = doc.prepare_cell_edit(s);
    doc.replace(prepared->cursor, 0, "Český\nsecond");
    auto next = *prepared;
    next.anchor = next.cursor = prepared->cursor + 12;
    assert(!doc.end_edit(next));
    const auto typed = doc.style_snapshot();
    unique_ids(typed);
    assert(typed.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
    assert(doc.modified() && doc.can_undo());
    auto updated_flow = doc.flow();
    auto after = WordsDisplayMap(updated_flow);
    size_t remaining = 0, changed = 0;
    for (const auto &record : after.paragraphs()) {
      if (record.paragraph >= updated_flow.paragraphs.size())
        ++remaining;
      else if (record.cells == next.cells) {
        ++changed;
        assert(
            read_words_format(typed, record.begin, record.begin + record.length)
                .bold == true);
      }
    }
    assert(remaining == count - 1 && changed == 2);
    for (const auto &a : next.cells) {
      const auto &t = updated_flow.tables[a.table];
      for (const auto &c : t.cells)
        if (c.row == a.row && c.column == a.column)
          assert(!c.source.hasAttributeNS(OFFICE, "string-value"));
    }
    auto typed_path = Text((dir / (std::string(name) + "-typed.odt")).string());
    doc.save(typed_path);
    assert(doc.undo() == s);
    assert(doc.style_snapshot().xml.toByteArray() == original &&
           doc.text() == before);
    assert(doc.redo() == next);
    assert(doc.style_snapshot().xml.toByteArray() == typed.xml.toByteArray());
    TextDocument reopened;
    reopened.load(typed_path);
    assert(reopened.text() == doc.text());
  }
  // Virtual cells have separate display identity even at identical scalar
  // offsets.
  auto hidden = fixture(true, false);
  auto style = hidden.xml.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", "Hidden");
  style.setAttributeNS(STYLE, "style:family", "table");
  auto props = hidden.xml.createElementNS(STYLE, "style:table-properties");
  props.setAttributeNS(TABLE, "table:display", "false");
  style.appendChild(props);
  child(hidden.xml.documentElement(), OFFICE, "automatic-styles")
      .appendChild(style);
  hidden.xml.elementsByTagNameNS(TABLE, "table")[0].setAttributeNS(
      TABLE, "table:style-name", "Hidden");
  assert(WordsDisplayMap(read_text_flow(hidden)).paragraphs().size() == 2);
  auto p = fixture(true, false);
  auto path = Text((dir / "range-source.odt").string());
  p.save(path);
  TextDocument range;
  range.load(path);
  auto map = WordsDisplayMap(range.flow());
  auto s = caret(map.paragraphs()[2]);
  s.anchor_cells = map.paragraphs()[1].cells;
  assert(!s.collapsed() && range.selected_text(s) == "\n");
  const auto original = range.style_snapshot().xml.toByteArray();
  auto prepared = range.prepare_cell_edit(s);
  range.accept_prepared_selection();
  assert(range.end_edit(*prepared) == s);
  assert(range.style_snapshot().xml.toByteArray() == original &&
         !range.can_undo());
  prepared = range.prepare_cell_edit(s);
  range.replace(prepared->cursor, 0, "Range");
  range.end_edit(TextSelection{prepared->cursor + 5, prepared->cursor + 5});
  range.save(Text((dir / "range-typed.odt").string()));
  assert(range.undo() == s &&
         range.style_snapshot().xml.toByteArray() == original);
  for (const auto &bad : {"numeric", "object", "stale"}) {
    auto invalid = fixture(true, false);
    auto cell = invalid.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
    if (Text(bad) == "numeric") {
      cell.setAttributeNS(OFFICE, "office:value-type", "float");
      cell.setAttributeNS(OFFICE, "office:value", "3");
    } else if (Text(bad) == "object")
      cell.appendChild(invalid.xml.createElementNS(TEXT, "text:section"));
    invalid.save(path);
    TextDocument doc;
    doc.load(path);
    auto f = doc.flow();
    auto m = WordsDisplayMap(f);
    auto selection = caret(m.paragraphs()[2]);
    if (Text(bad) == "stale")
      selection.anchor = selection.cursor = 0;
    auto before = doc.style_snapshot().xml.toByteArray();
    rejects([&] { doc.prepare_cell_edit(selection); });
    doc.end_edit();
    assert(!doc.editing() && !doc.can_undo() &&
           doc.style_snapshot().xml.toByteArray() == before);
  }
  std::cout
      << "PASS virtual empty cell layout, repeated/nested isolation, typing, "
         "breaks, cancellation, rejection, ODF and exact history\n";
}
