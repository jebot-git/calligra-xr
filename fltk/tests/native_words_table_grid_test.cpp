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
using Action = WordsTableGridAction;
template <class F> void rejects(F call) {
  bool failed = false;
  try {
    call();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
OdfPackage package(const std::string &table) {
  OdfPackage p(Kind::Words);
  auto xml =
      std::string(
          R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2" office:version="1.3"><office:automatic-styles><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-family="DejaVu Sans" fo:font-size="12pt"/></style:style><style:style style:name="Cell" style:family="table-cell"><style:table-cell-properties fo:border="1pt solid #808080" fo:padding="2pt"/></style:style></office:automatic-styles><office:body><office:text><text:p>Before</text:p>)XML") +
      table +
      "<text:p>After</text:p></office:text></office:body></"
      "office:document-content>";
  p.xml = read_document_xml(Bytes(xml));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
std::string cell(const std::string &text) {
  return "<table:table-cell><text:p text:style-name=\"Body\">" + text +
         "</text:p></table:table-cell>";
}
std::string row(const std::string &a, const std::string &b,
                const std::string &c) {
  return "<table:table-row>" + cell(a) + cell(b) + cell(c) +
         "</table:table-row>";
}
OdfPackage ordinary() {
  return package(
      "<table:table "
      "table:name=\"Grid\"><table:table-header-columns><table:table-column "
      "table:default-cell-style-name=\"Cell\"/></"
      "table:table-header-columns><table:table-column "
      "table:number-columns-repeated=\"2\" "
      "table:default-cell-style-name=\"Cell\"/><table:table-header-rows>" +
      row("H1", "H2", "H3") +
      "</table:table-header-rows><table:table-row-group>" +
      row("A1", "A2", "A3") + row("B1", "B2", "B3") +
      "</table:table-row-group></table:table>");
}
OdfPackage merged(bool hidden = false) {
  return package(
      std::string("<table:table table:name=\"Merged\"><table:table-column "
                  "table:number-columns-repeated=\"3\" "
                  "table:default-cell-style-name=\"Cell\"/"
                  "><table:table-row><table:table-cell "
                  "table:number-columns-spanned=\"2\" "
                  "table:number-rows-spanned=\"3\" xml:id=\"merged\"><text:p "
                  "xml:id=\"merged-p\">Merged</text:p></"
                  "table:table-cell><table:covered-table-cell/>") +
      cell("C0") + "</table:table-row><table:table-row>" +
      (hidden ? "<table:covered-table-cell xml:id=\"hidden-cell\"><text:p "
                "xml:id=\"hidden-p\">hidden</text:p></"
                "table:covered-table-cell><table:covered-table-cell/>"
              : "<table:covered-table-cell "
                "table:number-columns-repeated=\"2\"/>") +
      cell("C1") +
      "</table:table-row><table:table-row><table:covered-table-cell "
      "table:number-columns-repeated=\"2\"/>" +
      cell("C2") + "</table:table-row>" + row("D0", "D1", "D2") +
      "</table:table>");
}
OdfPackage repeated() {
  return package(
      R"XML(<table:table table:name="Repeat"><table:table-column table:number-columns-repeated="3" xml:id="columns"/><table:table-row table:number-rows-repeated="3" xml:id="row"><table:table-cell table:number-columns-repeated="3" xml:id="cell"><text:p xml:id="repeated-p" text:style-name="Body">Repeated</text:p><text:list xml:id="list"><text:list-item><text:p>One</text:p></text:list-item></text:list><text:list text:continue-list="list"><text:list-item><text:p>Continued</text:p></text:list-item></text:list><table:table table:name="Nested"><table:table-column/><table:table-row><table:table-cell><text:p>Nested data</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table>)XML");
}
size_t table_id(const TextFlow &flow, const Text &name) {
  for (size_t i = 0; i < flow.tables.size(); ++i)
    if (flow.tables[i].name == name)
      return i;
  throw std::runtime_error("Missing test table");
}
const TextTableCell &at(const TextTable &t, size_t row, size_t col) {
  for (const auto &c : t.cells)
    if (c.row <= row && row < c.row + c.rows && c.column <= col &&
        col < c.column + c.columns)
      return c;
  throw std::runtime_error("Missing test cell");
}
Text first(XmlElement source) {
  if (source.namespaceURI() == TEXT &&
      (source.localName() == "p" || source.localName() == "h"))
    return plain_text(source);
  for (auto e = source.firstChild(); !e.isNull(); e = e.nextSibling())
    if (e.isElement()) {
      if (e.namespaceURI() == TEXT &&
          (e.localName() == "p" || e.localName() == "h"))
        return plain_text(e);
      auto result = first(e);
      if (!result.empty())
        return result;
    }
  return {};
}
WordsTableEdit change(const OdfPackage &p, const Text &name, size_t row,
                      size_t col, Action action) {
  auto f = read_text_flow(p);
  auto index = table_id(f, name);
  const auto &c = at(f.tables[index], row, col);
  return edit_words_table_grid(p, {index, c.row, c.column, c.rows, c.columns},
                               action);
}
void unique(const OdfPackage &p) {
  std::set<Text> ids, names;
  std::function<void(XmlElement)> walk = [&](XmlElement e) {
    auto id = e.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    if (e.namespaceURI() == TABLE && e.localName() == "table") {
      auto name = e.attributeNS(TABLE, "name");
      assert(name.empty() || names.insert(name).second);
    }
    for (auto c = e.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        walk(c);
  };
  walk(p.xml.documentElement());
  for (auto list : p.xml.elementsByTagNameNS(TEXT, "list"))
    if (list.hasAttributeNS(TEXT, "continue-list"))
      assert(ids.contains(list.attributeNS(TEXT, "continue-list")));
}
Text cell_text(const OdfPackage &p, const Text &name, size_t row, size_t col) {
  auto f = read_text_flow(p);
  return first(at(f.tables[table_id(f, name)], row, col).source);
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-table-grid-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto save = [&](OdfPackage p, const char *name) {
    unique(p);
    p.save(Text((dir / (std::string(name) + ".odt")).string()));
  };
  auto source = ordinary();
  save(source, "source");
  auto f = read_text_flow(source);
  assert(f.tables[0].rows[0].source.isElement() &&
         f.tables[0].cells[0].source.isElement());
  assert(!words_table_cell_at(source, 0));
  assert(words_table_cell_at(source, 7));
  auto inserted = change(source, "Grid", 1, 0, Action::RowBefore);
  f = read_text_flow(inserted.document);
  assert(f.tables[0].rows.size() == 4 &&
         first(at(f.tables[0], 1, 0).source).empty());
  assert(cell_text(inserted.document, "Grid", 2, 0) == "A1");
  save(inserted.document, "row-before");
  auto header = change(source, "Grid", 0, 0, Action::RowAfter);
  f = read_text_flow(header.document);
  assert(f.tables[0].rows[0].header && f.tables[0].rows[1].header &&
         !f.tables[0].rows[2].header);
  save(header.document, "header-after");
  auto columns = change(source, "Grid", 1, 1, Action::ColumnBefore);
  f = read_text_flow(columns.document);
  assert(f.tables[0].columns.size() == 4 &&
         cell_text(columns.document, "Grid", 1, 1).empty() &&
         cell_text(columns.document, "Grid", 1, 2) == "A2");
  save(columns.document, "column-before");
  auto after = change(source, "Grid", 1, 2, Action::ColumnAfter);
  assert(cell_text(after.document, "Grid", 1, 3).empty());
  save(after.document, "column-after");
  auto removed = change(source, "Grid", 1, 0, Action::DeleteRow);
  assert(cell_text(removed.document, "Grid", 1, 0) == "B1");
  save(removed.document, "row-deleted");
  auto no_header = change(source, "Grid", 0, 0, Action::DeleteRow);
  f = read_text_flow(no_header.document);
  assert(!f.tables[0].rows[0].header);
  save(no_header.document, "header-row-deleted");
  auto no_header_col = change(source, "Grid", 1, 0, Action::DeleteColumn);
  f = read_text_flow(no_header_col.document);
  assert(f.tables[0].columns.size() == 2 && !f.tables[0].columns[0].header);
  assert(cell_text(no_header_col.document, "Grid", 1, 0) == "A2");
  save(no_header_col.document, "header-column-deleted");
  auto merge = merged();
  save(merge, "merged-source");
  auto span_row = change(merge, "Merged", 1, 2, Action::RowBefore);
  f = read_text_flow(span_row.document);
  assert(at(f.tables[0], 0, 0).rows == 4 &&
         cell_text(span_row.document, "Merged", 2, 2) == "C1");
  save(span_row.document, "span-row-inserted");
  auto span_col = change(merge, "Merged", 3, 1, Action::ColumnBefore);
  f = read_text_flow(span_col.document);
  assert(at(f.tables[0], 0, 0).columns == 3 &&
         cell_text(span_col.document, "Merged", 0, 3) == "C0");
  save(span_col.document, "span-column-inserted");
  auto shrink_col = change(merge, "Merged", 0, 0, Action::DeleteColumn);
  f = read_text_flow(shrink_col.document);
  assert(at(f.tables[0], 0, 0).columns == 1 && at(f.tables[0], 0, 0).rows == 3);
  assert(cell_text(shrink_col.document, "Merged", 0, 1) == "C0");
  save(shrink_col.document, "span-column-deleted");
  auto promote = change(merge, "Merged", 0, 0, Action::DeleteRow);
  f = read_text_flow(promote.document);
  assert(at(f.tables[0], 0, 0).rows == 2 &&
         first(at(f.tables[0], 0, 0).source) == "Merged");
  assert(cell_text(promote.document, "Merged", 0, 2) == "C1");
  save(promote.document, "span-anchor-promoted");
  auto shrink_row = change(merge, "Merged", 1, 2, Action::DeleteRow);
  f = read_text_flow(shrink_row.document);
  assert(at(f.tables[0], 0, 0).rows == 2 &&
         cell_text(shrink_row.document, "Merged", 1, 2) == "C2");
  save(shrink_row.document, "span-row-deleted");
  auto hidden = change(merged(true), "Merged", 0, 0, Action::DeleteRow);
  bool kept = false;
  for (auto e : hidden.document.xml.elementsByTagNameNS(TEXT, "p"))
    if (plain_text(e) == "hidden")
      kept = true;
  assert(kept);
  save(hidden.document, "hidden-covered-retained");
  auto horizontal = package(
      R"XML(<table:table table:name="Horizontal"><table:table-column table:number-columns-repeated="3"/><table:table-row><table:table-cell table:number-columns-spanned="2"><text:p>Anchor</text:p></table:table-cell><table:covered-table-cell xml:id="covered-horizontal"><text:p xml:id="hidden-horizontal">Hidden right</text:p></table:covered-table-cell>)XML" +
      cell("Right") + "</table:table-row></table:table>");
  auto horizontal_deleted =
      change(horizontal, "Horizontal", 0, 0, Action::DeleteColumn);
  f = read_text_flow(horizontal_deleted.document);
  assert(f.tables[0].columns.size() == 2 && at(f.tables[0], 0, 0).columns == 1);
  assert(plain_text(at(f.tables[0], 0, 0).source).contains("Hidden right"));
  assert(cell_text(horizontal_deleted.document, "Horizontal", 0, 1) == "Right");
  save(horizontal_deleted.document, "horizontal-covered-retained");
  auto covered =
      horizontal.xml.elementsByTagNameNS(TABLE, "covered-table-cell")[0];
  covered.setAttributeNS(OFFICE, "office:value-type", "float");
  covered.setAttributeNS(OFFICE, "office:value", "17");
  auto horizontal_before = horizontal.xml.toByteArray();
  rejects(
      [&] { change(horizontal, "Horizontal", 0, 0, Action::DeleteColumn); });
  assert(horizontal.xml.toByteArray() == horizontal_before);
  auto styled = package(
      R"XML(<table:table table:name="Styled"><table:table-column table:number-columns-repeated="2"/><table:table-row table:default-cell-style-name="Red"><table:table-cell table:number-rows-spanned="2"><text:p text:style-name="Body">Styled anchor</text:p></table:table-cell>)XML" +
      cell("top") +
      R"XML(</table:table-row><table:table-row table:default-cell-style-name="Green"><table:covered-table-cell/>)XML" +
      cell("bottom") + "</table:table-row></table:table>");
  auto add_style = [&](OdfPackage &p, const Text &name, const Text &color) {
    auto group = child(p.xml.documentElement(), OFFICE, "automatic-styles");
    auto style = p.xml.createElementNS(
        "urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style:style");
    style.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                         "style:name", name);
    style.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                         "style:family", "table-cell");
    auto props =
        p.xml.createElementNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                              "style:text-properties");
    props.setAttributeNS(
        "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
        "fo:color", color);
    style.appendChild(props);
    group.appendChild(style);
  };
  add_style(styled, "Red", "#ff0000");
  add_style(styled, "Green", "#00ff00");
  auto styled_promoted = change(styled, "Styled", 0, 0, Action::DeleteRow);
  f = read_text_flow(styled_promoted.document);
  assert(f.tables[0].cells[0].style_name == "Red");
  save(styled_promoted.document, "promoted-row-style-retained");
  auto neutral = styled;
  neutral.xml = read_document_xml(styled.xml.toByteArray());
  neutral.xml.elementsByTagNameNS(TABLE, "table-row")[0].removeAttributeNS(
      TABLE, "default-cell-style-name");
  auto neutral_promoted = change(neutral, "Styled", 0, 0, Action::DeleteRow);
  f = read_text_flow(neutral_promoted.document);
  assert(!f.tables[0].cells[0].style_name.empty() &&
         f.tables[0].cells[0].style_name != "Green");
  save(neutral_promoted.document, "promoted-neutral-style-retained");
  auto col_style = package(
      R"XML(<table:table table:name="ColumnStyle"><table:table-column table:default-cell-style-name="Red"/><table:table-column table:default-cell-style-name="Green"/><table:table-row><table:table-cell table:number-columns-spanned="2"><text:p text:style-name="Body">Styled merge</text:p></table:table-cell><table:covered-table-cell/></table:table-row></table:table>)XML");
  add_style(col_style, "Red", "#ff0000");
  add_style(col_style, "Green", "#00ff00");
  auto styled_column =
      change(col_style, "ColumnStyle", 0, 0, Action::DeleteColumn);
  f = read_text_flow(styled_column.document);
  assert(f.tables[0].cells[0].style_name == "Red");
  save(styled_column.document, "deleted-anchor-column-style-retained");
  auto repeat = repeated();
  save(repeat, "repeat-source");
  f = read_text_flow(repeat);
  auto outer = table_id(f, "Repeat");
  assert(f.tables[outer].rows[2].source_instance == 2 &&
         at(f.tables[outer], 0, 2).source_instance == 2);
  auto repeat_deleted = change(repeat, "Repeat", 1, 0, Action::DeleteRow);
  f = read_text_flow(repeat_deleted.document);
  assert(f.tables[table_id(f, "Repeat")].rows.size() == 2);
  save(repeat_deleted.document, "repeat-row-deleted");
  auto repeat_inserted = change(repeat, "Repeat", 1, 0, Action::RowBefore);
  f = read_text_flow(repeat_inserted.document);
  outer = table_id(f, "Repeat");
  assert(f.tables[outer].rows.size() == 4 &&
         cell_text(repeat_inserted.document, "Repeat", 1, 0).empty() &&
         cell_text(repeat_inserted.document, "Repeat", 2, 0) == "Repeated");
  save(repeat_inserted.document, "repeat-row-split");
  auto repeat_columns = change(repeat, "Repeat", 0, 1, Action::ColumnBefore);
  f = read_text_flow(repeat_columns.document);
  outer = table_id(f, "Repeat");
  assert(f.tables[outer].columns.size() == 4 &&
         cell_text(repeat_columns.document, "Repeat", 0, 1).empty() &&
         cell_text(repeat_columns.document, "Repeat", 0, 2) == "Repeated");
  save(repeat_columns.document, "repeat-cell-split");
  auto repeated_spans = package(
      R"XML(<table:table table:name="RepeatedSpans"><table:table-column table:number-columns-repeated="3"/><table:table-row><table:table-cell table:number-columns-repeated="3" table:number-rows-spanned="2" xml:id="repeated-anchor"><text:p xml:id="anchor-text">Anchor</text:p></table:table-cell></table:table-row><table:table-row><table:covered-table-cell table:number-columns-repeated="3" xml:id="covered-run"><text:p xml:id="covered-text">Hidden</text:p></table:covered-table-cell></table:table-row></table:table>)XML");
  auto promoted_repeats =
      change(repeated_spans, "RepeatedSpans", 0, 0, Action::DeleteRow);
  f = read_text_flow(promoted_repeats.document);
  for (const auto &c : f.tables[0].cells) {
    size_t anchors = 0, hidden_count = 0;
    for (auto paragraph : c.source.elementsByTagNameNS(TEXT, "p")) {
      anchors += plain_text(paragraph) == "Anchor";
      hidden_count += plain_text(paragraph) == "Hidden";
    }
    assert(anchors == 1 && hidden_count == 1);
  }
  save(promoted_repeats.document, "repeated-anchors-promoted");
  auto nested_flow = read_text_flow(repeat);
  auto repeat_index = table_id(nested_flow, "Repeat"),
       nested_index = table_id(nested_flow, "Nested");
  TextCellAddress outer_address{repeat_index, 1, 1, 1, 1},
      inner_address{nested_index, 0, 0, 1, 1};
  auto scoped = edit_words_table_grid(repeat, inner_address, Action::RowBefore,
                                      {outer_address, inner_address});
  f = read_text_flow(scoped.document);
  auto scoped_outer = table_id(f, "Repeat");
  for (const auto &c : f.tables[scoped_outer].cells) {
    size_t nested_rows = 0;
    for (const auto &block : c.blocks)
      if (block.kind == TextBlock::Table)
        nested_rows = f.tables[block.index].rows.size();
    assert(nested_rows == (c.row == 1 && c.column == 1 ? 2 : 1));
  }
  assert(scoped.cells.size() == 2 && scoped.cells[0].row == 1 &&
         scoped.cells[0].column == 1);
  save(scoped.document, "nested-repeated-row-isolated");
  auto local_column = change(repeat, "Repeat", 1, 0, Action::ColumnAfter);
  f = read_text_flow(local_column.document);
  auto local_outer = table_id(f, "Repeat");
  auto focused_source = at(f.tables[local_outer], 1, 1).source;
  assert(focused_source != at(f.tables[local_outer], 0, 1).source);
  assert(focused_source != at(f.tables[local_outer], 2, 1).source);
  repeat.save(Text((dir / "local-source.odt").string()));
  TextDocument local_book;
  local_book.load(Text((dir / "local-source.odt").string()));
  TextSelection local_previous{0, 0},
      local_next{local_column.cursor, local_column.cursor, local_column.cell,
                 local_column.cells};
  assert(local_book.commit_structure(local_column.document, local_previous,
                                     local_next, local_book.revision()));
  local_book.replace(local_next.cursor, 0, "x");
  auto local_snapshot = local_book.style_snapshot();
  assert(cell_text(local_snapshot, "Repeat", 1, 1) == "x");
  assert(cell_text(local_snapshot, "Repeat", 0, 1).empty() &&
         cell_text(local_snapshot, "Repeat", 2, 1).empty());
  save(local_snapshot, "new-repeated-column-typing-local");
  // Deleting the only row/column removes the addressed outer table, including
  // its nested table, rather than resolving the inner table at its first text.
  auto only =
      package("<table:table "
              "table:name=\"Outer\"><table:table-column/"
              "><table:table-row><table:table-cell><table:table "
              "table:name=\"Inner\"><table:table-column/><table:table-row>" +
              cell("Inside") +
              "</table:table-row></table:table></table:table-cell></"
              "table:table-row></table:table>");
  auto last = change(only, "Outer", 0, 0, Action::DeleteRow);
  assert(read_text_flow(last.document).tables.empty());
  save(last.document, "last-row-deleted");
  last = change(only, "Outer", 0, 0, Action::DeleteColumn);
  assert(read_text_flow(last.document).tables.empty());
  save(last.document, "last-column-deleted");
  // Structural source and saved-state history is exact through undo/redo.
  source.save(Text((dir / "history-source.odt").string()));
  TextDocument book;
  book.load(Text((dir / "history-source.odt").string()));
  auto before = book.style_snapshot().xml.toByteArray();
  auto result = change(book.style_snapshot(), "Grid", 1, 0, Action::RowBefore);
  auto cursor = book.text().find(Text("A1"));
  TextSelection previous{cursor, cursor, TextCellAddress{0, 1, 0, 1, 1}},
      next{result.cursor, result.cursor, result.cell, result.cells};
  previous.cells = {*previous.cell};
  assert(
      book.commit_structure(result.document, previous, next, book.revision()));
  auto changed = book.style_snapshot().xml.toByteArray();
  assert(book.undo() == previous && !book.modified() &&
         book.style_snapshot().xml.toByteArray() == before);
  assert(book.redo() == next &&
         book.style_snapshot().xml.toByteArray() == changed);
  auto stale = TextCellAddress{0, 1, 0, 2, 1};
  rejects([&] { edit_words_table_grid(source, stale, Action::RowBefore); });
  auto bad = source;
  bad.xml = read_document_xml(source.xml.toByteArray());
  auto target = bad.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  target.setAttributeNS(TABLE, "table:formula", "of:=[.A1]");
  auto bytes = bad.xml.toByteArray();
  rejects([&] { change(bad, "Grid", 1, 0, Action::RowBefore); });
  assert(bad.xml.toByteArray() == bytes);
  std::cout << "PASS native Words row/column editing, groups, spans, "
               "repetitions, IDs/references and structural history\n";
}
