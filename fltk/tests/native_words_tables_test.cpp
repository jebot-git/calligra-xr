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
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
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
  p.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-family="DejaVu Sans" fo:font-size="14pt"/></style:style><style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="bold"/></style:style></office:automatic-styles><office:body><office:text><text:p xml:id="first" text:style-name="Body">Alpha <text:span text:style-name="Bold">Český 日本語</text:span> <text:a xlink:href="https://example.invalid" xlink:type="simple">link</text:a></text:p><text:section text:name="Section"><text:p>Section end</text:p></text:section><text:list><text:list-item xml:id="item"><text:p>List one</text:p><text:p>List continuation</text:p></text:list-item><text:list-item><text:p>List two</text:p></text:list-item></text:list></office:text></office:body></office:document-content>)XML");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
Text all_text(const OdfPackage &p) {
  TextList parts;
  std::function<void(XmlElement)> walk = [&](XmlElement n) {
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement()) {
        if (c.namespaceURI() == TEXT &&
            (c.localName() == "p" || c.localName() == "h"))
          parts << plain_text(c);
        else
          walk(c);
      }
  };
  walk(p.body());
  return parts.join('\n');
}
void ids(const OdfPackage &p) {
  std::set<Text> seen;
  for (auto node : p.xml.elementsByTagNameNS(TEXT, "p")) {
    auto id = node.attributeNS(XML, "id");
    assert(id.empty() || seen.insert(id).second);
  }
  for (auto node : p.xml.elementsByTagNameNS(TEXT, "span")) {
    auto id = node.attributeNS(XML, "id");
    assert(id.empty() || seen.insert(id).second);
  }
}
} // namespace
int main(int argc, char **argv) {
  auto dir = std::filesystem::temp_directory_path() /
             ("calligra-words-tables-" + std::to_string(getpid()));
  if (argc == 3 && Text(argv[1]) == "--artifacts")
    dir = argv[2];
  std::filesystem::create_directories(dir);
  auto original = fixture();
  original.save(Text((dir / "source.odt").string()));
  TextDocument book;
  book.load(Text((dir / "source.odt").string()));
  auto before = book.style_snapshot().xml.toByteArray();
  auto text = book.text();
  // Splitting an inline span preserves both source text pieces and one ID.
  auto changed = insert_words_table(book.style_snapshot(), 9, {3, 2, true});
  auto flow = read_text_flow(changed.document);
  assert(flow.tables.size() == 1 && flow.tables[0].rows.size() == 3 &&
         flow.tables[0].columns.size() == 2);
  assert(flow.tables[0].rows[0].header && !flow.tables[0].rows[1].header);
  assert(flow.tables[0].cells.size() == 6);
  for (const auto &cell : flow.tables[0].cells) {
    assert(cell.blocks.size() == 1);
    const auto &paragraph = flow.paragraphs[cell.blocks[0].index];
    assert(paragraph.font.family == "DejaVu Sans" && paragraph.font.size == 14);
  }

  assert(changed.cursor == 10);
  ids(changed.document);
  auto expected = text.left(9) + Text(7, U'\n') + text.mid(9);
  assert(all_text(changed.document) == expected);
  auto selection = TextSelection{6, 9};
  auto next = TextSelection{changed.cursor, changed.cursor};
  assert(book.commit_structure(changed.document, selection, next,
                               book.revision()));
  assert(book.text() == expected && book.modified());
  book.save(Text((dir / "inserted.odt").string()));
  auto inserted = book.style_snapshot().xml.toByteArray();
  auto old = book.undo();
  assert(old && *old == selection && book.modified());
  assert(book.text() == text &&
         book.style_snapshot().xml.toByteArray() == before);
  assert(book.redo() == next && !book.modified());
  assert(book.style_snapshot().xml.toByteArray() == inserted);
  // Delete resolves the inner table and restores an independently editable
  // outer cell; unrelated outer cells and rich XML remain present.
  auto nested =
      insert_words_table(book.style_snapshot(), next.cursor, {2, 1, false});
  assert(read_text_flow(nested.document).tables.size() == 2);
  assert(words_in_table(nested.document, nested.cursor));
  nested.document.save(Text((dir / "nested.odt").string()));
  auto inner_deleted = delete_words_table(nested.document, nested.cursor);
  assert(read_text_flow(inner_deleted.document).tables.size() == 1);
  assert(words_in_table(inner_deleted.document, inner_deleted.cursor));
  inner_deleted.document.save(Text((dir / "nested-deleted.odt").string()));
  auto removed = delete_words_table(book.style_snapshot(), next.cursor);
  assert(read_text_flow(removed.document).tables.empty());
  assert(book.commit_structure(removed.document, next,
                               {removed.cursor, removed.cursor},
                               book.revision()));
  book.save(Text((dir / "deleted.odt").string()));
  assert(book.undo() == next);
  assert(book.style_snapshot().xml.toByteArray() == inserted);
  // Direct tables are not permitted inside ODF list items. Reject before
  // changing source; anchored table/list splitting belongs to a later command.
  auto list_position = text.find(Text("List one")) + 4;
  auto original_bytes = original.xml.toByteArray();
  rejects([&] { insert_words_table(original, list_position, {2, 2, false}); });
  assert(original.xml.toByteArray() == original_bytes);
  auto section_position = text.find(Text("Section end")) + 7;
  auto section = insert_words_table(original, section_position, {2, 2, false});
  section.document.save(Text((dir / "section-table.odt").string()));
  // Start-of-paragraph insertion moves the original empty prefix into the
  // first cell, preserving ID and avoiding a blank line before the table.
  auto start = insert_words_table(original, 0, {1, 1, false});
  assert(start.cursor == 0);
  assert(children(start.document.body(), TABLE, "table").size() == 1);
  assert(children(start.document.body(), TABLE, "table")[0] ==
         start.document.body().firstChild());
  assert(start.document.xml.elementsByTagNameNS(TEXT, "p")[0].attributeNS(
             XML, "id") == "first");
  start.document.save(Text((dir / "at-start.odt").string()));
  // Empty bodies become a table and an exit paragraph; delete restores a caret.
  OdfPackage empty(Kind::Words);
  auto body = empty.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  auto first = insert_words_table(empty, 0, {2, 2, true});
  assert(first.cursor == 0);
  auto erased = delete_words_table(first.document, 0);
  assert(read_text_flow(erased.document).tables.empty() &&
         all_text(erased.document).empty());
  first.document.save(Text((dir / "empty-table.odt").string()));
  erased.document.save(Text((dir / "empty-deleted.odt").string()));
  // An inserted table at a master-switch paragraph takes over that transition.
  // Deleting it retains the page setup on an empty anchor at the same location.
  OdfPackage paged(Kind::Words);
  paged.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="Page" style:family="paragraph" style:master-page-name="Other"><style:paragraph-properties fo:break-before="page"/></style:style></office:automatic-styles><office:body><office:text><text:p text:style-name="Page" xml:id="page">Page content</text:p></office:text></office:body></office:document-content>)XML");
  paged.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:styles/><office:automatic-styles><style:page-layout style:name="A"><style:page-layout-properties fo:page-width="12cm" fo:page-height="20cm" fo:margin="1cm"/></style:page-layout><style:page-layout style:name="B"><style:page-layout-properties fo:page-width="16cm" fo:page-height="20cm" fo:margin="1cm"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="First" style:page-layout-name="A"/><style:master-page style:name="Other" style:page-layout-name="B"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  auto page_table = insert_words_table(paged, 0, {2, 2, false});
  auto page_flow = read_text_flow(page_table.document);
  assert(page_flow.tables[0].master == "Other" &&
         page_flow.tables[0].break_before);
  auto measure = [](const Text &value, const TextFont &) {
    return TextMetrics{double(value.size()) * 6, 9, 3};
  };
  auto pages = paginate_text(page_flow, measure);
  assert(pages.pages.size() == 1 &&
         std::abs(pages.pages[0].style.width - 16 * 72 / 2.54) < 1e-8);
  page_table.document.save(Text((dir / "page-table.odt").string()));
  auto page_deleted =
      delete_words_table(page_table.document, page_table.cursor);
  pages = paginate_text(read_text_flow(page_deleted.document), measure);
  assert(pages.pages.size() == 1 &&
         std::abs(pages.pages[0].style.width - 16 * 72 / 2.54) < 1e-8);
  assert(page_deleted.document.entry("styles.xml") ==
         paged.entry("styles.xml"));
  page_deleted.document.save(Text((dir / "page-table-deleted.odt").string()));
  // Signed/wrong-kind packages reject structural changes without consuming
  // their saved state; an unchanged candidate also retains redo and revision.
  OdfPackage wrong(Kind::Stage);
  rejects([&] { insert_words_table(wrong, 0, {}); });
  rejects([&] { book.commit_structure(wrong, next, next, book.revision()); });
  auto unchanged_revision = book.revision();
  assert(!book.commit_structure(book.style_snapshot(), next, next,
                                unchanged_revision));
  assert(book.revision() == unchanged_revision);
  auto signed_source = fixture();
  signed_source.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                          "text/xml");
  signed_source.save(Text((dir / "signed-source.odt").string()));
  OdfPackage signed_package(Kind::Words);
  signed_package.load(Text((dir / "signed-source.odt").string()));
  assert(signed_package.read_only());
  rejects([&] { insert_words_table(signed_package, 0, {}); });
  rejects([&] { delete_words_table(signed_package, 0); });
  TextDocument signed_book;
  signed_book.load(Text((dir / "signed-source.odt").string()));
  rejects([&] {
    signed_book.commit_structure(original, {0, 0}, {0, 0},
                                 signed_book.revision());
  });
  assert(!signed_book.modified() && !signed_book.can_undo());
  // Failed and stale structural changes leave redo/source and saved state.
  auto checkpoint = book.style_snapshot().xml.toByteArray();
  auto revision = book.revision();
  rejects([&] {
    book.commit_structure(changed.document, selection, next, revision - 1);
  });
  rejects([&] {
    book.commit_structure(changed.document, selection, {999999, 999999},
                          revision);
  });
  assert(book.style_snapshot().xml.toByteArray() == checkpoint &&
         book.revision() == revision);
  for (auto dimensions : std::vector<WordsTableInsertion>{{0, 2, false},
                                                          {2, 0, false},
                                                          {100001, 1, false},
                                                          {1, 1025, false},
                                                          {1000, 1000, false}})
    rejects([&] { insert_words_table(original, 0, dimensions); });
  rejects([&] { insert_words_table(original, text.size() + 1, {}); });
  assert(!words_in_table(original, 0));
  rejects([&] { delete_words_table(original, 0); });
  book.begin_edit(next);
  auto grouped = delete_words_table(book.style_snapshot(), next.cursor);
  assert(book.commit_structure(grouped.document, next,
                               {grouped.cursor, grouped.cursor},
                               book.revision()));
  book.cancel_edit();
  assert(book.end_edit() == next);
  assert(book.style_snapshot().xml.toByteArray() == checkpoint);
  book.begin_edit(next);
  grouped = delete_words_table(book.style_snapshot(), next.cursor);
  assert(book.commit_structure(grouped.document, next,
                               {grouped.cursor, grouped.cursor},
                               book.revision()));
  rejects([&] {
    book.commit_structure(original, {0, 0}, {999999, 999999}, book.revision());
  });
  assert(book.edit_cancelled() && book.end_edit() == next);
  assert(book.style_snapshot().xml.toByteArray() == checkpoint);

  assert(book.style_snapshot().entry("Extras/opaque.bin") ==
         original.entry("Extras/opaque.bin"));
  std::cout << "PASS native Words table insertion/deletion, rich splits, "
               "nested/section scope and structural history\n";
}
