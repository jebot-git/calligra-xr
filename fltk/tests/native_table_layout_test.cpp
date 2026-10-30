// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_layout.h"
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace DomeOffice;
namespace {
void near(double a, double b) {
  if (std::abs(a - b) > 1e-7)
    std::cerr << a << " != " << b << '\n';
  assert(std::abs(a - b) < 1e-7);
}
TextMetrics metric(const Text &s, const TextFont &f) {
  return {s.size() * f.size / 2, f.size * .8, f.size * .2};
}
OdfPackage package(const Text &body, const Text &styles) {
  OdfPackage p(Kind::Words);
  Text xml =
      "<office:document-content "
      "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
      "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
      "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
      "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
      "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1."
      "0\">" +
      styles + "<office:body><office:text>" + body +
      "</office:text></office:body></office:document-content>";
  p.xml = read_document_xml(xml.toUtf8());
  return p;
}
TextFlow grid() {
  TextFlow flow;
  TextTable table;
  table.columns.resize(2);
  table.columns[0].relative = 2;
  table.columns[1].relative = 1;
  table.rows.resize(2);
  table.rows[1].height = 20;
  for (size_t i = 0; i < 4; ++i) {
    TextParagraph p;
    p.text = Text::number(i + 1);
    p.font.size = 10;
    p.line_factor = 1;
    flow.paragraphs.push_back(p);
    TextTableCell c;
    c.row = i / 2;
    c.column = i % 2;
    c.style.padding.fill(5);
    c.style.borders.fill({TextTableBorder::Solid, 1, {0, 0, 0}, {}});
    c.style.background = TextColor{255, 240, 200};
    c.blocks.push_back({TextBlock::Paragraph, i});
    table.cells.push_back(c);
  }
  flow.tables.push_back(table);
  flow.blocks.push_back({TextBlock::Table, 0});
  return flow;
}
} // namespace
int main() {
  auto flow = grid();
  auto box = layout_text_box(flow, 300, metric);
  assert(box.lines.size() == 4 && box.cells.size() == 4);
  for (size_t i = 0; i < box.lines.size(); ++i) {
    auto context = box.lines[i].cell_context;
    assert(context && !context->nested && context->address.table == 0 &&
           context->address.row == i / 2 && context->address.column == i % 2);
  }
  near(box.height, 42);
  near(box.cells[0].rect.width, 200);
  near(box.cells[1].rect.x, 200);
  near(box.cells[1].rect.width, 100);
  near(box.lines[0].x, 6);
  near(box.lines[0].y, 6);
  near(box.lines[0].clip->width, 188);
  near(box.lines[2].clip->height, 8);
  near(box.lines[2].y, 28);
  assert(box.paints.size() ==
         20); // Four cell fills and four separate borders per cell.
  flow.tables[0].collapsing = true;
  box = layout_text_box(flow, 300, metric);
  near(box.height, 41);
  near(box.lines[0].x, 5.5);
  near(box.lines[2].clip->height, 9);
  assert(box.paints.size() ==
         16); // Each shared edge is painted once, split at grid nodes.
  flow.tables[0].rtl = true;
  box = layout_text_box(flow, 300, metric);
  near(box.cells[0].rect.x, 100);
  near(box.cells[1].rect.x, 0);
  assert(box.lines[0].paragraph == 0 && box.lines[1].paragraph == 1);
  flow = grid();
  flow.tables[0].collapsing = true;
  flow.tables[0].rtl = true;
  flow.tables[0].cells[1].style.borders[1].width = 4;
  auto rtl_thick = layout_text_box(flow, 300, metric);
  near(rtl_thick.lines[0].x, 107);
  flow = grid();
  flow.tables[0].rows[0].height = 42;
  flow.tables[0].cells[0].style.vertical = TextCellStyle::Middle;
  near(layout_text_box(flow, 300, metric).lines[0].y, 16);
  flow.tables[0].cells[0].style.vertical = TextCellStyle::Bottom;
  near(layout_text_box(flow, 300, metric).lines[0].y, 26);
  flow = grid();
  for (auto &c : flow.tables[0].columns) {
    c.relative = 0;
    c.width.reset();
  }
  near(layout_text_box(flow, 300, metric).cells[0].rect.width, 150);
  flow.tables[0].columns[0].visible = false;
  auto hidden_column = layout_text_box(flow, 300, metric);
  assert(hidden_column.cells.size() == 2 && hidden_column.lines.size() == 2);
  near(hidden_column.cells[0].rect.width, 300);
  flow = grid();
  flow.tables[0].collapsing = true;
  flow.tables[0].rows.insert(flow.tables[0].rows.begin() + 1, TextTableRow{});
  flow.tables[0].rows[1].visible = false;
  flow.tables[0].cells[2].row = flow.tables[0].cells[3].row = 2;
  for (size_t col = 0; col < 2; ++col) {
    auto cell = flow.tables[0].cells[col];
    cell.row = 1;
    for (auto &edge : cell.style.borders)
      edge.width = 10;
    flow.tables[0].cells.push_back(cell);
  }
  near(layout_text_box(flow, 300, metric).height, 41);
  flow.tables[0].rows[1].visible = true;
  flow.tables[0].rows[1].height = 0;
  near(layout_text_box(flow, 300, metric).height, 41);
  Text styles =
      R"(<office:styles><style:default-style style:family="paragraph"><style:text-properties fo:font-size="10pt"/><style:paragraph-properties fo:line-height="100%"/></style:default-style>
<style:style style:name="Base" style:family="table-cell"><style:text-properties fo:font-size="20pt" fo:color="#cc0000"/></style:style>
<style:style style:name="Large" style:family="table-cell"><style:text-properties fo:font-size="30pt"/></style:style>
<style:style style:name="Nested" style:family="table-cell"><style:text-properties fo:font-size="150%"/></style:style>
<style:style style:name="P" style:family="paragraph"><style:text-properties fo:font-size="150%"/></style:style>
<style:style style:name="Half" style:family="text"><style:text-properties fo:font-size="50%" fo:font-style="italic" fo:color="#0000cc"/></style:style></office:styles>)";
  Text body =
      R"(<table:table><table:table-column table:default-cell-style-name="Base"/><table:table-column table:default-cell-style-name="Large"/><table:table-column table:default-cell-style-name="Base"/><table:table-row><table:table-cell table:number-columns-repeated="3"><text:p text:style-name="P">x<text:span text:style-name="Half">y</text:span></text:p><table:table><table:table-row><table:table-cell table:style-name="Nested"><text:p text:style-name="P">n</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table><text:p>After</text:p>)";
  auto p = package(body, styles);
  auto styled = read_text_flow(p);
  assert(styled.paragraphs.size() == 3);
  auto table = styled.blocks[0].table;
  assert(table && table->cells.size() == 3);
  auto first = table->cells[0].blocks[0].formatted,
       second = table->cells[1].blocks[0].formatted,
       third = table->cells[2].blocks[0].formatted;
  assert(first && second && first == third && first != second);
  near(first->font.size, 30);
  near(second->font.size, 45);
  near(first->spans[1].font.size, 15);
  assert(first->spans[1].font.italic && first->spans[1].color->blue == 204);
  auto child = table->cells[0].blocks[1].table;
  auto child2 = table->cells[1].blocks[1].table;
  assert(child && child2 && child != child2 &&
         child == table->cells[2].blocks[1].table);
  near(child->cells[0].blocks[0].formatted->font.size, 45);
  near(child2->cells[0].blocks[0].formatted->font.size, 67.5);
  box = layout_text_box(styled, 300, metric);
  assert(box.lines.size() == 7 && box.cells.size() == 6);
  assert(box.lines[0].paragraph == 0 && box.lines[1].paragraph == 1 &&
         box.lines[6].paragraph == 2);
  near(box.lines[0].style->font.size, 30);
  near(box.lines[2].style->font.size, 45);
  near(box.lines[3].style->font.size, 67.5);
  NativeTextShaper shaper("DejaVu Sans");
  auto shaped = layout_text_box(styled, 300, shaper);
  assert(shaped.lines.size() == 7 && shaped.lines[0].shaped &&
         shaped.lines[0].shaped->glyphs.size() == 2);
  assert(shaped.lines[0].shaped->glyphs[0].color->red == 204 &&
         shaped.lines[0].shaped->glyphs[1].color->blue == 204);
  // Text-only edits preserve inline line-break nodes and the original cells.
  auto mixed = package(
      "<table:table><table:table-row><table:table-cell><text:p>Left<text:line-"
      "break/>lower</text:p></"
      "table:table-cell><table:table-cell><text:p>Right</text:p></"
      "table:table-cell></table:table-row></table:table><text:p>After</text:p>",
      {});
  TempDir temp;
  auto path = temp.path() + "/mixed.odt";
  mixed.save(path);
  TextDocument document;
  document.load(path);
  auto original = document.text();
  document.set_text("Edited " + original);
  document.save(path);
  document.load(path);
  assert(document.text() == "Edited Left\nlower\nRight\nAfter");
  auto current = document.text();
  auto position = current.indexOf("lower");
  current = current.left(position) + "inside" + current.mid(position + 5);
  document.set_text(current);
  document.save(path);
  OdfPackage saved(Kind::Words);
  saved.load(path);
  assert(saved.xml.elementsByTagNameNS(TABLE, "table-cell").size() == 2 &&
         saved.xml.elementsByTagNameNS(TEXT, "line-break").size() == 1);
  auto breaks = saved.xml.elementsByTagNameNS(TEXT, "line-break");
  assert(breaks[0].parentNode().toElement().localName() == "p");
  auto invalid = grid();
  invalid.tables[0].columns[0].width = std::numeric_limits<double>::quiet_NaN();
  bool rejected = false;
  try {
    layout_text_box(invalid, 300, metric);
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "PASS native column/row geometry, fixed-row clipping, border "
               "grids, RTL and nested/repeated cell font inheritance\n";
}
