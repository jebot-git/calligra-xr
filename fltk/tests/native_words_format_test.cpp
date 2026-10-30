// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
template <class F> void rejects(F call) {
  bool failed = false;
  try {
    call();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  assert(p.xml.setContent(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles>
<style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="700"/></style:style><style:style style:name="Bold" style:family="paragraph"><style:paragraph-properties fo:margin-left="9pt"/></style:style>
<style:style style:name="Cell" style:family="table-cell"><style:text-properties fo:font-size="18pt" fo:font-style="italic" fo:background-color="#ffeeaa" style:text-underline-style="wave" style:text-underline-type="double" style:text-underline-color="#0055cc" style:text-line-through-style="dash" style:text-line-through-mode="skip-white-space"/></style:style>
<style:style style:name="WordsText1" style:family="text"><style:text-properties fo:color="#223344"/></style:style>
</office:automatic-styles><office:body><office:text>
<text:p xml:id="first" text:style-name="Body">Alpha <text:span text:style-name="Bold">Český<text:a xlink:href="https://example.invalid/" xlink:type="simple"> 日本語</text:a></text:span> tail<text:s text:c="3"/><text:tab/>end<text:line-break/>soft</text:p>
<text:section text:name="Section"><text:p text:style-name="Body">Section <text:meta xml:id="metadata">meta text</text:meta> end</text:p></text:section>
<table:table table:name="Cells"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell table:style-name="Cell" office:value-type="string"><text:p>Left cell</text:p></table:table-cell><table:table-cell office:value-type="string"><text:p text:style-name="Bold">Right cell</text:p></table:table-cell></table:table-row></table:table>
<text:list><text:list-item xml:id="item"><text:p text:style-name="Body">List item</text:p></text:list-item></text:list><text:p xml:id="empty" text:style-name="Body"/><text:p>Before <text:date>2026</text:date> after</text:p>
</office:text></office:body></office:document-content>)XML"));
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:styles><style:style style:name="Body" style:family="paragraph"><style:paragraph-properties fo:margin-left="4pt" fo:margin-top="3pt"/><style:text-properties fo:font-family="DejaVu Sans" fo:font-size="12pt" fo:color="#102030"/></style:style></office:styles></office:document-styles>)XML",
      "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
size_t at(const TextDocument &b, const char *s) {
  auto pos = b.text().find(Text(s));
  assert(pos != Text::npos);
  return pos;
}
TextSelection point(size_t p) { return {p, p}; }
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  std::filesystem::path dir(temp.path().toUtf8().toStdString());
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    dir = argv[2];
    if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir))
      throw std::runtime_error("Choose an empty artifact directory");
    std::filesystem::create_directories(dir);
  }
  auto input = Text((dir / "input.odt").string());
  auto source = fixture();
  source.save(input);
  TextDocument book;
  book.load(input);
  auto original = book.text();
  auto snapshot = [&](const char *name) {
    auto path = Text((dir / (std::string(name) + ".odt")).string());
    book.save(path);
    OdfPackage p(Kind::Words);
    p.load(path);
    assert(p.entry("Extras/opaque.bin") == source.entry("Extras/opaque.bin"));
    assert(p.entry("styles.xml") == source.entry("styles.xml"));
    return p.xml.toByteArray();
  };
  auto original_xml = snapshot("original");
  auto start = at(book, "Český"), end = start + Text("Český 日本語").size();
  TextSelection range{end, start};
  assert(book.format_info(range).character.weight == 700 &&
         book.format_info(range).bold == true);
  auto mixed = book.format_info({0, end}).character;
  assert(!mixed.weight);
  // Highlight is independent of ink; clears override inherited backgrounds.
  WordsCharacterFormat highlight;
  highlight.highlight = "#FFF176";
  assert(book.format_characters(range, highlight));
  assert(book.format_info(range).character.highlight == "#fff176");
  assert(!book.format_info({0, end}).character.highlight);
  auto highlight_xml = snapshot("highlight");
  assert(book.undo() == range && snapshot("highlight-undone") == original_xml);
  assert(book.redo() == range && snapshot("highlight-redone") == highlight_xml);
  WordsCharacterFormat clear;
  clear.highlight = "transparent";
  assert(book.format_characters(range, clear));
  assert(book.format_info(range).character.highlight == "transparent");
  snapshot("highlight-cleared");
  book.load(input);
  highlight.highlight = "#00ffcc";
  assert(!book.format_characters(point(2), highlight) &&
         book.has_typing_format());
  book.replace(2, 0, "世界");
  assert(book.format_info({2, 4}).character.highlight == "#00ffcc");
  snapshot("highlight-typed");
  book.load(input);
  rejects([&] {
    highlight.highlight = "auto";
    book.format_characters(range, highlight);
  });
  assert(!book.modified() && !book.can_undo());
  WordsCharacterFormat lines;
  lines.underline = true;
  lines.strike = true;
  assert(book.format_characters(range, lines));
  auto lines_info = book.format_info(range).character;
  assert(lines_info.underline == true && lines_info.strike == true);
  assert(!book.format_info({0, end}).character.underline);
  auto lines_xml = snapshot("lines");
  assert(book.undo() == range && snapshot("lines-undone") == original_xml);
  assert(book.redo() == range && snapshot("lines-redone") == lines_xml);
  lines.underline = false;
  lines.strike = false;
  assert(book.format_characters(range, lines));
  assert(book.format_info(range).character.underline == false);
  assert(book.format_info(range).character.strike == false);
  snapshot("lines-cleared");
  book.load(input);
  lines.underline = true;
  lines.strike = true;
  assert(!book.format_characters(point(2), lines) && book.has_typing_format());
  book.begin_edit(point(2));
  book.replace(2, 0, "世界");
  assert(book.format_info({2, 4}).character.underline == true);
  assert(book.format_info({2, 4}).character.strike == true);
  book.cancel_edit();
  assert(book.end_edit() == point(2) && book.has_typing_format());
  book.replace(2, 0, "世界");
  snapshot("lines-typed");
  book.load(input);
  WordsCharacterFormat font;
  font.weight = 400;
  font.italic = true;
  font.size = 20;
  font.color = "#A02040";
  font.family = "DejaVu Serif";
  assert(book.format_characters(range, font) && book.modified() &&
         book.text() == original);
  auto info = book.format_info(range);
  assert(info.character.weight == 400 && info.character.italic == true &&
         info.character.size == 20 && info.character.color == "#a02040" &&
         info.character.family == "DejaVu Serif");
  assert(book.format_info(point(start - 1)).character.size == 12);
  auto formatted_xml = snapshot("character");
  assert(book.undo() == range && book.modified());
  assert(snapshot("character-undone") == original_xml);
  assert(book.redo() == range);
  assert(snapshot("character-redone") == formatted_xml);
  auto revision = book.revision();
  assert(!book.format_characters(range, font));
  assert(book.revision() == revision && !book.modified());
  // The hyperlink and text-bearing metadata remain their original containers.
  OdfPackage inspected(Kind::Words);
  inspected.load(Text((dir / "character.odt").string()));
  auto links = inspected.xml.elementsByTagNameNS(TEXT, "a");
  assert(links.size() == 1 &&
         links[0].attributeNS(XLINK, "href") == "https://example.invalid/");
  auto meta = at(book, "meta text");
  WordsCharacterFormat red;
  red.color = "#ff0000";
  assert(book.format_characters({meta, meta + 9}, red));
  assert(book.format_info({meta, meta + 9}).character.color == "#ff0000");
  snapshot("metadata");
  // Repeated partial toggles reuse direct styles, with no unbounded nesting.
  TextSelection part{start + 1, start + 4};
  WordsCharacterFormat toggle;
  for (int i = 0; i < 140; ++i) {
    toggle.weight = i % 2 ? 400 : 700;
    book.format_characters(part, toggle);
  }
  snapshot("toggles");
  inspected.load(Text((dir / "toggles.odt").string()));
  assert(inspected.xml.elementsByTagNameNS(STYLE, "style").size() < 20);
  size_t maximum = 0;
  std::function<void(XmlElement, size_t)> depth = [&](XmlElement n,
                                                      size_t count) {
    count += n.namespaceURI() == TEXT && n.localName() == "span";
    maximum = std::max(maximum, count);
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        depth(c, count);
  };
  depth(inspected.body(), 0);
  assert(maximum <= 3);
  // Query includes the cell's inherited style, not only its source paragraph.
  auto cell = at(book, "Left cell");
  auto cell_info = book.format_info({cell, cell + 9});
  assert(cell_info.character.size == 18 && cell_info.character.italic == true &&
         cell_info.character.highlight == "#ffeeaa" &&
         cell_info.character.underline == true &&
         cell_info.character.strike == true);
  assert(book.format_characters({cell, cell + 9}, clear));
  assert(book.format_info({cell, cell + 9}).character.highlight ==
         "transparent");
  snapshot("cell-highlight-cleared");
  WordsCharacterFormat cell_font;
  cell_font.weight = 700;
  assert(book.format_characters({cell, cell + 9}, cell_font));
  cell_info = book.format_info({cell, cell + 9});
  assert(cell_info.character.size == 18 && cell_info.character.italic == true &&
         cell_info.character.weight == 700);
  snapshot("cell");
  auto right = at(book, "Right cell");
  assert(book.format_info(point(right)).paragraph.left == 9);
  // Whole source paragraph ranges can span sections, cells and list items.
  WordsParagraphFormat paragraph;
  paragraph.alignment = "center";
  paragraph.left = 14;
  paragraph.right = 8;
  paragraph.indent = -3;
  paragraph.before = 7;
  paragraph.after = 9;
  paragraph.line_factor = 1.5;
  paragraph.keep_next = true;
  paragraph.keep_together = true;
  paragraph.orphans = 3;
  paragraph.widows = 4;
  TextSelection paragraphs{at(book, "Section"), at(book, "List item")};
  assert(book.format_paragraphs(paragraphs, paragraph));
  auto pp = book.format_info(paragraphs).paragraph;
  assert(pp.alignment == "center" && pp.left == 14 && pp.right == 8 &&
         pp.indent == -3 && pp.before == 7 && pp.after == 9 &&
         pp.line_factor == 1.5 && pp.keep_next == true && pp.orphans == 3 &&
         pp.widows == 4);
  assert(book.format_info(point(at(book, "List item"))).paragraph.alignment ==
         "start");
  auto paragraph_xml = snapshot("paragraphs");
  assert(book.text() == original);
  assert(book.undo() == paragraphs);
  assert(book.redo() == paragraphs);
  assert(snapshot("paragraphs-redone") == paragraph_xml);
  revision = book.revision();
  assert(!book.format_paragraphs(paragraphs, paragraph));
  assert(book.revision() == revision && !book.modified());
  // Empty-paragraph font changes persist through save/reopen and future typing.
  auto empty = at(book, "List item") + Text("List item").size() + 1;
  WordsCharacterFormat empty_font;
  empty_font.size = 24;
  empty_font.italic = true;
  assert(book.format_characters(point(empty), empty_font));
  snapshot("empty-default");
  TextDocument reopened;
  reopened.load(Text((dir / "empty-default.odt").string()));
  assert(reopened.format_info(point(empty)).character.size == 24);
  // A caret in a nonempty paragraph changes future typing without modifying
  // any existing text. Group cancellation also restores this transient state.
  book.load(input);
  WordsCharacterFormat typing;
  typing.weight = 700;
  typing.color = "#008020";
  assert(!book.format_characters(point(2), typing) && !book.modified() &&
         book.has_typing_format());
  assert(book.format_info(point(2)).character.weight == 700);
  book.begin_edit(point(2));
  book.replace(2, 0, "世界");
  assert(book.format_info({2, 4}).character.color == "#008020");
  book.cancel_edit();
  assert(book.end_edit() == point(2) && book.text() == original &&
         book.has_typing_format());
  book.begin_edit(point(2));
  book.replace(2, 0, "世界");
  book.end_edit(point(4));
  auto typed_xml = snapshot("typed");
  assert(book.undo() == point(2) && !book.has_typing_format());
  assert(book.text() == original);
  assert(book.redo() == point(4));
  assert(snapshot("typed-redone") == typed_xml);
  book.clear_typing_format();
  assert(!book.has_typing_format());
  // Unsupported selected fields reject the complete command and keep redo.
  book.load(input);
  book.replace(0, 0, "x");
  book.undo();
  auto date = at(book, "2026");
  rejects([&] { book.format_characters({date, date + 4}, red); });
  assert(book.text() == original && !book.modified() && book.can_redo());
  rejects([&] { book.format_characters({0, original.size()}, red); });
  assert(book.text() == original && !book.modified() && book.can_redo());
  rejects([&] {
    WordsCharacterFormat bad;
    bad.size = 0;
    book.format_characters(range, bad);
  });
  rejects([&] {
    WordsCharacterFormat bad;
    bad.weight = 342;
    book.format_characters(range, bad);
  });
  rejects([&] {
    WordsParagraphFormat bad;
    bad.line_factor = 0;
    book.format_paragraphs(range, bad);
  });
  rejects([&] { book.format_characters({0, 99999}, red); });
  assert(snapshot("rejected") == original_xml && book.can_redo());
  rejects([&] {
    WordsParagraphFormat bad;
    bad.before = -1;
    book.format_paragraphs(range, bad);
  });
  book.begin_edit(range);
  assert(book.format_characters(range, red));
  rejects([&] {
    WordsParagraphFormat bad;
    bad.line_factor = 0;
    book.format_paragraphs(range, bad);
  });
  assert(book.end_edit() == range && !book.modified() && book.can_redo());
  assert(snapshot("rejected-compound") == original_xml);
  // Empty paragraph defaults survive Enter, deletion and save/reopen.
  book.load(input);
  auto paragraph_end = at(book, "Section") - 1;
  WordsCharacterFormat large;
  large.size = 28;
  book.format_characters(point(paragraph_end), large);
  book.replace(paragraph_end, 0, "\n");
  assert(book.format_info(point(paragraph_end + 1)).character.size == 28);
  snapshot("typing-new-empty");
  reopened.load(Text((dir / "typing-new-empty.odt").string()));
  assert(reopened.format_info(point(paragraph_end + 1)).character.size == 28);
  book.load(input);
  book.format_characters(point(0), large);
  book.replace(0, at(book, "Section") - 1, "");
  snapshot("typing-deleted-empty");
  reopened.load(Text((dir / "typing-deleted-empty.odt").string()));
  assert(reopened.format_info(point(0)).character.size == 28);
  // Namespace aliases are semantically identical. Overlay by expanded name,
  // so an original z:font-weight cannot win over a new fo:font-weight.
  auto aliased = fixture();
  std::string xml = aliased.xml.toByteArray();
  auto rename = [&](const std::string &from, const std::string &to) {
    for (size_t pos = 0; (pos = xml.find(from, pos)) != std::string::npos;
         pos += to.size())
      xml.replace(pos, from.size(), to);
  };
  rename("xmlns:fo=", "xmlns:z=");
  rename("fo:", "z:");
  rename("xmlns:style=", "xmlns:st=");
  rename("<style:", "<st:");
  rename("</style:", "</st:");
  rename(" style:", " st:");
  aliased.xml = read_document_xml(Bytes(xml));
  aliased.save(Text((dir / "aliased.odt").string()));
  book.load(Text((dir / "aliased.odt").string()));
  WordsCharacterFormat normal;
  normal.weight = 400;
  assert(book.format_characters({start, start + 5}, normal));
  assert(book.format_info({start, start + 5}).character.weight == 400);
  snapshot("aliased-formatted");
  book.load(input);
  WordsCharacterFormat heavy;
  heavy.weight = 800;
  book.format_characters({start, start + 2}, heavy);
  auto bold_mixed = book.format_info({start, start + 5});
  assert(!bold_mixed.character.weight && bold_mixed.bold == true);
  book.load(input);
  WordsCharacterFormat tiny;
  tiny.size = 0.000001;
  book.format_characters({0, 1}, tiny);
  WordsParagraphFormat fractional;
  fractional.before = 0.000001;
  book.format_paragraphs(point(0), fractional);
  snapshot("fractional-decimals");
  book.load(input);
  book.replace(0, 0, "x");
  book.undo();
  auto stable_revision = book.revision();
  for (const Text &family : TextList{"Hack'", "\"Hack", " , '', \"\" "}) {
    WordsCharacterFormat bad;
    bad.family = family;
    rejects([&] { book.format_characters(range, bad); });
    rejects([&] { book.format_characters(point(2), bad); });
    assert(book.revision() == stable_revision && !book.modified() &&
           book.can_redo());
    assert(!book.has_typing_format() && book.text() == original);
  }
  WordsCharacterFormat active;
  active.italic = true;
  book.format_characters(point(2), active);
  book.begin_edit(point(2));
  book.replace(2, 0, "y");
  WordsCharacterFormat bad;
  bad.family = "Hack'";
  rejects([&] { book.format_characters(point(3), bad); });
  assert(book.end_edit() == point(2) && book.has_typing_format());
  assert(book.format_info(point(2)).character.italic == true);
  assert(book.text() == original && !book.modified() && book.can_redo());
  assert(snapshot("rejected-family") == original_xml);
  WordsCharacterFormat quoted;
  quoted.family = "'Unavailable, Font', \"Hack\"";
  assert(book.format_characters(range, quoted));
  assert(book.format_info(range).character.family == quoted.family);
  snapshot("quoted-fallback");
  auto signed_source = fixture();
  signed_source.set_entry("META-INF/documentsignatures.xml",
                          "<document-signatures/>", "text/xml");
  signed_source.save(Text((dir / "signed.odt").string()));
  book.load(Text((dir / "signed.odt").string()));
  rejects([&] { book.format_characters(range, red); });
  rejects([&] { book.format_paragraphs(range, paragraph); });
  assert(!book.modified() && !book.can_undo());
  std::cout
      << "PASS Words native character/paragraph formatting, inherited/mixed "
         "styles, caret typing, preservation and XML undo/redo\n";
}
