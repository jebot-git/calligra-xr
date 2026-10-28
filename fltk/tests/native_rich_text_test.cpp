// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
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
TextMetrics metric(const Text &s, const TextFont &f) {
  return {s.size() * f.size / 2, f.size * .8, f.size * .2};
}
const char *styles =
    R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0">
<office:font-face-decls><style:font-face style:name="Face" svg:font-family="Noto Sans"/></office:font-face-decls>
<office:styles>
<style:default-style style:family="paragraph"><style:text-properties fo:font-family="Noto Sans" fo:font-size="12pt"/><style:paragraph-properties fo:line-height="100%"/></style:default-style>
<style:style style:name="Body" style:family="paragraph"><style:text-properties style:font-name="Face" fo:font-size="12pt" fo:color="#222222"/></style:style>
<style:style style:name="Heading" style:family="paragraph" style:parent-style-name="Body"><style:text-properties fo:font-size="20pt" fo:font-weight="bold" fo:color="#24466e"/><style:paragraph-properties fo:text-align="center" fo:margin-bottom="6pt"/></style:style>
<style:style style:name="Arabic" style:family="paragraph" style:parent-style-name="Body"><style:text-properties fo:font-family="Noto Sans Arabic" fo:font-size="16pt" fo:language="ar" fo:color="#2244aa"/><style:paragraph-properties style:writing-mode="rl-tb" fo:text-align="start"/></style:style>
<style:style style:name="Bottom" style:family="paragraph" style:parent-style-name="Body"><style:text-properties fo:color="#008844"/></style:style>
<style:style style:name="Red" style:family="text"><style:text-properties fo:font-size="18pt" fo:font-weight="bold" fo:color="#dd2233"/></style:style>
<style:style style:name="Green" style:family="text"><style:text-properties fo:font-style="italic" fo:color="#008844"/></style:style>
<style:style style:name="Blue" style:family="text"><style:text-properties fo:color="#2255dd"/></style:style>
<style:style style:name="Window" style:family="text"><style:text-properties style:use-window-font-color="true"/></style:style>
<style:style style:name="Banner" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#24466e"/></style:style>
<style:style style:name="Card" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#ffffff" draw:stroke="solid" svg:stroke-color="#aabbcc" svg:stroke-width=".5pt" fo:padding="8pt" fo:padding-left="12pt" fo:padding-right="10pt" draw:textarea-vertical-align="middle"/></style:style>
<style:style style:name="Overflow" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#fff5e6" fo:padding="5pt"/></style:style>
<style:style style:name="BottomFrame" style:family="graphic" style:parent-style-name="Card"><style:graphic-properties fo:padding="6pt" draw:textarea-vertical-align="bottom"/></style:style>
</office:styles><office:automatic-styles>
<style:style style:name="Scoped" style:family="paragraph"><style:text-properties style:font-name="Face" fo:font-size="18pt" fo:color="#ffffff"/></style:style>
<style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="480pt" fo:page-height="300pt" fo:margin="20pt"/></style:page-layout>
</office:automatic-styles><office:master-styles><style:master-page style:name="WideMaster" style:page-layout-name="Wide"><draw:rect draw:style-name="Banner" svg:x="0pt" svg:y="0pt" svg:width="480pt" svg:height="35pt"/><draw:frame svg:x="10pt" svg:y="1pt" svg:width="450pt" svg:height="32pt"><draw:text-box><text:p text:style-name="Scoped">Native paragraphs and inline colors</text:p></draw:text-box></draw:frame></style:master-page></office:master-styles></office:document-styles>)XML";
OdfPackage fixture(Kind kind) {
  OdfPackage p(kind);
  p.set_entry("styles.xml", styles, "text/xml");
  auto doc = p.xml;
  auto root = doc.documentElement();
  auto decl = doc.createElementNS(OFFICE, "office:font-face-decls");
  auto font = doc.createElementNS(STYLE, "style:font-face");
  font.setAttributeNS(STYLE, "style:name", "Face");
  font.setAttributeNS(SVG, "svg:font-family", "Liberation Mono");
  decl.appendChild(font);
  root.insertBefore(decl, root.firstChild());
  auto automatic = doc.createElementNS(OFFICE, "office:automatic-styles");
  root.insertAfter(automatic, decl);
  auto style = doc.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", "Scoped");
  style.setAttributeNS(STYLE, "style:family", "paragraph");
  auto props = doc.createElementNS(STYLE, "style:text-properties");
  props.setAttributeNS(STYLE, "style:font-name", "Face");
  props.setAttributeNS(
      "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
      "fo:font-size", "13pt");
  props.setAttributeNS(
      "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0", "fo:color",
      "#663399");
  style.appendChild(props);
  automatic.appendChild(style);
  auto paragraphs = read_document_xml(
      R"XML(<root xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><text:p text:style-name="Heading">Rich native text</text:p><text:p text:style-name="Body">A <text:span text:style-name="Red">RED</text:span><text:span text:style-name="Green"> green</text:span><text:a text:style-name="Blue" xlink:href="https://example.invalid/"> link</text:a> tail</text:p><text:p text:style-name="Arabic">سلام عليكم — العربية 123</text:p><text:p text:style-name="Scoped">Scoped face</text:p></root>)XML");
  if (kind == Kind::Words) {
    for (auto e : children(paragraphs.documentElement(), TEXT))
      p.body().appendChild(doc.importNode(e, true));
    return p;
  }
  auto page = doc.createElementNS(DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Rich native text");
  page.setAttributeNS(DRAW, "draw:master-page-name", "WideMaster");
  p.body().appendChild(page);
  auto frame = [&](const Text &name, const Text &paint, const Text &x,
                   const Text &y, const Text &w, const Text &h) {
    auto f = doc.createElementNS(DRAW, "draw:frame");
    f.setAttributeNS(DRAW, "draw:name", name);
    f.setAttributeNS(DRAW, "draw:style-name", paint);
    f.setAttributeNS(SVG, "svg:x", x);
    f.setAttributeNS(SVG, "svg:y", y);
    f.setAttributeNS(SVG, "svg:width", w);
    f.setAttributeNS(SVG, "svg:height", h);
    auto b = doc.createElementNS(DRAW, "draw:text-box");
    f.appendChild(b);
    page.appendChild(f);
    return b;
  };
  auto body = frame("Rich", "Card", "20pt", "55pt", "280pt", "175pt");
  for (auto e : children(paragraphs.documentElement(), TEXT))
    body.appendChild(doc.importNode(e, true));
  auto overflow =
      frame("Overflow", "Overflow", "320pt", "55pt", "140pt", "70pt");
  for (int i = 0; i < 12; ++i) {
    auto e = doc.createElementNS(TEXT, "text:p");
    e.setAttributeNS(TEXT, "text:style-name", "Body");
    set_plain_text(e, "Overflow line " + Text::number(i + 1));
    overflow.appendChild(e);
  }
  auto bottom =
      frame("Bottom", "BottomFrame", "20pt", "245pt", "280pt", "35pt");
  auto e = doc.createElementNS(TEXT, "text:p");
  e.setAttributeNS(TEXT, "text:style-name", "Bottom");
  set_plain_text(e, "Bottom aligned");
  bottom.appendChild(e);
  return p;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation p;
    p.load(Text::fromUtf8(argv[2]));
    const auto &scene = p.scene(0);
    assert(scene.editable_sources.size() == 1);
    const auto &frame = scene.shapes[scene.editor_shapes[0]];
    const auto &flow = frame.text_flow;
    assert(flow.paragraphs.size() == 3);
    assert(flow.paragraphs[0].alignment == TextParagraph::Center &&
           flow.paragraphs[0].font.bold);
    near(flow.paragraphs[0].font.size, 22);
    assert(flow.paragraphs[1].spans.size() == 3);
    assert(flow.paragraphs[1].spans[1].color->red == 0xdd &&
           flow.paragraphs[1].spans[1].font.size == 16);
    assert(flow.paragraphs[2].direction == TextParagraph::RTL &&
           frame.paint.vertical_alignment == ScenePaint::Middle);
    NativeTextShaper shaper;
    auto lines = layout_text_box(flow, 280, shaper);
    assert(lines.lines.size() == 3 && lines.lines[2].shaped->rtl);
    TempDir output;
    auto path = output.path() + "/edited.odp";
    auto text = p.slide(0).boxes[0].text;
    text.replace("colored", "edited");
    p.set_box_text(0, 0, text);
    p.save(path);
    Presentation reopened;
    reopened.load(path);
    const auto &edited = reopened.scene(0).shapes[0].text_flow;
    assert(edited.paragraphs[1].spans[1].color->red == 0xdd &&
           reopened.slide(0).boxes[0].text == text);
    assert(read_document_archive(path).value("styles.xml") ==
           read_document_archive(Text::fromUtf8(argv[2])).value("styles.xml"));
    std::cout << "PASS independent styled ODP paragraphs, colors, bidi, frame "
                 "alignment and edit/save\n";
    return 0;
  }
  TempDir directory;
  auto stagepath = directory.path() + "/rich.odp",
       wordspath = directory.path() + "/rich.odt";
  auto package = fixture(Kind::Stage);
  package.save(stagepath);
  fixture(Kind::Words).save(wordspath);
  if (argc == 4 && Text::fromUtf8(argv[1]) == "--fixture") {
    assert(File::copy(stagepath, Text::fromUtf8(argv[2])));
    assert(File::copy(wordspath, Text::fromUtf8(argv[3])));
  }
  Presentation stage;
  stage.load(stagepath);
  const auto &scene = stage.scene(0);
  assert(scene.shapes.size() == 5 && scene.editable_sources.size() == 3);
  const auto &master = scene.shapes[1].text_flow.paragraphs[0];
  assert(master.font.family == "Noto Sans" && master.color->red == 255);
  near(master.font.size, 18);
  const auto &rich = scene.shapes[scene.editor_shapes[0]];
  const auto &flow = rich.text_flow;
  assert(flow.paragraphs.size() == 4);
  assert(flow.paragraphs[0].alignment == TextParagraph::Center &&
         flow.paragraphs[0].font.bold);
  near(flow.paragraphs[0].font.size, 20);
  assert(flow.paragraphs[1].spans.size() == 5);
  assert(flow.paragraphs[1].font.family == "Noto Sans");
  const auto &red = flow.paragraphs[1].spans[1];
  assert(red.font.bold && red.font.size == 18 && red.color->red == 0xdd);
  assert(flow.paragraphs[1].spans[2].font.italic &&
         flow.paragraphs[1].spans[2].color->green == 0x88);
  assert(flow.paragraphs[2].direction == TextParagraph::RTL &&
         flow.paragraphs[3].font.family == "Liberation Mono" &&
         flow.paragraphs[3].color->red == 0x66);
  near(rich.paint.padding_left, 12);
  near(rich.paint.padding_right, 10);
  near(rich.paint.padding_top, 8);
  assert(rich.paint.vertical_alignment == ScenePaint::Middle);
  auto lines = layout_text_box(flow, 258, metric);
  assert(lines.lines.size() == 4);
  near(lines.lines[0].height, 20);
  near(lines.lines[1].y, 26);
  near(lines.lines[0].x, (258 - 160) / 2.);
  auto red_fragment = std::find_if(
      lines.lines[1].fragments.begin(), lines.lines[1].fragments.end(),
      [](const TextFragment &f) { return f.text == "RED"; });
  assert(red_fragment != lines.lines[1].fragments.end() &&
         red_fragment->color->red == 0xdd);
  NativeTextShaper shaper;
  auto shaped = layout_text_box(flow, 258, shaper);
  assert(shaped.lines.size() == 4 && shaped.lines[2].shaped->rtl);
  std::set<unsigned> colors;
  for (const auto &g : shaped.lines[1].shaped->glyphs)
    if (g.color)
      colors.insert(g.color->red * 65536 + g.color->green * 256 +
                    g.color->blue);
  assert(colors.contains(0xdd2233) && colors.contains(0x008844) &&
         colors.contains(0x2255dd));
  auto overflow = layout_text_box(
      scene.shapes[scene.editor_shapes[1]].text_flow, 130, metric);
  assert(overflow.lines.size() == 12);
  near(overflow.height, 144);
  for (size_t i = 0; i < 12; ++i)
    near(overflow.lines[i].y, 12 * i);
  const auto original_style = package.entry("styles.xml");
  auto old = stage.slide(0).boxes[0].text;
  rejects([&] { stage.set_box_text(0, 0, Text(1000001, U'x')); });
  assert(stage.slide(0).boxes[0].text == old);
  auto edited = old;
  edited.replace("RED", "RED edited");
  stage.set_box_text(0, 0, edited);
  const auto &after =
      stage.scene(0).shapes[stage.scene(0).editor_shapes[0]].text_flow;
  assert(after.paragraphs[1].spans[1].color->red == 0xdd &&
         after.paragraphs[2].direction == TextParagraph::RTL);
  auto saved = directory.path() + "/saved.odp";
  stage.save(saved);
  Presentation again;
  again.load(saved);
  assert(again.slide(0).boxes[0].text == edited);
  assert(read_document_archive(saved).value("styles.xml") == original_style);
  TextDocument words;
  words.load(wordspath);
  auto wordflow = words.flow();
  assert(wordflow.paragraphs.size() == 4 &&
         wordflow.paragraphs[1].spans[1].color->red == 0xdd);
  assert(wordflow.paragraphs[3].font.family == "Liberation Mono");
  auto wpage = words.paginate(metric);
  assert(wpage.pages.size() == 1 && wpage.pages[0].lines.size() == 4);
  assert(wpage.pages[0].style.background == (TextColor{255, 255, 255}));
  auto updated = words.text();
  updated.replace("RED", "RED edited");
  words.set_text(updated);
  auto wordcopy = directory.path() + "/saved.odt";
  words.save(wordcopy);
  TextDocument wordagain;
  wordagain.load(wordcopy);
  assert(wordagain.flow().paragraphs[1].spans[1].color->red == 0xdd);
  auto colored = fixture(Kind::Words);
  auto style_document = read_document_xml(colored.entry("styles.xml"));
  auto page_properties =
      style_document.elementsByTagNameNS(STYLE, "page-layout-properties")[0]
          .toElement();
  page_properties.setAttributeNS(
      "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
      "fo:background-color", "#fff5e6");
  colored.set_entry("styles.xml", style_document.toByteArray(), "text/xml");
  assert(paginate_text(read_text_flow(colored), metric)
             .pages[0]
             .style.background == (TextColor{255, 245, 230}));
  auto relative = fixture(Kind::Stage);
  auto relative_styles = read_document_xml(relative.entry("styles.xml"));
  for (auto node : relative_styles.elementsByTagNameNS(STYLE, "style")) {
    auto e = node.toElement();
    if (e.attributeNS(STYLE, "name") == "Heading")
      child(e, STYLE, "text-properties")
          .setAttributeNS(
              "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
              "fo:font-size", "150%");
  }
  relative.set_entry("styles.xml", relative_styles.toByteArray(), "text/xml");
  relative.xml.elementsByTagNameNS(DRAW, "frame")[0].toElement().setAttributeNS(
      DRAW, "draw:text-style-name", "Heading");
  auto relative_scene = read_presentation_scenes(relative);
  near(relative_scene[0].shapes[2].text_flow.paragraphs[0].font.size, 18);
  TextParagraph window = flow.paragraphs[1];
  window.text = "AB";
  window.spans = {{0, 1, window.font, TextColor{255, 0, 0}},
                  {1, 2, window.font, {}}};
  auto mixed = shaper.prepare(window).whole();
  assert(mixed.glyphs[0].color && !mixed.glyphs[1].color);
  window.color = TextColor{0, 0, 0, NAN};
  rejects([&] { shaper.prepare(window); });
  auto bad = flow;
  bad.paragraphs[0].spans[0].color = TextColor{0, 0, 0, 2};
  rejects([&] { layout_text_box(bad, 258, metric); });
  rejects([&] { layout_text_box(flow, 0, metric); });
  std::cout << "PASS scoped master/content text and font faces, "
               "paragraph/inline styles and colors, bidi, continuous frame "
               "flow, padding/alignment, edit/save and invalid input\n";
}
