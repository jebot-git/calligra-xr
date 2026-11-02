// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  p.xml = read_document_xml(Bytes(
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-size="12pt"/></style:style></office:automatic-styles><office:body><office:text><text:p>Before</text:p><table:table table:name="Repeated"><table:table-column table:number-columns-repeated="3"/><table:table-row table:number-rows-repeated="3" xml:id="row"><table:table-cell table:number-columns-repeated="3" xml:id="cell"><text:p text:style-name="Body" xml:id="para">Český text</text:p></table:table-cell></table:table-row></table:table><text:p>After</text:p></office:text></office:body></office:document-content>)"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
TextSelection range(size_t a, size_t b, TextCellAddress first,
                    TextCellAddress last) {
  TextSelection result{a, b, last, {last}};
  result.anchor_cells = std::vector{first};
  return result;
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
  auto directory = std::filesystem::temp_directory_path() /
                   ("words-display-range-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    directory = argv[2];
  std::filesystem::create_directories(directory);
  auto p = fixture();
  p.save(Text((directory / "source.odt").string()));
  auto original = p.xml.toByteArray();
  TextDocument doc;
  doc.load(Text((directory / "source.odt").string()));
  // Source position 8 is the same scalar in every displayed copy.
  auto selection = range(8, 8, {0, 1, 1, 1, 1}, {0, 2, 0, 1, 1});
  assert(!selection.collapsed());
  assert(doc.selected_text(selection) == Text("eský text\nČeský text\nČ"));
  auto map = WordsDisplayMap(doc.flow());
  auto span = map.span({8, *selection.anchor_cells}, {8, selection.cells});
  assert(span.first == 5 && span.last == 7 && !span.reverse);
  assert(!map.portion(1, {{0, 0, 0, 1, 1}}, span));
  auto prepared = doc.selection_snapshot(selection);
  assert(prepared.ranges.size() == 3);
  assert(prepared.anchor.position < prepared.cursor.position);
  for (auto e : prepared.document.xml.elementsByTagNameNS(TEXT, "p"))
    for (auto a : e.attributes())
      assert(!a.name.startsWith("domed-range-key"));
  WordsCharacterFormat bold;
  bold.weight = 700;
  TextSelection resolved;
  assert(doc.format_characters(selection, bold, &resolved));
  assert(!resolved.collapsed());
  auto after = doc.style_snapshot();
  after.save(Text((directory / "bold.odt").string()));
  auto flow = doc.flow();
  WordsDisplayMap aftermap(flow);
  auto selected = aftermap.span({resolved.anchor, *resolved.anchor_cells},
                                {resolved.cursor, resolved.cells});
  for (size_t i = 1; i < 10; ++i) {
    const auto &para = aftermap.paragraphs()[i];
    auto info = read_words_format(after, para.begin, para.begin + para.length);
    // Middle whole paragraph is bold; first/last partial paragraphs are mixed.
    if (i == 6)
      assert(info.bold == true);
    else if (i != 5 && i != 7)
      assert(info.bold == false);
  }
  auto revision = doc.revision();
  assert(!doc.format_characters(resolved, bold, &resolved));
  assert(doc.revision() == revision);
  assert(doc.undo() == selection);
  assert(doc.style_snapshot().xml.toByteArray() == original);
  assert(doc.redo() == resolved);
  assert(doc.style_snapshot().xml.toByteArray() == after.xml.toByteArray());
  WordsParagraphFormat center;
  center.alignment = "center";
  assert(doc.format_paragraphs(resolved, center, &resolved));
  doc.save(Text((directory / "center.odt").string()));
  assert(doc.format_info(resolved).bold == true);
  assert(doc.format_info(resolved).paragraph.alignment == Text("center"));
  auto reverse = range(8, 8, {0, 2, 0, 1, 1}, {0, 1, 1, 1, 1});
  TextDocument back;
  back.load(Text((directory / "source.odt").string()));
  assert(back.selected_text(reverse) == doc.selected_text(resolved));
  assert(back.format_characters(reverse, bold, &reverse));
  assert(reverse.anchor > reverse.cursor);
  back.save(Text((directory / "reverse.odt").string()));
  auto before_prepare = back.style_snapshot().xml.toByteArray();
  auto prepared_edit = back.prepare_cell_edit(reverse);
  assert(prepared_edit && prepared_edit->collapsed() && back.editing());
  back.cancel_edit();
  assert(back.end_edit() == reverse);
  assert(back.style_snapshot().xml.toByteArray() == before_prepare);
  assert(!back.editing());
  assert(p.xml.toByteArray() == original);
  assert(doc.style_snapshot().entry("Extras/opaque.bin") ==
         Bytes("\0keep\xff", 6));
  // A range ending at the next paragraph's start excludes that paragraph.
  auto boundary = range(7, 7, {0, 0, 0, 1, 1}, {0, 0, 1, 1, 1});
  auto boundary_edit = doc.selection_snapshot(boundary);
  assert(boundary_edit.ranges.size() == 1);
  // Named character/paragraph and tab assignments use the same displayed scope.
  auto styled = doc.style_snapshot();
  WordsCharacterStyleChange named;
  named.label = "Range italic";
  named.character.italic = true;
  auto style_name = define_words_character_style(styled, named);
  assert(doc.commit_styles(styled, resolved, doc.revision(), {}, style_name,
                           &resolved));
  assert(doc.current_character_style(resolved) == style_name);
  doc.save(Text((directory / "named.odt").string()));
  WordsTabsFormat tabs;
  tabs.stops = WordsTabSet{};
  tabs.stops->stops.push_back(words_new_tab_stop(24));
  assert(doc.format_tabs(resolved, tabs, doc.revision(), &resolved));
  auto tab_snapshot = doc.selection_snapshot(resolved);
  for (auto [begin, end] : tab_snapshot.ranges)
    assert(read_words_tabs(tab_snapshot.document, begin, end)
               .stops->stops.size() == 1);
  doc.save(Text((directory / "tabs.odt").string()));
  // Multiple nested source aliases require full outer and inner instance paths.
  auto nested = fixture();
  auto parent = nested.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  auto table = nested.xml.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name", "Inner");
  auto col = nested.xml.createElementNS(TABLE, "table:table-column");
  col.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  table.appendChild(col);
  auto row = nested.xml.createElementNS(TABLE, "table:table-row");
  row.setAttributeNS(TABLE, "table:number-rows-repeated", "2");
  table.appendChild(row);
  auto c = nested.xml.createElementNS(TABLE, "table:table-cell");
  c.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
  row.appendChild(c);
  auto para = nested.xml.createElementNS(TEXT, "text:p");
  set_plain_text(para, "Inner text");
  c.appendChild(para);
  parent.appendChild(table);
  nested.save(Text((directory / "nested-source.odt").string()));
  TextDocument nd;
  nd.load(Text((directory / "nested-source.odt").string()));
  WordsDisplayMap nested_map(nd.flow());
  std::vector<WordsDisplayParagraph> chosen;
  for (const auto &record : nested_map.paragraphs())
    if (record.cells.size() == 2 && record.cells[0].row == 1 &&
        record.cells[0].column == 1)
      chosen.push_back(record);
  TextSelection ns{chosen[1].begin + 1, chosen[2].begin + 2};
  ns.cells = chosen[2].cells;
  ns.cell = ns.cells.back();
  ns.anchor_cells = chosen[1].cells;
  auto expected = nd.selected_text(ns);
  assert(nd.format_characters(ns, bold, &ns));
  assert(nd.selected_text(ns) == expected);
  auto nf = nd.flow();
  WordsDisplayMap nm(nf);
  size_t bold_cells = 0;
  for (const auto &record : nm.paragraphs())
    if (record.cells.size() == 2) {
      auto info = read_words_format(nd.style_snapshot(), record.begin,
                                    record.begin + record.length);
      if (!info.bold || *info.bold)
        ++bold_cells;
    }
  assert(bold_cells == 2);
  nd.save(Text((directory / "nested-bold.odt").string()));
  // Covered source metadata lies between displayed cells but is not formatted.
  auto merged = fixture();
  auto mt = merged.xml.elementsByTagNameNS(TABLE, "table")[0];
  auto rows = merged.xml.elementsByTagNameNS(TABLE, "table-row");
  rows[0].removeAttributeNS(TABLE, "number-rows-repeated");
  auto mc = merged.xml.elementsByTagNameNS(TABLE, "table-cell")[0];
  mc.removeAttributeNS(TABLE, "number-columns-repeated");
  mc.setAttributeNS(TABLE, "table:number-columns-spanned", "2");
  auto covered = merged.xml.createElementNS(TABLE, "table:covered-table-cell");
  auto hidden = merged.xml.createElementNS(TEXT, "text:p");
  set_plain_text(hidden, "Hidden metadata");
  covered.appendChild(hidden);
  rows[0].appendChild(covered);
  auto tail = merged.xml.createElementNS(TABLE, "table:table-cell");
  auto last = merged.xml.createElementNS(TEXT, "text:p");
  set_plain_text(last, "Tail text");
  tail.appendChild(last);
  rows[0].appendChild(tail);
  auto styles = child(merged.xml.documentElement(), OFFICE, "automatic-styles");
  auto ht = merged.xml.createElementNS(STYLE, "style:style");
  ht.setAttributeNS(STYLE, "style:name", "HiddenTabs");
  ht.setAttributeNS(STYLE, "style:family", "paragraph");
  auto props = merged.xml.createElementNS(STYLE, "style:paragraph-properties");
  ht.appendChild(props);
  auto stops = merged.xml.createElementNS(STYLE, "style:tab-stops");
  props.appendChild(stops);
  auto stop = merged.xml.createElementNS(STYLE, "style:tab-stop");
  stop.setAttributeNS(STYLE, "style:position", "60pt");
  stop.setAttributeNS(STYLE, "style:type", "left");
  stops.appendChild(stop);
  styles.appendChild(ht);
  hidden.setAttributeNS(TEXT, "text:style-name", "HiddenTabs");
  merged.save(Text((directory / "merged-source.odt").string()));
  TextDocument md;
  md.load(Text((directory / "merged-source.odt").string()));
  WordsDisplayMap mm(md.flow());
  auto records = mm.paragraphs();
  assert(records.size() == 4);
  TextSelection ms{records[1].begin + 1, records[2].begin + 2};
  ms.cells = records[2].cells;
  ms.cell = ms.cells.back();
  ms.anchor_cells = records[1].cells;
  assert(md.format_characters(ms, bold, &ms));
  md.save(Text((directory / "merged-bold.odt").string()));
  for (auto node :
       md.style_snapshot().xml.elementsByTagNameNS(TABLE, "covered-table-cell"))
    assert(node.firstChild().attributeNS(TEXT, "style-name") == "HiddenTabs" &&
           plain_text(node.firstChild()) == "Hidden metadata");
  auto visible = md.selection_snapshot(ms);
  assert(!read_words_tabs(visible.document, visible.ranges.front().first,
                          visible.ranges.back().second)
              .stops);
  auto visible_tabs =
      read_words_tabs(visible.document, visible.ranges.front().first,
                      visible.ranges.back().second, visible.ranges);
  assert(visible_tabs.stops && visible_tabs.stops->stops.empty());
  TextSelection whole{0, doc.text().size()};
  whole.anchor_cells = std::vector<TextCellAddress>{};
  auto before_list = doc.revision();
  auto before_list_xml = doc.style_snapshot().xml.toByteArray();
  auto undo_before = doc.can_undo(), redo_before = doc.can_redo(), modified_before = doc.modified();
  TextSelection no_list;
  assert(!doc.list_action(whole, WordsListAction::Indent, false, &no_list));
  assert(doc.revision() == before_list && no_list == whole);
  assert(doc.style_snapshot().xml.toByteArray() == before_list_xml &&
         doc.can_undo() == undo_before && doc.can_redo() == redo_before &&
         doc.modified() == modified_before);
  std::cout
      << "PASS displayed range order, repeated scope, partial "
         "character/paragraph formatting, copy, no-op and exact history\n";
}
