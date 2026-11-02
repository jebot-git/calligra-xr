// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_words_geometry.h"
#include "native_table_model.h"
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

XmlElement style_named(const XmlDocument &doc, const Text &name) {
  for (auto style : doc.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == name)
      return style;
  throw std::runtime_error("Missing style");
}
XmlElement add_table_style(XmlDocument &doc, XmlElement group, const Text &name,
                           const Text &parent = {}, const Text &relative = {}) {
  auto style = doc.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", name);
  style.setAttributeNS(STYLE, "style:family", "table");
  if (!parent.empty())
    style.setAttributeNS(STYLE, "style:parent-style-name", parent);
  if (!relative.empty()) {
    auto props = doc.createElementNS(STYLE, "style:table-properties");
    props.setAttributeNS(STYLE, "style:rel-width", relative);
    style.appendChild(props);
  }
  group.appendChild(style);
  return style;
}
XmlElement peer(XmlDocument &doc, XmlElement parent, const Text &name,
                const Text &style = {}) {
  auto table = doc.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name", name);
  if (!style.empty())
    table.setAttributeNS(TABLE, "table:style-name", style);
  auto col = doc.createElementNS(TABLE, "table:table-column");
  col.setAttributeNS(TABLE, "table:number-columns-repeated", "3");
  table.appendChild(col);
  auto row = doc.createElementNS(TABLE, "table:table-row");
  auto cell = doc.createElementNS(TABLE, "table:table-cell");
  cell.setAttributeNS(TABLE, "table:number-columns-repeated", "3");
  auto para = doc.createElementNS(TEXT, "text:p");
  set_plain_text(para, "Peer");
  cell.appendChild(para);
  row.appendChild(cell);
  table.appendChild(row);
  parent.appendChild(table);
  return table;
}
OdfPackage relative_default_fixture(bool nested) {
  auto p = fixture(nested);
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  auto def = styles.createElementNS(STYLE, "style:default-style");
  def.setAttributeNS(STYLE, "style:family", "table");
  auto props = styles.createElementNS(STYLE, "style:table-properties");
  props.setAttributeNS(STYLE, "style:rel-width", "60.00%");
  props.setAttributeNS(STYLE, "style:width", "7in");
  props.setAttributeNS(TABLE, "table:align", "left");
  props.setAttributeNS(FO, "fo:margin", "1pt");
  def.appendChild(props);
  common.appendChild(def);
  child(style_named(styles, "RelBase"), STYLE, "table-properties")
      .removeAttributeNS(STYLE, "rel-width");
  child(style_named(p.xml, "Sized"), STYLE, "table-properties")
      .removeAttributeNS(STYLE, "rel-width");
  add_table_style(styles, common, "UnusedChild", "RelBase");
  add_table_style(styles, common, "Shadow", {}, "40%");
  add_table_style(styles, common, "Explicit", {}, "25%");
  add_table_style(p.xml,
                  child(p.xml.documentElement(), OFFICE, "automatic-styles"),
                  "Shadow");
  auto automatic = child(styles.documentElement(), OFFICE, "automatic-styles");
  add_table_style(styles, automatic, "Shadow");
  auto master = styles.elementsByTagNameNS(STYLE, "master-page")[0];
  auto header = styles.createElementNS(STYLE, "style:header");
  peer(styles, header, "HeaderUnstyled");
  peer(styles, header, "HeaderShadow", "Shadow");
  master.appendChild(header);
  peer(p.xml, p.body(), "PeerUnstyled");
  peer(p.xml, p.body(), "PeerShared", "Sized");
  peer(p.xml, p.body(), "PeerShadow", "Shadow");
  peer(p.xml, p.body(), "PeerExplicit", "Explicit");
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  return p;
}
void default_isolation(const std::filesystem::path &dir) {
  for (bool nested : {false, true}) {
    auto prefix =
        nested ? std::string("default-nested-") : std::string("default-flat-");
    auto p = relative_default_fixture(nested);
    auto file = Text((dir / (prefix + "source.odt")).string());
    p.save(file);
    TextDocument d;
    d.load(file);
    auto original = state(d);
    auto old = point(d, nested);
    WordsTableSizingChange change;
    change.table = WordsTableSize{WordsTableSize::Fixed, nested ? 70. : 240.,
                                  TextTable::Center};
    auto edit = size_words_table(d.style_snapshot(), *old.cell, change,
                                 old.cells, old.cursor);
    auto flow = read_text_flow(edit.document);
    for (size_t t = 0; t < flow.tables.size(); ++t) {
      const auto &table = flow.tables[t];
      if (t == edit.cell->table) {
        assert(!table.relative_width && table.width &&
               near(*table.width, change.table->value));
      } else {
        assert(table.relative_width &&
               near(*table.relative_width,
                    table.name == "PeerExplicit" ? .25 : .6));
      }
    }
    auto styles = read_document_xml(edit.document.entry("styles.xml"));
    for (auto def : styles.elementsByTagNameNS(STYLE, "default-style"))
      if (def.attributeNS(STYLE, "family") == "table") {
        auto props = child(def, STYLE, "table-properties");
        assert(!props.hasAttributeNS(STYLE, "rel-width") &&
               props.attributeNS(STYLE, "width") == "7in" &&
               props.attributeNS(FO, "margin") == "1pt");
      }
    auto common_shadow = style_named(styles, "Shadow");
    assert(child(common_shadow, STYLE, "table-properties")
               .attributeNS(STYLE, "rel-width") == "40%");
    OdfTableModelReader catalogue({styles, edit.document.xml});
    for (auto table : styles.elementsByTagNameNS(TABLE, "table")) {
      size_t slots = 0;
      auto model = catalogue.read(
          table, [](auto) { return std::vector<TextBlock>{}; }, slots, true);
      assert(model.relative_width && near(*model.relative_width, .6));
    }
    auto unused = catalogue.style_chain("table", "UnusedChild");
    assert(std::any_of(unused.begin(), unused.end(), [](auto style) {
      return child(style, STYLE, "table-properties")
                 .attributeNS(STYLE, "rel-width") == "60.00%";
    }));
    TextSelection next{edit.cursor, edit.cursor, edit.cell, edit.cells};
    next.anchor_cells = next.cells;
    assert(d.commit_structure(edit.document, old, next, d.revision()));
    auto changed = state(d);
    assert(d.undo() == old && state(d) == original);
    assert(d.redo() == next && state(d) == changed);
    auto same = size_words_table(d.style_snapshot(), *next.cell, change,
                                 next.cells, next.cursor);
    assert(same.document.xml.toByteArray() == changed.xml &&
           same.document.entry("styles.xml") == changed.styles);
    d.save(Text((dir / (prefix + "fixed.odt")).string()));
  }
}
OdfPackage fixed_columns_fixture(bool nested, bool mixed = false) {
  auto p = fixture(nested);
  auto group = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  auto cols =
      child(style_named(p.xml, "Cols"), STYLE, "table-column-properties");
  cols.setAttributeNS(STYLE, "style:rel-column-width", mixed ? "4*" : "0*");
  cols.setAttributeNS(STYLE, "style:column-width", "40pt");
  cols.setAttributeNS(STYLE, "style:use-optimal-column-width", "false");
  auto head = p.xml.createElementNS(STYLE, "style:style");
  head.setAttributeNS(STYLE, "style:name", "Head");
  head.setAttributeNS(STYLE, "style:family", "table-column");
  head.setAttributeNS(STYLE, "style:parent-style-name", "ColBase");
  auto props = p.xml.createElementNS(STYLE, "style:table-column-properties");
  props.setAttributeNS(STYLE, "style:column-width", "20pt");
  props.setAttributeNS(STYLE, "style:rel-column-width", "0*");
  head.appendChild(props);
  group.appendChild(head);
  auto table = p.xml.elementsByTagNameNS(TABLE, "table")[nested ? 1 : 0];
  auto column =
      child(child(table, TABLE, "table-header-columns"), TABLE, "table-column");
  column.setAttributeNS(TABLE, "table:style-name", "Head");
  return p;
}
void proportional_resize(const std::filesystem::path &dir) {
  NativeTextShaper shaper("monospace", 12);
  for (bool nested : {false, true})
    for (bool mixed : {false, true}) {
      auto prefix = std::string(nested ? "resize-nested-" : "resize-flat-") +
                    (mixed ? "mixed-" : "fixed-");
      auto p = fixed_columns_fixture(nested, mixed);
      auto file = Text((dir / (prefix + "source.odt")).string());
      p.save(file);
      TextDocument d;
      d.load(file);
      auto old = point(d, nested);
      auto original = state(d);
      auto before =
          measure_words_table_geometry(d.flow(), old.cells, {}, shaper);
      double total =
          std::accumulate(before.columns.begin(), before.columns.end(), 0.);
      WordsTableSizingChange change;
      change.proportional_columns = true;
      change.table =
          WordsTableSize{WordsTableSize::Fixed, total * 1.5, TextTable::Center};
      auto edit = size_words_table(d.style_snapshot(), *old.cell, change,
                                   old.cells, old.cursor, {}, &shaper);
      auto flow = read_text_flow(edit.document);
      auto after = measure_words_table_geometry(flow, edit.cells, {}, shaper);
      for (size_t c = 0; c < 3; ++c)
        assert(near(after.columns[c], before.columns[c] * 1.5));
      const auto &table = flow.tables[edit.cell->table];
      if (mixed)
        assert(table.columns[1].relative == 4 &&
               table.columns[2].relative == 4);
      for (const auto &peer : flow.tables)
        if (peer.source != table.source && peer.name.startsWith("Grid"))
          assert(near(*peer.columns[0].width, 20) &&
                 near(*peer.columns[1].width, 40));
      TextSelection next{edit.cursor, edit.cursor, edit.cell, edit.cells};
      next.anchor_cells = next.cells;
      assert(d.commit_structure(edit.document, old, next, d.revision()));
      auto changed = state(d);
      assert(d.undo() == old && state(d) == original);
      assert(d.redo() == next && state(d) == changed);
      auto same = size_words_table(d.style_snapshot(), *next.cell, change,
                                   next.cells, next.cursor, {}, &shaper);
      assert(same.document.xml.toByteArray() == changed.xml &&
             same.document.entry("styles.xml") == changed.styles);
      d.save(Text((dir / (prefix + "sized.odt")).string()));
      change.table =
          WordsTableSize{WordsTableSize::Percentage, 35, TextTable::Right};
      edit = size_words_table(d.style_snapshot(), *next.cell, change,
                              next.cells, next.cursor, {}, &shaper);
      auto percentage = measure_words_table_geometry(
          read_text_flow(edit.document), edit.cells, {}, shaper);
      double desired = percentage.available_width * .35;
      for (size_t c = 0; c < 3; ++c)
        assert(
            near(percentage.columns[c], before.columns[c] * desired / total));
      edit.document.save(Text((dir / (prefix + "relative.odt")).string()));
      change.table = WordsTableSize{};
      auto stretched = size_words_table(edit.document, *edit.cell, change,
                                        edit.cells, edit.cursor, {}, &shaper);
      auto stretch = measure_words_table_geometry(
          read_text_flow(stretched.document), stretched.cells, {}, shaper);
      desired = stretch.available_width - table.left - table.right;
      for (size_t c = 0; c < 3; ++c)
        assert(near(stretch.columns[c], before.columns[c] * desired / total));
      stretched.document.save(Text((dir / (prefix + "stretch.odt")).string()));
      change.column = WordsColumnSize{WordsColumnSize::Fixed, 80};
      rejects([&] {
        size_words_table(d.style_snapshot(), *next.cell, change, next.cells,
                         next.cursor);
      });
      assert(state(d) == changed);
    }
  // Measure implicit fixed-column shrink rather than scaling raw declarations.
  auto shrink = fixed_columns_fixture(false);
  auto styles = read_document_xml(shrink.entry("styles.xml"));
  for (auto document : {styles, shrink.xml})
    for (auto style : document.elementsByTagNameNS(STYLE, "style"))
      if (style.attributeNS(STYLE, "family") == "table") {
        auto props = child(style, STYLE, "table-properties");
        props.removeAttributeNS(STYLE, "width");
        props.removeAttributeNS(STYLE, "rel-width");
      }
  shrink.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  std::vector<TextCellAddress> path{{0, 1, 1, 1, 1}};
  auto before =
      measure_words_table_geometry(read_text_flow(shrink), path, 80, shaper);
  double total =
      std::accumulate(before.columns.begin(), before.columns.end(), 0.);
  WordsTableSizingChange change;
  change.table =
      WordsTableSize{WordsTableSize::Fixed, total * 2, TextTable::Left};
  change.proportional_columns = true;
  auto edit =
      size_words_table(shrink, path.back(), change, path, {}, 80, &shaper);
  auto after = measure_words_table_geometry(read_text_flow(edit.document),
                                            edit.cells, 80, shaper);
  for (size_t c = 0; c < 3; ++c)
    assert(near(after.columns[c], before.columns[c] * 2));
  edit.document.save(Text((dir / "resize-implicit-shrink.odt").string()));
  // A zero-width visible fixed column cannot be represented as positiveLength.
  auto original = shrink.xml.toByteArray();
  rejects([&] {
    size_words_table(shrink, path.back(), change, path, {}, 40, &shaper);
  });
  assert(shrink.xml.toByteArray() == original);
  auto hidden = fixed_columns_fixture(false);
  auto columns = hidden.xml.elementsByTagNameNS(TABLE, "table-column");
  columns[1].setAttributeNS(TABLE, "table:visibility", "collapse");
  before =
      measure_words_table_geometry(read_text_flow(hidden), path, {}, shaper);
  change.table = WordsTableSize{WordsTableSize::Fixed, before.columns[0] * 2,
                                TextTable::Left};
  edit = size_words_table(hidden, path.back(), change, path, {}, {}, &shaper);
  auto flow = read_text_flow(edit.document);
  assert(near(*flow.tables[0].columns[0].width, 40) &&
         !flow.tables[0].columns[1].visible &&
         near(*flow.tables[0].columns[1].width, 80));
  edit.document.save(Text((dir / "resize-hidden.odt").string()));
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
  // Relative defaults are relocated before a fixed table conversion.
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
  auto fixed_default =
      size_words_table(inherited_default, {0, 1, 1, 1, 1}, change);
  auto fixed_flow = read_text_flow(fixed_default.document);
  assert(!fixed_flow.tables[0].relative_width && fixed_flow.tables[0].width &&
         near(*fixed_flow.tables[0].width, 100));
  fixed_default.document.save(Text((dir / "default-fixed.odt").string()));
  assert(inherited_default.xml.toByteArray() == content_before &&
         inherited_default.entry("styles.xml") == styles_before);
  default_isolation(dir);
  proportional_resize(dir);
  std::cout
      << "PASS table/row/column sizing, inherited style isolation, "
         "repeated/nested scope, empty/numeric cells and exact "
         "archive history, default relocation and proportional resizing\n";
}
