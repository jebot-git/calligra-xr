// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_table_layout.h"
#include <cmath>
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

const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
TextCellAddress address(const OdfPackage &p, const Text &name, size_t r,
                        size_t c) {
  auto f = read_text_flow(p);
  auto index = table_id(f, name);
  const auto &cell = at(f.tables[index], r, c);
  return {index, cell.row, cell.column, cell.rows, cell.columns};
}
OdfPackage geometry(bool relative = false) {
  auto p = package("<table:table table:name=\"Grid\"><table:table-column "
                   "table:style-name=\"C0\"/><table:table-column "
                   "table:style-name=\"C1\"/><table:table-column "
                   "table:style-name=\"C2\"/><table:table-header-rows>" +
                   row("H1", "H2", "H3") +
                   "</table:table-header-rows><table:table-row-group>" +
                   row("A1", "A2", "A3") + row("B1", "B2", "B3") +
                   "</table:table-row-group></table:table>");
  auto group = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  for (size_t c = 0; c < 3; ++c) {
    auto style = p.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:name", "C" + Text::number(c));
    style.setAttributeNS(STYLE, "style:family", "table-column");
    auto props = p.xml.createElementNS(STYLE, "style:table-column-properties");
    if (relative && c > 0)
      props.setAttributeNS(STYLE, "style:rel-column-width",
                           c == 1 ? "2*" : "1*");
    else
      props.setAttributeNS(STYLE, "style:column-width",
                           c == 0   ? "100pt"
                           : c == 1 ? "180pt"
                                    : "120pt");
    style.appendChild(props);
    group.appendChild(style);
  }
  auto style = p.xml.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", "Row");
  style.setAttributeNS(STYLE, "style:family", "table-row");
  auto props = p.xml.createElementNS(STYLE, "style:table-row-properties");
  props.setAttributeNS(STYLE, "style:row-height", "72pt");
  props.setAttributeNS(FO, "fo:background-color", "#eeeeee");
  style.appendChild(props);
  group.appendChild(style);
  for (auto row : p.xml.elementsByTagNameNS(TABLE, "table-row"))
    row.setAttributeNS(TABLE, "table:style-name", "Row");
  return p;
}
TextMetrics measure(const Text &s, const TextFont &font) {
  return {s.size() * font.size / 2, font.size * .8, font.size * .2};
}
TextRect box(const OdfPackage &p, size_t row, size_t col, double width) {
  auto f = read_text_flow(p);
  auto layout = layout_text_box(f, width, measure);
  for (const auto &cell : layout.cells)
    if (cell.table == 0 && cell.row == row && cell.column == col)
      return cell.rect;
  throw std::runtime_error("Missing geometry cell");
}
void near(double a, double b) {
  if (std::abs(a - b) > 1e-7)
    std::cerr << a << " != " << b << '\n';
  assert(std::abs(a - b) < 1e-7);
}
void same_rect(TextRect a, TextRect b) {
  near(a.x, b.x);
  near(a.y, b.y);
  near(a.width, b.width);
  near(a.height, b.height);
}
OdfPackage nested() {
  return package(
      R"XML(<table:table table:name="Outer"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3"><table:table-cell table:number-columns-repeated="3"><text:p>Outer</text:p><table:table table:name="Inner"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell><text:p>I00</text:p></table:table-cell><table:table-cell><text:p>I01</text:p></table:table-cell></table:table-row><table:table-row><table:table-cell><text:p>I10</text:p></table:table-cell><table:table-cell><text:p>I11</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table>)XML");
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-subdivide-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto save = [&](OdfPackage p, const char *name) {
    unique(p);
    p.save(Text((dir / (std::string(name) + ".odt")).string()));
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
  };
  for (bool relative : {false, true}) {
    auto source = geometry(relative);
    save(source, relative ? "relative-source" : "source");
    auto before = source.xml.toByteArray();
    auto a = address(source, "Grid", 1, 1);
    auto result = subdivide_words_table_cell(source, a, 2, 3);
    save(result.document, relative ? "relative-subdivided" : "subdivided");
    assert(source.xml.toByteArray() == before);
    auto flow = read_text_flow(result.document);
    auto &t = flow.tables[0];
    assert(t.rows.size() == 4 && t.columns.size() == 5 && t.cells.size() == 14);
    assert(result.cell == TextCellAddress({0, 1, 1, 1, 1}));
    for (size_t r = 1; r < 3; ++r)
      for (size_t c = 1; c < 4; ++c) {
        const auto &cell = at(t, r, c);
        assert(cell.row == r && cell.column == c && cell.rows == 1 &&
               cell.columns == 1);
        assert(first(cell.source) == (r == 1 && c == 1 ? Text("A2") : Text()));
      }
    for (double width : {400., 600.}) {
      same_rect(box(source, 0, 1, width), box(result.document, 0, 1, width));
      same_rect(box(source, 1, 0, width), box(result.document, 1, 0, width));
      same_rect(box(source, 1, 2, width), box(result.document, 1, 4, width));
      same_rect(box(source, 2, 1, width), box(result.document, 3, 1, width));
      auto old = box(source, 1, 1, width);
      auto part = box(result.document, 1, 1, width);
      near(part.width, old.width / 3);
      near(part.height, old.height / 2);
    }
    auto no = subdivide_words_table_cell(source, a, 1, 1);
    assert(no.document.xml.toByteArray() == before);
    rejects([&] { subdivide_words_table_cell(source, a, 0, 2); });
    rejects([&] { subdivide_words_table_cell(source, a, 100000, 1024); });
  }

  auto large = geometry(true);
  for (auto style : large.xml.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "C2" ||
        style.attributeNS(STYLE, "name") == "C1")
      child(style, STYLE, "table-column-properties")
          .setAttributeNS(STYLE, "style:rel-column-width",
                          style.attributeNS(STYLE, "name") == "C2" ? "1000000*"
                                                                   : "999998*");
  auto large_result =
      subdivide_words_table_cell(large, address(large, "Grid", 1, 1), 2, 3);
  save(large_result.document, "large-relative-subdivided");
  same_rect(box(large, 1, 2, 600), box(large_result.document, 1, 4, 600));
  auto old = merged(true);
  save(old, "merged-source");
  auto divided =
      subdivide_words_table_cell(old, address(old, "Merged", 0, 0), 2, 3);
  save(divided.document, "merged-subdivided");
  auto mf = read_text_flow(divided.document);
  assert(mf.tables[0].rows.size() == 7 && mf.tables[0].columns.size() == 7);
  assert(divided.cell->rows == 3 && divided.cell->columns == 2);
  auto repeat = repeated();
  save(repeat, "repeated-source");
  auto rd =
      subdivide_words_table_cell(repeat, address(repeat, "Repeat", 1, 1), 2, 2);
  save(rd.document, "repeated-subdivided");
  auto rf = read_text_flow(rd.document);
  assert(rf.tables[table_id(rf, "Repeat")].cells.size() == 12);
  auto n = nested();
  save(n, "nested-source");
  std::vector<TextCellAddress> path{address(n, "Outer", 1, 2),
                                    address(n, "Inner", 0, 0)};
  auto nd = subdivide_words_table_cell(n, path.back(), 2, 3, path);
  save(nd.document, "nested-subdivided");
  assert(nd.cells.size() == 2 && nd.cells[0].row == 1 &&
         nd.cells[0].column == 2);
  auto nf = read_text_flow(nd.document);
  auto outer = nf.blocks[1].table;
  size_t leaves = 0, changed = 0;
  for (const auto &cell : outer->cells)
    for (const auto &b : cell.blocks)
      if (b.kind == TextBlock::Table) {
        leaves += b.table->cells.size();
        changed += b.table->cells.size() == 9;
      }
  assert(leaves == 41 && changed == 1);
  rejects([&] { subdivide_words_table_cell(n, path.back(), 2, 3); });
  auto value = geometry();
  auto va = address(value, "Grid", 1, 1);
  auto target = at(read_text_flow(value).tables[0], 1, 1).source;
  target.setAttributeNS(OFFICE, "office:value-type", "float");
  target.setAttributeNS(OFFICE, "office:value", "3");
  auto vd = subdivide_words_table_cell(value, va, 2, 2);
  save(vd.document, "numeric-subdivided");
  assert(at(read_text_flow(vd.document).tables[0], 1, 1)
             .source.attributeNS(OFFICE, "value") == "3");
  target.setAttributeNS(TABLE, "table:formula", "of:=1+2");
  rejects([&] { subdivide_words_table_cell(value, va, 2, 2); });
  TextDocument d;
  d.load(Text((dir / "source.odt").string()));
  auto initial = d.style_snapshot();
  auto a = address(initial, "Grid", 1, 1);
  TextSelection previous{
      d.text().find(Text("A2")), d.text().find(Text("A2")), a, {a}};
  auto edit = subdivide_words_table_cell(initial, a, 2, 3);
  TextSelection next{edit.cursor, edit.cursor, edit.cell, edit.cells};
  assert(d.commit_structure(edit.document, previous, next, d.revision()));
  auto after = d.style_snapshot().xml.toByteArray();
  d.replace(next.cursor, 0, "X", true, next, next);
  save(d.style_snapshot(), "typed-subdivided");
  assert(d.undo());
  assert(d.undo() == previous &&
         d.style_snapshot().xml.toByteArray() == initial.xml.toByteArray());
  assert(d.redo() == next && d.style_snapshot().xml.toByteArray() == after);
  std::cout << "PASS native Words cell subdivision, preserved neighboring "
               "geometry, repeated/nested scope, values and history\n";
}
