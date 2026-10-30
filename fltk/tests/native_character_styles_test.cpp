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
  p.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles><style:style style:name="Outer" style:family="text"><style:text-properties fo:font-size="20pt" fo:font-weight="700" fo:background-color="#ffee00"/></style:style><style:style style:name="Accent" style:family="text"><style:text-properties fo:font-size="3pt"/></style:style><style:style style:name="Cell" style:family="table-cell"><style:text-properties fo:font-size="22pt"/></style:style></office:automatic-styles><office:body><office:text><text:p text:style-name="Body" xml:id="first">Start <text:meta xml:id="outer"><text:span text:style-name="Outer">before <text:a xlink:href="https://example.invalid/" xlink:type="simple" office:name="keep-link">Český <text:meta xml:id="meta">日本語</text:meta> after</text:a> end</text:span></text:meta> tail<text:s text:c="3"/><text:tab/>last<text:line-break/>soft</text:p><text:p text:style-name="Body">Second paragraph</text:p><table:table table:name="Cells"><table:table-column/><table:table-row><table:table-cell table:style-name="Cell" office:value-type="string"><text:p>Cell text</text:p></table:table-cell></table:table-row></table:table><text:p>Before <text:date>2026</text:date> after</text:p></office:text></office:body></office:document-content>)XML");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:styles><style:style style:name="Body" style:family="paragraph"><style:text-properties fo:font-size="14pt" fo:font-weight="400" fo:font-family="Noto Sans" fo:color="#103050"/></style:style><style:style style:name="Accent" style:display-name="Accent 日本語" style:family="text"><style:text-properties fo:font-size="125%" fo:font-style="italic" fo:color="#165aa7"/></style:style><style:style style:name="Accent" style:family="paragraph"><style:text-properties fo:font-size="44pt"/></style:style><style:style style:name="Unused" style:family="text"/></office:styles></office:document-styles>)XML",
      "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
struct Snapshot {
  Bytes content, styles, manifest;
  bool operator==(const Snapshot &) const = default;
};
Snapshot state(const TextDocument &d) {
  auto p = d.style_snapshot();
  return {p.xml.toByteArray(), p.entry("styles.xml"),
          p.entry("META-INF/manifest.xml")};
}
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  std::filesystem::path dir = temp.path().toStdString();
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    dir = argv[2];
    if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir))
      throw std::runtime_error("Choose an empty artifact directory");
    std::filesystem::create_directories(dir);
  }
  auto p = fixture();
  auto path = Text((dir / "original.odt").string());
  p.save(path);
  TextDocument d;
  d.load(path);
  auto original = state(d);
  auto text = d.text();
  auto save = [&](const char *name) {
    auto target = Text((dir / (std::string(name) + ".odt")).string());
    d.save(target);
    OdfPackage reopened(Kind::Words);
    reopened.load(target);
    assert(reopened.entry("Extras/opaque.bin") == p.entry("Extras/opaque.bin"));
    return reopened;
  };
  auto listed = words_character_styles(p);
  assert(listed.size() == 2 && listed[0].name == "Accent");
  assert(words_character_style_format(p, "Accent").character.size ==
         15); // default paragraph:12 *125%
  auto first = text.find(Text("Český")),
       last = first + Text("Český 日本語").size();
  TextSelection range{first, last};
  auto before = read_text_flow(p);
  auto working = d.style_snapshot();
  WordsCharacterStyleChange create;
  create.label = "Notice";
  create.parent = "Accent";
  auto name = define_words_character_style(working, create);
  assert(name.startsWith("WordsCharacterNamed"));
  assert(d.commit_styles(working, range, d.revision(), {}, name));
  assert(d.modified());
  auto info = d.format_info(range).character;
  assert(info.size == 17.5 && info.weight == 400 && info.italic == true &&
         info.color == "#165aa7" && info.highlight == "transparent");
  auto applied = state(d);
  auto saved = save("created-applied");
  assert(words_current_character_style(saved, first, last) == name);
  assert(words_current_character_style(saved, 0, last) == std::nullopt);
  // No original container/ID/link is split or duplicated.
  for (auto id : {"first", "outer", "meta"}) {
    size_t count = 0;
    std::function<void(XmlNode)> walk = [&](XmlNode n) {
      count += n.attributeNS(XML, "id") == id;
      for (auto child = n.firstChild(); !child.isNull();
           child = child.nextSibling())
        walk(child);
    };
    walk(saved.xml.documentElement());
    assert(count == 1);
  }
  auto metas = saved.xml.documentElement().elementsByTagNameNS(TEXT, "meta");
  assert(metas.size() == 2);
  assert(std::count_if(metas.begin(), metas.end(), [](const auto &node) {
           return node.attributeNS(XML, "id") == "meta" &&
                  plain_text(node) == "日本語";
         }) == 1);
  auto links = saved.xml.documentElement().elementsByTagNameNS(TEXT, "a");
  assert(links.size() == 1 &&
         links[0].attributeNS(OFFICE, "name") == "keep-link");
  assert(links[0].attributeNS("http://www.w3.org/1999/xlink", "href") ==
         "https://example.invalid/");
  // Every unselected scalar keeps its complete resolved appearance.
  auto after = read_text_flow(saved);
  auto span_at = [](const TextParagraph &paragraph,
                    size_t i) -> const TextSpan & {
    for (const auto &s : paragraph.spans)
      if (i >= s.begin && i < s.end)
        return s;
    throw std::runtime_error("missing span");
  };
  for (size_t i = 0; i < before.paragraphs[0].text.size(); ++i)
    if (i < first || i >= last) {
      auto &a = span_at(before.paragraphs[0], i);
      auto &b = span_at(after.paragraphs[0], i);
      assert(a.font == b.font && a.color == b.color &&
             a.background == b.background && a.decorations == b.decorations);
    }
  assert(!d.commit_styles(d.style_snapshot(), range, d.revision(), {}, name) &&
         state(d) == applied);
  assert(d.undo() == range && state(d) == original);
  save("assignment-undone");
  assert(d.redo() == range && state(d) == applied);
  save("assignment-redone");
  auto repeated = d.style_snapshot();
  for (size_t i = 0; i < 64; ++i)
    apply_words_character_style(repeated, first, last,
                                i % 2 ? name : Text("Accent"));
  size_t max_depth = 0;
  std::function<void(XmlNode, size_t)> nesting = [&](XmlNode node,
                                                     size_t depth) {
    max_depth = std::max(max_depth, depth);
    for (auto child = node.firstChild(); !child.isNull();
         child = child.nextSibling())
      nesting(child, depth + 1);
  };
  nesting(repeated.body(), 0);
  assert(max_depth < 20);
  repeated.save(Text((dir / "repeated-assignment.odt").string()));
  // A range ending inside metadata retains its ID/value and the appearance
  // of the excluded trailing scalar, without splitting the metadata wrapper.
  assert(d.commit_styles(d.style_snapshot(), {first + 2, last - 1},
                         d.revision(), {}, Text{}));
  auto partial = save("metadata-partial-default");
  auto partial_flow = read_text_flow(partial);
  assert(span_at(partial_flow.paragraphs[0], last - 1).font ==
         span_at(after.paragraphs[0], last - 1).font);
  auto partial_metas =
      partial.xml.documentElement().elementsByTagNameNS(TEXT, "meta");
  assert(partial_metas.size() == 2);
  assert(std::count_if(partial_metas.begin(), partial_metas.end(),
                       [](const auto &node) {
                         return node.attributeNS(XML, "id") == "meta" &&
                                plain_text(node) == "日本語";
                       }) == 1);
  assert(d.undo() && state(d) == applied);
  // Renaming and editing a used definition changes no content XML.
  working = d.style_snapshot();
  WordsCharacterStyleChange edit;
  edit.name = name;
  edit.label = "Notice renamed";
  edit.character.size = 24;
  assert(define_words_character_style(working, edit) == name);
  assert(d.commit_styles(working, range, d.revision()));
  auto edited = state(d);
  assert(edited.content == applied.content && edited.styles != applied.styles &&
         d.format_info(range).character.size == 24);
  save("definition-edited");
  assert(d.undo() && state(d) == applied);
  assert(d.redo() && state(d) == edited);
  working = d.style_snapshot();
  rejects([&] { delete_words_character_style(working, name); });
  working = d.style_snapshot();
  assert(delete_words_character_style(working, "Unused"));
  assert(d.commit_styles(working, range, d.revision()));
  save("unused-deleted");
  assert(d.undo() && state(d) == edited);
  working = d.style_snapshot();
  edit = {};
  edit.name = "Accent";
  edit.parent = name;
  rejects([&] { define_words_character_style(working, edit); });
  assert(state(d) == edited);
  // Character 'default' removes all selected inline overrides but keeps the
  // paragraph/cell defaults and the outside styles.
  assert(d.commit_styles(d.style_snapshot(), range, d.revision(), {}, Text{}));
  info = d.format_info(range).character;
  assert(info.size == 14 && info.weight == 400 && info.italic == false &&
         info.color == "#103050" && info.highlight == "transparent");
  save("default-applied");
  assert(d.undo() && state(d) == edited);
  // A colliding automatic/common name gets a reference to the common style.
  assert(d.commit_styles(d.style_snapshot(), range, d.revision(), {},
                         Text("Accent")));
  assert(d.format_info(range).character.size == 17.5);
  auto common = save("common-collision");
  assert(words_current_character_style(common, first, last) == "Accent");
  assert(d.undo() && state(d) == edited);
  // Explicit spaces, tabs, soft breaks and paragraph boundaries retain
  // source text and structural order under a multi-paragraph assignment.
  auto controls = text.find(Text("tail")) + 4;
  auto second = text.find(Text("Second")) + 4;
  assert(d.commit_styles(d.style_snapshot(), {controls, second}, d.revision(),
                         {}, name));
  assert(d.text() == text);
  assert(d.format_info({controls, controls + 3}).character.size == 24);
  auto control_package = save("controls-and-paragraphs");
  assert(read_text_flow(control_package).paragraphs.size() ==
         before.paragraphs.size());
  assert(d.undo() && state(d) == edited);
  // Import states whose full style cascade is not implemented reject before
  // changing source or history, including a failure after earlier text.
  for (Text attribute : {"visited-style-name", "class-names"}) {
    auto unsupported = d.style_snapshot();
    auto nodes = unsupported.xml.documentElement().elementsByTagNameNS(
        TEXT, attribute == Text("visited-style-name") ? "a" : "span");
    assert(!nodes.empty());
    auto target = nodes.begin();
    if (attribute == "class-names")
      target = std::find_if(nodes.begin(), nodes.end(), [&](const auto &node) {
        return node.attributeNS(TEXT, "style-name") == name &&
               plain_text(node).contains("Český");
      });
    assert(target != nodes.end());
    target->setAttributeNS(TEXT, Text("text:") + attribute, "Accent");
    rejects(
        [&] { d.commit_styles(unsupported, range, d.revision(), {}, name); });
    assert(state(d) == edited);
  }
  // Caret assignment is transient; it keeps the style reference for typing,
  // rather than baking the current definition into direct font attributes.
  TextSelection caret{first, first};
  auto rev = d.revision();
  assert(!d.commit_styles(d.style_snapshot(), caret, rev, {}, name));
  assert(d.revision() == rev && state(d) == edited && d.has_typing_format());
  assert(d.format_info(caret).character.size == 24);
  rejects([&] { d.replace(first, 0, "x"); });
  assert(state(d) == edited && d.has_typing_format());
  const auto typing_at = text.find(Text("Second")) + 3;
  caret = {typing_at, typing_at};
  assert(!d.commit_styles(d.style_snapshot(), caret, d.revision(), {}, name));
  d.begin_edit(caret);
  d.replace(typing_at, 0, "世界");
  assert(d.format_info({typing_at, typing_at + 2}).character.size == 24);
  d.cancel_edit();
  assert(d.end_edit() == caret && state(d) == edited && d.has_typing_format());
  d.replace(typing_at, 0, "世界");
  assert(d.text() == text.left(typing_at) + "世界" + text.mid(typing_at));
  assert(d.format_info({typing_at, typing_at + 2}).character.size == 24);
  save("named-typed");
  assert(d.undo() && state(d) == edited && !d.has_typing_format());
  // Querying a table caret retains cell text defaults; the name's125% uses22pt.
  auto cell = text.find(Text("Cell text"));
  assert(words_character_style_format(d.style_snapshot(), "Accent", cell)
             .character.size == 27.5);
  // Specialized inline objects fail without consuming history or changing
  // earlier supported text in the same candidate transaction.
  auto old = state(d);
  auto oldrev = d.revision();
  auto field = text.find(Text("2026"));
  rejects([&] {
    d.commit_styles(d.style_snapshot(), {first, field + 4}, d.revision(), {},
                    name);
  });
  assert(state(d) == old && d.revision() == oldrev);
  rejects([&] {
    d.commit_styles(d.style_snapshot(), range, oldrev - 1, {}, name);
  });
  assert(state(d) == old);
  // Empty body: named caret formatting remains transient, then typing creates
  // the first source paragraph and a real named style reference.
  OdfPackage empty(Kind::Words);
  auto ep = Text((dir / "empty-original.odt").string());
  empty.save(ep);
  TextDocument e;
  e.load(ep);
  auto empty_before = state(e);
  working = e.style_snapshot();
  create = {};
  create.label = "Empty typing";
  create.character.size = 19;
  auto ename = define_words_character_style(working, create);
  assert(e.commit_styles(working, {0, 0}, e.revision(), {}, ename));
  auto definitions = state(e);
  assert(definitions.content == empty_before.content && e.has_typing_format());
  e.replace(0, 0, "x");
  assert(e.format_info({0, 1}).character.size == 19);
  auto out = Text((dir / "empty-typed.odt").string());
  e.save(out);
  assert(e.undo() && state(e) == definitions);
  assert(e.undo() && state(e) == empty_before);
  assert(e.redo() && state(e) == definitions);
  assert(
      !e.commit_styles(e.style_snapshot(), {0, 0}, e.revision(), {}, ename) &&
      e.has_typing_format());
  working = e.style_snapshot();
  assert(delete_words_character_style(working, ename));
  assert(e.commit_styles(working, {0, 0}, e.revision()) &&
         !e.has_typing_format());
  e.save(Text((dir / "empty-unused-deleted.odt").string()));
  assert(e.undo() && state(e) == definitions);
  std::cout << "PASS named character styles, absolute range assignment, source "
               "identity, caret typing and ZIP history\n";
}
