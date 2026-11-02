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

const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
TextCellAddress address(const OdfPackage &p, const Text &name, size_t r,
                        size_t c) {
  auto f = read_text_flow(p);
  auto id = table_id(f, name);
  const auto &cell = at(f.tables[id], r, c);
  return {id, cell.row, cell.column, cell.rows, cell.columns};
}
std::vector<TextParagraph> visible(const OdfPackage &p) {
  auto flow = read_text_flow(p);
  std::vector<TextParagraph> result;
  std::function<void(const std::vector<TextBlock> &)> walk =
      [&](const auto &blocks) {
        for (const auto &b : blocks)
          if (b.kind == TextBlock::Paragraph)
            result.push_back(b.formatted ? *b.formatted
                                         : flow.paragraphs.at(b.index));
          else {
            const auto &t = b.table ? *b.table : flow.tables.at(b.index);
            for (const auto &cell : t.cells)
              walk(cell.blocks);
          }
      };
  walk(flow.blocks);
  return result;
}
const TextParagraph find(const OdfPackage &p, const Text &text) {
  for (const auto &para : visible(p))
    if (para.text == text)
      return para;
  throw std::runtime_error("Missing styled paragraph");
}
OdfPackage styled() {
  auto p = package(
      R"XML(<table:table table:name="Styled"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell table:style-name="Red"><text:p>Red</text:p></table:table-cell><table:table-cell table:style-name="Blue" xml:id="blue-cell"><text:p xml:id="blue-p">Blue <text:span text:style-name="Italic">rich</text:span><text:tab/>tail</text:p><table:table table:name="InnerStyled"><table:table-column/><table:table-row table:number-rows-repeated="2"><table:table-cell><text:p>Nested blue</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table>)XML");
  auto styles = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  auto style = [&](const Text &name, const Text &family, const Text &color) {
    auto e = p.xml.createElementNS(STYLE, "style:style");
    e.setAttributeNS(STYLE, "style:name", name);
    e.setAttributeNS(STYLE, "style:family", family);
    auto props = p.xml.createElementNS(STYLE, "style:text-properties");
    props.setAttributeNS(FO, "fo:color", color);
    props.setAttributeNS(FO, "fo:font-size", name == "Blue" ? "15pt" : "10pt");
    props.setAttributeNS(STYLE, "style:text-underline-style",
                         name == "Blue" ? "wave" : "none");
    props.setAttributeNS(STYLE, "style:text-underline-width", "bold");
    e.appendChild(props);
    styles.appendChild(e);
    return e;
  };
  style("Red", "table-cell", "#a71a16");
  auto blue = style("Blue", "table-cell", "#165aa7");
  auto para = p.xml.createElementNS(STYLE, "style:paragraph-properties");
  para.setAttributeNS(FO, "fo:text-align", "center");
  para.setAttributeNS(FO, "fo:margin-top", "3pt");
  auto tabs = p.xml.createElementNS(STYLE, "style:tab-stops"),
       tab = p.xml.createElementNS(STYLE, "style:tab-stop");
  tab.setAttributeNS(STYLE, "style:position", "20mm");
  tabs.appendChild(tab);
  para.appendChild(tabs);
  blue.insertBefore(para, blue.firstChild());
  auto it = style("Italic", "text", "#00aa00");
  child(it, STYLE, "text-properties")
      .setAttributeNS(FO, "fo:font-style", "italic");
  return p;
}

OdfPackage common_styled() {
  auto p = styled();
  auto group = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  auto external = read_document_xml(p.entry("styles.xml"));
  auto shared = child(external.documentElement(), OFFICE, "styles");
  if (shared.isNull()) {
    shared = external.createElementNS(OFFICE, "office:styles");
    external.documentElement().appendChild(shared);
  }
  for (auto e : children(group, STYLE, "style"))
    if (e.attributeNS(STYLE, "name") == "Blue") {
      auto blue = external.importNode(e, true).toElement();
      shared.appendChild(blue);
      group.removeChild(e);
      auto text = child(blue, STYLE, "text-properties");
      text.setAttributeNS(FO, "fo:language", "none");
      text.setAttributeNS(FO, "fo:country", "US");
      auto tab = child(
          child(child(blue, STYLE, "paragraph-properties"), STYLE, "tab-stops"),
          STYLE, "tab-stop");
      tab.setAttributeNS(STYLE, "style:leader-text", ".");
      tab.setAttributeNS(STYLE, "style:leader-text-style", "Leader");
    }
  for (bool common : {true, false}) {
    auto doc = common ? external : p.xml;
    auto leader = doc.createElementNS(STYLE, "style:style");
    leader.setAttributeNS(STYLE, "style:name", "Leader");
    leader.setAttributeNS(STYLE, "style:family", "text");
    auto props = doc.createElementNS(STYLE, "style:text-properties");
    props.setAttributeNS(FO, "fo:color", common ? "#165aa7" : "#cc2200");
    leader.appendChild(props);
    (common ? shared : group).appendChild(leader);
  }
  auto scale = p.xml.createElementNS(STYLE, "style:style");
  scale.setAttributeNS(STYLE, "style:name", "Scale");
  scale.setAttributeNS(STYLE, "style:family", "paragraph");
  auto props = p.xml.createElementNS(STYLE, "style:text-properties");
  props.setAttributeNS(FO, "fo:font-size", "150%");
  scale.appendChild(props);
  group.appendChild(scale);
  for (auto para : p.xml.elementsByTagNameNS(TEXT, "p"))
    if (plain_text(para) == "Blue rich\ttail")
      para.setAttributeNS(TEXT, "text:style-name", "Scale");
  p.set_entry("styles.xml", external.toByteArray(), "text/xml");
  return p;
}
OdfPackage nested() {
  return package(
      R"XML(<table:table table:name="Outer"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3"><table:table-cell table:number-columns-repeated="3"><text:p>Outer</text:p><table:table table:name="Inner"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell><text:p>I00</text:p></table:table-cell><table:table-cell><text:p>I01</text:p></table:table-cell></table:table-row><table:table-row><table:table-cell><text:p>I10</text:p></table:table-cell><table:table-cell><text:p>I11</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table>)XML");
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-table-merge-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto save = [&](OdfPackage p, const char *name) {
    unique(p);
    p.save(Text((dir / (std::string(name) + ".odt")).string()));
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
  };
  auto empty = package(
      "<table:table "
      "table:name=\"Empty\"><table:table-column/"
      "><table:table-row><table:table-cell/></table:table-row></table:table>");
  auto empty_before = empty.xml.toByteArray();
  auto empty_address = address(empty, "Empty", 0, 0);
  assert(merge_words_table_cells(empty, empty_address, 1, 1)
             .document.xml.toByteArray() == empty_before);
  assert(
      split_words_table_cell(empty, empty_address).document.xml.toByteArray() ==
      empty_before);
  auto source = ordinary();
  save(source, "source");
  auto before = source.xml.toByteArray();
  auto a = address(source, "Grid", 1, 0);
  auto edit = merge_words_table_cells(source, a, 2, 2);
  assert(source.xml.toByteArray() == before);
  auto f = read_text_flow(edit.document);
  const auto &m = f.tables[0];
  assert(m.rows.size() == 3 && m.columns.size() == 3 && m.cells.size() == 6);
  assert(edit.cell == TextCellAddress({0, 1, 0, 2, 2}));
  assert(at(m, 1, 1).source == at(m, 2, 0).source && at(m, 1, 0).rows == 2 &&
         at(m, 1, 0).columns == 2);
  auto paras = visible(edit.document);
  std::vector<Text> words;
  for (auto p : paras)
    words.push_back(p.text);
  assert((words == std::vector<Text>{"Before", "H1", "H2", "H3", "A1", "A2",
                                     "B1", "B2", "A3", "B3", "After"}));
  save(edit.document, "merged");
  auto split = split_words_table_cell(edit.document, *edit.cell, edit.cells);
  save(split.document, "split");
  assert(read_text_flow(split.document).tables[0].cells.size() == 9);
  assert(at(read_text_flow(split.document).tables[0], 1, 1).blocks.size() == 1);
  auto no = split_words_table_cell(source, a);
  assert(no.document.xml.toByteArray() == before);
  no = merge_words_table_cells(edit.document, *edit.cell, 2, 2, edit.cells);
  assert(no.document.xml.toByteArray() == edit.document.xml.toByteArray());
  for (auto dims : {std::pair<size_t, size_t>{0, 1}, {100, 1}, {1, 0}})
    rejects(
        [&] { merge_words_table_cells(source, a, dims.first, dims.second); });
  rejects([&] {
    merge_words_table_cells(source, address(source, "Grid", 0, 0), 2, 2);
  });
  rejects([&] {
    merge_words_table_cells(edit.document, *edit.cell, 1, 2, edit.cells);
  });
  auto oldmerge = merged(true);
  save(oldmerge, "old-merged");
  auto unfolded =
      split_words_table_cell(oldmerge, address(oldmerge, "Merged", 0, 0));
  save(unfolded.document, "hidden-split");
  assert(read_text_flow(unfolded.document).tables[0].cells.size() == 12);
  assert(cell_text(unfolded.document, "Merged", 1, 0) == "hidden");
  auto p = styled();
  save(p, "styled-source");
  auto blue_before = find(p, "Blue rich\ttail"),
       nested_before = find(p, "Nested blue");
  auto styled_merge =
      merge_words_table_cells(p, address(p, "Styled", 0, 0), 1, 2);
  save(styled_merge.document, "styled-merged");
  auto blue_after = find(styled_merge.document, "Blue rich\ttail"),
       nested_after = find(styled_merge.document, "Nested blue");
  for (auto pair : {std::pair{blue_before, blue_after},
                    std::pair{nested_before, nested_after}}) {
    assert(pair.first.font == pair.second.font &&
           pair.first.color == pair.second.color &&
           pair.first.decorations == pair.second.decorations);
    assert(pair.first.left == pair.second.left &&
           pair.first.before == pair.second.before &&
           pair.first.alignment == pair.second.alignment);
    assert(pair.first.tabs.size() == pair.second.tabs.size());
    assert(pair.first.spans.size() == pair.second.spans.size());
    for (size_t i = 0; i < pair.first.spans.size(); ++i)
      assert(pair.first.spans[i].font == pair.second.spans[i].font &&
             pair.first.spans[i].color == pair.second.spans[i].color);
  }
  save(split_words_table_cell(styled_merge.document, *styled_merge.cell,
                              styled_merge.cells)
           .document,
       "styled-split");

  auto common = common_styled();
  save(common, "common-styled-source");
  auto old_common = find(common, "Blue rich\ttail");
  assert(old_common.font.size == 22.5 && old_common.font.language.empty());
  assert(old_common.tabs.size() == 1 &&
         old_common.tabs[0].leader_font.color == TextColor({22, 90, 167}));
  auto common_merge =
      merge_words_table_cells(common, address(common, "Styled", 0, 0), 1, 2);
  save(common_merge.document, "common-styled-merged");
  auto new_common = find(common_merge.document, "Blue rich\ttail");
  assert(new_common.font == old_common.font &&
         new_common.color == old_common.color);
  assert(new_common.tabs.size() == 1 &&
         new_common.tabs[0].leader_font.color ==
             old_common.tabs[0].leader_font.color);
  save(split_words_table_cell(common_merge.document, *common_merge.cell,
                              common_merge.cells)
           .document,
       "common-styled-split");
  auto repeats = repeated();
  save(repeats, "repeated-source");
  auto repeated_merge =
      merge_words_table_cells(repeats, address(repeats, "Repeat", 1, 1), 2, 2);
  save(repeated_merge.document, "repeated-merged");
  auto rf = read_text_flow(repeated_merge.document);
  assert(rf.tables[table_id(rf, "Repeat")].cells.size() == 6);
  auto n = nested();
  save(n, "nested-source");
  std::vector<TextCellAddress> path{address(n, "Outer", 1, 2),
                                    address(n, "Inner", 0, 0)};
  auto inner_merge = merge_words_table_cells(n, path.back(), 2, 2, path);
  save(inner_merge.document, "nested-merged");
  assert(inner_merge.cells.size() == 2 && inner_merge.cells[0].row == 1 &&
         inner_merge.cells[0].column == 2);
  auto outer = read_text_flow(inner_merge.document).blocks[1].table;
  assert(outer);
  size_t leaves = 0, merged_count = 0;
  for (const auto &c : outer->cells)
    for (const auto &b : c.blocks)
      if (b.kind == TextBlock::Table) {
        leaves += b.table->cells.size();
        if (b.table->cells.size() == 1)
          ++merged_count;
      }
  assert(leaves == 33 && merged_count == 1);
  save(split_words_table_cell(inner_merge.document, *inner_merge.cell,
                              inner_merge.cells)
           .document,
       "nested-split");
  rejects([&] { merge_words_table_cells(n, path.back(), 2, 2); });
  auto valued = ordinary();
  auto target = at(read_text_flow(valued).tables[0], 1, 1).source;
  target.setAttributeNS(OFFICE, "office:value-type", "float");
  target.setAttributeNS(OFFICE, "office:value", "3");
  rejects([&] {
    merge_words_table_cells(valued, address(valued, "Grid", 1, 0), 2, 2);
  });
  target.removeAttributeNS(OFFICE, "value-type");
  target.removeAttributeNS(OFFICE, "value");
  target.setAttributeNS(TABLE, "table:formula", "of:=1+2");
  rejects([&] {
    merge_words_table_cells(valued, address(valued, "Grid", 1, 0), 2, 2);
  });
  TextDocument d;
  d.load(Text((dir / "source.odt").string()));
  auto initial = d.style_snapshot();
  TextSelection previous{
      d.text().find(Text("A1")), d.text().find(Text("A1")), a, {a}};
  TextSelection next{edit.cursor, edit.cursor, edit.cell, edit.cells};
  assert(d.commit_structure(edit.document, previous, next, d.revision()));
  d.replace(next.cursor, 0, "X", true, next, next);
  save(d.style_snapshot(), "typed-merged");
  assert(d.undo());
  assert(d.undo() == previous &&
         d.style_snapshot().xml.toByteArray() == initial.xml.toByteArray());
  assert(d.redo() == next);
  assert(d.style_snapshot().xml.toByteArray() ==
         edit.document.xml.toByteArray());
  auto revision = d.revision();
  rejects([&] {
    d.commit_structure(split.document, next,
                       {split.cursor, split.cursor, split.cell, split.cells},
                       revision - 1);
  });
  std::cout << "PASS native Words cell merge/split, rich inheritance, "
               "repeated/nested scope and structural history\n";
}
