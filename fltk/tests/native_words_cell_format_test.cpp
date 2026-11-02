// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_table_layout.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
OdfPackage fixture(bool nested = false, bool merged = false) {
  OdfPackage p(Kind::Words);
  std::string inner =
      R"(<table:table table:name="Inner"><table:table-column table:number-columns-repeated="2" table:default-cell-style-name="Cell"/><table:table-row table:number-rows-repeated="2"><table:table-cell table:number-columns-repeated="2"><text:p text:style-name="Body">Inner text</text:p></table:table-cell></table:table-row></table:table>)";
  std::string cells =
      merged
          ? R"(<table:table-cell table:number-columns-spanned="2"><text:p text:style-name="Body">Merged text</text:p></table:table-cell><table:covered-table-cell><text:p>Covered metadata</text:p></table:covered-table-cell>)"
          : std::string(
                R"(<table:table-cell table:number-columns-repeated="2" xml:id="cell"><text:p text:style-name="Body"><text:span text:style-name="Emphasis">Cell text</text:span></text:p>)") +
                (nested ? inner : "") + "</table:table-cell>";
  auto document =
      std::string(
          R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xml="http://www.w3.org/XML/1998/namespace" office:version="1.3"><office:automatic-styles><style:style style:name="Cell" style:family="table-cell"><style:table-cell-properties fo:padding="2pt" fo:border="1pt solid #808080" fo:background-color="#eeeeee"/><style:text-properties fo:color="#165aa7"/></style:style><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-size="12pt"/></style:style><style:style style:name="Emphasis" style:family="text"><style:text-properties fo:font-weight="700"/></style:style><style:style style:name="Row" style:family="table-row"><style:table-row-properties style:row-height="90pt"/></style:style></office:automatic-styles><office:body><office:text><text:p>Before</text:p><table:table table:name="Outer"><table:table-column table:number-columns-repeated="2" table:default-cell-style-name="Cell"/><table:table-row table:style-name="Row" table:number-rows-repeated="3" xml:id="row">)") +
      cells +
      "</table:table-row></table:table><text:p>After</text:p></office:text></"
      "office:body></office:document-content>";
  p.xml = read_document_xml(Bytes(document));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
size_t table(const TextFlow &flow, const Text &name) {
  for (size_t i = 0; i < flow.tables.size(); ++i)
    if (flow.tables[i].name == name)
      return i;
  throw std::runtime_error("Missing test table");
}
const TextTableCell &cell(const TextTable &table, size_t row, size_t column) {
  for (const auto &cell : table.cells)
    if (cell.row == row && cell.column == column)
      return cell;
  throw std::runtime_error("Missing test cell");
}
template <class F> void rejects(F action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void unique(const OdfPackage &p) {
  std::set<Text> ids, names;
  for (auto e : p.xml.elementsByTagNameNS(TABLE, "table"))
    assert(names.insert(e.attributeNS(TABLE, "name")).second);
  auto walk = [&](auto &&self, XmlElement e) -> void {
    auto id = e.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto c = e.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        self(self, c);
  };
  walk(walk, p.xml.documentElement());
}
} // namespace
int main(int argc, char **argv) {
  auto directory = std::filesystem::temp_directory_path() /
                   ("words-cell-format-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    directory = argv[2];
  std::filesystem::create_directories(directory);
  auto save = [&](OdfPackage p, const char *name) {
    unique(p);
    p.save(Text((directory / (std::string(name) + ".odt")).string()));
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
  };
  auto p = fixture();
  save(p, "source");
  auto xml = p.xml.toByteArray();
  TextCellAddress address{0, 1, 1, 1, 1};
  auto original = words_table_cell_format(p, address);
  assert(original.padding[0] == 2 && original.borders[0].width == 1);
  assert(format_words_table_cell(p, address, {}).document.xml.toByteArray() ==
         xml);
  WordsCellFormat no_op;
  no_op.padding[0] = 2;
  no_op.background = TextColor{238, 238, 238};
  assert(
      format_words_table_cell(p, address, no_op).document.xml.toByteArray() ==
      xml);
  WordsCellFormat format;
  format.padding = {5., 6., 7., 8.};
  format.background = TextColor{255, 241, 118};
  format.vertical = TextCellStyle::Bottom;
  TextTableBorder edge;
  edge.style = TextTableBorder::Double;
  edge.width = 3;
  edge.color = {22, 90, 167};
  format.borders[0] = edge;
  format.borders[2] = TextTableBorder{};
  auto result = format_words_table_cell(p, address, format);
  save(result.document, "formatted");
  auto retained = format_words_table_cell(p, address, format, {}, 10);
  assert(retained.cursor == result.cursor + 3);
  assert(retained.document.xml.toByteArray() ==
         result.document.xml.toByteArray());
  assert(p.xml.toByteArray() == xml);
  auto flow = read_text_flow(result.document);
  assert(flow.tables.size() == 1);
  for (const auto &c : flow.tables[0].cells) {
    bool selected = c.row == 1 && c.column == 1;
    assert(c.style.padding[0] == (selected ? 5 : 2));
    assert(c.style.background ==
           std::optional<TextColor>(selected ? TextColor{255, 241, 118}
                                             : TextColor{238, 238, 238}));
    if (selected) {
      assert(c.style.vertical == TextCellStyle::Bottom);
      assert(c.style.borders[0].style == TextTableBorder::Double);
      std::array<double, 3> thirds{1., 1., 1.};
      assert(c.style.borders[0].double_widths == thirds);
      assert(c.style.borders[2].style == TextTableBorder::None);
    }
  }
  assert(format_words_table_cell(result.document, *result.cell, format,
                                 result.cells)
             .document.xml.toByteArray() == result.document.xml.toByteArray());
  TextDocument document;
  document.load(Text((directory / "source.odt").string()));
  auto previous = TextSelection{6, 6, address, {address}};
  assert(document.commit_structure(
      result.document, previous,
      {result.cursor, result.cursor, result.cell, result.cells},
      document.revision()));
  assert(document.undo() == previous);
  assert(document.style_snapshot().xml.toByteArray() == xml);
  assert(document.redo());
  assert(document.style_snapshot().xml.toByteArray() ==
         result.document.xml.toByteArray());
  document.save(Text((directory / "redone.odt").string()));
  WordsCellFormat clear;
  clear.background = TextColor{0, 0, 0, 0};
  auto cleared = format_words_table_cell(result.document, *result.cell, clear,
                                         result.cells);
  save(cleared.document, "cleared");
  assert(words_table_cell_format(cleared.document, *cleared.cell, cleared.cells)
             .background->alpha == 0);
  auto nested = fixture(true);
  save(nested, "nested-source");
  auto nf = read_text_flow(nested);
  auto outer = table(nf, "Outer"), inner = table(nf, "Inner");
  TextCellAddress parent{outer, 1, 1, 1, 1}, leaf{inner, 1, 1, 1, 1};
  auto nr = format_words_table_cell(nested, leaf, format, {parent, leaf});
  save(nr.document, "nested-formatted");
  auto after = read_text_flow(nr.document);
  size_t changed = 0;
  for (const auto &t : after.tables)
    for (const auto &c : t.cells)
      if (c.style.padding[0] == 5)
        ++changed;
  assert(changed == 1);
  assert(after.tables.size() == 5);
  assert(nr.cells.size() == 2);
  auto merged = fixture(false, true);
  save(merged, "merged-source");
  auto mr = format_words_table_cell(merged, {0, 1, 0, 1, 2}, format);
  save(mr.document, "merged-formatted");
  assert(cell(read_text_flow(mr.document).tables[0], 1, 0).columns == 2);
  auto common = fixture();
  auto styles = read_document_xml(common.entry("styles.xml"));
  auto group = child(styles.documentElement(), OFFICE, "styles");
  if (group.isNull()) {
    group = styles.createElementNS(OFFICE, "office:styles");
    styles.documentElement().appendChild(group);
  }
  for (auto node : common.xml.elementsByTagNameNS(STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == "Cell") {
      group.appendChild(styles.importNode(node, true));
      node.parentNode().removeChild(node);
      break;
    }
  common.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  save(common, "common-source");
  auto common_result = format_words_table_cell(common, address, format);
  save(common_result.document, "common-formatted");
  assert(common_result.document.entry("styles.xml") ==
         common.entry("styles.xml"));
  auto common_flow = read_text_flow(common_result.document);
  const auto &selected = cell(common_flow.tables[0], 1, 1);
  const auto &paragraph = block_paragraph(common_flow, selected.blocks[0]);
  auto old_common_flow = read_text_flow(common);
  const auto &old_paragraph = block_paragraph(
      old_common_flow, cell(old_common_flow.tables[0], 1, 1).blocks[0]);
  assert(paragraph.color == old_paragraph.color &&
         paragraph.spans[0].font == old_paragraph.spans[0].font &&
         paragraph.spans[0].color == old_paragraph.spans[0].color);
  assert(paragraph.spans[0].font.weight == 700);
  auto invalid = format;
  invalid.padding[0] = -1;
  rejects([&] { format_words_table_cell(p, address, invalid); });
  invalid = format;
  invalid.padding[0] = std::numeric_limits<double>::infinity();
  rejects([&] { format_words_table_cell(p, address, invalid); });
  invalid = format;
  invalid.background = TextColor{0, 0, 0, .5};
  rejects([&] { format_words_table_cell(p, address, invalid); });
  rejects([&] { format_words_table_cell(p, {0, 9, 0, 1, 1}, format); });
  rejects([&] { format_words_table_cell(nested, leaf, format, {leaf}); });
  // An empty source cell stays empty; formatting must not manufacture text.
  auto empty = fixture();
  auto ef = read_text_flow(empty);
  auto source = cell(ef.tables[0], 0, 0).source;
  while (!source.firstChild().isNull())
    source.removeChild(source.firstChild());
  auto er = format_words_table_cell(empty, address, format);
  save(er.document, "empty-formatted");
  assert(cell(read_text_flow(er.document).tables[0], 1, 1)
             .source.firstChild()
             .isNull());
  std::cout
      << "PASS native cell padding, paint, borders, alignment, "
         "repeated/nested/merged scope, no-ops, validation and exact history\n";
}
