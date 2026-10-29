// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <functional>
#include <filesystem>
#include <iostream>
using namespace DomeOffice;
namespace {
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
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" office:version="1.3"><office:automatic-styles><text:list-style style:name="Numbers"><text:list-level-style-number text:level="1" style:num-format="1" style:num-suffix="."/><text:list-level-style-number text:level="2" style:num-format="a" style:num-suffix=")" text:display-levels="2"/></text:list-style></office:automatic-styles><office:body><office:text>
<text:p xml:id="WordsList1">Before</text:p><text:list text:style-name="Numbers" xml:id="main"><text:list-header><text:p>Heading</text:p></text:list-header><text:list-item xml:id="alpha"><text:p>Alpha</text:p><text:p>A continuation</text:p><text:list xml:id="child"><text:list-item xml:id="child1"><text:p>Child one</text:p></text:list-item><text:list-item xml:id="child2"><text:p>Child two</text:p></text:list-item></text:list></text:list-item><text:list-item xml:id="beta"><text:p>Beta</text:p></text:list-item><text:list-item xml:id="gamma"><text:p>Gamma</text:p></text:list-item></text:list>
<text:list text:style-name="Numbers" text:continue-list="main"><text:list-item><text:p>Continued</text:p></text:list-item></text:list><text:list text:style-name="Numbers" text:continue-list="child"><text:list-item><text:p>Child continuation</text:p></text:list-item></text:list>
<text:list text:style-name="Numbers" xml:id="empty"><text:list-item><text:p>First</text:p></text:list-item><text:list-item xml:id="blank-item"><text:number>2.</text:number><text:p xml:id="blank-p"/></text:list-item><text:list-item><text:p>After</text:p></text:list-item></text:list><text:list text:style-name="Numbers" text:continue-list="empty"><text:list-item><text:p>Empty continuation</text:p></text:list-item></text:list>
<table:table table:name="Cells"><table:table-column/><table:table-row><table:table-cell><text:list text:style-name="Numbers" xml:id="cell"><text:list-item><text:p>Cell one</text:p></text:list-item><text:list-item><text:p>Cell two</text:p></text:list-item></text:list></table:table-cell></table:table-row></table:table>
<text:section text:name="Section"><text:list text:style-name="Numbers" xml:id="section"><text:list-item><text:p>Section one</text:p></text:list-item><text:list-item><text:p>Section two</text:p></text:list-item></text:list></text:section>
<text:list text:style-name="Numbers" xml:id="terminal"><text:list-item><text:p>Parent</text:p><text:list xml:id="terminal-child"><text:list-item><text:p>Nested before</text:p></text:list-item><text:list-item xml:id="nested-blank"><text:p xml:id="nested-blank-p"/></text:list-item></text:list></text:list-item><text:list-item><text:p>Root after</text:p></text:list-item></text:list><text:list text:style-name="Numbers" text:continue-list="terminal"><text:list-item><text:p>Root continuation</text:p></text:list-item></text:list><text:list text:style-name="Numbers" text:continue-list="terminal-child"><text:list-item><text:p>Nested continuation</text:p></text:list-item></text:list>
</office:text></office:body></office:document-content>)XML"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
XmlElement id(const OdfPackage &p, const char *key) {
  XmlElement found;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    if (node.attributeNS(XML, "id") == key)
      found = node;
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        walk(n);
  };
  walk(p.xml.documentElement());
  assert(!found.isNull());
  return found;
}
size_t at(const TextDocument &book, const char *text) {
  auto result = book.text().find(Text(text));
  assert(result != Text::npos);
  return result;
}
size_t source_offset(const OdfPackage &p, const char *key) {
  size_t offset = 0;
  std::optional<size_t> result;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        if (n.namespaceURI() == TEXT &&
            (n.localName() == "p" || n.localName() == "h")) {
          if (n.attributeNS(XML, "id") == key)
            result = offset;
          offset += plain_text(n).size() + 1;
        } else
          walk(n);
      }
  };
  walk(p.body());
  assert(result);
  return *result;
}
TextSelection point(size_t n) { return {n, n}; }
const TextParagraph paragraph(const TextDocument &book, const Text &text) {
  for (const auto &p : book.flow().paragraphs)
    if (p.text == text)
      return p;
  throw std::runtime_error("Missing paragraph");
}
Text label(const TextDocument &book, const Text &text) {
  auto p = paragraph(book, text);
  return p.list ? p.list->text : Text();
}
void unique_ids(const OdfPackage &p) {
  std::set<Text> ids;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    auto key = node.attributeNS(XML, "id");
    assert(key.empty() || ids.insert(key).second);
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        walk(n);
  };
  walk(p.xml.documentElement());
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
  std::filesystem::path artifact_dir;
  size_t artifact_number = 0;
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    artifact_dir = argv[2];
    if (std::filesystem::exists(artifact_dir) &&
        !std::filesystem::is_empty(artifact_dir))
      throw std::runtime_error("Choose a new or empty artifact directory");
    std::filesystem::create_directories(artifact_dir);
  }
  auto retain = [&](const Text &path) {
    if (!artifact_dir.empty()) {
      auto target = artifact_dir /
                    ("state-" + std::to_string(++artifact_number) + ".odt");
      assert(File::copy(path, Text(target.string())));
    }
  };
  TextDocument book;
  book.load(input);
  OdfPackage inspected(Kind::Words);
  auto snapshot = [&] {
    book.save(saved);
    inspected.load(saved);
    unique_ids(inspected);
    retain(saved);
    assert(inspected.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
    return inspected.xml.toByteArray();
  };
  auto original = snapshot();
  auto original_text = book.text();
  auto restore = [&] {
    assert(book.undo());
    assert(book.text() == original_text);
    assert(snapshot() == original);
  };
  // Tab appends to the previous item's compatible nested list and moves the
  // complete source item, with no text/caret changes and one native command.
  auto beta = at(book, "Beta");
  auto revision = book.revision();
  assert(!book.list_action(point(beta + 1), WordsListAction::Indent, true));
  assert(book.revision() == revision && !book.modified());
  assert(book.list_action(point(beta), WordsListAction::Indent, true));
  assert(book.text() == original_text && book.modified());
  auto changed = snapshot();
  assert(id(inspected, "beta").parentNode() == id(inspected, "child"));
  assert(label(book, "Beta") == "1.c)" && label(book, "Gamma") == "2." &&
         label(book, "Continued") == "3.");
  assert(book.undo() == point(beta));
  assert(snapshot() == original);
  assert(book.redo() == point(beta));
  assert(snapshot() == changed);
  restore();
  // Outdent preserves the following source items in a continued nested tail.
  assert(
      book.list_action(point(at(book, "Child one")), WordsListAction::Outdent));
  snapshot();
  assert(id(inspected, "child1").parentNode() == id(inspected, "main"));
  auto tail = child(id(inspected, "child1"), TEXT, "list");
  assert(!tail.isNull() && id(inspected, "child2").parentNode() == tail);
  assert(tail.attributeNS(TEXT, "continue-list") == "child");
  assert(label(book, "Child one") == "2." &&
         label(book, "Child two") == "2.a)");
  assert(label(book, "Continued") == "5." &&
         label(book, "Child continuation") == "2.");
  restore();
  auto end = at(book, "Gamma") + 3;
  assert(book.list_action({beta + 1, end}, WordsListAction::Indent));
  snapshot();
  assert(id(inspected, "beta").parentNode() == id(inspected, "child"));
  assert(id(inspected, "gamma").parentNode() == id(inspected, "child"));
  assert(label(book, "Gamma") == "1.d)" && label(book, "Continued") == "2.");
  restore();
  // A range ending exactly at the next paragraph excludes that next item.
  assert(book.list_action({beta, at(book, "Gamma")}, WordsListAction::Indent,
                          true));
  snapshot();
  assert(id(inspected, "gamma").parentNode() == id(inspected, "main"));
  restore();
  assert(book.list_action({at(book, "Alpha"), at(book, "Child two") + 2},
                          WordsListAction::Indent));
  assert(paragraph(book, "Alpha").list->level == 2 &&
         paragraph(book, "Child one").list->level == 3);
  restore();
  revision = book.revision();
  assert(book.list_action(point(beta), WordsListAction::Outdent));
  assert(book.revision() == revision && !book.modified());
  assert(!book.list_action(point(at(book, "Before")), WordsListAction::Indent));
  // Both IDs of an empty item/paragraph survive removal of numbering. Later
  // explicit references follow the tail, not the truncated original prefix.
  auto blank = at(book, "After") - 1;
  assert(book.list_action(point(blank), WordsListAction::ExitEmpty, true));
  snapshot();
  auto p = id(inspected, "blank-p");
  assert(p.parentNode() == inspected.body());
  assert(id(inspected, "blank-item").parentNode() == p);
  assert(id(inspected, "blank-item").localName() == "meta");
  assert(label(book, "After") == "2." &&
         label(book, "Empty continuation") == "3.");
  assert(book.text() == original_text);
  auto exited = snapshot();
  book.replace(blank, 0, "x");
  snapshot();
  assert(id(inspected, "blank-item").localName() == "meta");
  assert(book.undo());
  assert(snapshot() == exited);
  book.set_text(book.text().left(blank) + "bulk" + book.text().mid(blank));
  snapshot();
  assert(id(inspected, "blank-item").localName() == "meta");
  assert(book.undo());
  assert(snapshot() == exited);
  restore();
  // Terminal nested empty items exit all numbering levels, with source order
  // and continuation counters retained at every intermediate list anchor.
  auto nested_blank = at(book, "Root after") - 1;
  assert(book.list_action(point(nested_blank), WordsListAction::ExitEmpty));
  snapshot();
  assert(id(inspected, "nested-blank-p").parentNode() == inspected.body());
  assert(label(book, "Root after") == "2." &&
         label(book, "Root continuation") == "3.");
  assert(label(book, "Nested continuation") == "2.");
  restore();
  for (auto name : {"Cell two", "Section two"}) {
    assert(book.list_action(point(at(book, name)), WordsListAction::Indent));
    assert(paragraph(book, name).list->level == 2);
    restore();
  }
  // First-item indentation does not invent an editable paragraph or visible
  // parent number. Empty structural headers are valid OpenDocument lists.
  auto no_header = fixture();
  auto main = id(no_header, "main");
  main.removeChild(child(main, TEXT, "list-header"));
  no_header.save(dir.path() + "/no-header.odt");
  TextDocument first;
  first.load(dir.path() + "/no-header.odt");
  auto first_text = first.text();
  assert(first.list_action(point(at(first, "Alpha")), WordsListAction::Indent));
  assert(first.text() == first_text &&
         paragraph(first, "Alpha").list->level == 2);
  first.save(saved);
  retain(saved);
  for (auto warning : first.flow().unsupported)
    assert(!warning.contains("without an editable paragraph"));
  assert(
      first.list_action(point(at(first, "Alpha")), WordsListAction::Outdent));
  assert(first.text() == first_text &&
         paragraph(first, "Alpha").list->level == 1);
  first.save(saved);
  retain(saved);
  // Empty-item exit works at the start/end and in otherwise empty root lists.
  for (int variant = 0; variant < 3; ++variant) {
    auto p = fixture();
    auto list = id(p, "empty");
    auto items = children(list, TEXT, "list-item");
    if (variant == 0 || variant == 2)
      list.removeChild(items[0]);
    if (variant == 1 || variant == 2)
      list.removeChild(items[2]);
    p.save(dir.path() + "/empty.odt");
    TextDocument test;
    test.load(dir.path() + "/empty.odt");
    auto before = test.text();
    auto position = source_offset(p, "blank-p");
    assert(test.list_action(point(position), WordsListAction::ExitEmpty));
    assert(test.text() == before);
    test.save(saved);
    retain(saved);
    inspected.load(saved);
    unique_ids(inspected);
    assert(id(inspected, "blank-p").parentNode() == inspected.body());
    assert(label(test, "Empty continuation") == (variant == 1   ? "2."
                                                 : variant == 2 ? "1."
                                                                : "2."));
  }
  // Unsupported paragraph-level reparenting and mixed-list selections fail
  // atomically instead of changing unselected source text or hidden objects.
  for (int variant = 0; variant < 2; ++variant) {
    auto p = fixture();
    if (variant == 0) {
      auto extra = p.xml.createElementNS(TEXT, "text:p");
      set_plain_text(extra, "Trailing parent");
      id(p, "alpha").appendChild(extra);
    } else
      set_plain_text(child(id(p, "child1"), TEXT, "p"), "");
    p.save(dir.path() + "/guarded.odt");
    TextDocument test;
    test.load(dir.path() + "/guarded.odt");
    auto before = test.text();
    rejects([&] {
      test.list_action(point(variant == 0 ? at(test, "Child one")
                                          : at(test, "Child two") - 1),
                       variant == 0 ? WordsListAction::Outdent
                                    : WordsListAction::ExitEmpty);
    });
    assert(test.text() == before && !test.modified() && !test.can_undo());
    test.save(saved);
    retain(saved);
    inspected.load(saved);
    assert(inspected.xml.toByteArray() == p.xml.toByteArray());
  }
  rejects([&] {
    book.list_action({beta, at(book, "Cell two") + 2}, WordsListAction::Indent);
  });
  assert(!book.modified() && snapshot() == original);
  rejects([&] {
    book.list_action(point(book.text().size() + 1), WordsListAction::Indent);
  });
  auto serial = fixture();
  auto automatic =
      child(serial.xml.documentElement(), OFFICE, "automatic-styles");
  auto definition = serial.xml.createElementNS(TEXT, "text:list-style");
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  definition.setAttributeNS(STYLE, "style:name", "Serial");
  definition.setAttributeNS(TEXT, "text:consecutive-numbering", "true");
  for (int level = 1; level <= 2; ++level) {
    auto number =
        serial.xml.createElementNS(TEXT, "text:list-level-style-number");
    number.setAttributeNS(TEXT, "text:level", level);
    number.setAttributeNS(STYLE, "style:num-format", "1");
    number.setAttributeNS(STYLE, "style:num-suffix", ".");
    definition.appendChild(number);
  }
  automatic.appendChild(definition);
  id(serial, "main").setAttributeNS(TEXT, "text:style-name", "Serial");
  serial.save(dir.path() + "/serial.odt");
  TextDocument consecutive;
  consecutive.load(dir.path() + "/serial.odt");
  assert(label(consecutive, "Beta") == "4." &&
         label(consecutive, "Gamma") == "5.");
  assert(consecutive.list_action(point(at(consecutive, "Beta")),
                                 WordsListAction::Indent));
  consecutive.save(saved);
  retain(saved);
  assert(label(consecutive, "Beta") == "4." &&
         label(consecutive, "Gamma") == "5.");
  assert(consecutive.undo());
  assert(consecutive.list_action(point(at(consecutive, "Child one")),
                                 WordsListAction::Outdent));
  consecutive.save(saved);
  retain(saved);
  assert(label(consecutive, "Child two") == "3." &&
         label(consecutive, "Gamma") == "5.");
  auto signed_source = fixture();
  signed_source.set_entry("META-INF/documentsignatures.xml",
                          "<document-signatures/>", "text/xml");
  signed_source.save(dir.path() + "/signed.odt");
  TextDocument read_only;
  read_only.load(dir.path() + "/signed.odt");
  assert(read_only.read_only());
  rejects([&] {
    read_only.list_action(point(at(read_only, "Beta")),
                          WordsListAction::Indent);
  });
  assert(!read_only.can_undo() && !read_only.modified());
  auto metadata = fixture();
  auto before_paragraph = id(metadata, "WordsList1");
  set_plain_text(before_paragraph, "");
  auto meta = metadata.xml.createElementNS(TEXT, "text:meta");
  meta.setAttributeNS(XML, "xml:id", "metadata-text");
  meta.appendChild(metadata.xml.createTextNode("Metadata"));
  before_paragraph.appendChild(meta);
  metadata.save(dir.path() + "/metadata.odt");
  TextDocument meta_book;
  meta_book.load(dir.path() + "/metadata.odt");
  assert(meta_book.text().startsWith("Metadata\n"));
  assert(paragraph(meta_book, "Metadata").text == "Metadata");
  for (auto warning : meta_book.flow().unsupported)
    assert(!warning.contains("Inline fields"));
  rejects([&] { meta_book.replace(0, 1, "X"); });
  assert(!meta_book.can_undo() && !meta_book.modified());
  meta_book.save(saved);
  retain(saved);
  auto cdata = fixture();
  auto cdata_p = id(cdata, "WordsList1");
  set_plain_text(cdata_p, "");
  auto cdata_xml = read_document_xml("<root><![CDATA[Before]]></root>");
  cdata_p.appendChild(
      cdata.xml.importNode(cdata_xml.documentElement().firstChild(), true));
  cdata.save(dir.path() + "/cdata.odt");
  TextDocument cdata_book;
  cdata_book.load(dir.path() + "/cdata.odt");
  assert(paragraph(cdata_book, "Before").text == "Before");
  cdata_book.set_text("Changed" + cdata_book.text().mid(6));
  assert(cdata_book.text().startsWith("Changed\n"));
  assert(paragraph(cdata_book, "Changed").text == "Changed");
  cdata_book.save(saved);
  retain(saved);
  assert(cdata_book.undo());
  cdata_book.save(saved);
  retain(saved);
  inspected.load(saved);
  assert(inspected.xml.toByteArray() == cdata.xml.toByteArray());
  std::cout << "PASS Words list levels, ranges, empty exit, preserved source "
               "order/IDs, continued tails and XML undo\n";
}
