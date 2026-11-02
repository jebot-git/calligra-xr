// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_words_geometry.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
OdfPackage fixture(bool nested = false, bool empty = false) {
  OdfPackage p(Kind::Words);
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto definitions = read_document_xml(Bytes(
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"><office:styles><style:default-style style:family="table-row"><style:table-row-properties style:min-row-height="10pt"/></style:default-style><style:default-style style:family="table-column"><style:table-column-properties style:column-width="0.5in" style:rel-column-width="1*"/></style:default-style><style:style style:name="RelBase" style:family="table"><style:table-properties style:width="500pt" style:rel-width="75%" table:align="left" fo:margin-left="12pt" fo:margin-right="10pt" fo:background-color="#fff9c4"/></style:style><style:style style:name="RowBase" style:family="table-row"><style:table-row-properties style:row-height="20pt" style:min-row-height="18pt" style:use-optimal-row-height="false" fo:keep-together="always" fo:background-color="#e0edf8"/></style:style><style:style style:name="ColBase" style:family="table-column"><style:table-column-properties style:column-width="50pt" style:rel-column-width="2*" style:use-optimal-column-width="true" fo:break-after="auto"/></style:style></office:styles></office:document-styles>)"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  for (auto node :
       children(child(definitions.documentElement(), OFFICE, "styles"), STYLE))
    common.appendChild(styles.importNode(node, true));
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  Text xml =
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2" office:version="1.3"><office:automatic-styles><style:style style:name="Sized" style:family="table" style:parent-style-name="RelBase" style:master-page-name="Default"><style:table-properties style:rel-width="50%" fo:margin-top="7pt"/></style:style><style:style style:name="Rows" style:family="table-row" style:parent-style-name="RowBase"><style:table-row-properties style:min-row-height="24pt" style:use-optimal-row-height="true"/></style:style><style:style style:name="Cols" style:family="table-column" style:parent-style-name="ColBase"><style:table-column-properties style:rel-column-width="4*"/></style:style><style:style style:name="Cell" style:family="table-cell"><style:table-cell-properties fo:padding="3pt" fo:border="0.5pt solid #222222"/></style:style></office:automatic-styles><office:body><office:text><text:p>Before</text:p>)";
  Text leaf =
      R"(<table:table table:name="Grid" table:style-name="Sized"><table:table-header-columns><table:table-column table:style-name="Cols"/></table:table-header-columns><table:table-columns><table:table-column table:style-name="Cols" table:number-columns-repeated="2"/></table:table-columns><table:table-row-group><table:table-row table:style-name="Rows" table:number-rows-repeated="3" xml:id="row"><table:table-cell table:style-name="Cell" table:number-columns-repeated="3" xml:id="cell" office:value-type="string" office:string-value="cached">)";
  if (!empty)
    leaf += "<text:p xml:id=\"para\">Cell</text:p>";
  leaf += "</table:table-cell></table:table-row></table:table-row-group></"
          "table:table>";
  if (nested)
    xml +=
        R"(<table:table table:name="Outer"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3"><table:table-cell table:style-name="Cell" table:number-columns-repeated="3"><text:p>Outer</text:p>)" +
        leaf +
        "<text:p>Tail</text:p></table:table-cell></table:table-row></"
        "table:table>";
  else
    xml += leaf;
  xml += "<text:p>After</text:p></office:text></office:body></"
         "office:document-content>";
  p.xml = read_document_xml(xml.toUtf8());
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-7; }
size_t table_id(const TextFlow &f, const Text &name) {
  for (size_t i = 0; i < f.tables.size(); ++i)
    if (f.tables[i].name == name)
      return i;
  throw std::runtime_error("Missing table");
}
TextSelection point(const TextDocument &d, bool nested, bool empty = false) {
  auto flow = d.flow();
  std::vector<TextCellAddress> path;
  if (nested)
    path.push_back({table_id(flow, "Outer"), 1, 1, 1, 1});
  path.push_back({table_id(flow, "Grid"), 1, 1, 1, 1});
  WordsDisplayMap map(flow);
  for (const auto &r : map.paragraphs())
    if (r.cells == path) {
      TextSelection s{r.begin + (empty ? 0 : 2), r.begin + (empty ? 0 : 2),
                      path.back(), path};
      s.anchor_cells = path;
      return s;
    }
  throw std::runtime_error("Missing table caret");
}
struct State {
  Bytes xml, styles, manifest;
  bool operator==(const State &) const = default;
};
State state(const TextDocument &d) {
  auto p = d.style_snapshot();
  return {p.xml.toByteArray(), p.entry("styles.xml"),
          p.entry("META-INF/manifest.xml")};
}
void check(const TextDocument &d, bool nested, double width, double row_height,
           double column_width) {
  auto f = d.flow();
  auto inspect = [&](const TextTable &t, bool changed) {
    assert(t.rows.size() == 3 && t.columns.size() == 3 && t.columns[0].header &&
           !t.columns[1].header);
    assert(near(t.left, 12) && near(t.right, 10) && near(t.before, 7));
    assert(t.background == std::optional(TextColor{255, 249, 196}));
    for (size_t i = 0; i < 3; ++i) {
      assert(t.rows[i].background == std::optional(TextColor{224, 237, 248}));
      assert(t.rows[i].keep_together);
      if (changed && i == 1) {
        assert(t.rows[i].height && near(*t.rows[i].height, row_height));
        assert(t.columns[i].width && near(*t.columns[i].width, column_width) &&
               t.columns[i].relative == 0 && !t.columns[i].optimal);
      } else {
        assert(!t.rows[i].height && near(t.rows[i].minimum, 24));
        assert(near(t.columns[i].relative, 4) && t.columns[i].optimal);
      }
    }
    if (changed) {
      assert(!t.relative_width && t.width && near(*t.width, width) &&
             t.alignment == TextTable::Center);
    } else
      assert(t.relative_width && near(*t.relative_width, .5));
    for (const auto &c : t.cells)
      assert(c.source.attributeNS(OFFICE, "string-value") == "cached");
  };
  const auto &outer = f.tables.back();
  if (!nested)
    inspect(outer, true);
  else
    for (const auto &c : outer.cells)
      for (const auto &b : c.blocks)
        if (b.kind == TextBlock::Table)
          inspect(block_table(f, b), c.row == 1 && c.column == 1);
  auto styles = read_document_xml(d.style_snapshot().entry("styles.xml"));
  for (auto s : styles.elementsByTagNameNS(STYLE, "style"))
    if (s.attributeNS(STYLE, "name") == "RelBase")
      assert(
          child(s, STYLE, "table-properties").attributeNS(STYLE, "rel-width") ==
          "75%");
  std::set<Text> ids;
  auto walk = [&](auto &&self, XmlElement node) -> void {
    auto id = node.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        self(self, n);
  };
  walk(walk, d.style_snapshot().xml.documentElement());
  assert(d.style_snapshot().entry("Extras/opaque.bin") ==
         Bytes("\0keep\xff", 6));
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("words-table-sizing-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  for (bool nested : {false, true}) {
    auto prefix = nested ? std::string("nested-") : std::string("flat-");
    auto p = fixture(nested);
    auto path = Text((dir / (prefix + "source.odt")).string());
    p.save(path);
    TextDocument d;
    d.load(path);
    auto before = state(d);
    auto old = point(d, nested);
    auto initial = words_table_sizing(d.style_snapshot(), *old.cell, old.cells);
    assert(initial.table.mode == WordsTableSize::Percentage &&
           near(initial.table.value, 50) &&
           initial.row.mode == WordsRowSize::Minimum &&
           near(initial.row.value, 24) && initial.column.value == 4);
    auto same = size_words_table(d.style_snapshot(), *old.cell,
                                 {initial.table, initial.row, initial.column},
                                 old.cells, old.cursor);
    assert(same.document.xml.toByteArray() == before.xml &&
           same.document.entry("styles.xml") == before.styles &&
           same.cells == old.cells && same.cursor == old.cursor);
    WordsTableSizingChange change;
    change.table = WordsTableSize{WordsTableSize::Fixed, nested ? 90. : 300.,
                                  TextTable::Center};
    change.row = WordsRowSize{WordsRowSize::Exact, 40};
    change.column = WordsColumnSize{WordsColumnSize::Fixed, nested ? 20. : 60.};
    auto edit = size_words_table(d.style_snapshot(), *old.cell, change,
                                 old.cells, old.cursor);
    TextSelection next{edit.cursor, edit.cursor, edit.cell, edit.cells};
    next.anchor_cells = next.cells;
    assert(d.commit_structure(edit.document, old, next, d.revision()));
    check(d, nested, nested ? 90 : 300, 40, nested ? 20 : 60);
    auto changed = state(d);
    assert(changed.styles != before.styles);
    assert(d.undo() == old && state(d) == before && !d.modified());
    assert(d.redo() == next && state(d) == changed);
    auto active =
        words_table_sizing(d.style_snapshot(), *next.cell, next.cells);
    assert(active.table == *change.table && active.row == *change.row &&
           active.column == *change.column);
    auto no_op = size_words_table(d.style_snapshot(), *next.cell, change,
                                  next.cells, next.cursor);
    assert(no_op.document.xml.toByteArray() == changed.xml &&
           no_op.document.entry("styles.xml") == changed.styles);
    NativeTextShaper shaper("monospace", 12);
    auto measured =
        measure_words_table_geometry(d.flow(), next.cells, {}, shaper);
    assert(near(measured.columns[1], nested ? 20 : 60) &&
           near(measured.rows[1], 40));
    assert(near(
        std::accumulate(measured.columns.begin(), measured.columns.end(), 0.),
        nested ? 90 : 300));
    d.save(Text((dir / (prefix + "sized.odt")).string()));
    change.table =
        WordsTableSize{WordsTableSize::Percentage, 33.3, TextTable::Right};
    change.row = WordsRowSize{};
    change.column = WordsColumnSize{WordsColumnSize::Weight, 7};
    edit = size_words_table(d.style_snapshot(), *next.cell, change, next.cells,
                            next.cursor);
    auto reset = words_table_sizing(edit.document, *edit.cell, edit.cells);
    assert(reset.table.mode == WordsTableSize::Percentage &&
           near(reset.table.value, 33.3) &&
           reset.table.alignment == TextTable::Right &&
           reset.row.mode == WordsRowSize::Automatic &&
           reset.column.value == 7);
    edit.document.save(Text((dir / (prefix + "relative.odt")).string()));
    auto repeat = size_words_table(edit.document, *edit.cell, change,
                                   edit.cells, edit.cursor);
    assert(repeat.document.xml.toByteArray() ==
           edit.document.xml.toByteArray());
    change.table = WordsTableSize{};
    change.row = WordsRowSize{WordsRowSize::Minimum, 60};
    change.column = WordsColumnSize{WordsColumnSize::Fixed, 30};
    edit = size_words_table(edit.document, *edit.cell, change, edit.cells,
                            edit.cursor);
    reset = words_table_sizing(edit.document, *edit.cell, edit.cells);
    assert(reset.table.mode == WordsTableSize::Stretch &&
           reset.row.value == 60 && reset.column.value == 30);
    (void)paginate_text(read_text_flow(edit.document), shaper);
    edit.document.save(Text((dir / (prefix + "stretch.odt")).string()));
    auto invalid = change;
    invalid.row = WordsRowSize{WordsRowSize::Exact, -1};
    rejects([&] {
      size_words_table(p, *old.cell, invalid, old.cells, old.cursor);
    });
    invalid = change;
    invalid.column = WordsColumnSize{WordsColumnSize::Weight, 1.5};
    rejects([&] {
      size_words_table(p, *old.cell, invalid, old.cells, old.cursor);
    });
    assert(p.xml.toByteArray() == before.xml &&
           p.entry("styles.xml") == before.styles);
  }
  auto empty = fixture(false, true);
  auto path = Text((dir / "empty-source.odt").string());
  empty.save(path);
  TextDocument d;
  d.load(path);
  auto s = point(d, false, true);
  WordsTableSizingChange change;
  change.row = WordsRowSize{WordsRowSize::Exact, 0};
  rejects([&] {
    size_words_table(d.style_snapshot(), *s.cell, change, s.cells, s.cursor);
  });
  change.row = WordsRowSize{WordsRowSize::Exact, 20};
  auto edit =
      size_words_table(d.style_snapshot(), *s.cell, change, s.cells, s.cursor);
  assert(edit.cells.back().column == 1 &&
         edit.document.xml.elementsByTagNameNS(TEXT, "p").size() == 2);
  NativeTextShaper shaper("monospace", 12);
  (void)paginate_text(read_text_flow(edit.document), shaper);
  edit.document.save(Text((dir / "empty-sized.odt").string()));
  // Geometry edits of an independent numeric cell retain cached
  // values/formulas.
  auto numeric = fixture();
  auto row = numeric.xml.elementsByTagNameNS(TABLE, "table-row")[0];
  row.removeAttributeNS(TABLE, "number-rows-repeated");
  auto cell = numeric.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  cell.removeAttributeNS(TABLE, "number-columns-repeated");
  cell.setAttributeNS(OFFICE, "office:value-type", "float");
  cell.removeAttributeNS(OFFICE, "string-value");
  cell.setAttributeNS(OFFICE, "office:value", "3");
  cell.setAttributeNS(TABLE, "table:formula", "of:=1+2");
  change.row = WordsRowSize{WordsRowSize::Minimum, 60};
  edit = size_words_table(numeric, {0, 0, 0, 1, 1}, change);
  cell = edit.document.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  assert(cell.attributeNS(TABLE, "formula") == "of:=1+2" &&
         cell.attributeNS(OFFICE, "value") == "3");
  edit.document.save(Text((dir / "numeric-sized.odt").string()));
  // Explicit definitions for implicit columns retain automatic peer widths.
  auto implicit = fixture();
  auto grid = implicit.xml.elementsByTagNameNS(TABLE, "table")[0];
  for (auto node : children(grid, TABLE))
    if (node.localName() == "table-header-columns" ||
        node.localName() == "table-columns")
      grid.removeChild(node);
  auto implicit_styles = read_document_xml(implicit.entry("styles.xml"));
  for (auto style : implicit_styles.elementsByTagNameNS(STYLE, "default-style"))
    if (style.attributeNS(STYLE, "family") == "table-column")
      child(style, STYLE, "table-column-properties")
          .removeAttributeNS(STYLE, "rel-column-width");
  implicit.set_entry("styles.xml", implicit_styles.toByteArray(), "text/xml");
  change = {};
  change.column = WordsColumnSize{WordsColumnSize::Weight, 2};
  edit = size_words_table(implicit, {0, 1, 1, 1, 1}, change);
  auto implicit_flow = read_text_flow(edit.document);
  for (size_t c = 0; c < 3; ++c)
    assert(implicit_flow.tables[0].columns[c].relative == (c == 1 ? 2 : 1));
  edit.document.save(Text((dir / "implicit-sized.odt").string()));
  // Covered metadata and cell spans survive dimension-only edits.
  auto merged = merge_words_table_cells(fixture(), {0, 0, 0, 1, 1}, 2, 2);
  for (auto covered :
       merged.document.xml.elementsByTagNameNS(TABLE, "covered-table-cell")) {
    auto metadata = merged.document.xml.createElementNS(TEXT, "text:p");
    set_plain_text(metadata, "Hidden metadata");
    covered.appendChild(metadata);
  }
  change = {};
  change.row = WordsRowSize{WordsRowSize::Exact, 50};
  change.column = WordsColumnSize{WordsColumnSize::Fixed, 80};
  edit = size_words_table(merged.document, *merged.cell, change, merged.cells);
  auto merged_flow = read_text_flow(edit.document);
  assert(merged_flow.tables[0].cells[0].rows == 2 &&
         merged_flow.tables[0].cells[0].columns == 2);
  for (auto covered :
       edit.document.xml.elementsByTagNameNS(TABLE, "covered-table-cell"))
    assert(plain_text(covered) == "Hidden metadata");
  edit.document.save(Text((dir / "merged-sized.odt").string()));
  // Relative widths in document defaults cannot be removed by a named layer.
  auto inherited_default = fixture();
  auto default_styles =
      read_document_xml(inherited_default.entry("styles.xml"));
  auto def = default_styles.createElementNS(STYLE, "style:default-style");
  def.setAttributeNS(STYLE, "style:family", "table");
  auto relative =
      default_styles.createElementNS(STYLE, "style:table-properties");
  relative.setAttributeNS(STYLE, "style:rel-width", "90%");
  def.appendChild(relative);
  child(default_styles.documentElement(), OFFICE, "styles").appendChild(def);
  inherited_default.set_entry("styles.xml", default_styles.toByteArray(),
                              "text/xml");
  auto content_before = inherited_default.xml.toByteArray(),
       styles_before = inherited_default.entry("styles.xml");
  change = {};
  change.table = WordsTableSize{WordsTableSize::Fixed, 100, TextTable::Left};
  rejects(
      [&] { size_words_table(inherited_default, {0, 1, 1, 1, 1}, change); });
  assert(inherited_default.xml.toByteArray() == content_before &&
         inherited_default.entry("styles.xml") == styles_before);
  std::cout << "PASS table/row/column sizing, inherited style isolation, "
               "repeated/nested scope, empty/numeric cells and exact "
               "archive history\n";
}
