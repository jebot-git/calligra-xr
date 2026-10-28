// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_list_number.h"
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
           FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-6); }
TextMetrics metric(const Text &t, const TextFont &f) {
  return {double(t.size()) * f.size / 2, f.size * .8, f.size * .2};
}
XmlElement element(XmlElement parent, const Text &ns, const Text &name) {
  auto e = parent.ownerDocument().createElementNS(ns, name);
  parent.appendChild(e);
  return e;
}
void attr(XmlElement node, const Text &ns, const Text &name,
          const Text &value) {
  node.setAttributeNS(ns, name, value);
}
OdfPackage fixture(Kind kind = Kind::Words) {
  OdfPackage package(kind);
  package.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles>
 <style:default-style style:family="paragraph"><style:text-properties fo:font-size="12pt" fo:font-family="Noto Sans"/><style:paragraph-properties fo:line-height="120%"/></style:default-style>
 <style:style style:name="Label" style:family="text"><style:text-properties fo:font-size="10pt" fo:font-weight="bold" fo:color="#bb2244"/></style:style>
 <style:style style:name="Inherited" style:family="paragraph" style:list-style-name="Numbers"/>
 <text:list-style style:name="Numbers"><text:list-level-style-number text:level="1" style:num-format="1" style:num-prefix="(" style:num-suffix=")" text:style-name="Label"><style:list-level-properties text:list-level-position-and-space-mode="label-alignment" fo:text-align="end"><style:list-level-label-alignment fo:margin-left="30pt" fo:text-indent="-15pt" text:label-followed-by="listtab" text:list-tab-stop-position="30pt"/></style:list-level-properties></text:list-level-style-number>
 <text:list-level-style-number text:level="2" style:num-format="a" style:num-suffix=")" text:display-levels="2"><style:list-level-properties text:space-before="30pt" text:min-label-width="20pt" text:min-label-distance="4pt"/></text:list-level-style-number></text:list-style>
 <text:list-style style:name="Bullets"><text:list-level-style-bullet text:level="1" text:bullet-char="•" text:bullet-relative-size="150%" text:style-name="Label"><style:list-level-properties text:space-before="8pt" text:min-label-width="22pt" text:min-label-distance="5pt"/></text:list-level-style-bullet></text:list-style>
 <text:list-style style:name="Alternate"><text:list-level-style-number text:level="1" style:num-format="I" style:num-suffix="."><style:list-level-properties text:space-before="0pt" text:min-label-width="30pt"/></text:list-level-style-number></text:list-style>
 <text:list-style style:name="Serial" text:consecutive-numbering="true"><text:list-level-style-number text:level="1" style:num-format="1" style:num-suffix="."/><text:list-level-style-number text:level="2" style:num-format="1" style:num-suffix="."/></text:list-style>
 </office:styles><office:automatic-styles><style:page-layout style:name="Paper"><style:page-layout-properties fo:page-width="400pt" fo:page-height="300pt" fo:margin="20pt"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Paper"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  auto body = read_document_xml(
      R"XML(<root xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
 <text:p>Native list layout — Český</text:p>
 <text:list text:style-name="Bullets"><text:list-item><text:p>Unicode bullet text</text:p></text:list-item></text:list>
 <text:list text:style-name="Numbers" xml:id="first"><text:list-header><text:p>List header</text:p></text:list-header><text:list-item>
 <text:p>First numbered paragraph with enough words to wrap onto another line while its generated label stays on the first line.</text:p><text:p>Continuation paragraph</text:p>
 <text:list><text:list-item><text:p>Nested alpha</text:p></text:list-item><text:list-item text:start-value="3"><text:p>Nested restart</text:p></text:list-item></text:list></text:list-item>
 <text:list-item text:style-override="Alternate"><text:p>Alternate style</text:p></text:list-item><text:list-item text:start-value="7"><text:p>Restarted seven</text:p></text:list-item></text:list>
 <text:p>Between lists</text:p><text:list text:style-name="Numbers" text:continue-list="first"><text:list-item><text:p>Continued eight</text:p></text:list-item></text:list>
 <text:list text:style-name="Numbers" text:continue-numbering="true"><text:list-item><text:p>Continued nine</text:p></text:list-item></text:list>
 <text:list><text:list-item><text:p text:style-name="Inherited">Paragraph style default</text:p></text:list-item></text:list>
 <text:numbered-paragraph text:list-id="np" text:style-name="Numbers"><text:p>Numbered first</text:p></text:numbered-paragraph>
 <text:numbered-paragraph text:list-id="np" text:style-name="Numbers" text:level="2"><text:p>Numbered child</text:p></text:numbered-paragraph>
 <text:numbered-paragraph text:list-id="np" text:style-name="Numbers"><text:p>Numbered next</text:p></text:numbered-paragraph>
 <text:list text:style-name="Serial"><text:list-item><text:p>Serial one</text:p><text:list><text:list-item><text:p>Serial two</text:p></text:list-item></text:list></text:list-item><text:list-item><text:p>Serial three</text:p></text:list-item></text:list>
 </root>)XML");
  XmlElement target = package.body();
  if (kind == Kind::Stage) {
    auto page = element(target, DRAW, "draw:page");
    attr(page, DRAW, "draw:name", "Native list layout");
    attr(page, DRAW, "draw:master-page-name", "Default");
    auto frame = element(page, DRAW, "draw:frame");
    for (auto [name, value] :
         std::vector<std::pair<Text, Text>>{{"x", "10pt"},
                                            {"y", "10pt"},
                                            {"width", "380pt"},
                                            {"height", "280pt"}})
      attr(frame, SVG, "svg:" + name, value);
    target = element(frame, DRAW, "draw:text-box");
  }
  int blocks = 0;
  for (auto node = body.documentElement().firstChild(); !node.isNull();
       node = node.nextSibling()) {
    if (node.isElement() && kind == Kind::Stage && blocks++ >= 3)
      break;
    target.appendChild(package.xml.importNode(node, true));
  }
  package.set_entry("extra/retained.bin", "retained original payload",
                    "application/octet-stream");
  return package;
}
const TextParagraph &find(const TextFlow &flow, const Text &text) {
  for (const auto &p : flow.paragraphs)
    if (p.text.startsWith(text))
      return p;
  throw std::runtime_error("Paragraph not found");
}
Text label(const TextFlow &flow, const Text &text) {
  const auto &p = find(flow, text);
  return p.list ? p.list->text : Text();
}
TextLine first_line(const TextFlow &flow, const Text &text) {
  const auto &p = find(flow, text);
  auto lines = layout_text_box(TextFlow{{p}}, 200, metric).lines;
  assert(!lines.empty());
  return lines[0];
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  TempDir directory;
  auto path = directory.path() + "/list.odt",
       saved = directory.path() + "/saved.odt";
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--corpus") {
    size_t documents = 0, paragraphs = 0, labels = 0;
    for (int i = 2; i < argc; ++i) {
      TextDocument document;
      document.load(Text::fromUtf8(argv[i]));
      auto flow = document.flow();
      auto pages = paginate_text(flow, metric);
      assert(!pages.pages.empty());
      ++documents;
      size_t local = 0;
      for (const auto &p : flow.paragraphs) {
        ++paragraphs;
        if (p.list && !p.list->text.empty()) {
          ++labels;
          ++local;
        }
      }
      std::cout << argv[i] << ": " << local << " labels";
      for (const auto &note : flow.unsupported)
        std::cout << "; " << note.toStdString();
      std::cout << '\n';
    }
    assert(labels);
    std::cout << "PASS " << documents << " independent/upstream documents, "
              << paragraphs << " paragraphs and " << labels << " list labels\n";
    return 0;
  }
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    TextDocument document;
    document.load(Text::fromUtf8(argv[2]));
    auto flow = document.flow();
    assert(label(flow, "Independent first") == "[1]" &&
           label(flow, "Independent nested") == "1.i)" &&
           label(flow, "Independent continuation") == "[2]" &&
           label(flow, "Independent bullet") == "✓");
    auto text = document.text();
    assert(!text.contains("[1]"));
    text.replace("Independent first", "Edited independently");
    document.set_text(text);
    document.save(Text::fromUtf8(argv[2]) + ".native.odt");
    NativeTextShaper shaper("Noto Sans");
    auto pages = paginate_text(document.flow(), shaper);
    assert(!pages.pages.empty());
    std::cout << "PASS independent list styles, nested counters, continuation, "
                 "Unicode labels and edit/save\n";
    return 0;
  }
  auto package = fixture();
  package.save(path);
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--fixture") {
    assert(File::copy(path, Text::fromUtf8(argv[2])));
    if (argc >= 4)
      fixture(Kind::Stage).save(Text::fromUtf8(argv[3]));
    return 0;
  }
  for (auto [n, format, expected] :
       std::vector<std::tuple<int64_t, Text, Text>>{
           {0, "1", "0"},
           {9, "001", "009"},
           {7, "00001, 00002, 00003, ...", "00007"},
           {27, "a", "aa"},
           {52, "A", "AZ"},
           {3999, "i", "mmmcmxcix"},
           {10000, "I", "10000"},
           {12, "١", "١٢"},
           {12, "ก, ข, ค, ...", "๑๒"},
           {12, "౧, ౨, ౩, ...", "౧౨"},
           {12, "௧, ௨, ௪, ...", "௧௨"},
           {12, "୧, ୨, ୩, ...", "୧୨"},
           {12, "൧, ൨, ൩, ...", "൧൨"},
           {12, "೧, ೨, ೩, ...", "೧೨"},
           {12, "੧, ੨, ੩, ...", "੧੨"},
           {12, "૧, ૨, ૩, ...", "૧૨"},
           {12, "১, ২, ৩, ...", "১২"},
           {3, "أ, ب, ت, ...", "ت"},
           {4, "أ, ب, ج, ...", "د"}})
    assert(format_list_number(n, format) == expected);
  assert(format_list_number(27, "a", true) == "aa" &&
         format_list_number(28, "A", true) == "BB");
  rejects([&] { format_list_number(-1, "1"); });
  rejects([&] { format_list_number(10000000, "a", true); });
  assert(!supported_list_number_format("unknown"));
  std::cout << "PASS native decimal, alphabetic, Roman, synchronized and "
               "legacy script numbering\n";
  TextDocument document;
  document.load(path);
  auto flow = document.flow();
  assert(flow.unsupported.empty());
  assert(!find(flow, "Native list").list &&
         label(flow, "Unicode bullet") == "•");
  assert(label(flow, "List header").empty() &&
         label(flow, "First numbered") == "(1)" &&
         label(flow, "Continuation").empty());
  assert(label(flow, "Nested alpha") == "1.a)" &&
         label(flow, "Nested restart") == "1.c)" &&
         label(flow, "Alternate") == "II.");
  assert(label(flow, "Restarted") == "(7)" &&
         label(flow, "Continued eight") == "(8)" &&
         label(flow, "Continued nine") == "(9)");
  assert(label(flow, "Paragraph style") == "(1)" &&
         label(flow, "Numbered first") == "(1)" &&
         label(flow, "Numbered child") == "1.a)" &&
         label(flow, "Numbered next") == "(2)");
  assert(label(flow, "Serial one") == "1." &&
         label(flow, "Serial two") == "2." &&
         label(flow, "Serial three") == "3.");
  assert(find(flow, "First numbered").list->font.bold &&
         find(flow, "First numbered").list->color->red == 0xbb);
  near(find(flow, "Unicode bullet").list->font.size, 15);
  std::cout << "PASS nested counters, headers, continuation, restarts, "
               "item/paragraph styles and numbered paragraphs\n";
  auto first = first_line(flow, "First numbered");
  assert(first.label && first.label->text == "(1)");
  near(first.x, 30);
  near(first.label->x, 0);
  near(first.label->width, 15);
  auto nested = first_line(flow, "Nested alpha");
  near(nested.x, 58);
  near(nested.label->x, 30);
  auto bullet = first_line(flow, "Unicode bullet");
  near(bullet.x, 30);
  near(bullet.label->x, 8);
  near(bullet.height, 18);
  const auto &wrapped = find(flow, "First numbered");
  auto lines = layout_text_box(TextFlow{{wrapped}}, 100, metric).lines;
  assert(lines.size() > 2 && lines.front().label);
  for (size_t i = 1; i < lines.size(); ++i)
    assert(!lines[i].label && lines[i].x == 30);
  auto pages = paginate_text(flow, metric);
  assert(pages.pages.size() > 1);
  size_t labels = 0;
  for (const auto &page : pages.pages)
    for (const auto &line : page.lines)
      labels += bool(line.label);
  size_t visible = 0;
  for (const auto &p : flow.paragraphs)
    visible += p.list && !p.list->text.empty();
  assert(labels == visible);
  NativeTextShaper shaper("Noto Sans");
  auto shaped = paginate_text(flow, shaper);
  for (const auto &page : shaped.pages)
    for (const auto &line : page.lines)
      if (line.label)
        assert(line.label->shaped && !line.label->shaped->glyphs.empty());
  std::cout << "PASS old/new label geometry, wrapping, large bullet metrics "
               "and one label across page breaks\n";
  TextParagraph rtl;
  rtl.text = "אבג";
  rtl.font.size = 10;
  rtl.direction = TextParagraph::RTL;
  rtl.left = 30;
  rtl.line_factor = 1;
  TextListLabel marker;
  marker.text = "1.";
  marker.font = rtl.font;
  marker.alignment_mode = true;
  marker.alignment = TextListLabel::End;
  marker.indent = -15;
  marker.tab_stop = 30;
  rtl.list = marker;
  auto right = layout_text_box(TextFlow{{rtl}}, 200, metric).lines[0];
  near(right.x, 155);
  near(right.label->x, 185);
  auto &l = *rtl.list;
  l.follow = TextListLabel::Space;
  right = layout_text_box(TextFlow{{rtl}}, 200, metric).lines[0];
  near(right.x, 165);
  l.follow = TextListLabel::Tab;
  l.tab_stop = 100;
  l.paragraph_tabs = {10};
  right = layout_text_box(TextFlow{{rtl}}, 200, metric).lines[0];
  near(right.x, 145);
  l.paragraph_tabs.clear();
  l.tab_stop = 0;
  l.default_tab = 20;
  right = layout_text_box(TextFlow{{rtl}}, 200, metric).lines[0];
  near(right.x, 155);
  std::cout
      << "PASS RTL marker placement, separators and intervening/default tabs\n";
  TextList body;
  for (const auto &p : flow.paragraphs)
    body << p.text;
  assert(body.join('\n') == document.text());
  auto edited = document.text();
  edited.replace("First numbered", "Edited numbered");
  document.set_text(edited);
  document.save(saved);
  TextDocument reopened;
  reopened.load(saved);
  assert(label(reopened.flow(), "Edited numbered") == "(1)");
  auto archive = read_document_archive(saved);
  assert(archive.value("styles.xml") == package.entry("styles.xml") &&
         archive.value("extra/retained.bin") == "retained original payload");
  rejects([&] { reopened.set_text("Changed paragraph count"); });
  assert(label(reopened.flow(), "Edited numbered") == "(1)");
  auto stage = fixture(Kind::Stage);
  stage.save(directory.path() + "/stage.odp");
  Presentation presentation;
  presentation.load(directory.path() + "/stage.odp");
  const auto &shape = presentation.scene(0).shapes[0];
  assert(label(shape.text_flow, "Nested alpha") == "1.a)");
  OdfTextReader shared(stage);
  auto container = child(shape.source, DRAW, "text-box");
  auto a = shared.frame(container, TextParagraph{}),
       b = shared.frame(container, TextParagraph{});
  assert(label(a, "First numbered") == label(b, "First numbered"));
  std::cout << "PASS editable-source offsets, retained list structure, Stage "
               "sharing and reader counter reset\n";
  auto scoped = fixture();
  auto automatic =
      child(scoped.xml.documentElement(), OFFICE, "automatic-styles");
  auto conflict = element(automatic, STYLE, "style:style");
  attr(conflict, STYLE, "style:name", "Label");
  attr(conflict, STYLE, "style:family", "text");
  auto ink = element(conflict, STYLE, "style:text-properties");
  attr(ink, FO, "fo:color", "#00aa00");
  auto scope_flow = read_text_flow(scoped);
  assert(find(scope_flow, "First numbered").list->color->red == 0xbb);
  auto override_list = element(automatic, TEXT, "text:list-style");
  attr(override_list, STYLE, "style:name", "Numbers");
  auto override_level =
      element(override_list, TEXT, "text:list-level-style-number");
  attr(override_level, TEXT, "text:level", "1");
  attr(override_level, STYLE, "style:num-format", "A");
  attr(override_level, STYLE, "style:num-suffix", ".");
  assert(label(read_text_flow(scoped), "First numbered") == "A.");
  OdfTextReader scoped_reader(scoped);
  auto master =
      scoped_reader.frame(scoped.body(), TextParagraph{}, Text(), true);
  assert(label(master, "First numbered") == "(1)");
  auto inherited = fixture(Kind::Stage);
  auto inherited_body = inherited.body();
  auto inherited_container = element(inherited_body, DRAW, "draw:text-box");
  auto inherited_list = element(inherited_container, TEXT, "text:list");
  auto inherited_item = element(inherited_list, TEXT, "text:list-item");
  set_plain_text(element(inherited_item, TEXT, "text:p"),
                 "Inherited frame list");
  OdfTextReader inherited_reader(inherited);
  assert(label(inherited_reader.frame(inherited_container, TextParagraph{},
                                      "Inherited"),
               "Inherited frame list") == "(1)");
  std::cout
      << "PASS scoped common/automatic/master list styles, inherited frame "
         "lists and label character styles\n";
  auto custom = fixture();
  auto custom_styles = read_document_xml(custom.entry("styles.xml"));
  auto custom_level =
      custom_styles.elementsByTagNameNS(TEXT, "list-level-style-number")[1];
  attr(custom_level,
       "urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0",
       "loext:num-list-format", "[%1%/%2%]");
  custom.set_entry("styles.xml", custom_styles.toByteArray(), "text/xml");
  assert(label(read_text_flow(custom), "Nested alpha") == "[1/a]");
  auto overrides = fixture();
  auto auto_styles =
      child(overrides.xml.documentElement(), OFFICE, "automatic-styles");
  auto paragraph_style = element(auto_styles, STYLE, "style:style");
  attr(paragraph_style, STYLE, "style:name", "Override");
  attr(paragraph_style, STYLE, "style:family", "paragraph");
  auto paragraph_props =
      element(paragraph_style, STYLE, "style:paragraph-properties");
  attr(paragraph_props, FO, "fo:margin-left", "12pt");
  attr(paragraph_props, FO, "fo:text-indent", "-3pt");
  for (auto p : overrides.xml.elementsByTagNameNS(TEXT, "p"))
    if (plain_text(p).startsWith("First numbered"))
      attr(p, TEXT, "text:style-name", "Override");
  auto overridden = read_text_flow(overrides);
  near(find(overridden, "First numbered").left, 12);
  near(find(overridden, "First numbered").list->indent, -3);
  attr(paragraph_props, FO, "fo:margin-left", "0pt");
  near(find(read_text_flow(overrides), "First numbered").left, 0);
  auto unresolved = fixture();
  for (auto list : unresolved.xml.elementsByTagNameNS(TEXT, "list"))
    if (list.hasAttributeNS(TEXT, "continue-list"))
      attr(list, TEXT, "text:continue-list", "missing-target");
  auto unresolved_flow = read_text_flow(unresolved);
  assert(label(unresolved_flow, "Continued eight") == "(1)" &&
         !unresolved_flow.unsupported.empty());
  std::cout << "PASS explicit paragraph geometry overrides and diagnosed "
               "unresolved continuation\n";
  auto invalid = fixture();
  auto liststyles = read_document_xml(invalid.entry("styles.xml"));
  auto level =
      liststyles.elementsByTagNameNS(TEXT, "list-level-style-number")[0];
  attr(level, TEXT, "text:level", "-1");
  invalid.set_entry("styles.xml", liststyles.toByteArray(), "text/xml");
  rejects([&] { read_text_flow(invalid); });
  auto bad = flow;
  bad.paragraphs[1].list->default_tab = 0;
  rejects([&] { paginate_text(bad, metric); });
  bad = flow;
  bad.paragraphs[1].list->min_width = -1;
  rejects([&] { paginate_text(bad, metric); });
  auto counter = fixture();
  auto items = counter.xml.elementsByTagNameNS(TEXT, "list-item");
  attr(items[1], TEXT, "text:start-value", "9223372036854775808");
  rejects([&] { read_text_flow(counter); });
  std::cout << "PASS invalid list levels, counters and geometry rejection\n";
}
