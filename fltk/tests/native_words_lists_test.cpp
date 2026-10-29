// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <functional>
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
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" office:version="1.3"><office:automatic-styles>
<text:list-style style:name="Numbers"><text:list-level-style-number text:level="1" style:num-format="1" style:num-suffix="."/><text:list-level-style-number text:level="2" style:num-format="a" style:num-suffix=")" text:display-levels="2"/></text:list-style>
<text:list-style style:name="Roman"><text:list-level-style-number text:level="1" style:num-format="I" style:num-suffix="."/></text:list-style>
<style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="bold"/></style:style>
</office:automatic-styles><office:body><office:text><text:p>Before</text:p>
<text:list text:style-name="Numbers" xml:id="main"><text:list-header xml:id="header"><text:p xml:id="headerp">List header</text:p></text:list-header>
<text:list-item xml:id="alpha" text:start-value="5"><text:number>5.</text:number><text:p xml:id="alphap">Alpha <text:span text:style-name="Bold" xml:id="rich">Český</text:span> <text:a xlink:href="https://example.invalid/list">link</text:a><text:s text:c="2"/>end</text:p><text:p>Continuation</text:p><text:list><text:list-item><text:p>Child one</text:p></text:list-item></text:list></text:list-item>
<text:list-item xml:id="beta" text:style-override="Roman"><text:p>Beta</text:p></text:list-item><text:list-item><text:p>Gamma</text:p></text:list-item></text:list>
<text:p>Between</text:p><text:list text:style-name="Numbers" text:continue-list="main"><text:list-item><text:p>Continued main</text:p></text:list-item></text:list>
<text:list text:style-name="Numbers" xml:id="join"><text:list-item xml:id="join1"><text:p>Join one</text:p></text:list-item><text:list-item xml:id="join2" text:start-value="17" text:style-override="Roman"><text:number>XVII.</text:number><text:p>Join two</text:p><text:p>After join</text:p><text:list><text:list-item><text:p>Join child</text:p></text:list-item></text:list></text:list-item><text:list-item><text:p>Join three</text:p></text:list-item></text:list>
<text:list text:style-name="Numbers" text:continue-list="join"><text:list-item><text:p>Continued join</text:p></text:list-item></text:list>
<table:table table:name="Cells"><table:table-row><table:table-cell><text:list text:style-name="Numbers" xml:id="cell"><text:list-item><text:p>Cell first</text:p></text:list-item><text:list-item><text:p>Cell second</text:p></text:list-item></text:list></table:table-cell><table:table-cell><text:p>Other cell</text:p></table:table-cell></table:table-row></table:table>
</office:text></office:body></office:document-content>)XML"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
XmlElement list(const OdfPackage &p, const char *id) {
  for (auto node : p.xml.elementsByTagNameNS(TEXT, "list"))
    if (node.attributeNS(XML, "id") == id)
      return node;
  throw std::runtime_error("Missing test list");
}
size_t at(const TextDocument &book, const char *text) {
  auto result = book.text().find(Text(text));
  assert(result != Text::npos);
  return result;
}
Text label(const TextDocument &book, const Text &text) {
  for (const auto &p : book.flow().paragraphs)
    if (p.text == text)
      return p.list && p.list->visible ? p.list->text : Text();
  throw std::runtime_error("Missing test paragraph");
}
Text style(const TextDocument &book, const Text &text) {
  for (const auto &p : book.flow().paragraphs)
    if (p.text == text)
      return p.list ? p.list->style : Text();
  throw std::runtime_error("Missing test paragraph");
}
void unique_ids(const OdfPackage &p) {
  std::set<Text> ids;
  std::function<void(XmlElement)> walk = [&](XmlElement e) {
    auto id = e.attributeNS(XML, "id");
    assert(id.empty() || ids.insert(id).second);
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
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
  TextDocument book;
  book.load(input);
  OdfPackage inspected(Kind::Words);
  auto snapshot = [&] {
    book.save(saved);
    inspected.load(saved);
    unique_ids(inspected);
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
  assert(label(book, "Beta") == "VI.");
  assert(label(book, "Continued main") == "8.");
  assert(style(book, "Join child") == "Roman");
  // Split a rich first paragraph; following paragraphs and nested lists travel
  // with its suffix. Only the original item retains its identity/restart.
  auto split = at(book, "Český") + 2;
  book.replace(split, 0, "\n");
  assert(book.text() ==
         original_text.left(split) + "\n" + original_text.mid(split));
  auto split_xml = snapshot();
  auto items = children(list(inspected, "main"), TEXT, "list-item");
  assert(items.size() == 4 && items[0].attributeNS(XML, "id") == "alpha");
  assert(items[0].attributeNS(TEXT, "start-value") == "5");
  assert(!items[1].hasAttributeNS(XML, "id") &&
         !items[1].hasAttributeNS(TEXT, "start-value"));
  assert(children(items[0], TEXT, "p").size() == 1);
  assert(children(items[1], TEXT, "p").size() == 2 &&
         children(items[1], TEXT, "list").size() == 1);
  assert(label(book, "Beta") == "VII.");
  assert(label(book, "Continued main") == "9.");
  restore();
  assert(book.redo());
  assert(snapshot() == split_xml);
  restore();
  // Enter in an unnumbered continuation paragraph also starts a new item.
  split = at(book, "Continuation") + 6;
  book.replace(split, 0, "\n");
  snapshot();
  items = children(list(inspected, "main"), TEXT, "list-item");
  assert(children(items[0], TEXT, "p").size() == 2);
  assert(children(items[1], TEXT, "list").size() == 1);
  assert(label(book, "Contin") == "");
  assert(label(book, "uation") == "6.");
  restore();
  // A header stays unnumbered; its continuation becomes a real list item.
  split = at(book, "List header") + 4;
  book.replace(split, 0, "\n");
  snapshot();
  auto main = list(inspected, "main");
  auto headers = children(main, TEXT, "list-header");
  items = children(main, TEXT, "list-item");
  assert(headers.size() == 1 &&
         plain_text(child(headers[0], TEXT, "p")) == "List");
  assert(headers[0].attributeNS(XML, "id") == "header");
  assert(plain_text(child(items[0], TEXT, "p")) == " header");
  assert(label(book, " header") == "1." && label(book, "Beta") == "VI.");
  restore();
  // Multiple pasted breaks inherit the item style override without a restart.
  split = at(book, "Beta") + 2;
  book.replace(split, 0, "\nMiddle\n");
  snapshot();
  items = children(list(inspected, "main"), TEXT, "list-item");
  for (size_t i = 1; i <= 3; ++i)
    assert(items[i].attributeNS(TEXT, "style-override") == "Roman");
  assert(label(book, "Middle") == "VII." && label(book, "ta") == "VIII.");
  assert(label(book, "Continued main") == "10.");
  restore();
  // Join two adjacent items, moving the final item's trailing paragraph/list
  // intact. The removed item's restart/style override does not infect the
  // first.
  auto join = at(book, "Join two") - 1;
  book.replace(join, 1, "");
  snapshot();
  items = children(list(inspected, "join"), TEXT, "list-item");
  assert(items.size() == 2 && items[0].attributeNS(XML, "id") == "join1");
  assert(!items[0].hasAttributeNS(TEXT, "start-value"));
  assert(children(items[0], TEXT, "p").size() == 2 &&
         children(items[0], TEXT, "list").size() == 1);
  assert(label(book, "Join oneJoin two") == "1.");
  assert(label(book, "After join") == "");
  assert(style(book, "Join child") == "Roman" &&
         label(book, "Join child") == "I.");
  assert(child(items[0], TEXT, "list").attributeNS(TEXT, "style-name") ==
         "Roman");
  assert(label(book, "Join three") == "2." &&
         label(book, "Continued join") == "3.");
  restore();
  // A destination override must not change an inherited/default source
  // sub-list.
  for (int variant = 0; variant < 3; ++variant) {
    auto inherited = fixture();
    auto list_node = list(inherited, "join");
    auto group = children(list_node, TEXT, "list-item");
    group[0].setAttributeNS(TEXT, "text:style-override", "Roman");
    group[1].removeAttributeNS(TEXT, "style-override");
    if (variant) {
      list_node.removeAttributeNS(TEXT, "style-name");
      if (variant == 2) {
        auto auto_styles =
            child(inherited.xml.documentElement(), OFFICE, "automatic-styles");
        auto paragraph_style =
            inherited.xml.createElementNS(STYLE, "style:style");
        paragraph_style.setAttributeNS(STYLE, "style:name", "InheritedNumbers");
        paragraph_style.setAttributeNS(STYLE, "style:family", "paragraph");
        paragraph_style.setAttributeNS(STYLE, "style:list-style-name",
                                       "Numbers");
        auto_styles.appendChild(paragraph_style);
        // The outer list stays unnamed; only its trailing sub-list derives a
        // named default from a paragraph inside that sub-list.
        auto nested = child(group[1], TEXT, "list");
        auto nested_item = child(nested, TEXT, "list-item");
        child(nested_item, TEXT, "p")
            .setAttributeNS(TEXT, "text:style-name", "InheritedNumbers");
      }
    }
    assert(read_source_list_style(inherited, group[0]) == "Roman");
    assert(read_source_list_style(inherited, group[1]) ==
           (variant ? "" : "Numbers"));
    inherited.save(dir.path() + "/inherited.odt");
    TextDocument test;
    test.load(dir.path() + "/inherited.odt");
    auto original_style = style(test, "Join child");
    assert(original_style == (variant == 1 ? "" : "Numbers"));
    test.replace(at(test, "Join two") - 1, 1, "");
    assert(style(test, "Join child") ==
           (variant == 1 ? "WordsListDefault1" : "Numbers"));
    if (variant == 1)
      assert(label(test, "Join child") == "1.");
    test.save(saved);
    inspected.load(saved);
    unique_ids(inspected);
    auto changed = inspected.xml.toByteArray();
    assert(test.undo());
    test.save(saved);
    inspected.load(saved);
    assert(inspected.xml.toByteArray() == inherited.xml.toByteArray());
    assert(test.redo());
    test.save(saved);
    inspected.load(saved);
    assert(inspected.xml.toByteArray() == changed);
    TextDocument reopened;
    reopened.load(saved);
    assert(style(reopened, "Join child") == style(test, "Join child"));
  }
  // The same item operation works inside a table cell without crossing cells.
  join = at(book, "Cell second") - 1;
  book.replace(join, 1, "");
  snapshot();
  assert(children(list(inspected, "cell"), TEXT, "list-item").size() == 1);
  assert(label(book, "Cell firstCell second") == "1.");
  restore();
  // A selected range can remove several adjacent simple items in one command.
  auto first = at(book, "Beta") + 2, end = at(book, "Gamma") + 3;
  book.replace(first, end - first, "X");
  assert(book.text() ==
         original_text.left(first) + "X" + original_text.mid(end));
  assert(label(book, "BeXma") == "VI.");
  assert(label(book, "Continued main") == "7.");
  restore();
  for (auto text : {"Child one", "Other cell", "Between"}) {
    auto before = snapshot();
    auto undo = book.can_undo(), redo = book.can_redo();
    rejects([&] { book.replace(at(book, text) - 1, 1, ""); });
    assert(book.text() == original_text && !book.modified());
    assert(book.can_undo() == undo && book.can_redo() == redo);
    assert(snapshot() == before);
  }
  // Invisible structures cannot be silently consumed by a flat-text join.
  for (int variant = 0; variant < 3; ++variant) {
    auto guarded = fixture();
    auto list_node = list(guarded, "join");
    auto first_item = children(list_node, TEXT, "list-item")[0];
    auto node =
        guarded.xml.createElementNS(TEXT, variant == 0   ? "text:list-item"
                                          : variant == 1 ? "text:list"
                                                         : "text:list-header");
    if (variant == 1)
      first_item.appendChild(node);
    else
      list_node.insertAfter(node, first_item);
    guarded.save(dir.path() + "/guarded.odt");
    TextDocument test;
    test.load(dir.path() + "/guarded.odt");
    rejects([&] { test.replace(at(test, "Join two") - 1, 1, ""); });
    assert(!test.can_undo() && !test.modified() &&
           test.text() == original_text);
    test.save(saved);
    inspected.load(saved);
    assert(inspected.xml.toByteArray() == guarded.xml.toByteArray());
  }
  OdfPackage bounded(Kind::Words);
  auto bounded_list = bounded.xml.createElementNS(TEXT, "text:list");
  auto bounded_item = bounded.xml.createElementNS(TEXT, "text:list-item");
  bounded_item.setAttributeNS(TEXT, "text:start-value", "9223372036854775806");
  auto bounded_p = bounded.xml.createElementNS(TEXT, "text:p");
  set_plain_text(bounded_p, "Limit");
  bounded_item.appendChild(bounded_p);
  bounded_list.appendChild(bounded_item);
  bounded.body().appendChild(bounded_list);
  bounded.save(dir.path() + "/bounded.odt");
  TextDocument limited;
  limited.load(dir.path() + "/bounded.odt");
  assert(!limited.flow().paragraphs.empty());
  rejects([&] { limited.replace(2, 0, "\n\n"); });
  assert(limited.text() == "Limit" && !limited.can_undo() &&
         !limited.modified());
  std::cout
      << "PASS Words list splits/joins, headers, overrides, restarts, nested "
         "suffixes, table cells, continuation counters and XML undo\n";
}
