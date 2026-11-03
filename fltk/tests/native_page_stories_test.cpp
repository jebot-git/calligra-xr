// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_page_stories.h"
#include "native_words_story.h"
#include "native_text_editing.h"
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
void close(double a, double b) { assert(std::abs(a - b) < 1e-6); }
TextMetrics metrics(const Text &text, const TextFont &) {
  return {double(text.size()) * 5, 8, 2};
}
OdfPackage fixture(bool variants = false, size_t pages = 2) {
  OdfPackage p(Kind::Words);
  Text xml =
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="P1" style:family="paragraph"><style:text-properties fo:font-size="20pt" fo:color="#008000"/></style:style><style:style style:name="Next" style:family="paragraph"><style:paragraph-properties fo:break-before="page"/></style:style><style:style style:name="MasterB" style:family="paragraph" style:master-page-name="B"/><style:style style:name="MasterA" style:family="paragraph" style:master-page-name="A"/></office:automatic-styles><office:body><office:text>)";
  for (size_t i = 0; i < pages; ++i) {
    Text style = i == 0   ? "P1"
                 : i == 3 ? "MasterB"
                 : i == 4 ? "MasterA"
                          : "Next";
    xml += "<text:p text:style-name=\"" + style + "\">Body" +
           Text::number(i + 1) + "</text:p>";
  }
  xml += "</office:text></office:body></office:document-content>";
  p.xml = read_document_xml(xml.toUtf8());
  Text styles =
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" office:version="1.3"><office:styles><style:default-style style:family="paragraph"><style:text-properties fo:font-family="monospace" fo:font-size="12pt"/></style:default-style><style:style style:name="P1" style:family="paragraph"><style:text-properties fo:font-size="14pt" fo:color="#0000ff"/></style:style></office:styles><office:automatic-styles><style:style style:name="P1" style:family="paragraph"><style:text-properties fo:font-size="10pt" fo:color="#ff0000"/></style:style><style:page-layout style:name="Layout"><style:page-layout-properties fo:page-width="250pt" fo:page-height="200pt" fo:margin="10pt"/><style:header-style><style:header-footer-properties fo:min-height="20pt" fo:margin-bottom="5pt" fo:padding="2pt" fo:border="1pt solid #220000" fo:background-color="#fafaff"/></style:header-style><style:footer-style><style:header-footer-properties fo:min-height="18pt" fo:margin-top="4pt"/></style:footer-style></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="A" style:page-layout-name="Layout" style:next-style-name="A"><style:header><text:p text:style-name="P1">RightHeader</text:p></style:header>)";
  if (variants)
    styles +=
        R"(<style:header-left><text:p text:style-name="P1">LeftHeader</text:p></style:header-left><style:header-first><text:p text:style-name="P1">FirstHeader</text:p></style:header-first>)";
  styles +=
      R"(<style:footer><text:p>Page <text:page-number text:select-page="current">99</text:page-number>/<text:page-count>999</text:page-count></text:p></style:footer></style:master-page><style:master-page style:name="B" style:page-layout-name="Layout"><style:header><text:p>BHeader</text:p></style:header></style:master-page></office:master-styles></office:document-styles>)";
  p.set_entry("styles.xml", styles.toUtf8(), "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
XmlDocument styles_of(const OdfPackage &p) {
  return read_document_xml(p.entry("styles.xml"));
}
XmlElement master_a(const XmlDocument &styles) {
  for (auto m : styles.elementsByTagNameNS(STYLE, "master-page"))
    if (m.attributeNS(STYLE, "name") == "A")
      return m;
  throw std::runtime_error("Missing master A");
}
void put_styles(OdfPackage &p, const XmlDocument &styles) {
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
}
Text line_text(const TextLine &line) {
  Text text;
  for (const auto &part : line.fragments)
    text += part.text;
  return text;
}
Text story_text(const TextPage &page, TextPageStory::Kind kind) {
  Text text;
  for (const auto &story : page.stories)
    if (story.source->kind == kind)
      for (const auto &line : story.content->lines) {
        if (!text.empty())
          text += "\n";
        text += line_text(line);
      }
  return text;
}
template <class F> void rejects(F fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("page-stories-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto p = fixture();
  auto original = p.xml.toByteArray(), original_styles = p.entry("styles.xml");
  auto flow = read_text_flow(p);
  assert(flow.paragraphs.size() == 2 && flow.paragraphs[0].text == "Body1");
  assert(flow.paragraphs[0].font.size == 20 &&
         flow.paragraphs[0].color == std::optional(TextColor{0, 128, 0}));
  auto pages = paginate_text(flow, metrics);
  assert(pages.pages.size() == 2 && pages.unsupported.empty());
  for (size_t i = 0; i < pages.pages.size(); ++i) {
    const auto &page = pages.pages[i];
    assert(page.lines.size() == 1 && page.lines[0].paragraph == i &&
           page.cells.empty());
    assert(page.stories.size() == 2 &&
           story_text(page, TextPageStory::Header) == "RightHeader");
    assert(story_text(page, TextPageStory::Footer) ==
           "Page " + Text::number(i + 1) + "/2");
    close(page.body_top, 35);
    close(page.body_bottom, 168);
    close(page.lines[0].y, 35);
    const auto &header = page.stories[0];
    close(header.bounds.x, 10);
    close(header.bounds.y, 10);
    close(header.bounds.width, 230);
    close(header.bounds.height, 20);
    assert(header.source->source.localName() == "header" &&
           header.source->master == "A");
    auto parsed = header.source->read({i + 1, 2, i + 1, {}, {}});
    assert(parsed[0].flow.paragraphs[0].font.size == 10);
    assert(parsed[0].flow.paragraphs[0].color ==
           std::optional(TextColor{255, 0, 0}));
    assert(!header.content->paints.empty());
  }
  assert(p.xml.toByteArray() == original &&
         p.entry("styles.xml") == original_styles);
  p.save(Text((dir / "basic.odt").string()));
  NativeTextShaper shaper("monospace", 12);
  auto shaped = paginate_text(flow, shaper);
  assert(shaped.pages.size() == 2);
  for (const auto &page : shaped.pages)
    for (const auto &story : page.stories)
      for (const auto &line : story.content->lines)
        assert(line.shaped && !line.shaped->missing_glyphs);
  TextEditMap map;
  map.reset(flow);
  assert(map.text() == "Body1\nBody2");
  auto variants = fixture(true, 5);
  auto variant_pages = paginate_text(read_text_flow(variants), metrics);
  assert(variant_pages.pages.size() == 5);
  const char *expected[] = {"FirstHeader", "LeftHeader", "RightHeader",
                            "BHeader", "FirstHeader"};
  for (size_t i = 0; i < 5; ++i)
    assert(story_text(variant_pages.pages[i], TextPageStory::Header) ==
           expected[i]);
  variants.save(Text((dir / "variants.odt").string()));
  auto styles = styles_of(variants);
  child(master_a(styles), STYLE, "header-first")
      .setAttributeNS(STYLE, "style:display", "false");
  child(master_a(styles), STYLE, "header-left")
      .setAttributeNS(STYLE, "style:display", "false");
  put_styles(variants, styles);
  variant_pages = paginate_text(read_text_flow(variants), metrics);
  assert(story_text(variant_pages.pages[0], TextPageStory::Header).empty() &&
         story_text(variant_pages.pages[1], TextPageStory::Header).empty());
  close(variant_pages.pages[0].body_top, 10);
  assert(story_text(variant_pages.pages[2], TextPageStory::Header) ==
         "RightHeader");
  variants.save(Text((dir / "disabled.odt").string()));

  auto numbered = fixture(true);
  auto num_styles = styles_of(numbered);
  auto layout_props =
      num_styles.elementsByTagNameNS(STYLE, "page-layout-properties")[0];
  layout_props.setAttributeNS(STYLE, "style:num-format", "I");
  put_styles(numbered, num_styles);
  auto content_styles =
      child(numbered.xml.documentElement(), OFFICE, "automatic-styles");
  for (auto style : children(content_styles, STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "MasterA") {
      auto props =
          numbered.xml.createElementNS(STYLE, "style:paragraph-properties");
      props.setAttributeNS(STYLE, "style:page-number", "6");
      style.appendChild(props);
    }
  child(numbered.body(), TEXT, "p")
      .setAttributeNS(TEXT, "text:style-name", "MasterA");
  auto numbering = paginate_text(read_text_flow(numbered), metrics);
  assert(numbering.pages.size() == 2 && numbering.pages[0].number == 6 &&
         numbering.pages[1].number == 7);
  assert(story_text(numbering.pages[0], TextPageStory::Footer) == "Page VI/2");
  assert(story_text(numbering.pages[1], TextPageStory::Footer) == "Page VII/2");
  assert(story_text(numbering.pages[1], TextPageStory::Header) ==
         "RightHeader");
  numbered.save(Text((dir / "numbered.odt").string()));
  auto fixed_field =
      master_a(num_styles).elementsByTagNameNS(TEXT, "page-number")[0];
  fixed_field.setAttributeNS(TEXT, "text:fixed", "true");
  put_styles(numbered, num_styles);
  numbering = paginate_text(read_text_flow(numbered), metrics);
  assert(story_text(numbering.pages[0], TextPageStory::Footer) == "Page 99/2");
  assert(story_text(numbering.pages[1], TextPageStory::Footer) == "Page 99/2");
  numbered.save(Text((dir / "fixed-field.odt").string()));
  fixed_field.removeAttributeNS(TEXT, "fixed");
  fixed_field.setAttributeNS(TEXT, "text:select-page", "previous");
  fixed_field.setAttributeNS(STYLE, "style:num-format", "1");
  put_styles(numbered, num_styles);
  numbering = paginate_text(read_text_flow(numbered), metrics);
  assert(story_text(numbering.pages[0], TextPageStory::Footer) == "Page /2");
  assert(story_text(numbering.pages[1], TextPageStory::Footer) == "Page 6/2");
  numbered.save(Text((dir / "previous-field.odt").string()));
  fixed_field.setAttributeNS(TEXT, "text:select-page", "next");
  put_styles(numbered, num_styles);
  numbering = paginate_text(read_text_flow(numbered), metrics);
  assert(story_text(numbering.pages[0], TextPageStory::Footer) == "Page 7/2");
  assert(story_text(numbering.pages[1], TextPageStory::Footer) == "Page /2");
  numbered.save(Text((dir / "next-field.odt").string()));

  // Adjacent physical pages need not have consecutive logical numbers.
  for (auto style : children(content_styles, STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "MasterB") {
      auto props =
          numbered.xml.createElementNS(STYLE, "style:paragraph-properties");
      props.setAttributeNS(STYLE, "style:page-number", "3");
      style.appendChild(props);
    }
  auto body_paragraphs = numbered.xml.elementsByTagNameNS(TEXT, "p");
  body_paragraphs[1].setAttributeNS(TEXT, "text:style-name", "MasterB");
  auto master_b = num_styles.elementsByTagNameNS(STYLE, "master-page")[1];
  auto footer_b =
      child(master_a(num_styles), STYLE, "footer").cloneNode(true).toElement();
  footer_b.elementsByTagNameNS(TEXT, "page-number")[0].setAttributeNS(
      TEXT, "text:select-page", "previous");
  master_b.appendChild(footer_b);
  put_styles(numbered, num_styles);
  numbering = paginate_text(read_text_flow(numbered), metrics);
  assert(numbering.pages[0].number == 6 && numbering.pages[1].number == 3);
  assert(story_text(numbering.pages[0], TextPageStory::Footer) == "Page 3/2");
  assert(story_text(numbering.pages[1], TextPageStory::Footer) == "Page 6/2");
  numbered.save(Text((dir / "adjacent-restart-fields.odt").string()));
  auto table_header = fixture();
  auto table_styles = styles_of(table_header);
  auto table_head = child(master_a(table_styles), STYLE, "header");
  while (!table_head.firstChild().isNull())
    table_head.removeChild(table_head.firstChild());
  auto cell_style = table_styles.createElementNS(STYLE, "style:style");
  cell_style.setAttributeNS(STYLE, "style:name", "HeaderCell");
  cell_style.setAttributeNS(STYLE, "style:family", "table-cell");
  auto cell_props =
      table_styles.createElementNS(STYLE, "style:table-cell-properties");
  cell_props.setAttributeNS(FO, "fo:padding", "1pt");
  cell_props.setAttributeNS(FO, "fo:background-color", "#abcdee");
  cell_style.appendChild(cell_props);
  child(table_styles.documentElement(), OFFICE, "automatic-styles")
      .appendChild(cell_style);
  auto table = table_styles.createElementNS(TABLE, "table:table");
  auto col = table_styles.createElementNS(TABLE, "table:table-column");
  col.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  table.appendChild(col);
  auto row = table_styles.createElementNS(TABLE, "table:table-row");
  row.setAttributeNS(TABLE, "table:number-rows-repeated", "2");
  auto cell = table_styles.createElementNS(TABLE, "table:table-cell");
  cell.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  cell.setAttributeNS(TABLE, "table:style-name", "HeaderCell");
  auto grid_para = table_styles.createElementNS(TEXT, "text:p");
  set_plain_text(grid_para, "Grid");
  cell.appendChild(grid_para);
  row.appendChild(cell);
  table.appendChild(row);
  table_head.appendChild(table);
  put_styles(table_header, table_styles);
  auto table_pages = paginate_text(read_text_flow(table_header), metrics);
  assert(table_pages.pages.size() == 2 && table_pages.pages[0].cells.empty());
  const auto &header_table = table_pages.pages[0].stories[0];
  assert(header_table.content->lines.size() == 4);
  assert(std::any_of(
      header_table.content->paints.begin(), header_table.content->paints.end(),
      [](auto paint) { return paint.color == TextColor{171, 205, 238}; }));
  close(header_table.bounds.height, 34);
  close(table_pages.pages[0].body_top, 49);
  table_header.save(Text((dir / "table.odt").string()));
  auto regions = fixture();
  styles = styles_of(regions);
  auto header = child(master_a(styles), STYLE, "header");
  while (!header.firstChild().isNull())
    header.removeChild(header.firstChild());
  for (auto [name, text] : std::vector<std::pair<Text, Text>>{
           {"left", "L"}, {"center", "C"}, {"right", "R"}}) {
    auto region = styles.createElementNS(STYLE, "style:region-" + name);
    auto para = styles.createElementNS(TEXT, "text:p");
    set_plain_text(para, text);
    region.appendChild(para);
    header.appendChild(region);
  }
  put_styles(regions, styles);
  auto region_pages = paginate_text(read_text_flow(regions), metrics);
  const auto &box = region_pages.pages[0].stories[0];
  assert(box.content->lines.size() == 3);
  close(box.content->lines[0].y, box.content->lines[1].y);
  close(box.bounds.height, 20);
  close(box.content->lines[0].x, 13);
  close(box.content->lines[1].x, 122.5);
  close(box.content->lines[2].x, 232);
  regions.save(Text((dir / "regions.odt").string()));
  auto dynamic = fixture();
  styles = styles_of(dynamic);
  header = child(master_a(styles), STYLE, "header");
  auto para = child(header, TEXT, "p");
  set_plain_text(para, "One");
  para.appendChild(styles.createElementNS(TEXT, "text:line-break"));
  para.appendChild(styles.createTextNode("Two"));
  auto props = styles.elementsByTagNameNS(STYLE, "header-footer-properties")[0];
  props.setAttributeNS(FO, "fo:margin-bottom", "10pt");
  put_styles(dynamic, styles);
  auto fixed_gap = paginate_text(read_text_flow(dynamic), metrics);
  close(fixed_gap.pages[0].body_top, 50);
  props.setAttributeNS(STYLE, "style:dynamic-spacing", "true");
  put_styles(dynamic, styles);
  auto dynamic_gap = paginate_text(read_text_flow(dynamic), metrics);
  close(dynamic_gap.pages[0].body_top, 40);
  dynamic.save(Text((dir / "dynamic.odt").string()));
  props.removeAttributeNS(FO, "min-height");
  props.setAttributeNS(SVG, "svg:height", "16pt");
  put_styles(dynamic, styles);
  auto exact = paginate_text(read_text_flow(dynamic), metrics);
  close(exact.pages[0].stories[0].bounds.height, 16);
  close(exact.pages[0].body_top, 36);
  dynamic.save(Text((dir / "fixed-height.odt").string()));
  props.setAttributeNS(SVG, "svg:height", "200pt");
  put_styles(dynamic, styles);
  rejects([&] { paginate_text(read_text_flow(dynamic), metrics); });

  // Header editing uses styles.xml automatic definitions, not the body shadow.
  WordsStoryEdit edit(p, {"A"});
  auto projection = edit.document();
  assert(read_text_flow(projection).paragraphs[0].font.size == 10);
  auto noop = edit.finish(projection, edit.displayed());
  assert(noop.xml.toByteArray() == original &&
         noop.entry("styles.xml") == original_styles);
  TextDocument local;
  local.load_package(projection);
  assert(local.text() == "RightHeader" && !local.can_undo());
  local.replace(local.text().size(), 0, " – edited", true);
  WordsCharacterFormat bold;
  bold.weight = 700;
  assert(local.format_characters({0, local.text().size()}, bold));
  auto edited_header = edit.finish(local.style_snapshot(), true);
  assert(edited_header.xml.toByteArray() == original);
  assert(edited_header.entry("Extras/opaque.bin") ==
         p.entry("Extras/opaque.bin"));
  auto edited_pages = paginate_text(read_text_flow(edited_header), metrics);
  assert(story_text(edited_pages.pages[0], TextPageStory::Header) ==
         "RightHeader – edited");
  assert(read_text_flow(edited_header).paragraphs[0].font.size == 20);
  TextDocument parent;
  parent.load(Text((dir / "basic.odt").string()));
  const auto initial_selection = TextSelection{2, 4};
  auto parent_revision = parent.revision();
  assert(
      parent.commit_styles(edited_header, initial_selection, parent_revision));
  assert(parent.text() == "Body1\nBody2" && parent.modified());
  assert(parent.undo() == initial_selection);
  assert(parent.style_snapshot().entry("styles.xml") == original_styles);
  assert(!parent.modified());
  assert(parent.redo() == initial_selection);
  assert(parent.style_snapshot().entry("styles.xml") ==
         edited_header.entry("styles.xml"));
  rejects([&] {
    parent.commit_styles(edited_header, initial_selection, parent_revision);
  });
  parent.save(Text((dir / "header-edited.odt").string()));
  TextDocument reopened;
  reopened.load(Text((dir / "header-edited.odt").string()));
  assert(reopened.text() == parent.text());
  assert(story_text(paginate_text(reopened.flow(), metrics).pages[0],
                    TextPageStory::Header) == "RightHeader – edited");

  // Cached page fields retain their source attributes and remain indivisible.
  WordsStoryEdit footer_edit(p, {"A", TextPageStory::Footer});
  local.load_package(footer_edit.document());
  assert(local.text() == "Page 99/999");
  local.replace(0, 0, "Footer ", true);
  auto before_failed = local.style_snapshot().xml.toByteArray();
  rejects([&] { local.replace(13, 0, "x", true); });
  assert(local.style_snapshot().xml.toByteArray() == before_failed);
  assert(local.format_characters({0, local.text().size()}, bold));
  auto footer_package = footer_edit.finish(local.style_snapshot(), true);
  assert(footer_package.xml.toByteArray() == original);
  auto footer_source = styles_of(footer_package)
                           .elementsByTagNameNS(TEXT, "page-number")
                           .front();
  assert(footer_source.text() == "99" &&
         footer_source.attributeNS(TEXT, "select-page") == "current");
  assert(story_text(
             paginate_text(read_text_flow(footer_package), metrics).pages[0],
             TextPageStory::Footer) == "Footer Page 1/2");
  footer_package.save(Text((dir / "footer-edited.odt").string()));

  WordsStoryEdit center_edit(regions, {"A", TextPageStory::Header,
                                       TextPageStory::Default,
                                       WordsStoryAddress::Center});
  local.load_package(center_edit.document());
  assert(local.text() == "C");
  local.replace(1, 0, "enter", true);
  auto center_package = center_edit.finish(local.style_snapshot(), true);
  auto center_header =
      child(master_a(styles_of(center_package)), STYLE, "header");
  assert(plain_text(child(center_header, STYLE, "region-left")) == "L");
  assert(plain_text(child(center_header, STYLE, "region-center")) == "Center");
  assert(plain_text(child(center_header, STYLE, "region-right")) == "R");
  assert(center_package.xml.toByteArray() == regions.xml.toByteArray());
  rejects([&] { WordsStoryEdit invalid(regions, {"A"}); });
  center_package.save(Text((dir / "region-edited.odt").string()));

  WordsStoryEdit convert_edit(p, {"A", TextPageStory::Header,
                                  TextPageStory::Default,
                                  WordsStoryAddress::Center});
  local.load_package(convert_edit.document());
  local.replace(0, 0, "Center", true);
  auto converted = convert_edit.finish(local.style_snapshot(), true);
  auto converted_header =
      child(master_a(styles_of(converted)), STYLE, "header");
  assert(plain_text(child(converted_header, STYLE, "region-left")) ==
         "RightHeader");
  assert(plain_text(child(converted_header, STYLE, "region-center")) ==
         "Center");
  assert(child(converted_header, TEXT, "p").isNull());
  converted.save(Text((dir / "regions-converted.odt").string()));

  OdfPackage blank(Kind::Words);
  auto absent_styles = blank.entry("styles.xml");
  auto absent_manifest = blank.entry("META-INF/manifest.xml");
  auto manifest = read_document_xml(absent_manifest);
  const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
  for (auto entry :
       children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (entry.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest.documentElement().removeChild(entry);
  auto without_styles = manifest.toByteArray();
  blank.replay_part_changes(
      {{"styles.xml",
        true,
        false,
        0,
        absent_styles.size(),
        0,
        absent_styles,
        {}},
       {"META-INF/manifest.xml", true, true, 0, absent_manifest.size(),
        without_styles.size(), absent_manifest, without_styles}},
      true);
  auto body_para = blank.xml.createElementNS(TEXT, "text:p");
  set_plain_text(body_para, "Keep body");
  blank.body().appendChild(body_para);
  auto blank_content = blank.xml.toByteArray();
  WordsStoryEdit create(blank,
                        {{}, TextPageStory::Header, TextPageStory::First});
  local.load_package(create.document());
  local.replace(0, 0, "First page", true);
  auto created = create.finish(local.style_snapshot(), true);
  assert(created.xml.toByteArray() == blank_content);
  auto created_flow = read_text_flow(created);
  assert(!created_flow.first_master.empty());
  assert(story_text(paginate_text(created_flow, metrics).pages[0],
                    TextPageStory::Header) == "First page");
  created.save(Text((dir / "header-created.odt").string()));
  // Creation and archive part/manifest absence are a single parent undo step.
  parent.load_package(blank);
  assert(parent.commit_styles(created, {}, parent.revision()));
  assert(parent.undo());
  assert(parent.style_snapshot().entry("styles.xml").empty());
  assert(parent.style_snapshot().entry("META-INF/manifest.xml") ==
         blank.entry("META-INF/manifest.xml"));
  assert(parent.redo());

  auto collisions = fixture();
  child(collisions.body(), TEXT, "p")
      .setAttributeNS("http://www.w3.org/XML/1998/namespace", "xml:id",
                      "WordsList1");
  auto body_table = insert_words_table(collisions, 0, {1, 1});
  collisions = std::move(body_table.document);
  auto body_table_name = collisions.xml.elementsByTagNameNS(TABLE, "table")
                             .front()
                             .attributeNS(TABLE, "name");
  WordsStoryEdit collision_edit(collisions, {"A"});
  auto collision_projection = collision_edit.document();
  auto header_table_edit = insert_words_table(collision_projection, 0, {1, 1});
  collision_projection = std::move(header_table_edit.document);
  auto marker_para = collision_projection.xml.createElementNS(TEXT, "text:p");
  marker_para.setAttributeNS("http://www.w3.org/XML/1998/namespace", "xml:id",
                             "WordsList1");
  auto link = collision_projection.xml.createElementNS(TEXT, "text:a");
  link.setAttributeNS(XLINK, "xlink:href", "#WordsList1");
  link.setAttributeNS(XLINK, "xlink:type", "simple");
  set_plain_text(link, "Self link");
  marker_para.appendChild(link);
  collision_projection.body().appendChild(marker_para);
  auto isolated = collision_edit.finish(collision_projection, true);
  assert(isolated.xml.toByteArray() == collisions.xml.toByteArray());
  auto isolated_styles = styles_of(isolated);
  auto header_table_name = isolated_styles.elementsByTagNameNS(TABLE, "table")
                               .front()
                               .attributeNS(TABLE, "name");
  assert(header_table_name != body_table_name);
  auto isolated_link = isolated_styles.elementsByTagNameNS(TEXT, "a").front();
  auto isolated_id = isolated_link.parentNode().attributeNS(
      "http://www.w3.org/XML/1998/namespace", "id");
  assert(isolated_id != "WordsList1" &&
         isolated_link.attributeNS(XLINK, "href") == "#" + isolated_id);
  isolated.save(Text((dir / "identity-isolated.odt").string()));

  auto shared = fixture();
  auto shared_styles = styles_of(shared);
  auto body_only = shared_styles.createElementNS(STYLE, "style:style");
  body_only.setAttributeNS(STYLE, "style:family", "paragraph");
  body_only.setAttributeNS(STYLE, "style:name", "BodyOnly");
  child(shared_styles.documentElement(), OFFICE, "styles")
      .appendChild(body_only);
  put_styles(shared, shared_styles);
  child(shared.body(), TEXT, "p")
      .setAttributeNS(TEXT, "text:style-name", "BodyOnly");
  WordsStoryEdit shared_edit(shared, {"A"});
  auto shared_projection = shared_edit.document();
  assert(delete_words_paragraph_style(shared_projection, "BodyOnly"));
  rejects([&] { shared_edit.finish(shared_projection, true); });
  assert(p.xml.toByteArray() == original &&
         p.entry("styles.xml") == original_styles);

  auto insertion = footer_edit.document();
  insert_words_page_field(insertion, 5, 2, false);
  auto inserted = footer_edit.finish(insertion, true);
  assert(story_text(paginate_text(read_text_flow(inserted), metrics).pages[0],
                    TextPageStory::Footer) == "Page 1/2");
  auto inserted_styles = styles_of(inserted);
  assert(
      inserted_styles.elementsByTagNameNS(TEXT, "page-number").front().text() ==
      "1");
  insertion = edit.document();
  insert_words_page_field(insertion, plain_text(insertion.body()).size(), 0,
                          true);
  auto count_inserted = edit.finish(insertion, true);
  assert(story_text(
             paginate_text(read_text_flow(count_inserted), metrics).pages[0],
             TextPageStory::Header) == "RightHeader2");
  count_inserted.save(Text((dir / "field-inserted.odt").string()));
  auto older = fixture();
  older.xml.documentElement().setAttributeNS(OFFICE, "office:version", "1.2");
  auto older_styles = styles_of(older);
  older_styles.documentElement().setAttributeNS(OFFICE, "office:version",
                                                "1.2");
  put_styles(older, older_styles);
  WordsStoryEdit older_edit(older,
                            {"A", TextPageStory::Header, TextPageStory::First});
  local.load_package(older_edit.document());
  local.replace(0, 0, "First page", true);
  auto upgraded = older_edit.finish(local.style_snapshot(), true);
  assert(upgraded.xml.documentElement().attributeNS(OFFICE, "version") ==
         "1.3");
  assert(styles_of(upgraded).documentElement().attributeNS(OFFICE, "version") ==
         "1.3");
  upgraded.save(Text((dir / "first-version-upgraded.odt").string()));
  auto tampered = edit.document();
  auto bad_styles = styles_of(tampered);
  bad_styles.elementsByTagNameNS(STYLE, "page-layout-properties")
      .front()
      .setAttributeNS(FO, "fo:page-width", "999pt");
  put_styles(tampered, bad_styles);
  rejects([&] { edit.finish(tampered, true); });
  tampered = edit.document();
  tampered.set_entry("Extras/opaque.bin", "changed",
                     "application/octet-stream");
  rejects([&] { edit.finish(tampered, true); });
  std::cout << "PASS native header/footer placement, style scopes, page "
               "fields, variants, regions, dynamic spacing, clipping and "
               "unchanged body sources, scoped rich editing and exact parent "
               "history\n";
}
