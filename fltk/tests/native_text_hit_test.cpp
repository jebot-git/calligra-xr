// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

using namespace DomeOffice;
namespace {
TextParagraph paragraph(const Text &text, const Text &family = "Noto Sans") {
  TextParagraph p;
  p.text = text;
  p.font.family = family;
  p.font.size = 20;
  return p;
}
TextPagination layout(NativeTextShaper &shaper, const TextParagraph &p,
                      double width = 400) {
  TextFlow flow;
  TextPageStyle page;
  page.width = width;
  page.height = 1000;
  page.left = page.right = page.top = page.bottom = 10;
  flow.masters[{}] = page;
  flow.paragraphs.push_back(p);
  return paginate_text(flow, shaper);
}
std::set<size_t> offsets(const TextLine &line) {
  std::set<size_t> result;
  for (const auto &caret : text_line_carets(line)) {
    assert(std::isfinite(caret.rect.x) && caret.rect.height > 0);
    result.insert(caret.offset);
  }
  return result;
}
void close(double a, double b) { assert(std::abs(a - b) < 1e-6); }
void glyph_positions(const TextLine &line) {
  // Hack has no horizontal ink offset for these plain ASCII fixtures. This
  // independently checks that caret advances followed every layout transform.
  for (const auto &cluster : line.shaped->clusters) {
    auto glyph =
        std::find_if(line.shaped->glyphs.begin(), line.shaped->glyphs.end(),
                     [&](const auto &g) { return g.begin == cluster.begin; });
    if (glyph != line.shaped->glyphs.end())
      close(cluster.x, glyph->x);
  }
}
} // namespace
int main() {
  NativeTextShaper shaper;
  auto result = layout(shaper, paragraph("office"));
  const auto &line = result.pages[0].lines[0];
  assert(line.shaped->glyphs.size() < 6);
  assert((offsets(line) == std::set<size_t>{0, 1, 2, 3, 4, 5, 6}));
  const auto &ligature = line.shaped->clusters.at(1);
  assert(ligature.begin == 1 && ligature.end == 4 &&
         ligature.carets.size() == 4);
  assert(ligature.carets[1].fraction > 0 &&
         ligature.carets[1].fraction < ligature.carets[2].fraction &&
         ligature.carets[2].fraction < 1);
  // The installed Noto Sans GDEF places the ffi carets at 315/631 design
  // units per 1000 em. This distinguishes real font data from equal thirds.
  assert(std::abs(ligature.carets[1].fraction * ligature.metrics.width - 6.30) <
         .003);
  assert(std::abs(ligature.carets[2].fraction * ligature.metrics.width -
                  12.62) < .003);
  for (const auto &caret : text_line_carets(line)) {
    auto hit =
        text_caret_at(result.pages[0], caret.rect.x, line.y + line.height / 2);
    assert(hit && hit->caret.offset == caret.offset);
  }
  auto left = text_caret_at(result.pages[0], -100, line.y + 1);
  auto right = text_caret_at(result.pages[0], 1000, line.y + 1);
  assert(left && left->caret.offset == 0 && right && right->caret.offset == 6);
  assert(!text_caret_at(result.pages[0], NAN, line.y));
  assert(!text_caret_at(result.pages[0], 0, INFINITY));
  assert(!text_caret_at(result.pages[0], 0, -10));

  for (const auto &text :
       {Text("á"), Text("क्षि"), Text("👩‍💻"), Text("🇨🇿")}) {
    auto shaped = layout(shaper, paragraph(text));
    assert((offsets(shaped.pages[0].lines[0]) ==
            std::set<size_t>{0, text.size()}));
  }
  auto mixed = layout(shaper, paragraph("abc אבג 123"));
  const auto &bidi = mixed.pages[0].lines[0];
  std::vector<TextCaret> seams;
  for (const auto &c : text_line_carets(bidi))
    if (c.offset == 4)
      seams.push_back(c);
  assert(seams.size() == 2 && seams[0].rect.x != seams[1].rect.x);
  assert(seams[0].affinity != seams[1].affinity &&
         seams[0].rtl != seams[1].rtl);
  auto rtl = paragraph("אבג", "Noto Sans Hebrew");
  rtl.direction = TextParagraph::RTL;
  auto hebrew = layout(shaper, rtl);
  auto carets = text_line_carets(hebrew.pages[0].lines[0]);
  assert(carets.front().offset == 3 && carets.back().offset == 0);

  auto empty = layout(shaper, paragraph("\n\n"));
  assert(empty.pages[0].lines.size() == 3);
  for (size_t i = 0; i < 3; ++i) {
    const auto &blank = empty.pages[0].lines[i];
    auto hit =
        text_caret_at(empty.pages[0], blank.x + 10, blank.y + blank.height / 2);
    assert(hit && hit->line == i && hit->caret.offset == i);
  }
  auto blank_tabs = layout(shaper, paragraph("\t\n\nx"));
  assert(offsets(blank_tabs.pages[0].lines[1]) == std::set<size_t>{2});
  auto wrapped =
      layout(shaper, paragraph("one two three four five", "Hack"), 100);
  for (const auto &wrapped_line : wrapped.pages[0].lines) {
    auto stops = offsets(wrapped_line);
    assert(*stops.begin() == wrapped_line.begin &&
           *stops.rbegin() == wrapped_line.end);
    glyph_positions(wrapped_line);
  }
  auto justified =
      paragraph("one two three four five six seven eight nine ten", "Hack");
  justified.alignment = TextParagraph::Justify;
  auto expanded = layout(shaper, justified, 180);
  assert(expanded.pages[0].lines.size() > 1);
  close(expanded.pages[0].lines[0].width, 160);
  for (const auto &l : expanded.pages[0].lines)
    glyph_positions(l);
  auto tabbed = paragraph("ab\tcd\tef", "Hack");
  auto tabs = layout(shaper, tabbed);
  const auto &tabline = tabs.pages[0].lines[0];
  glyph_positions(tabline);
  assert(tabline.tabs.size() == 2);
  for (const auto &tab : tabline.tabs) {
    auto before = text_caret_at(tabs.pages[0], tabline.x + tab.x1 + .01,
                                tabline.y + tabline.height / 2);
    auto after = text_caret_at(tabs.pages[0], tabline.x + tab.x2 - .01,
                               tabline.y + tabline.height / 2);
    assert(before && before->caret.offset == tab.source);
    assert(after && after->caret.offset == tab.source + 1);
  }
  for (auto direction : {TextParagraph::LTR, TextParagraph::RTL})
    for (auto type : {TextTabStop::Left, TextTabStop::Right,
                      TextTabStop::Center, TextTabStop::Character}) {
      auto p = paragraph(
          "ab\tone two 12.34 three four five six seven eight nine ten", "Hack");
      p.font.size = 10;
      p.direction = direction;
      p.alignment = TextParagraph::Justify;
      TextTabStop stop;
      stop.position = 70;
      stop.type = type;
      p.tabs.push_back(stop);
      auto aligned = layout(shaper, p, 220);
      for (const auto &l : aligned.pages[0].lines) {
        glyph_positions(l);
        assert(!text_line_carets(l).empty());
        for (const auto &tab : l.tabs) {
          auto c = std::find_if(
              l.shaped->clusters.begin(), l.shaped->clusters.end(),
              [&](const auto &c) { return c.begin == tab.source; });
          assert(c != l.shaped->clusters.end());
          close(c->x, tab.x1);
          close(c->metrics.width, tab.x2 - tab.x1);
        }
      }
    }
  // Repeated instances retain original paragraph/scalar identity and local
  // page coordinates. Clipped text never produces a caret outside its cell.
  TextPage repeated;
  auto first = line;
  first.paragraph = 7;
  first.clip = TextRect{first.x, first.y, 120, first.height};
  repeated.cells.push_back({0, 0, 0, 1, 1, *first.clip});
  repeated.lines.push_back(first);
  auto second = first;
  second.y += 100;
  second.baseline += 100;
  second.clip->y += 100;
  repeated.cells.push_back({0, 1, 0, 1, 1, *second.clip});
  repeated.lines.push_back(second);
  for (size_t i = 0; i < 2; ++i) {
    const auto &l = repeated.lines[i];
    auto hit = text_caret_at(repeated, l.x, l.y + 1);
    assert(hit && hit->line == i && hit->caret.paragraph == 7 &&
           hit->caret.offset == 0);
    close(hit->caret.rect.y, l.baseline - l.shaped->metrics.ascent);
  }
  assert(!text_caret_at(repeated, 200, first.y + 1));
  std::cout << "PASS native ligature/grapheme/bidi caret positions, wrapping, "
               "blank lines, "
               "tabs, justification and repeated/clipped source hit testing\n";
}
