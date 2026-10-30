// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_tabs.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
template <class F> void rejects(F call) {
  bool failed = false;
  try {
    call();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
struct Snapshot {
  Bytes content, styles, manifest;
  bool operator==(const Snapshot &) const = default;
};
Snapshot state(const OdfPackage &p) {
  return {p.xml.toByteArray(), p.entry("styles.xml"),
          p.entry("META-INF/manifest.xml")};
}
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  p.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles><style:style style:name="Ink" style:family="text"><style:text-properties fo:color="#cc2200" fo:font-size="2pt"/></style:style><style:style style:name="Cell" style:family="table-cell"><style:paragraph-properties><style:tab-stops><style:tab-stop style:position="55pt" style:type="center"/></style:tab-stops></style:paragraph-properties></style:style></office:automatic-styles><office:body><office:text><text:p text:style-name="Body" xml:id="first">Alpha<text:tab/>12.50</text:p><text:p text:style-name="Body">Beta<text:tab/><text:a xlink:href="https://example.invalid/" xlink:type="simple">34.20</text:a></text:p><table:table table:name="Cells"><table:table-column/><table:table-row><table:table-cell table:style-name="Cell" office:value-type="string"><text:p>Cell<text:tab/>XY</text:p></table:table-cell></table:table-row></table:table></office:text></office:body></office:document-content>)XML");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:styles><style:default-style style:family="paragraph"><style:paragraph-properties style:tab-stop-distance="48pt"><style:tab-stops><style:tab-stop style:position="10pt"/></style:tab-stops></style:paragraph-properties><style:text-properties fo:font-family="DejaVu Sans" fo:font-size="12pt"/></style:default-style><style:style style:name="Ink" style:family="text"><style:text-properties fo:color="#0022cc" fo:font-size="150%"/></style:style><style:style style:name="Body" style:family="paragraph"><style:paragraph-properties fo:margin-left="10pt"><style:tab-stops><!-- keep source comment --><style:tab-stop style:position="20mm" style:type="left" style:leader-style="solid" style:leader-type="single" style:leader-width="120%" style:leader-text="·" style:leader-text-style="Ink"/><style:tab-stop style:position="40mm" style:type="char" style:char="." style:leader-style="dotted" style:leader-color="#336699"/></style:tab-stops></style:paragraph-properties></style:style></office:styles></office:document-styles>)XML",
      "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
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
  auto input = Text((dir / "original.odt").string());
  p.save(input);
  auto initial = state(p);
  TextDocument d;
  d.load(input);
  auto original = d.text();
  auto first_end = original.find(U'\n');
  TextSelection range{0, first_end};
  auto save = [&](const char *name) {
    auto path = Text((dir / (std::string(name) + ".odt")).string());
    d.save(path);
    OdfPackage read(Kind::Words);
    read.load(path);
    assert(read.entry("Extras/opaque.bin") == p.entry("Extras/opaque.bin"));
    return read;
  };
  auto info = read_words_tabs(p, 0, first_end);
  assert(info.stops && info.stops->stops.size() == 2 && !info.origin_editable);
  near(info.default_distance, 48);
  assert(info.relative);
  near(words_tab_position(info.stops->stops[0]), 20 * 72 / 25.4);
  assert(info.stops->stops[0].leader_scope == WordsTabStopSpec::Common);
  auto cell = original.find(Text("Cell"));
  auto cell_info = read_words_tabs(p, cell, cell + 2);
  assert(cell_info.stops && cell_info.stops->stops.size() == 1);
  near(words_tab_position(cell_info.stops->stops[0]), 55);
  assert(!read_words_tabs(p, 0, original.size()).stops);
  WordsTabsFormat format;
  format.stops = info.stops;
  auto work = d.style_snapshot();
  assert(!format_words_tabs(work, 0, first_end, format));
  assert(state(work) == initial);
  assert(!d.commit_styles(work, range, d.revision()));
  format.stops->stops[0].properties["position"] = "72pt";
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, first_end, format));
  assert(d.commit_styles(work, range, d.revision()));
  auto edited = state(d.style_snapshot());
  assert(edited.styles == initial.styles &&
         edited.manifest == initial.manifest);
  auto modified = save("position-edited");
  auto flow = read_text_flow(modified);
  near(flow.paragraphs[0].tabs[0].position, 72);
  assert(flow.paragraphs[0].tabs[0].leader_font.color->blue == 204 &&
         flow.paragraphs[0].tabs[0].leader_font.color->red == 0);
  near(flow.paragraphs[0].tabs[0].leader_font.scale, 1.5);
  auto after = read_words_tabs(modified, 0, first_end);
  assert(after.stops);
  auto &stop = after.stops->stops[0];
  assert(stop.properties.at("leader-width") == "120%" &&
         stop.properties.at("leader-text-style") != "Ink");
  assert(d.text() == original);
  assert(d.undo() == range && state(d.style_snapshot()) == initial);
  save("position-undone");
  assert(d.redo() == range && state(d.style_snapshot()) == edited);
  save("position-redone");
  format = {};
  format.stops = read_words_tabs(d.style_snapshot(), 0, first_end).stops;
  work = d.style_snapshot();
  assert(!format_words_tabs(work, 0, first_end, format));
  assert(state(work) == edited);
  // Explicit automatic and master automatic leader references retain their
  // source scope instead of resolving to the colliding common Ink style.
  auto scoped = d.style_snapshot();
  WordsTabsFormat scoped_format;
  WordsTabSet scoped_set;
  auto scoped_stop = words_new_tab_stop(92);
  scoped_stop.properties["leader-text"] = "·";
  scoped_stop.properties["leader-text-style"] = "Ink";
  scoped_stop.leader_scope = WordsTabStopSpec::ContentAutomatic;
  scoped_set.stops.push_back(scoped_stop);
  scoped_format.stops = scoped_set;
  assert(format_words_tabs(scoped, 0, first_end, scoped_format));
  auto scoped_flow = read_text_flow(scoped);
  assert(scoped_flow.paragraphs[0].tabs[0].leader_font.color->red == 204);
  near(*scoped_flow.paragraphs[0].tabs[0].leader_font.size, 2);
  scoped.save(Text((dir / "content-leader-scope.odt").string()));
  auto master_doc = read_document_xml(scoped.entry("styles.xml"));
  auto auto_group =
      master_doc.createElementNS(OFFICE, "office:automatic-styles");
  master_doc.documentElement().appendChild(auto_group);
  auto master_ink = master_doc.createElementNS(STYLE, "style:style");
  master_ink.setAttributeNS(STYLE, "style:name", "Ink");
  master_ink.setAttributeNS(STYLE, "style:family", "text");
  auto ink_props = master_doc.createElementNS(STYLE, "style:text-properties");
  ink_props.setAttributeNS(FO, "fo:color", "#22cc00");
  ink_props.setAttributeNS(FO, "fo:font-size", "200%");
  master_ink.appendChild(ink_props);
  auto_group.appendChild(master_ink);
  scoped.set_entry("styles.xml", master_doc.toByteArray(), "text/xml");
  scoped_format.stops->stops[0].leader_scope =
      WordsTabStopSpec::MasterAutomatic;
  assert(format_words_tabs(scoped, 0, first_end, scoped_format));
  scoped_flow = read_text_flow(scoped);
  assert(scoped_flow.paragraphs[0].tabs[0].leader_font.color->green == 204);
  near(scoped_flow.paragraphs[0].tabs[0].leader_font.scale, 2);
  scoped.save(Text((dir / "master-leader-scope.odt").string()));
  for (auto id : TextList{"CycleA", "CycleB"}) {
    auto node = master_doc.createElementNS(STYLE, "style:style");
    node.setAttributeNS(STYLE, "style:name", id);
    node.setAttributeNS(STYLE, "style:family", "text");
    node.setAttributeNS(STYLE, "style:parent-style-name",
                        id == "CycleA" ? "CycleB" : "CycleA");
    auto_group.appendChild(node);
  }
  scoped.set_entry("styles.xml", master_doc.toByteArray(), "text/xml");
  auto before_failure = state(scoped);
  scoped_format.default_distance = 70;
  scoped_format.stops->stops[0].properties["leader-text-style"] = "CycleA";
  rejects([&] { format_words_tabs(scoped, 0, first_end, scoped_format); });
  assert(state(scoped) == before_failure);
  // Explicit empty tabs suppress inherited stops; removing the direct override
  // returns to the parent's list. Both are different native undo operations.
  format = {};
  format.stops = WordsTabSet{};
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, first_end, format));
  assert(d.commit_styles(work, range, d.revision()));
  assert(d.flow().paragraphs[0].tabs.empty());
  save("cleared");
  format = {};
  format.inherit = true;
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, first_end, format));
  assert(d.commit_styles(work, range, d.revision()));
  near(d.flow().paragraphs[0].tabs[0].position, 20 * 72 / 25.4);
  save("inherited");
  assert(d.undo() && d.flow().paragraphs[0].tabs.empty());
  assert(d.undo() && state(d.style_snapshot()) == edited);
  // Defaults are edited in the document's paragraph default, not copied into
  // each direct paragraph style. ODF1.3 cannot newly encode relative origins.
  format = {};
  format.default_distance = 60;
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, first_end, format));
  assert(d.commit_styles(work, range, d.revision()));
  auto defaults = state(d.style_snapshot());
  assert(defaults.content == edited.content &&
         defaults.styles != edited.styles);
  near(read_words_tabs(d.style_snapshot(), cell, cell + 2).default_distance,
       60);
  save("default-distance");
  assert(d.undo() && state(d.style_snapshot()) == edited);
  format = {};
  format.relative = false;
  work = d.style_snapshot();
  rejects([&] { format_words_tabs(work, 0, first_end, format); });
  assert(state(work) == edited);
  // ODF1.4 origin metadata is an explicit versioned source fixture.
  auto modern = d.style_snapshot();
  modern.xml.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  auto styles = read_document_xml(modern.entry("styles.xml"));
  styles.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  modern.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  assert(read_words_tabs(modern, 0, first_end).origin_editable);
  assert(format_words_tabs(modern, 0, first_end, format));
  assert(!read_text_flow(modern).paragraphs[0].relative_tabs);
  modern.save(Text((dir / "origin-absolute-1.4.odt").string()));
  format.relative = true;
  assert(format_words_tabs(modern, 0, first_end, format));
  modern.save(Text((dir / "origin-relative-1.4.odt").string()));
  // Range application covers body and table cells while preserving raw text.
  format = {};
  WordsTabSet set;
  auto unicode = words_new_tab_stop(84);
  unicode.properties["type"] = "char";
  unicode.properties["char"] = "٫";
  unicode.properties["leader-style"] = "wave";
  unicode.properties["leader-type"] = "double";
  unicode.properties["leader-width"] = ".8pt";
  unicode.properties["leader-color"] = "#1234ab";
  set.stops.push_back(unicode);
  format.stops = set;
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, original.size(), format));
  assert(d.commit_styles(work, {0, original.size()}, d.revision()));
  for (const auto &para : d.flow().paragraphs) {
    assert(para.tabs.size() == 1 && para.tabs[0].delimiter == U'٫' &&
           para.tabs[0].double_line);
    near(para.tabs[0].position, 84);
  }
  assert(d.text() == original);
  save("range-unicode-leader");
  assert(d.undo() && state(d.style_snapshot()) == edited);
  // Bad input and a failure after changing defaults are atomic.
  for (auto width :
       {Text("1.2"), Text("-1pt"), Text("0%"), Text("1e2%"), Text("9000pt")}) {
    auto bad = format;
    bad.stops->stops[0].properties["leader-width"] = width;
    work = d.style_snapshot();
    rejects([&] { format_words_tabs(work, 0, first_end, bad); });
    assert(state(work) == edited);
  }
  auto bad = format;
  bad.stops->stops.push_back(bad.stops->stops[0]);
  work = d.style_snapshot();
  rejects([&] { format_words_tabs(work, 0, first_end, bad); });
  assert(state(work) == edited);
  bad = format;
  bad.stops->stops[0].properties["char"] = "ab";
  rejects([&] { format_words_tabs(work, 0, first_end, bad); });
  assert(state(work) == edited);
  // XML-forbidden scalars must never reach an exported attribute, even though
  // they fit in one native Unicode codepoint. Supplementary characters work.
  for (auto key : {Text("char"), Text("leader-text")}) {
    for (char32_t c : {char32_t(1), char32_t(0xb), char32_t(0xd800),
                       char32_t(0xfffe), char32_t(0x110000)}) {
      bad = format;
      bad.stops->stops[0].properties[key] = Text(std::u32string(1, c));
      rejects([&] { format_words_tabs(work, 0, first_end, bad); });
      assert(state(work) == edited);
    }
    auto valid = format;
    valid.stops->stops[0].properties[key] = Text(std::u32string(1, 0x1f600));
    validate_words_tabs(valid);
  }
  auto before_revision = d.revision();
  rejects([&] { d.commit_styles(work, range, before_revision - 1); });
  assert(state(d.style_snapshot()) == edited);
  d.begin_edit(range);
  format = {};
  format.default_distance = 64;
  work = d.style_snapshot();
  assert(format_words_tabs(work, 0, first_end, format));
  assert(d.commit_styles(work, range, d.revision()));
  d.cancel_edit();
  assert(d.end_edit() == range && state(d.style_snapshot()) == edited);
  // Empty bodies remain empty for no-op clearing; an explicit stop can create
  // the first source paragraph and exact empty-body undo still works.
  OdfPackage empty(Kind::Words);
  auto empty_xml = empty.xml.toByteArray();
  format = {};
  format.stops = WordsTabSet{};
  assert(!format_words_tabs(empty, 0, 0, format) &&
         empty.xml.toByteArray() == empty_xml);
  format.stops->stops.push_back(words_new_tab_stop(90));
  assert(format_words_tabs(empty, 0, 0, format));
  empty.save(Text((dir / "empty-styled.odt").string()));
  std::cout << "PASS native Words tab editing, inherited/cell/scoped leaders, "
               "defaults, version gates and atomic history\n";
}
