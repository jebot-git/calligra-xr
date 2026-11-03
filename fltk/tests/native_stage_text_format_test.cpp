// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
int shape(const Presentation &p, const Text &name) {
  for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
    if (p.scene(0).shapes[i].name == name)
      return int(i);
  std::abort();
}
int box(const Presentation &p, const Text &name) {
  return p.scene(0).shapes.at(shape(p, name)).editor_index;
}
Bytes content(const Presentation &p) {
  return p.scene(0)
      .shapes.at(shape(p, "Rich"))
      .source.ownerDocument()
      .toByteArray();
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-8); }
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto group = child(styles.documentElement(), OFFICE, "styles");
  auto defs = read_document_xml(
      R"XML(<root xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><style:style style:name="Graphic" style:family="graphic"><style:text-properties fo:font-size="20pt" fo:color="#223344"/></style:style><style:style style:name="FrameP" style:family="paragraph"><style:paragraph-properties fo:margin-left="3pt" fo:line-height="140%"/></style:style><style:style style:name="Strong" style:family="text"><style:text-properties fo:font-weight="700"/></style:style></root>)XML");
  for (auto n : children(defs.documentElement(), STYLE))
    group.appendChild(styles.importNode(n, true));
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:automatic-styles/><office:body><office:presentation><draw:page draw:name="Formatting" draw:master-page-name="Default"><draw:g draw:transform="translate(10pt 20pt)"><draw:frame draw:name="Rich" draw:style-name="Graphic" draw:text-style-name="FrameP" svg:x="20pt" svg:y="30pt" svg:width="250pt" svg:height="100pt"><draw:text-box><text:p xml:id="paragraph">A<text:span text:style-name="Strong" xml:id="strong">Český</text:span><text:a xlink:type="simple" xlink:href="#paragraph"> Ωlink</text:a> Z</text:p></draw:text-box></draw:frame></draw:g><draw:frame draw:name="Other" draw:style-name="Graphic" svg:width="150pt" svg:height="60pt"><draw:text-box><text:p><text:span text:style-name="WordsText1">untouched</text:span></text:p></draw:text-box></draw:frame><draw:frame draw:name="Empty" draw:style-name="Graphic" svg:width="100pt" svg:height="50pt"><draw:text-box/></draw:frame><draw:frame draw:name="EmptySpan" draw:style-name="Graphic" svg:width="100pt" svg:height="50pt"><draw:text-box><text:p><text:span text:style-name="Strong"/></text:p></draw:text-box></draw:frame><draw:frame draw:name="Image" svg:width="100pt" svg:height="50pt"><draw:image xlink:type="simple" xlink:href="Pictures/original.svg"/></draw:frame><draw:frame draw:name="Table" draw:style-name="Graphic" svg:width="200pt" svg:height="100pt"><draw:text-box><table:table table:name="Grid"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell><text:p>Tbl</text:p></table:table-cell><table:table-cell/></table:table-row></table:table></draw:text-box></draw:frame><draw:frame draw:name="Field" draw:style-name="Graphic" svg:width="100pt" svg:height="50pt"><draw:text-box><text:p><text:page-number>12</text:page-number></text:p></draw:text-box></draw:frame></draw:page><draw:page draw:name="Second" draw:master-page-name="Default"><draw:rect svg:width="60pt" svg:height="40pt"><text:p>Second page stays</text:p></draw:rect></draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry(
      "Pictures/original.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>)",
      "image/svg+xml");
  p.set_entry("opaque/original", Bytes("keep\0bytes", 10),
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && Text::fromUtf8(argv[1]) == "--document") {
    Presentation p;
    p.load(Text::fromUtf8(argv[2]));
    const auto rich = box(p, "Rich");
    const auto original = p.slide(0).boxes.at(rich).text;
    assert(original == "AČeský Ωlink Z");
    WordsCharacterFormat c;
    c.size = 24;
    c.color = "#ff0000";
    c.highlight = "#ffee00";
    c.underline = true;
    assert(p.format_box_characters(0, rich, {1, 6}, c));
    WordsParagraphFormat f;
    f.alignment = "center";
    f.left = 7;
    f.right = 9;
    f.before = 4;
    f.after = 5;
    f.line_factor = 1.8;
    assert(p.format_box_paragraphs(0, rich, {1, 6}, f));
    c = {};
    c.size = 14;
    c.weight = 700;
    c.italic = true;
    p.format_box_characters(0, rich, {}, c);
    p.set_box_text(0, rich, "x" + original, true);
    p.set_box_text(0, rich, "xy" + original, true);
    assert(p.undo() && p.scene(0).shapes.at(shape(p, "Rich")).text == original);
    auto redo = p.redo();
    assert(redo && redo->selection && redo->selection->cursor == 2);
    p.save(Text::fromUtf8(argv[3]));
    Presentation reopened;
    reopened.load(Text::fromUtf8(argv[3]));
    near(*reopened.box_format_info(0, box(reopened, "Rich"), {3, 8})
              .character.size,
         24);
    assert(reopened.box_format_info(0, box(reopened, "Rich"), {0, 2})
               .character.italic == true);
    std::cout << "PASS independent Stage scoped formatting, transient typing, "
                 "selection history and reopen\n";
    return 0;
  }

  TempDir directory;
  auto input = directory.path() + "/input.odp",
       output = directory.path() + "/output.odp";
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  int rich = box(p, "Rich");
  auto baseline = content(p);
  auto original_text = p.slide(0).boxes.at(rich).text;
  auto inherited = p.box_format_info(0, rich, {0, 1});
  near(*inherited.character.size, 20);
  near(*inherited.paragraph.left, 3);
  near(*inherited.paragraph.line_factor, 1.4);
  auto strong = p.box_format_info(0, rich, {1, 6});
  assert(strong.bold && *strong.bold);
  near(*strong.character.size, 20);
  auto empty = p.box_format_info(0, box(p, "Empty"), {});
  near(*empty.character.size, 20);
  auto empty_span = p.box_format_info(0, box(p, "EmptySpan"), {});
  assert(empty_span.bold && *empty_span.bold);
  assert(content(p) == baseline && !p.modified());
  auto table = p.box_format_info(0, box(p, "Table"), {0, 3});
  near(*table.character.size, 20);
  auto other = p.box_format_info(0, box(p, "Other"), {0, 9});
  auto original_shape = p.scene(0).shapes.at(shape(p, "Rich"));
  WordsCharacterFormat character;
  character.size = 24;
  character.color = "#ff0000";
  character.highlight = "#ffee00";
  character.underline = true;
  assert(p.format_box_characters(0, rich, {1, 6}, character));
  auto after_character = content(p);
  auto formatted = p.box_format_info(0, rich, {1, 6});
  near(*formatted.character.size, 24);
  assert(formatted.character.color == Text("#ff0000") &&
         formatted.character.underline == true && formatted.bold == true);
  assert(!p.box_format_info(0, rich, {0, original_text.size()}).character.size);
  assert(p.box_format_info(0, box(p, "Other"), {0, 9}).character ==
         other.character);
  auto &changed = p.scene(0).shapes.at(shape(p, "Rich"));
  near(changed.transform.e, original_shape.transform.e);
  assert(changed.text == original_text);
  WordsParagraphFormat paragraph;
  paragraph.alignment = "center";
  paragraph.left = 7;
  paragraph.right = 9;
  paragraph.before = 4;
  paragraph.after = 5;
  paragraph.line_factor = 1.8;
  assert(p.format_box_paragraphs(0, rich, {0, 0}, paragraph));
  auto info = p.box_format_info(0, rich, {0, 0});
  assert(info.paragraph.alignment == Text("center"));
  near(*info.paragraph.left, 7);
  near(*info.paragraph.line_factor, 1.8);
  auto at = p.undo();
  assert(at && at->selection && at->selection->cursor == 0 &&
         at->shape == shape(p, "Rich") && content(p) == after_character);
  at = p.undo();
  assert(at && at->selection && at->selection->anchor == 1 &&
         at->selection->cursor == 6 && content(p) == baseline && !p.modified());
  assert(p.redo() && p.redo());
  p.save(output);
  Presentation reopened;
  reopened.load(output);
  near(*reopened.box_format_info(0, box(reopened, "Rich"), {1, 6})
            .character.size,
       24);
  auto old = read_document_archive(input),
       saved = read_document_archive(output);
  for (auto it = old.cbegin(); it != old.cend(); ++it)
    if (it.key() != "content.xml")
      assert(saved.value(it.key()) == it.value());
  // Future typing is transient for nonempty text; its inserted source spans
  // undo together.
  p.load(input);
  rich = box(p, "Rich");
  WordsCharacterFormat typing;
  typing.size = 14;
  typing.weight = 700;
  assert(!p.format_box_characters(0, rich, {}, typing) && !p.modified() &&
         !p.can_undo() && p.has_typing_format());
  near(*p.box_format_info(0, rich, {}).character.size, 14);
  p.set_box_text(0, rich, "x" + original_text, true);
  p.set_box_text(0, rich, "xy" + original_text, true);
  near(*p.box_format_info(0, rich, {0, 2}).character.size, 14);
  near(*p.box_format_info(0, rich, {2, 3}).character.size, 20);
  at = p.undo();
  assert(at && at->selection && at->selection->cursor == 0 &&
         content(p) == baseline && !p.modified() && !p.has_typing_format());
  at = p.redo();
  assert(at && at->selection && at->selection->cursor == 2 &&
         p.scene(0).shapes.at(shape(p, "Rich")).text == "xy" + original_text);
  // Repeated letters are ambiguous in a whole-buffer diff. The real editor
  // supplies exact replacement offsets, retaining the intended run/cursor.
  p.load(input);
  rich = box(p, "Rich");
  p.set_box_text(0, rich, "aaa");
  p.save(output);
  auto repeated = content(p);
  typing = {};
  typing.size = 14;
  p.format_box_characters(0, rich, {}, typing);
  p.replace_box_text(0, rich, 0, 0, "a", true);
  near(*p.box_format_info(0, rich, {0, 1}).character.size, 14);
  near(*p.box_format_info(0, rich, {1, 4}).character.size, 20);
  at = p.undo();
  assert(at && at->selection && at->selection->cursor == 0 &&
         content(p) == repeated && !p.modified());
  at = p.redo();
  assert(at && at->selection && at->selection->cursor == 1);
  auto exact = content(p);
  rejects([&] { p.replace_box_text(0, rich, 5, 0, "a"); });
  assert(content(p) == exact);
  // Empty frame defaults persist, while future typing remains scoped to that
  // frame.
  p.load(input);
  int empty_box = box(p, "Empty");
  typing = {};
  typing.size = 16;
  typing.italic = true;
  assert(p.format_box_characters(0, empty_box, {}, typing) && p.modified());
  p.set_box_text(0, empty_box, "β", true);
  auto typed = p.box_format_info(0, empty_box, {0, 1});
  near(*typed.character.size, 16);
  assert(typed.character.italic == true);
  assert(p.undo() && p.scene(0).shapes.at(shape(p, "Empty")).text.empty());
  assert(p.undo() && content(p) == baseline && !p.modified());
  // Range errors and partial atomic fields leave source and redo unchanged.
  p.load(input);
  rich = box(p, "Rich");
  p.format_box_characters(0, rich, {1, 6}, character);
  assert(p.undo());
  auto revision = p.revision();
  WordsParagraphFormat page_only;
  page_only.break_before = true;
  rejects([&] { p.format_box_paragraphs(0, rich, {}, page_only); });
  rejects([&] { p.format_box_characters(0, rich, {0, 999}, character); });
  rejects([&] { p.format_box_characters(0, box(p, "Image"), {}, character); });
  rejects(
      [&] { p.format_box_characters(0, box(p, "Field"), {0, 1}, character); });
  assert(content(p) == baseline && p.revision() == revision && p.can_redo());
  int field = box(p, "Field");
  assert(p.format_box_characters(0, field, {0, 2}, character));
  near(*p.box_format_info(0, field, {0, 2}).character.size, 24);
  assert(p.scene(0)
             .shapes.at(shape(p, "Field"))
             .source.elementsByTagNameNS(TEXT, "page-number")
             .size() == 1);
  assert(p.undo() && content(p) == baseline);
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  // Inspection probes run on private copies even for signed source packages.
  auto readonly_source = content(p);
  assert(p.box_format_info(0, box(p, "EmptySpan"), {}).bold == true);
  near(*p.box_format_info(0, box(p, "Empty"), {}).character.size, 20);
  assert(content(p) == readonly_source && !p.modified());
  rejects(
      [&] { p.format_box_characters(0, box(p, "Rich"), {0, 1}, character); });
  rejects([&] { p.replace_box_text(0, box(p, "Rich"), 0, 0, "x"); });
  assert(!p.modified());
  std::cout
      << "PASS scoped Stage font/paragraph formatting, inherited runs, empty "
         "spans/tables, future typing, source/archive and selection history\n";
}
