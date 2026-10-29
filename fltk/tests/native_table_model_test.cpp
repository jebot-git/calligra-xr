// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_layout.h"
#include "native_table_model.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
OdfPackage package(const Text &body, const Text &styles = {}) {
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
void near(double a, double b) { assert(std::abs(a - b) < 1e-9); }
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
Text wrap(const Text &body) {
  return "<table:table>" + body + "</table:table>";
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--inspect") {
    TextDocument document;
    document.load(Text(argv[2]));
    auto flow = document.flow();
    using Json = nlohmann::json;
    auto blocks = [](const std::vector<TextBlock> &source) {
      Json result = Json::array();
      for (const auto &b : source)
        result.push_back(
            {{"kind", b.kind == TextBlock::Table ? "table" : "paragraph"},
             {"index", b.index}});
      return result;
    };
    Json report = {{"blocks", blocks(flow.blocks)},
                   {"paragraphs", Json::array()},
                   {"tables", Json::array()}};
    for (const auto &p : flow.paragraphs)
      report["paragraphs"].push_back(p.text.toStdString());
    for (const auto &table : flow.tables) {
      Json t = {{"name", table.name.toStdString()},
                {"rows", table.rows.size()},
                {"columns", table.columns.size()},
                {"cells", Json::array()},
                {"headers", Json::array()}};
      for (const auto &r : table.rows)
        t["headers"].push_back(r.header);
      for (const auto &cell : table.cells) {
        Json c = {{"row", cell.row},
                  {"column", cell.column},
                  {"rows", cell.rows},
                  {"columns", cell.columns},
                  {"blocks", blocks(cell.blocks)}};
        if (cell.style.background)
          c["background"] = {cell.style.background->red,
                             cell.style.background->green,
                             cell.style.background->blue};
        t["cells"].push_back(c);
      }
      report["tables"].push_back(t);
    }
    std::cout << report.dump() << '\n';
    return 0;
  }
  Text styles = R"(<office:styles>
<style:style style:name="Base" style:family="table-cell"><style:table-cell-properties fo:padding="2pt" fo:border="1pt solid #112233"/></style:style>
<style:style style:name="Red" style:family="table-cell" style:parent-style-name="Base"><style:table-cell-properties fo:background-color="#ff0000"/></style:style>
<style:style style:name="Green" style:family="table-cell" style:parent-style-name="Base"><style:table-cell-properties fo:background-color="#00ff00"/></style:style>
<style:style style:name="Blue" style:family="table-cell" style:parent-style-name="Base"><style:table-cell-properties fo:background-color="#880000"/></style:style>
</office:styles><office:automatic-styles>
<style:style style:name="T" style:family="table" style:master-page-name="First"><style:table-properties style:width="240pt" style:rel-width="75%" table:align="center" table:border-model="collapsing" style:writing-mode="rl-tb" fo:margin-top="4pt" fo:margin-bottom="5pt" fo:break-after="page" fo:keep-with-next="always" style:may-break-between-rows=" false "/></style:style>
<style:style style:name="Fixed" style:family="table-column"><style:table-column-properties style:column-width="40pt"/></style:style>
<style:style style:name="Relative" style:family="table-column"><style:table-column-properties style:rel-column-width="2*" style:use-optimal-column-width="true"/></style:style>
<style:style style:name="Row" style:family="table-row"><style:table-row-properties style:min-row-height="18pt" fo:keep-together="always" fo:background-color="#eeeeee"/></style:style>
<style:style style:name="Blue" style:family="table-cell" style:parent-style-name="Base"><style:table-cell-properties fo:background-color="#0000ff" fo:padding-left="3pt" fo:border-left="2pt double #334455" style:border-line-width-left=".5pt 1pt .5pt" style:vertical-align="middle"/></style:style>
</office:automatic-styles>)";
  Text body =
      R"(<text:p>Before</text:p><table:table table:name="Grid" table:style-name="T">
<table:table-column table:style-name="Fixed" table:default-cell-style-name="Red"/>
<table:table-column table:style-name="Relative"/><table:table-column table:style-name="Fixed"/>
<table:table-header-rows><table:table-row><table:table-cell table:number-columns-spanned="2" table:style-name="Blue"><text:p>Heading</text:p></table:table-cell><table:covered-table-cell/><table:table-cell><text:p>Header C</text:p></table:table-cell></table:table-row></table:table-header-rows>
<table:table-row table:style-name="Row" table:default-cell-style-name="Green"><table:table-cell table:number-columns-spanned="2" table:number-rows-spanned="2"><text:p>Merged</text:p></table:table-cell><table:covered-table-cell/><table:table-cell><text:p>B</text:p></table:table-cell></table:table-row>
<table:table-row><table:covered-table-cell table:number-columns-repeated="2"/><table:table-cell><text:p>C</text:p></table:table-cell></table:table-row>
<table:table-row table:number-rows-repeated="2"><table:table-cell table:number-columns-repeated="3"><text:p>Repeat</text:p><table:table table:name="Nested"><table:table-row><table:table-cell><text:p>Nested</text:p></table:table-cell></table:table-row></table:table><text:p>Last</text:p></table:table-cell></table:table-row>
</table:table><text:p>After</text:p>)";
  auto original = package(body, styles);
  auto flow = read_text_flow(original);
  assert(flow.blocks.size() == 3 &&
         flow.blocks[0].kind == TextBlock::Paragraph &&
         flow.blocks[1].kind == TextBlock::Table);
  assert(flow.tables.size() == 2 && flow.blocks[1].index == 1 &&
         flow.paragraphs.size() == 10);
  assert(flow.paragraphs[0].text == "Before" &&
         flow.paragraphs[9].text == "After");
  const auto &table = flow.tables[1];
  assert(table.name == "Grid" && table.rows.size() == 5 &&
         table.columns.size() == 3 && table.cells.size() == 11);
  near(*table.width, 240);
  near(*table.relative_width, .75);
  near(*table.columns[0].width, 40);
  near(table.columns[1].relative, 2);
  assert(table.columns[1].optimal && table.rtl && table.collapsing &&
         table.keep_next && !table.may_break_rows && table.break_after &&
         table.master == "First");
  assert(table.rows[0].header && !table.rows[1].header &&
         table.rows[1].keep_together);
  near(table.rows[1].minimum, 18);
  const auto &heading = table.cells[0];
  assert(heading.rows == 1 && heading.columns == 2 &&
         heading.style.background->blue == 255 &&
         heading.style.background->red == 0);
  near(heading.style.padding[3], 3);
  near(heading.style.padding[0], 2);
  near(heading.style.borders[3].width, 2);
  assert(heading.style.borders[3].style == TextTableBorder::Double &&
         heading.style.vertical == TextCellStyle::Middle);
  assert(heading.style.borders[3].double_widths);
  near((*heading.style.borders[3].double_widths)[1], 1);
  const auto &merged = table.cells[2];
  assert(merged.style_name == "Green" && heading.style_name == "Blue");
  assert(merged.rows == 2 && merged.columns == 2 &&
         merged.style.background->green == 255);
  for (size_t i = 5; i < table.cells.size(); ++i) {
    const auto &cell = table.cells[i];
    assert(cell.blocks.size() == 3 && cell.blocks[0].index == 6 &&
           cell.blocks[1].kind == TextBlock::Table &&
           cell.blocks[1].index == 0 && cell.blocks[2].index == 8);
  }
  assert(table.cells[5].style.background->red ==
         255); // Column default survives repetition.
  std::cout << "PASS ordered blocks, nested source identity, repeated "
               "rows/cells, merged spans, header rows and scoped styles\n";
  auto grouped = read_text_flow(
      package(wrap("<table:table-header-columns><table:table-column "
                   "table:visibility=\"collapse\"/></"
                   "table:table-header-columns><table:table-column/"
                   "><table:table-rows><table:table-row><table:table-cell "
                   "table:number-columns-repeated=\"2\"/></table:table-row></"
                   "table:table-rows>")));
  assert(grouped.tables[0].columns[0].header &&
         !grouped.tables[0].columns[0].visible &&
         !grouped.tables[0].columns[1].header);
  auto hidden = read_text_flow(package(
      wrap("<table:table-row><table:table-cell "
           "table:number-columns-spanned=\"2\"><text:p>Visible</text:p></"
           "table:table-cell><table:covered-table-cell><text:p>Hidden "
           "source</text:p></table:covered-table-cell></table:table-row>") +
      "<text:p>Following</text:p>"));
  assert(hidden.paragraphs.size() == 3 &&
         hidden.paragraphs[1].text == "Hidden source");
  assert(hidden.blocks.back().kind == TextBlock::Paragraph &&
         hidden.blocks.back().index == 2);
  assert(hidden.tables[0].cells.size() == 1 &&
         hidden.tables[0].cells[0].blocks[0].index == 0);
  // ODF 19.501 ignores paragraph master assignments inside tables. The same
  // inherited style still assigns the master to root paragraphs; repeated
  // cells and their scoped/nested formatting must retain that distinction.
  auto cell_master = package(
      R"(<text:p text:style-name="Child">Root</text:p><table:table><table:table-row><table:table-cell table:number-columns-repeated="2"><text:p text:style-name="Child">Cell heading</text:p><text:p>Cell body</text:p><table:table><table:table-row><table:table-cell><text:p text:style-name="Child">Nested heading</text:p><text:p>Nested body</text:p></table:table-cell></table:table-row></table:table></table:table-cell></table:table-row></table:table><text:p text:style-name="Child">Root after</text:p>)",
      R"(<office:styles><style:style style:name="Parent" style:family="paragraph" style:master-page-name="Master"><style:paragraph-properties fo:keep-with-next="always"/><style:text-properties fo:font-size="14pt"/></style:style><style:style style:name="Child" style:family="paragraph" style:parent-style-name="Parent"/></office:styles>)");
  auto cell_master_xml = cell_master.xml.toByteArray();
  auto cell_master_flow = read_text_flow(cell_master);
  assert(cell_master_flow.paragraphs.size() == 6);
  assert(cell_master_flow.paragraphs[0].master == "Master" &&
         cell_master_flow.paragraphs[5].master == "Master");
  for (size_t index : {1, 3}) {
    const auto &p = cell_master_flow.paragraphs[index];
    assert(p.master.empty() && p.keep_next && p.font.size == 14);
  }
  const auto &outer = block_table(cell_master_flow, cell_master_flow.blocks[1]);
  assert(outer.cells.size() == 2);
  for (const auto &cell : outer.cells) {
    const auto &p = block_paragraph(cell_master_flow, cell.blocks[0]);
    assert(p.master.empty() && p.keep_next && p.font.size == 14);
    const auto &inner = block_table(cell_master_flow, cell.blocks[2]);
    const auto &nested =
        block_paragraph(cell_master_flow, inner.cells[0].blocks[0]);
    assert(nested.master.empty() && nested.keep_next && nested.font.size == 14);
  }
  assert(cell_master.xml.toByteArray() == cell_master_xml);
  // Automatic styles in master/header sources have their own namespace even
  // when the caller parsed styles.xml into a different XML document object.
  auto scoped = package(body, styles);
  Text master_xml =
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:automatic-styles><style:style style:name="Blue" style:family="table-cell"><style:table-cell-properties fo:background-color="#773355"/></style:style></office:automatic-styles><office:master-styles><style:master-page style:name="Master"><style:header><table:table><table:table-row><table:table-cell table:style-name="Blue"><text:p>Header source</text:p></table:table-cell></table:table-row></table:table></style:header></style:master-page></office:master-styles></office:document-styles>)";
  scoped.set_entry("styles.xml", master_xml.toUtf8(), "text/xml");
  auto separate = read_document_xml(scoped.entry("styles.xml"));
  auto header =
      child(child(child(separate.documentElement(), OFFICE, "master-styles"),
                  STYLE, "master-page"),
            STYLE, "header");
  OdfTextReader reader(scoped);
  for (int i = 0; i < 3; ++i) {
    auto master = reader.frame(header, TextParagraph{}, {}, true);
    assert(master.tables.size() == 1 && master.paragraphs.size() == 1);
    assert(master.tables[0].cells[0].style.background->red == 0x77);
    auto content = reader.frame(scoped.body(), TextParagraph{});
    assert(content.tables.size() == 2 &&
           content.tables[1].cells[0].style.background->blue == 255);
  }
  // The structural model is read-only; the source XML and saved tables survive
  // edits.
  TempDir tmp;
  Text path = tmp.path() + "/tables.odt";
  original.save(path);
  TextDocument doc;
  doc.load(path);
  Text before = doc.text();
  assert(
      before ==
      "Before\nHeading\nHeader C\nMerged\nB\nC\nRepeat\nNested\nLast\nAfter");
  doc.set_text("Changed" + before.mid(6));
  doc.save(path);
  TextDocument reopened;
  reopened.load(path);
  auto restored = reopened.flow();
  assert(restored.tables.size() == 2 && restored.tables[1].cells.size() == 11 &&
         restored.paragraphs[0].text == "Changed");
  for (auto xml :
       {wrap("<table:table-row><table:covered-table-cell/></table:table-row>"),
        wrap("<table:table-row><table:table-cell "
             "table:number-columns-spanned=\"2\"/><table:table-cell/></"
             "table:table-row>"),
        wrap("<table:table-row><table:table-cell "
             "table:number-rows-spanned=\"2\"/></table:table-row>"),
        wrap("<table:table-row><table:table-cell "
             "table:number-columns-repeated=\"0\"/></table:table-row>"),
        wrap("<table:table-row table:number-rows-repeated=\"100001\"/>"),
        wrap("<table:table-column table:number-columns-repeated=\"1025\"/>"),
        wrap("<table:table-column "
             "table:number-columns-repeated=\"1024\"/><table:table-row "
             "table:number-rows-repeated=\"100000\"/>"),
        wrap("<table:table-column/><table:table-row><table:table-cell "
             "table:number-columns-repeated=\"2\"/></table:table-row>")})
    rejects([&] { read_text_flow(package(xml)); });
  for (auto property :
       {"fo:padding=\"-1pt\"", "fo:border=\"1pt nonsense #000000\"",
        "style:vertical-align=\"bogus\"",
        "style:border-line-width=\"1pt 2pt\""}) {
    auto invalid = package(
        wrap("<table:table-row><table:table-cell "
             "table:style-name=\"Bad\"/></table:table-row>"),
        "<office:automatic-styles><style:style style:name=\"Bad\" "
        "style:family=\"table-cell\"><style:table-cell-properties " +
            Text(property) + "/></style:style></office:automatic-styles>");
    rejects([&] { read_text_flow(invalid); });
  }
  auto cyclic = package(
      wrap("<table:table-row><table:table-cell "
           "table:style-name=\"A\"/></table:table-row>"),
      R"(<office:styles><style:style style:name="A" style:family="table-cell" style:parent-style-name="B"/><style:style style:name="B" style:family="table-cell" style:parent-style-name="A"/></office:styles>)");
  rejects([&] { read_text_flow(cyclic); });
  Text deeply_nested = "<text:p>Deep</text:p>";
  for (int i = 0; i < 17; ++i)
    deeply_nested = wrap("<table:table-row><table:table-cell>" + deeply_nested +
                         "</table:table-cell></table:table-row>");
  auto excessive = package(deeply_nested);
  rejects([&] { reader.frame(excessive.body(), TextParagraph{}); });
  auto recovered = reader.frame(scoped.body(), TextParagraph{});
  assert(recovered.tables.size() == 2 && recovered.paragraphs.size() == 10);
  // Repetition expands the grid but reads the original paragraph only once.
  auto large = read_text_flow(package(
      wrap("<table:table-row table:number-rows-repeated=\" +20000 "
           "\"><table:table-cell "
           "table:number-columns-repeated=\"2\"><text:p>Shared</text:p></"
           "table:table-cell></table:table-row>")));
  assert(large.tables[0].cells.size() == 40000 &&
         large.paragraphs.size() == 1 &&
         large.tables[0].cells.back().blocks[0].index == 0);
  std::cout
      << "PASS preserved source editing/save, invalid grids/styles/resource "
         "rejection and bounded 40,000-cell source reuse\n";
}
