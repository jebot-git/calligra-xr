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

void narrow_page(OdfPackage &p, double body) {
  auto f = read_text_flow(p);
  auto style = f.masters.at(f.first_master);
  auto doc = read_document_xml(p.entry("styles.xml"));
  for (auto prop : doc.elementsByTagNameNS(STYLE, "page-layout-properties"))
    prop.setAttributeNS(FO, "fo:page-width",
                        Text::number(body + style.left + style.right, 'g', 17) +
                            "pt");
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
}
} // namespace

int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-cell-geometry-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto save = [&](OdfPackage p, const char *name) {
    unique(p);
    p.save(Text((dir / (std::string(name) + ".odt")).string()));
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
  };
  auto p = geometry();
  narrow_page(p, 250);
  save(p, "source");
  auto a = address(p, "Grid", 1, 1);
  auto changed = subdivide_words_table_cell(p, a, 2, 3);
  same_rect(box(p, 1, 0, 250), box(changed.document, 1, 0, 250));
  same_rect(box(p, 1, 2, 250), box(changed.document, 1, 4, 250));
  save(changed.document, "subdivided");
  // Equal physical cuts retain all old boundaries while inserting new ones.
  auto wide = geometry();
  save(wide, "wide-source");
  auto merged_result =
      merge_words_table_cells(wide, address(wide, "Grid", 1, 0), 1, 2);
  save(merged_result.document, "unequal-source");
  auto equal =
      subdivide_words_table_cell(merged_result.document, *merged_result.cell, 1,
                                 3, merged_result.cells, 400., true);
  save(equal.document, "equal-columns");
  auto ef = read_text_flow(equal.document);
  size_t count = 0;
  double sum = 0;
  for (const auto &cell : ef.tables[0].cells)
    if (cell.row == 1 && cell.column < 4) {
      auto r = box(equal.document, cell.row, cell.column, 400);
      near(r.width, 280. / 3);
      sum += r.width;
      ++count;
    }
  assert(count == 3);
  near(sum, 280);
  same_rect(box(wide, 0, 0, 400), box(equal.document, 0, 0, 400));
  same_rect(box(wide, 1, 2, 400), box(equal.document, 1, 4, 400));
  // Physical rows grow uniformly when narrow parts need additional text height.
  auto auto_rows = geometry();
  for (auto style : auto_rows.xml.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "Row")
      child(style, STYLE, "table-row-properties")
          .removeAttributeNS(STYLE, "row-height");
  save(auto_rows, "auto-source");
  auto tall = subdivide_words_table_cell(
      auto_rows, address(auto_rows, "Grid", 1, 1), 2, 3, {}, 400., true);
  save(tall.document, "equal-auto-rows");
  auto top = box(tall.document, 1, 1, 400),
       bottom = box(tall.document, 2, 1, 400);
  near(top.height, bottom.height);
  assert(top.height >= box(auto_rows, 1, 1, 400).height - 1e-7);
  // Repeated ancestors are localized before resolving nested content widths.
  auto nested_source = nested();
  save(nested_source, "nested-source");
  std::vector<TextCellAddress> path{address(nested_source, "Outer", 1, 2),
                                    address(nested_source, "Inner", 0, 0)};
  auto nested_equal = subdivide_words_table_cell(nested_source, path.back(), 2,
                                                 3, path, 400., true);
  save(nested_equal.document, "nested-equal");
  assert(nested_equal.cells.size() == 2 && nested_equal.cells[0].row == 1 &&
         nested_equal.cells[0].column == 2);
  auto nf = read_text_flow(nested_equal.document);
  size_t leaves = 0, changed_tables = 0;
  for (const auto &cell : nf.blocks[1].table->cells)
    for (const auto &b : cell.blocks)
      if (b.kind == TextBlock::Table) {
        leaves += b.table->cells.size();
        changed_tables += b.table->cells.size() == 9;
      }
  assert(leaves == 41 && changed_tables == 1);
  // An existing unequal merged row grid also divides into equal-height parts.
  auto rowspan = merged(true);
  auto style_group =
      child(rowspan.xml.documentElement(), OFFICE, "automatic-styles");
  size_t index = 0;
  for (auto row_node : rowspan.xml.elementsByTagNameNS(TABLE, "table-row")) {
    auto name = "Height" + Text::number(index);
    auto style = rowspan.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:name", name);
    style.setAttributeNS(STYLE, "style:family", "table-row");
    auto props =
        rowspan.xml.createElementNS(STYLE, "style:table-row-properties");
    props.setAttributeNS(STYLE, "style:row-height",
                         index == 0   ? "40pt"
                         : index == 1 ? "80pt"
                                      : "60pt");
    style.appendChild(props);
    style_group.appendChild(style);
    row_node.setAttributeNS(TABLE, "table:style-name", name);
    ++index;
  }
  save(rowspan, "unequal-rows-source");
  auto row_equal = subdivide_words_table_cell(
      rowspan, address(rowspan, "Merged", 0, 0), 3, 1, {}, 400., true);
  save(row_equal.document, "equal-rows");
  auto rf = read_text_flow(row_equal.document);
  count = 0;
  for (const auto &cell : rf.tables[0].cells)
    if (cell.column == 0 && cell.row < 4) {
      near(box(row_equal.document, cell.row, 0, 400).height, 60);
      ++count;
    }
  assert(count == 3);
  // A cut at a hidden row boundary must start the next part after the
  // zero-height slot, while retaining that original logical slot and its data.
  auto zero = rowspan;
  zero.xml = read_document_xml(rowspan.xml.toByteArray());
  auto zero_rows = zero.xml.elementsByTagNameNS(TABLE, "table-row");
  zero_rows[1].setAttributeNS(TABLE, "table:visibility", "collapse");
  for (auto style : zero.xml.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "Height2")
      child(style, STYLE, "table-row-properties")
          .setAttributeNS(STYLE, "style:row-height", "80pt");
  save(zero, "zero-source");
  auto zero_result = subdivide_words_table_cell(
      zero, address(zero, "Merged", 0, 0), 3, 1, {}, 400., true);
  save(zero_result.document, "zero-equal");
  auto zf = read_text_flow(zero_result.document);
  count = 0;
  for (const auto &cell : zf.tables[0].cells)
    if (cell.column == 0 && cell.row < 4) {
      near(box(zero_result.document, cell.row, 0, 400).height, 40);
      assert(zf.tables[0].rows[cell.row].visible);
      ++count;
    }
  assert(count == 3 && !zf.tables[0].rows[1].visible);
  auto covered_zero =
      package("<table:table "
              "table:name=\"CoveredZero\"><table:table-column/"
              "><table:table-row><table:table-cell "
              "table:number-rows-spanned=\"3\"><text:p>Merged "
              "text</text:p></table:table-cell></"
              "table:table-row><table:table-row><table:covered-table-cell/></"
              "table:table-row><table:table-row><table:covered-table-cell/></"
              "table:table-row></table:table>");
  save(covered_zero, "covered-zero-source");
  auto covered_result = subdivide_words_table_cell(
      covered_zero, address(covered_zero, "CoveredZero", 0, 0), 2, 1, {}, 400.,
      true);
  save(covered_result.document, "covered-zero-equal");
  auto cf = read_text_flow(covered_result.document);
  std::vector<double> heights;
  for (const auto &cell : cf.tables[0].cells)
    heights.push_back(
        box(covered_result.document, cell.row, cell.column, 400).height);
  assert(heights.size() == 2);
  near(heights[0], heights[1]);
  auto micro = geometry();
  for (auto style : micro.xml.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "C1")
      child(style, STYLE, "table-column-properties")
          .setAttributeNS(STYLE, "style:column-width", "100.0000004pt");
  auto micro_merged =
      merge_words_table_cells(micro, address(micro, "Grid", 1, 0), 1, 2);
  save(micro_merged.document, "micro-source");
  auto micro_result =
      subdivide_words_table_cell(micro_merged.document, *micro_merged.cell, 1,
                                 2, micro_merged.cells, 400., true);
  save(micro_result.document, "micro-equal");
  auto mic = read_text_flow(micro_result.document);
  assert(mic.tables[0].columns.size() == 4);
  near(box(micro_result.document, 1, 0, 400).width, 100.0000002);
  near(box(micro_result.document, 1, 2, 400).width, 100.0000002);
  // RTL positions and exact native structural history use the same geometry.
  auto rtl = geometry();
  auto table = read_text_flow(rtl).tables[0].source;
  auto ts = rtl.xml.createElementNS(STYLE, "style:style");
  ts.setAttributeNS(STYLE, "style:name", "RTL");
  ts.setAttributeNS(STYLE, "style:family", "table");
  auto tp = rtl.xml.createElementNS(STYLE, "style:table-properties");
  tp.setAttributeNS(STYLE, "style:writing-mode", "rl-tb");
  ts.appendChild(tp);
  child(rtl.xml.documentElement(), OFFICE, "automatic-styles").appendChild(ts);
  table.setAttributeNS(TABLE, "table:style-name", "RTL");
  narrow_page(rtl, 250);
  auto rtl_result =
      subdivide_words_table_cell(rtl, address(rtl, "Grid", 1, 1), 2, 3);
  same_rect(box(rtl, 1, 0, 250), box(rtl_result.document, 1, 0, 250));
  same_rect(box(rtl, 1, 2, 250), box(rtl_result.document, 1, 4, 250));
  save(rtl_result.document, "rtl-subdivided");
  TextDocument d;
  d.load(Text((dir / "source.odt").string()));
  auto initial = d.style_snapshot();
  TextSelection previous{
      d.text().find(Text("A2")), d.text().find(Text("A2")), a, {a}};
  TextSelection next{changed.cursor, changed.cursor, changed.cell,
                     changed.cells};
  assert(d.commit_structure(changed.document, previous, next, d.revision()));
  assert(d.undo() == previous &&
         d.style_snapshot().xml.toByteArray() == initial.xml.toByteArray() &&
         d.style_snapshot().entry("styles.xml") == initial.entry("styles.xml"));
  assert(d.redo() == next);
  save(d.style_snapshot(), "redone");
  std::cout << "PASS measured cell geometry, equal physical parts, auto-height "
               "fit, RTL, nested repeats and exact history\n";
}
