// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void near(double actual, double expected) { assert(std::abs(actual - expected) < 1e-6); }
TextMetrics metric(const Text &text, const TextFont &font) {
  return {double(text.size()) * font.size / 2, font.size * .8, font.size * .2};
}
TextFlow small(const std::vector<Text> &paragraphs) {
  TextFlow flow;
  TextPageStyle style;
  style.width = 100;
  style.height = 60;
  style.left = style.right = style.top = style.bottom = 10;
  flow.masters[{}] = style;
  for (const auto &text : paragraphs) {
    TextParagraph p;
    p.text = text;
    p.font.size = 10;
    p.line_factor = 1;
    flow.paragraphs.push_back(p);
  }
  return flow;
}
OdfPackage fixture() {
  OdfPackage package(Kind::Words);
  package.set_entry(
      "styles.xml",
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0">
<office:font-face-decls><style:font-face style:name="BodyFont" svg:font-family="Liberation Mono"/></office:font-face-decls>
<office:styles><style:default-style style:family="paragraph"><style:text-properties fo:font-size="10pt" style:font-name="BodyFont"/><style:paragraph-properties fo:line-height="100%"/></style:default-style>
<style:style style:name="Body" style:family="paragraph"><style:paragraph-properties fo:margin-bottom="2pt" fo:orphans="2" fo:widows="2"/></style:style>
<style:style style:name="Title" style:family="paragraph" style:parent-style-name="Body" style:master-page-name="First"><style:text-properties fo:font-size="150%" fo:font-weight="bold"/><style:paragraph-properties fo:keep-with-next="always"/></style:style>
<style:style style:name="Emphasis" style:family="text"><style:text-properties fo:font-style="italic"/></style:style>
</office:styles><office:automatic-styles>
<style:page-layout style:name="first-layout"><style:page-layout-properties fo:page-width="4in" fo:page-height="5in" fo:margin=".5in"/></style:page-layout>
<style:page-layout style:name="body-layout"><style:page-layout-properties fo:page-width="10cm" fo:page-height="120mm" fo:margin="12pt"/></style:page-layout>
</office:automatic-styles><office:master-styles><style:master-page style:name="First" style:page-layout-name="first-layout" style:next-style-name="BodyPage"/><style:master-page style:name="BodyPage" style:page-layout-name="body-layout"/></office:master-styles></office:document-styles>)",
      "text/xml");
  auto automatic = package.xml.createElementNS(OFFICE, "office:automatic-styles");
  package.xml.documentElement().insertBefore(automatic, package.xml.documentElement().firstChild());
  auto style =
      package.xml.createElementNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style:style");
  style.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style:name", "NewPage");
  style.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style:family",
                       "paragraph");
  style.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0", "style:parent-style-name",
                       "Body");
  auto props = package.xml.createElementNS("urn:oasis:names:tc:opendocument:xmlns:style:1.0",
                                           "style:paragraph-properties");
  props.setAttributeNS("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
                       "fo:break-before", "page");
  style.appendChild(props);
  automatic.appendChild(style);
  auto p = package.xml.createElementNS(TEXT, "text:h");
  p.setAttributeNS(TEXT, "text:style-name", "Title");
  set_plain_text(p, "Native ");
  auto span = package.xml.createElementNS(TEXT, "text:span");
  span.setAttributeNS(TEXT, "text:style-name", "Emphasis");
  set_plain_text(span, Text::fromUtf8("Český 日本語"));
  p.appendChild(span);
  package.body().appendChild(p);
  p = package.xml.createElementNS(TEXT, "text:p");
  p.setAttributeNS(TEXT, "text:style-name", "Body");
  set_plain_text(p, "First body paragraph");
  package.body().appendChild(p);
  p = package.xml.createElementNS(TEXT, "text:p");
  p.setAttributeNS(TEXT, "text:style-name", "NewPage");
  set_plain_text(p, "After an explicit page break");
  package.body().appendChild(p);
  return package;
}
} // namespace
int main(int argc, char **argv) {
  TempDir directory;
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    TextDocument document;
    document.load(Text::fromUtf8(argv[2]));
    auto flow = document.flow();
    assert(flow.paragraphs.size() == 3 && flow.paragraphs[0].font.bold);
    near(flow.paragraphs[0].font.size, 18);
    assert(flow.paragraphs[2].break_before);
    auto layout = document.paginate(metric);
    assert(layout.pages.size() == 2);
    near(layout.pages[0].style.width, 612);
    near(layout.pages[0].style.height, 792);
    near(layout.pages[0].style.left, 72);
    document.set_text("Changed heading\nBody paragraph\nNext page");
    auto saved = directory.path() + "/external.odt";
    document.save(saved);
    TextDocument again;
    again.load(saved);
    assert(again.paginate(metric).pages.size() == 2 && again.flow().paragraphs[0].font.bold);
    std::cout << "PASS independently generated ODF style/page-flow import, edit and save\n";
    return 0;
  }
  auto path = directory.path() + "/flow.odt";
  auto package = fixture();
  package.save(path);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture")
    assert(File::copy(path, Text::fromUtf8(argv[2])));
  TextDocument doc;
  doc.load(path);
  auto flow = doc.flow();
  assert(flow.paragraphs.size() == 3 && flow.paragraphs[0].spans.size() == 2);
  const auto &title = flow.paragraphs[0];
  assert(title.text == Text::fromUtf8("Native Český 日本語") && title.font.bold && title.keep_next);
  near(title.font.size, 15);
  near(title.after, 2);
  assert(title.font.family == "Liberation Mono" && title.spans[1].font.bold &&
         title.spans[1].font.italic);
  auto pages = doc.paginate(metric);
  assert(pages.pages.size() == 2);
  near(pages.pages[0].style.width, 288);
  near(pages.pages[0].style.height, 360);
  near(pages.pages[0].lines.front().x, 36);
  assert(pages.pages[1].style.name == "BodyPage" && pages.pages[1].lines[0].paragraph == 2);
  near(pages.pages[1].style.width, 720 / 2.54);
  auto saved = directory.path() + "/saved.odt";
  doc.save(saved);
  TextDocument again;
  again.load(saved);
  assert(again.text() == doc.text() && again.flow().paragraphs[0].spans[1].font.italic);
  doc.set_text(
      Text::fromUtf8("Changed Český 日本語\nFirst body paragraph\nAfter an explicit page break"));
  assert(doc.paginate(metric).pages.size() == 2);
  std::cout << "PASS native ODF page geometry, master continuation, inherited and inline styles, "
               "edit/save round trip\n";

  auto nine = paginate_text(small({"1\n2\n3\n4\n5\n6\n7\n8\n9"}), metric);
  assert(nine.pages.size() == 3 && nine.pages[0].lines.size() == 4 &&
         nine.pages[1].lines.size() == 3 && nine.pages[2].lines.size() == 2);
  auto orphan = paginate_text(small({"1\n2\n3", "4\n5"}), metric);
  assert(orphan.pages.size() == 2 && orphan.pages[0].lines.size() == 3 &&
         orphan.pages[1].lines.size() == 2);
  auto kept = small({"1\n2", "3\n4\n5"});
  kept.paragraphs[1].keep_together = true;
  auto whole = paginate_text(kept, metric);
  assert(whole.pages.size() == 2 && whole.pages[0].lines.size() == 2 &&
         whole.pages[1].lines.size() == 3);
  auto chain = small({"1\n2\n3", "Heading", "Body"});
  chain.paragraphs[1].keep_next = true;
  auto grouped = paginate_text(chain, metric);
  assert(grouped.pages.size() == 2 && grouped.pages[0].lines.size() == 3 &&
         grouped.pages[1].lines.size() == 2);
  auto forced = small({"One", "Two"});
  forced.paragraphs[0].break_after = true;
  auto broken = paginate_text(forced, metric);
  assert(broken.pages.size() == 2 && broken.pages[1].lines[0].paragraph == 1);
  std::cout << "PASS page breaks, widow/orphan control, keep-together and keep-with-next\n";

  auto unicode = small({Text::fromUtf8("áááááááááááá")});
  unicode.masters[{}].width = 40; // 20pt text area, two combining clusters per line.
  auto combining = paginate_text(unicode, metric);
  for (const auto &page : combining.pages)
    for (const auto &line : page.lines)
      assert(line.begin % 2 == 0 && line.end % 2 == 0);
  auto emoji = small({Text::fromUtf8("👩‍💻👩‍💻👩‍💻")});
  emoji.masters[{}].width = 40;
  auto clusters = paginate_text(emoji, metric);
  size_t count = 0;
  for (const auto &page : clusters.pages)
    for (const auto &line : page.lines) {
      assert(line.end - line.begin == 3);
      ++count;
    }
  assert(count == 3);
  auto spaces = paginate_text(small({"one two three four five"}), metric);
  assert(spaces.pages[0].lines[0].end ==
         14); // Last full word and its trailing space, never mid-word.
  auto empty = paginate_text(small({"", "x\n"}), metric);
  assert(empty.pages[0].lines.size() == 3);
  auto center = small({"word"});
  center.paragraphs[0].alignment = TextParagraph::Center;
  near(paginate_text(center, metric).pages[0].lines[0].x, 40);
  center.paragraphs[0].alignment = TextParagraph::Right;
  near(paginate_text(center, metric).pages[0].lines[0].x, 70);
  std::cout << "PASS Unicode grapheme/line boundaries, blank and hard-broken lines and alignment\n";
  Text long_paragraph;
  for (int i = 0; i < 19999; ++i)
    long_paragraph += "x\n";
  long_paragraph += "x";
  auto many_pages = paginate_text(small({long_paragraph}), metric);
  assert(many_pages.pages.size() == 5000);
  assert(many_pages.pages.back().lines.back().end == long_paragraph.size());
  std::cout
      << "PASS a 20,000-line paragraph paginates across 5,000 pages without repeated wrapping\n";
  OdfPackage cdata(Kind::Words);
  cdata.xml = read_document_xml(
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"><office:body><office:text><text:p>One <![CDATA[two]]><text:s/>three</text:p></office:text></office:body></office:document-content>)");
  assert(read_text_flow(cdata).paragraphs[0].text == "One two three");
  auto body = cdata.body();
  while (!body.firstChild().isNull())
    body.removeChild(body.firstChild());
  for (int i = 0; i < 5; ++i) {
    auto p = cdata.xml.createElementNS(TEXT, "text:p");
    auto spaces = cdata.xml.createElementNS(TEXT, "text:s");
    spaces.setAttributeNS(TEXT, "text:c", "900000");
    p.appendChild(spaces);
    body.appendChild(p);
  }
  rejects([&] { read_text_flow(cdata); });
  auto invalid = small({"text"});
  invalid.masters[{}].left = 100;
  rejects([&] { paginate_text(invalid, metric); });
  rejects([&] {
    paginate_text(small({"text"}),
                  [](const Text &, const TextFont &) { return TextMetrics{NAN, 8, 2}; });
  });
  rejects([&] { paginate_text(small({"text"}), MeasureText{}); });
  auto huge = small({Text(4'000'001, U'x')});
  rejects([&] { paginate_text(huge, metric); });
  auto cyclic = fixture();
  auto styles = cyclic.entry("styles.xml");
  styles.replace(
      "style:name=\"Body\" style:family=\"paragraph\"",
      "style:name=\"Body\" style:family=\"paragraph\" style:parent-style-name=\"Title\"");
  cyclic.set_entry("styles.xml", styles, "text/xml");
  rejects([&] { read_text_flow(cyclic); });
  std::cout << "PASS invalid geometry/metrics, cyclic styles and resource limits\n";
}
