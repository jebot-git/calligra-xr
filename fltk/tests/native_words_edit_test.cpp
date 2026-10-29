// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
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
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" office:version="1.3"><office:automatic-styles>
<style:style style:name="Base" style:family="paragraph"><style:text-properties fo:font-size="8pt"/></style:style>
<style:style style:name="PageFirst" style:family="paragraph" style:parent-style-name="Base" style:master-page-name="Default"><style:paragraph-properties fo:break-before="page" fo:margin-left="5pt"/><style:text-properties fo:font-size="150%" fo:font-weight="bold"/></style:style>
<style:style style:name="Heading" style:family="paragraph" style:next-style-name="Body"><style:text-properties fo:font-weight="bold"/></style:style><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-size="10pt"/></style:style>
<style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="bold"/></style:style></office:automatic-styles><office:body><office:text>
<text:p text:style-name="PageFirst" xml:id="p1">Start <text:span text:style-name="Bold" xml:id="bold">Český 日本語</text:span> tail</text:p>
<text:section text:name="Section"><text:p text:style-name="Body">Section alpha</text:p><text:p text:style-name="Body">Section beta</text:p></text:section>
<table:table table:name="Cells"><table:table-row><table:table-cell><text:p text:style-name="Body" xml:id="cellp">Cell <text:a xlink:href="https://example.invalid/document" xml:id="link">link</text:a><text:s text:c="3"/><text:tab/>end<text:line-break/>soft</text:p></table:table-cell><table:table-cell><text:p>Other cell</text:p></table:table-cell></table:table-row></table:table>
<text:p text:style-name="Heading">Heading text</text:p>
<text:list><text:list-item><text:p>List item</text:p></text:list-item></text:list>
</office:text></office:body></office:document-content>)XML"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
TextSelection point(size_t at) { return {at, at}; }
size_t find(const TextDocument &book, const char *s) {
  auto at = book.text().find(Text(s));
  assert(at != Text::npos);
  return at;
}
Bytes content(TextDocument &book, const Text &path) {
  book.save(path);
  OdfPackage p(Kind::Words);
  p.load(path);
  return p.xml.toByteArray();
}
void unique_ids(const XmlElement &root) {
  std::set<Text> ids;
  std::function<void(XmlElement)> walk = [&](XmlElement e) {
    auto id = e.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        walk(n);
  };
  walk(root);
}
} // namespace
int main(int argc, char **argv) {
  TempDir dir;
  auto input = dir.path() + "/input.odt", saved = dir.path() + "/saved.odt";
  auto source = fixture();
  source.save(input);
  if (argc == 3 && Text(argv[1]) == "--fixture") {
    File::copy(input, Text(argv[2]));
    return 0;
  }
  TextDocument book;
  book.load(input);
  assert(!book.modified() && !book.can_undo());
  auto original = content(book, saved);
  auto original_text = book.text();
  // Split a styled body paragraph: retain runs, identities and indentation,
  // but clear the inherited forced page/master assignment on its continuation.
  auto split = find(book, "日本語") + 1;
  book.replace(split, 0, "\n");
  assert(book.text() ==
         original_text.left(split) + "\n" + original_text.mid(split));
  auto after = content(book, saved);
  OdfPackage inspected(Kind::Words);
  inspected.load(saved);
  unique_ids(inspected.body());
  auto ps = inspected.xml.elementsByTagNameNS(TEXT, "p");
  assert(ps[0].attributeNS(XML, "id") == "p1");
  assert(ps[1].attributeNS(TEXT, "style-name").startsWith("WordsSplit"));
  auto flow = book.flow();
  assert(flow.paragraphs[0].master == "Default" &&
         flow.paragraphs[1].master.empty());
  assert(flow.paragraphs[0].break_before && !flow.paragraphs[1].break_before);
  assert(flow.paragraphs[1].left == 5);
  assert(flow.paragraphs[0].font.size == 12 &&
         flow.paragraphs[1].font.size == 12);
  auto style_count = inspected.xml.elementsByTagNameNS(STYLE, "style").size();
  book.replace(split + 1, 0, "\n");
  book.save(saved);
  inspected.load(saved);
  assert(inspected.xml.elementsByTagNameNS(STYLE, "style").size() ==
         style_count);
  assert(book.undo());
  assert(content(book, saved) == after);
  assert(book.undo() == point(split));
  assert(book.text() == original_text);
  assert(book.modified());
  assert(content(book, saved) == original);
  assert(!book.modified());
  assert(book.redo() == point(split + 1));
  assert(content(book, saved) == after);
  assert(book.undo());
  // Splitting inside a table cell preserves its link and whitespace controls.
  auto cell = find(book, "Cell link") + 7;
  auto before = book.text();
  book.replace(cell, 0, "\n");
  assert(book.text() == before.left(cell) + "\n" + before.mid(cell));
  book.save(saved);
  inspected.load(saved);
  auto cells = inspected.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(children(cells[0], TEXT, "p").size() == 2 &&
         children(cells[1], TEXT, "p").size() == 1);
  auto links = inspected.xml.elementsByTagNameNS(TEXT, "a");
  assert(links.size() == 2);
  for (auto a : links)
    assert(a.attributeNS(XLINK, "href") == "https://example.invalid/document");
  unique_ids(inspected.body());
  // Join the same two paragraphs without rebuilding the runs as plain text.
  book.replace(cell, 1, "");
  assert(book.text() == before);
  book.save(saved);
  inspected.load(saved);
  cells = inspected.xml.elementsByTagNameNS(TABLE, "table-cell");
  assert(children(cells[0], TEXT, "p").size() == 1);
  unique_ids(inspected.body());
  assert(inspected.xml.elementsByTagNameNS(TEXT, "line-break").size() == 1);
  // Soft line breaks are distinct from paragraph separators and can be edited.
  auto soft = find(book, "end") + 1;
  book.replace(soft, 0, "\n", false);
  book.save(saved);
  inspected.load(saved);
  assert(inspected.xml.elementsByTagNameNS(TEXT, "line-break").size() == 2);
  auto paragraphs = inspected.xml.elementsByTagNameNS(TEXT, "p").size();
  book.replace(soft, 1, "");
  book.save(saved);
  inspected.load(saved);
  assert(inspected.xml.elementsByTagNameNS(TEXT, "line-break").size() == 1 &&
         inspected.xml.elementsByTagNameNS(TEXT, "p").size() == paragraphs);
  // Replacing part of text:s preserves the remaining exact spaces.
  auto spaces = find(book, "   ") + 1;
  before = book.text();
  book.replace(spaces, 1, "x");
  assert(book.text() == before.left(spaces) + "x" + before.mid(spaces + 1));
  assert(book.undo());
  // Sections support paragraph joins and splits; neighboring cells do not.
  auto section = find(book, "Section alpha") + 7;
  book.replace(section, 0, "\n");
  book.save(saved);
  inspected.load(saved);
  assert(
      children(inspected.xml.elementsByTagNameNS(TEXT, "section")[0], TEXT, "p")
          .size() == 3);
  assert(book.undo());
  auto boundary = find(book, "Other cell") - 1;
  before = book.text();
  auto checkpoint = content(book, saved);
  rejects([&] { book.replace(boundary, 1, ""); });
  assert(book.text() == before && !book.modified());
  assert(content(book, saved) == checkpoint);
  auto list = find(book, "List item") + 4;
  book.replace(list, 0, "\n");
  assert(book.text() == before.left(list) + "\n" + before.mid(list));
  book.save(saved);
  inspected.load(saved);
  assert(inspected.xml.elementsByTagNameNS(TEXT, "list-item").size() == 2);
  assert(book.undo());
  assert(book.text() == before);
  // next-style-name follows the retained KoTextEditor newline behavior.
  auto heading = find(book, "Heading text") + 7;
  book.replace(heading, 0, "\n");
  book.save(saved);
  inspected.load(saved);
  ps = inspected.xml.elementsByTagNameNS(TEXT, "p");
  bool found = false;
  for (auto p : ps)
    if (plain_text(p) == " text") {
      found = true;
      assert(p.attributeNS(TEXT, "style-name") == "Body");
    }
  assert(found);
  // Explicit selections survive native undo/redo, not just text contents.
  auto position = find(book, "Start");
  TextSelection selection{position + 3, position};
  book.replace(position, 3, "New", true, selection, point(position + 3));
  assert(book.undo() == selection);
  assert(book.redo() == point(position + 3));
  // Saved-state tracking and a branching edit after undo discard redo.
  book.save(saved);
  assert(!book.modified());
  assert(book.undo() && book.modified());
  assert(book.redo() && !book.modified());
  assert(book.undo());
  book.replace(position, 1, "Z");
  assert(!book.can_redo());
  book.save(saved);
  TextDocument reopened;
  reopened.load(saved);
  assert(reopened.text() == book.text() && !reopened.can_undo());
  inspected.load(saved);
  assert(inspected.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
  // Failed loads and invalid edits leave both document and undo state intact.
  bool undo = book.can_undo();
  before = book.text();
  rejects([&] { book.load(dir.path() + "/missing.odt"); });
  assert(book.text() == before && book.can_undo() == undo);
  rejects([&] { book.replace(book.text().size() + 1, 0, "x"); });
  assert(book.text() == before);
  // A style with conditional mappings cannot be rewritten silently when a
  // continuation needs to discard inherited page/master assignments.
  auto conditional = fixture();
  for (auto style : conditional.xml.elementsByTagNameNS(STYLE, "style"))
    if (style.attributeNS(STYLE, "name") == "PageFirst") {
      auto rule = conditional.xml.createElementNS(STYLE, "style:map");
      rule.setAttributeNS(STYLE, "style:condition", "table()");
      rule.setAttributeNS(STYLE, "style:apply-style-name", "Body");
      style.appendChild(rule);
    }
  conditional.save(dir.path() + "/conditional.odt");
  TextDocument guarded;
  guarded.load(dir.path() + "/conditional.odt");
  auto guarded_xml = content(guarded, saved);
  rejects([&] { guarded.replace(2, 0, "\n"); });
  assert(!guarded.can_undo() && !guarded.modified());
  assert(content(guarded, saved) == guarded_xml);
  // Typing in a styled span must not accumulate one XML wrapper per key.
  TextDocument typing;
  typing.load(input);
  auto typed_at = find(typing, "Český") + 2;
  for (int i = 0; i < 130; ++i)
    typing.replace(typed_at + i, 0, "x");
  typing.save(saved);
  inspected.load(saved);
  assert(inspected.xml.elementsByTagNameNS(TEXT, "span").size() == 1);
  unique_ids(inspected.body());
  // The entry-count budget retains a valid bounded history.
  TextDocument bounded;
  for (int i = 0; i < 130; ++i)
    bounded.replace(0, 0, "x");
  int count = 0;
  while (bounded.undo())
    ++count;
  assert(count == 100 && bounded.text().size() == 30);
  std::cout
      << "PASS Words XML undo, styled body/cell/section splits and joins, soft "
         "breaks, selections, atomic failures and archive retention\n";
}
