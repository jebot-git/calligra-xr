// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include "native_font_family.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F fn) {
  bool caught = false;
  try {
    fn();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
TextFont font(const Text &family, double size = 12) {
  TextFont f;
  f.family = family;
  f.size = size;
  return f;
}
void fixture(const Text &path) {
  OdfPackage package(Kind::Words);
  package.set_entry(
      "styles.xml",
      R"(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles>
<style:default-style style:family="paragraph"><style:text-properties fo:font-family="Noto Serif" fo:font-size="14pt"/><style:paragraph-properties fo:margin-bottom="8pt"/></style:default-style>
<style:style style:name="Heading" style:family="paragraph"><style:text-properties fo:font-size="22pt" fo:font-weight="bold"/></style:style>
<style:style style:name="Arabic" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans Arabic" fo:font-size="18pt" fo:language="ar"/><style:paragraph-properties style:writing-mode="rl-tb" fo:text-align="start"/></style:style>
<style:style style:name="Hebrew" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans Hebrew" fo:font-size="16pt" fo:language="he"/><style:paragraph-properties style:writing-mode="rl-tb" fo:text-align="start"/></style:style>
<style:style style:name="Indic" style:family="paragraph"><style:text-properties fo:font-family="Noto Sans Devanagari" fo:font-size="16pt" fo:language="hi"/></style:style>
<style:style style:name="Mono" style:family="paragraph"><style:text-properties fo:font-family="Liberation Mono"/></style:style>
<style:style style:name="Emphasis" style:family="text"><style:text-properties fo:font-weight="bold" fo:font-style="italic"/></style:style>
</office:styles><office:automatic-styles><style:page-layout style:name="Page"><style:page-layout-properties fo:page-width="8.5in" fo:page-height="11in" fo:margin=".5in"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Page"/></office:master-styles></office:document-styles>)",
      "text/xml");
  for (auto [style, text] :
       std::vector<std::pair<Text, Text>>{{"Heading", "Native shaping — office AV"},
                                          {"", "Ligatures: office affine — marks: á ö"},
                                          {"Arabic", "سلام عليكم — العربية 123"},
                                          {"Hebrew", "עברית — Latin 123"},
                                          {"Indic", "देवनागरी: क्षि नमस्ते"},
                                          {"Mono", "Mixed fallback: 日本語 中文 Český"}}) {
    auto p = package.xml.createElementNS(TEXT, "text:p");
    if (!style.empty())
      p.setAttributeNS(TEXT, "text:style-name", style);
    set_plain_text(p, text);
    package.body().appendChild(p);
  }
  auto p = package.xml.createElementNS(TEXT, "text:p");
  set_plain_text(p, "Styled face: ");
  auto span = package.xml.createElementNS(TEXT, "text:span");
  span.setAttributeNS(TEXT, "text:style-name", "Emphasis");
  set_plain_text(span, "bold italic office");
  p.appendChild(span);
  package.body().appendChild(p);
  package.save(path);
}
void coverage(const ShapedText &shape, size_t length) {
  size_t end = 0;
  for (const auto &c : shape.clusters) {
    assert(c.begin == end && c.end > end);
    end = c.end;
  }
  assert(end == length);
  for (const auto &g : shape.glyphs)
    assert(g.begin < g.end && g.end <= length && g.advance >= 0 && std::isfinite(g.x) &&
           std::isfinite(g.y));
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture") {
    fixture(Text::fromUtf8(argv[2]));
    return 0;
  }
  const std::string config = getenv("FONTCONFIG_FILE") ? getenv("FONTCONFIG_FILE") : "";
  NativeTextShaper engine;
  if (argc == 4 && Text::fromUtf8(argv[1]) == "--dump") {
    auto result = engine.shape(Text::fromUtf8(argv[3]), font(Text::fromUtf8(argv[2])));
    nlohmann::json data = {
        {"width", result.metrics.width}, {"rtl", result.rtl}, {"glyphs", nlohmann::json::array()}};
    for (const auto &g : result.glyphs)
      data["glyphs"].push_back({{"id", g.glyph},
                                {"begin", g.begin},
                                {"end", g.end},
                                {"x", g.x},
                                {"y", g.y},
                                {"advance", g.advance},
                                {"file", g.face->file},
                                {"face_index", g.face->index}});
    std::cout << data.dump() << '\n';
    return 0;
  }
  assert((document_font_families("'A, B', \"Hack\", 日本語, monospace") ==
          TextList{"A, B", "Hack", "日本語", "monospace"}));
  assert(document_font_families(" , '', \"\" ").empty());
  rejects([&] { document_font_families("Hack'"); });
  rejects([&] { document_font_families("\"Hack"); });
  Text many_families;
  for (int i = 0; i < 32; ++i)
    many_families += (i ? ", " : "") + Text("Font") + Text::number(i);
  assert(document_font_families(many_families).size() == 32);
  rejects([&] { document_font_families(many_families + ", extra"); });
  auto latin = font("Noto Sans");
  auto ligatures = engine.shape("office", latin);
  coverage(ligatures, 6);
  assert(ligatures.glyphs.size() == 4 && ligatures.clusters[1].begin == 1 &&
         ligatures.clusters[1].end == 4);
  auto serif = font("Noto Serif", 24);
  assert(engine.shape("AV", serif).metrics.width <
         engine.shape("A", serif).metrics.width + engine.shape("V", serif).metrics.width);
  auto combining = engine.shape(Text::fromUtf8("á"), latin);
  coverage(combining, 2);
  assert(combining.clusters.size() == 1);
  auto regular = engine.shape("Abc", font("Liberation Mono"));
  auto bold_italic = font("Liberation Mono");
  bold_italic.bold = bold_italic.italic = true;
  auto styled = engine.shape("Abc", bold_italic);
  assert(styled.glyphs[0].face->file != regular.glyphs[0].face->file);
  assert(styled.glyphs[0].face->style.find("Bold") != std::string::npos);
  auto bitmap = NativeTextShaper::rasterize(*styled.glyphs[0].face, styled.glyphs[0].glyph, 40);
  assert(bitmap.width > 0 && bitmap.height > 0 &&
         bitmap.alpha.size() == size_t(bitmap.width) * bitmap.height);
  assert(std::any_of(bitmap.alpha.begin(), bitmap.alpha.end(), [](auto v) { return v > 0; }));
  auto before = engine.shape("office", latin);
  for (unsigned pixels : {8, 16, 32, 64})
    NativeTextShaper::rasterize(*before.glyphs[0].face, before.glyphs[0].glyph, pixels);
  auto after = engine.shape("office", latin);
  assert(before.metrics.width == after.metrics.width);
  assert(before.source && *before.source == Text("office"));
  auto paragraph_source =
      engine.prepare(TextParagraph{.text = "one two three", .font = latin});
  auto first_source_line = paragraph_source.line(0, 4);
  auto next_source_line = paragraph_source.line(4, 13);
  assert(first_source_line->source == next_source_line->source &&
         first_source_line->source == paragraph_source.whole().source &&
         *first_source_line->source == Text("one two three"));
  ShapedText retained;
  {
    NativeTextShaper temporary;
    retained = temporary.shape("A", latin);
  }
  assert(NativeTextShaper::rasterize(*retained.glyphs[0].face, retained.glyphs[0].glyph, 24).width >
         0);
  assert(retained.source && *retained.source == Text("A"));
  std::cout << "PASS native ligatures, kerning, combining clusters, styled faces and glyph "
               "rasterization\n";

  const auto arabic_text = Text::fromUtf8("سلام");
  auto arabic = engine.shape(arabic_text, font("Noto Sans Arabic", 24));
  coverage(arabic, arabic_text.size());
  assert(arabic.rtl && !arabic.missing_glyphs);
  assert(arabic.glyphs.front().begin > arabic.glyphs.back().begin);
  auto isolated = engine.shape(arabic_text.left(1), font("Noto Sans Arabic", 24));
  assert(arabic.glyphs.back().glyph != isolated.glyphs[0].glyph);
  auto devanagari_text = Text::fromUtf8("क्षि");
  auto devanagari = engine.shape(devanagari_text, font("Noto Sans Devanagari"));
  coverage(devanagari, devanagari_text.size());
  assert(!devanagari.missing_glyphs && devanagari.clusters.size() == 1);
  auto mixed = engine.shape(Text::fromUtf8("abc אבג 123"), latin);
  coverage(mixed, 11);
  assert(!mixed.missing_glyphs);
  std::vector<size_t> order;
  for (const auto &g : mixed.glyphs)
    order.push_back(g.begin);
  assert((order == std::vector<size_t>{0, 1, 2, 3, 8, 9, 10, 7, 6, 5, 4}));
  auto fallback = engine.shape(Text::fromUtf8("Latin 日本語"), font("Liberation Mono"));
  coverage(fallback, 9);
  assert(!fallback.missing_glyphs);
  std::set<uint64_t> faces;
  for (const auto &g : fallback.glyphs)
    faces.insert(g.face->id);
  assert(faces.size() >= 2);
  auto list = engine.shape("Abc", font("'Nonexistent native test face', 'Liberation Mono'"));
  assert(list.glyphs[0].face->file == regular.glyphs[0].face->file);
  auto light = font("Noto Sans");
  light.weight = 300;
  assert(engine.shape("Weight", light).glyphs[0].face->id !=
         engine.shape("Weight", latin).glyphs[0].face->id);
  std::cout << "PASS Arabic joining, Indic shaping, mixed Hebrew/Latin/numeric bidi order and CJK "
               "fallback\n";

  TextFlow flow;
  TextPageStyle page;
  page.width = 160;
  page.height = 400;
  page.left = page.right = page.top = page.bottom = 10;
  flow.masters[{}] = page;
  TextParagraph p;
  p.text = Text::fromUtf8("سلام عليكم سلام عليكم سلام عليكم");
  p.font = font("Noto Sans Arabic", 20);
  p.direction = TextParagraph::RTL;
  flow.paragraphs.push_back(p);
  auto layout = paginate_text(flow, engine);
  size_t end = 0, lines = 0;
  for (const auto &pg : layout.pages)
    for (const auto &line : pg.lines) {
      assert(line.shaped && line.shaped->rtl && !line.shaped->missing_glyphs);
      assert(line.begin == end);
      end = line.end;
      ++lines;
      assert(line.width <= 140 + 1e-6 && std::abs(line.x + line.width - 150) < 1e-6);
      auto stop = line.end;
      auto reference = engine.prepare(p).line(line.begin, stop);
      assert(reference->glyphs.size() == line.shaped->glyphs.size());
      for (size_t i = 0; i < reference->glyphs.size(); ++i)
        assert(reference->glyphs[i].glyph == line.shaped->glyphs[i].glyph);
    }
  assert(lines > 1 && end == p.text.size());
  flow.paragraphs[0].indent = 12;
  auto indented = paginate_text(flow, engine);
  const auto &first = indented.pages[0].lines[0];
  assert(std::abs(first.x + first.width - 138) < 1e-6);
  flow.paragraphs[0].indent = 0;
  auto justified = flow;
  justified.paragraphs[0].text = "one two three four five six seven eight nine ten";
  justified.paragraphs[0].font = latin;
  justified.paragraphs[0].direction = TextParagraph::LTR;
  justified.paragraphs[0].alignment = TextParagraph::Justify;
  auto expanded = paginate_text(justified, engine);
  const auto &expanded_line = expanded.pages[0].lines[0];
  assert(std::abs(expanded_line.width - 140) < 1e-6);
  double total_advance = 0;
  for (const auto &g : expanded_line.shaped->glyphs)
    total_advance += g.advance;
  assert(std::abs(total_advance - expanded_line.shaped->metrics.width) < 1e-6);
  flow.paragraphs[0].text = "office";
  flow.paragraphs[0].font = latin;
  flow.paragraphs[0].direction = TextParagraph::LTR;
  flow.masters[{}].width = 25;
  auto narrow = paginate_text(flow, engine);
  for (const auto &pg : narrow.pages)
    for (const auto &line : pg.lines)
      assert(line.end != 2 && line.end != 3);
  auto empty = engine.shape({}, latin);
  assert(empty.glyphs.empty() && empty.metrics.ascent > 0);
  // An empty line between styled explicit breaks takes the break's font,
  // while the terminal empty line retains the paragraph-mark/default font.
  TextParagraph blanks;
  blanks.font = font("Hack", 24);
  blanks.text = "\n\nA";
  blanks.spans = {{0, 3, font("Hack", 6), {}}};
  auto blank_shape = engine.prepare(blanks);
  auto small_empty = engine.shape({}, font("Hack", 6));
  assert(std::abs(blank_shape.line(0, 0)->metrics.ascent -
                  small_empty.metrics.ascent) < 1e-8);
  assert(std::abs(blank_shape.line(1, 1)->metrics.descent -
                  small_empty.metrics.descent) < 1e-8);
  assert(std::abs(blank_shape.line(3, 3)->metrics.ascent -
                  engine.shape({}, blanks.font).metrics.ascent) < 1e-8);
  auto blank_flow = TextFlow{};
  blank_flow.paragraphs = {blanks};
  auto blank_layout = layout_text_box(blank_flow, 200, engine);
  assert(blank_layout.lines.size() == 3);
  for (auto &line : blank_layout.lines)
    assert(std::abs(line.height -
                    (small_empty.metrics.ascent + small_empty.metrics.descent) *
                        blanks.line_factor) < 1e-8);
  auto controls = engine.shape(Text::fromUtf8("\u200eA\u200f"), latin);
  coverage(controls, 3);
  TextParagraph soft;
  soft.font = latin;
  soft.text = Text::fromUtf8("עברית\nLatin\r\n日本語");
  auto soft_layout = paginate_text(
      [&] {
        auto f = flow;
        f.paragraphs = {soft};
        f.masters[{}].width = 200;
        return f;
      }(),
      engine);
  size_t soft_lines = 0;
  for (const auto &page : soft_layout.pages)
    for (const auto &line : page.lines) {
      assert(line.shaped->rtl);
      ++soft_lines;
    }
  assert(soft_lines == 3);
  NativeTextShaper unicode_bitmap("Unifont");
  auto bitmap_text = unicode_bitmap.shape(Text::fromUtf8("Český 日本語"), font({}));
  assert(!bitmap_text.missing_glyphs);
  for (const auto &g : bitmap_text.glyphs) {
    auto mask = NativeTextShaper::rasterize(*g.face, g.glyph, 32);
    assert(mask.pixels_per_em > 0);
  }
  assert((getenv("FONTCONFIG_FILE") ? getenv("FONTCONFIG_FILE") : "") == config);
  TempDir directory;
  auto path = directory.path() + "/shaping.odt", saved = directory.path() + "/saved.odt";
  fixture(path);
  TextDocument document;
  document.load(path);
  auto model = document.flow();
  assert(model.paragraphs[0].font.weight == 700 && model.paragraphs[0].font.bold);
  assert(model.paragraphs[2].direction == TextParagraph::RTL &&
         model.paragraphs[2].font.language == "ar");
  auto original = read_document_archive(path);
  auto layout_before = paginate_text(document.flow(), engine);
  assert(layout_before.pages.size() == 1 && layout_before.pages[0].lines.size() == 7);
  for (const auto &line : layout_before.pages[0].lines)
    assert(!line.shaped->missing_glyphs);
  document.save(saved);
  TextDocument reopened;
  reopened.load(saved);
  assert(document.text() == reopened.text());
  assert(read_document_archive(saved).value("styles.xml") == original.value("styles.xml"));
  TextFlow large;
  TextPageStyle compact;
  compact.width = 100;
  compact.height = 60;
  compact.left = compact.right = compact.top = compact.bottom = 10;
  large.masters[{}] = compact;
  TextParagraph many;
  many.font = latin;
  many.line_height = 10;
  for (int i = 0; i < 19999; ++i)
    many.text += "x\n";
  many.text += "x";
  large.paragraphs.push_back(many);
  auto many_pages = paginate_text(large, engine);
  assert(many_pages.pages.size() == 5000 &&
         many_pages.pages.back().lines.back().end == many.text.size());
  assert(many_pages.pages.back().lines.back().shaped->glyphs.size() == 1);
  auto missing = engine.shape(Text(1, Char(0x10ffff)), latin);
  assert(missing.missing_glyphs);
  rejects([&] { NativeTextShaper::rasterize(*regular.glyphs[0].face, 0xffffffff, 16); });
  rejects([&] { engine.shape("x", font("Noto Sans", NAN)); });
  rejects([&] { engine.shape(Text(1, Char(0xd800)), latin); });
  std::cout << "PASS shaped page wrapping, RTL alignment, cluster-safe forced breaks, "
               "empty/control text and invalid inputs\n";
}
