// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_editing.h"
#include "native_text_shaper.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
TextParagraph paragraph(const Text &text) {
  TextParagraph p;
  p.text = text;
  p.font.family = "Noto Sans";
  p.font.size = 20;
  return p;
}
TextPagination layout(const TextParagraph &p, NativeTextShaper &shaper) {
  TextFlow flow;
  flow.paragraphs.push_back(p);
  return paginate_text(flow, shaper);
}
void close(double a, double b) { assert(std::abs(a - b) < 1e-6); }
} // namespace
int main() {
  TextFlow flow;
  flow.paragraphs = {paragraph("á👩‍💻 b"), paragraph("Český office"),
                     paragraph("\n")};
  TextEditMap map;
  map.reset(flow);
  assert(map.text() == Text("á👩‍💻 b\nČeský office\n\n"));
  assert(map.global(1, 0) == 8 && map.global(2, 0) == 21);
  assert(map.global(0, 100) == 7 && map.paragraph_size(0) == 7);
  assert(map.previous_grapheme(2) == 0 && map.next_grapheme(0) == 2);
  assert(map.previous_grapheme(5) == 2 && map.next_grapheme(2) == 5);
  assert(map.next_grapheme(7) == 8 && map.previous_grapheme(8) == 7);
  assert(map.previous_word(13) == 8 && map.next_word(8) == 14);
  assert((map.word_at(9) == std::pair<size_t, size_t>{8, 13}));
  assert(map.next_grapheme(map.text().size()) == map.text().size());
  TextFlow empty;
  empty.paragraphs.emplace_back();
  map.reset(empty);
  assert(map.text().empty() && map.previous_grapheme(0) == 0 &&
         map.next_grapheme(0) == 0);
  assert((map.word_at(0) == std::pair<size_t, size_t>{0, 0}));

  NativeTextShaper shaper;
  auto pages = layout(paragraph("office"), shaper);
  auto line = pages.pages[0].lines[0];
  auto all = text_selection_rects(line, 0, 6);
  assert(all.size() == 1);
  close(all[0].x, line.x);
  close(all[0].width, line.shaped->metrics.width);
  auto first_f = text_selection_rects(line, 1, 2);
  assert(first_f.size() == 1 && std::abs(first_f[0].width - 6.30) < .003);
  auto one = text_selection_rects(line, 2, 3);
  assert(one.size() == 1 && one[0].width > 0 &&
         one[0].width < line.shaped->clusters[1].metrics.width / 2);
  auto ligature = text_selection_rects(line, 1, 4);
  assert(ligature.size() == 1);
  close(ligature[0].width, line.shaped->clusters[1].metrics.width);
  assert(text_selection_rects(line, 2, 2).empty());
  line.clip = TextRect{all[0].x + 3, line.y + 2, 10, line.height - 4};
  auto clipped = text_selection_rects(line, 0, 6);
  assert(clipped.size() == 1);
  close(clipped[0].x, line.clip->x);
  close(clipped[0].width, 10);
  close(clipped[0].y, line.clip->y);
  close(clipped[0].height, line.clip->height);

  auto bidi = layout(paragraph("abc אבג 123"), shaper);
  auto disjoint = text_selection_rects(bidi.pages[0].lines[0], 0, 5);
  assert(disjoint.size() == 2 &&
         disjoint[0].x + disjoint[0].width < disjoint[1].x);
  auto rtl = paragraph("אבג 123");
  rtl.direction = TextParagraph::RTL;
  auto rtl_pages = layout(rtl, shaper);
  const auto &rtl_line = rtl_pages.pages[0].lines[0];
  auto end_hit = text_caret_at(rtl_pages.pages[0], rtl_line.x, rtl_line.y + 1);
  assert(end_hit && end_hit->caret.offset == 7 && end_hit->caret.rtl);
  auto end_mark = text_selection_rects(rtl_line, 7, 8, true);
  assert(end_mark.size() == 1 && end_mark[0].x < rtl_line.x);
  close(end_mark[0].x + end_mark[0].width, rtl_line.x);
  auto tabs = layout(paragraph("ab\tcd"), shaper);
  const auto &tabline = tabs.pages[0].lines[0];
  auto tab = text_selection_rects(tabline, 2, 3);
  assert(tab.size() == 1 && tabline.tabs.size() == 1);
  close(tab[0].x, tabline.x + tabline.tabs[0].x1);
  close(tab[0].width, tabline.tabs[0].x2 - tabline.tabs[0].x1);
  auto blanks = layout(paragraph("\n"), shaper);
  assert(blanks.pages[0].lines.size() == 2);
  auto br = text_selection_rects(blanks.pages[0].lines[0], 0, 1);
  assert(br.size() == 1 && br[0].width > 0);
  auto paragraph_break =
      text_selection_rects(blanks.pages[0].lines[1], 1, 2, true);
  assert(paragraph_break.size() == 1 && paragraph_break[0].width > 0);
  std::cout << "PASS native scalar/grapheme/word edit map and ligature, bidi, "
               "tab, break and clipped selection geometry\n";
}
