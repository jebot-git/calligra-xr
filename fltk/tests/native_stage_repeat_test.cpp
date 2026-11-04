// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment_fixture.h"
#include "native_stage_repeat.h"
#include "native_stage_names.h"
#include "native_stage_import.h"
#include <filesystem>
namespace {
XmlElement frame(OdfPackage &p) {
  auto old = named(p.xml, "X");
  auto n = element(p.xml, DRAW, "draw:frame");
  for (const auto &a : old.attributes())
    n.setAttributeNS(a.uri, a.name, a.value);
  old.parentNode().insertBefore(n, old);
  old.parentNode().removeChild(old);
  return n;
}
OdfPackage table_source(bool empty = false) {
  auto p = source();
  auto x = frame(p);
  x.removeAttributeNS(DRAW, "transform");
  x.setAttributeNS(SVG, "svg:x", "20pt");
  x.setAttributeNS(SVG, "svg:y", "20pt");
  x.setAttributeNS(SVG, "svg:width", "400pt");
  x.setAttributeNS(SVG, "svg:height", "280pt");
  auto box = element(p.xml, DRAW, "draw:text-box", x);
  auto table = element(p.xml, TABLE, "table:table", box);
  table.setAttributeNS(TABLE, "table:name", "Outer");
  auto col = element(p.xml, TABLE, "table:table-column", table);
  col.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  auto row = element(p.xml, TABLE, "table:table-row", table);
  row.setAttributeNS(TABLE, "table:number-rows-repeated", "2");
  auto cell = element(p.xml, TABLE, "table:table-cell", row);
  cell.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  if (!empty) {
    auto paragraph = element(p.xml, TEXT, "text:p", cell);
    paragraph.setAttributeNS(XML, "xml:id", "body-id");
    paragraph.appendChild(p.xml.createTextNode("Repeated Ω"));
    auto mark = element(p.xml, TEXT, "text:bookmark-start", paragraph);
    mark.setAttributeNS(TEXT, "text:name", "bookmark");
    auto link = element(p.xml, TEXT, "text:a", paragraph);
    link.setAttributeNS(XLINK, "xlink:href", "#bookmark");
    link.setAttributeNS(XLINK, "xlink:type", "simple");
    link.appendChild(p.xml.createTextNode(" link"));
    auto end = element(p.xml, TEXT, "text:bookmark-end", paragraph);
    end.setAttributeNS(TEXT, "text:name", "bookmark");
    auto nested = element(p.xml, TABLE, "table:table", cell);
    nested.setAttributeNS(TABLE, "table:name", "Nested");
    element(p.xml, TABLE, "table:table-column", nested);
    auto nr = element(p.xml, TABLE, "table:table-row", nested);
    auto nc = element(p.xml, TABLE, "table:table-cell", nr);
    auto np = element(p.xml, TEXT, "text:p", nc);
    np.appendChild(p.xml.createTextNode("Nested"));
  }
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto common = child(doc.documentElement(), OFFICE, "styles");
  auto d = element(doc, STYLE, "style:default-style");
  d.setAttributeNS(STYLE, "style:family", "table-cell");
  common.insertBefore(d, common.firstChild());
  auto props = element(doc, STYLE, "style:table-cell-properties", d);
  props.setAttributeNS(FO, "fo:border", "2pt solid #2040a0");
  props.setAttributeNS(FO, "fo:padding", "4pt");
  props.setAttributeNS(FO, "fo:background-color", "#f0e0c0");
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  return p;
}
const SceneShape &xshape(const std::vector<SlideScene> &scenes) {
  for (const auto &s : scenes[0].shapes)
    if (s.name == "X")
      return s;
  std::abort();
}
NativeStageFragment fragment(const OdfPackage &p) {
  auto scenes = read_presentation_scenes(p);
  const auto &s = xshape(scenes);
  return native_stage_fragment(p, scenes[0],
                               {{int(&s - scenes[0].shapes.data()), 0}});
}
OdfPackage expand(bool empty = false) {
  auto p = table_source(empty);
  auto xml = p.xml.toByteArray(), styles = p.entry("styles.xml");
  const auto before = read_presentation_scenes(p);
  assert(xshape(before).text_flow.blocks.size() == 1);
  const auto &table = *xshape(before).text_flow.blocks[0].table;
  assert(table.rows.size() == 2 && table.cells.size() == 4);
  if (empty) {
    assert(xshape(before).text.empty() &&
           xshape(before).text_flow.paragraphs.empty());
    for (const auto &cell : table.cells)
      assert(cell.empty_format && cell.style.borders[0].width == 2);
  }
  auto wire = encode_stage_fragment(fragment(p));
  auto subset = materialize_stage_repeated_cells(wire);
  OdfPackage result(Kind::Stage);
  result.load_bytes(subset.archive);
  const auto scenes = read_presentation_scenes(result);
  const auto &after = scenes[0].shapes[0];
  const auto &expanded = *after.text_flow.blocks[0].table;
  assert(expanded.rows.size() == 2 && expanded.cells.size() == 4);
  std::set<const void *> cell_nodes;
  for (const auto &cell : expanded.cells) {
    assert(cell.source_instance == 0 &&
           cell_nodes.insert(cell.source.identity()).second);
    assert(!cell.source.hasAttributeNS(TABLE, "number-columns-repeated"));
    assert(cell.style == table.cells[0].style);
    if (empty) {
      assert(cell.empty_format && table.cells[0].empty_format);
      assert(cell.empty_format->font == table.cells[0].empty_format->font);
    }
    if (!empty) {
      assert(after.text_flow.paragraphs[cell.blocks[0].index].text ==
             "Repeated Ω link");
      assert(cell.blocks[1].table->cells.size() == 1);
    }
  }
  std::set<Text> identities, tables, marks;
  for (auto n : result.xml.elementsByTagNameNS(TABLE, "table"))
    assert(tables.insert(n.attributeNS(TABLE, "name")).second);
  for (auto n : result.xml.elementsByTagNameNS(TEXT, "p"))
    if (n.hasAttributeNS(XML, "id"))
      assert(identities.insert(n.attributeNS(XML, "id")).second);
  for (auto n : result.xml.elementsByTagNameNS(TEXT, "bookmark-start"))
    assert(marks.insert(n.attributeNS(TEXT, "name")).second);
  for (auto n : result.xml.elementsByTagNameNS(TEXT, "a"))
    assert(marks.contains(n.attributeNS(XLINK, "href").mid(1)));
  assert(tables.size() == (empty ? 1 : 5));
  if (!empty)
    assert(identities.size() == 4 && marks.size() == 4);
  assert(p.xml.toByteArray() == xml && p.entry("styles.xml") == styles);
  assert(subset.selection == decode_stage_fragment(wire).selection);
  return result;
}
void bounds() {
  auto p = table_source();
  auto row = p.xml.elementsByTagNameNS(TABLE, "table-row").front();
  row.setAttributeNS(TABLE, "table:number-rows-repeated", "100001");
  rejects([&] {
    materialize_stage_repeated_cells(encode_stage_fragment(fragment(p)));
  });
  rejects([&] { materialize_stage_repeated_cells("invalid"); });
  auto huge = table_source();
  auto huge_row = huge.xml.elementsByTagNameNS(TABLE, "table-row").front();
  huge_row.setAttributeNS(TABLE, "table:number-rows-repeated", "1024");
  auto long_p = huge.xml.elementsByTagNameNS(TABLE, "table")
                    .front()
                    .elementsByTagNameNS(TEXT, "p")
                    .front();
  set_plain_text(long_p, Text(std::string(200000, 'x')));
  bool allocation_guard = false;
  try {
    materialize_stage_repeated_cells(encode_stage_fragment(fragment(huge)));
  } catch (const std::exception &e) {
    allocation_guard =
        std::string(e.what()).find("clone allocation") != std::string::npos;
  }
  assert(allocation_guard);
  auto embedded = table_source();
  auto parent = embedded.xml.elementsByTagNameNS(TABLE, "table")
                    .front()
                    .elementsByTagNameNS(TEXT, "p")
                    .front();
  auto object = element(embedded.xml, DRAW, "draw:object", parent);
  auto doc = element(embedded.xml, OFFICE, "office:document", object);
  auto meta = element(embedded.xml, OFFICE, "office:meta", doc);
  meta.setAttributeNS(XML, "xml:id", "opaque-identity");
  bool identity_guard = false;
  try {
    materialize_stage_repeated_cells(encode_stage_fragment(fragment(embedded)));
  } catch (const std::exception &e) {
    identity_guard =
        std::string(e.what()).find("scoped cloning") != std::string::npos;
  }
  assert(identity_guard);
  // Reservation covers lexical, percent-escaped, XML-entity and opaque values.
  auto q = source();
  q.set_entry("opaque/NativePaste1", "NativePaste2",
              "application/octet-stream");
  q.set_entry("resource.xml", "<x a='NativePa&#115;te3' b='%4EativePaste4'/>",
              "text/xml");
  NativeStageNames names(q);
  assert(names.take() == "NativePaste5");
  q.set_entry("new", "NativePaste6", "application/octet-stream");
  names.reserve(q);
  assert(names.take() == "NativePaste7");
}
} // namespace
int main(int argc, char **argv) {
  auto ordinary = expand();
  auto empty = expand(true);
  bounds();
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    auto path = Text(argv[2]);
    auto a = table_source(), b = table_source(true);
    a.save(path + "/repeated-source.odp");
    ordinary.save(path + "/expanded-selected.odp");
    b.save(path + "/empty-table-source.odp");
    empty.save(path + "/empty-table-selected.odp");
  }
  std::cout << "PASS bounded repeated row/cell instances, typed local "
               "references, source retention and empty-table scene flow\n";
}
