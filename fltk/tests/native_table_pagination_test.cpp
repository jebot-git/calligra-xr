// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_pagination.h"
#include "native_text_shaper.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
TextMetrics metric(const Text &text, const TextFont &font) {
  return {text.size() * font.size / 2, font.size * .8, font.size * .2};
}
TextFlow document() {
  TextFlow flow;
  TextPageStyle page;
  page.width = 120;
  page.height = 75;
  page.left = page.right = page.top = page.bottom = 5;
  page.next = "Wide";
  flow.masters["Narrow"] = page;
  page.width = 220;
  page.next = "Wide";
  flow.masters["Wide"] = page;
  flow.first_master = "Narrow";
  for (size_t i = 0; i < 7; ++i) {
    TextParagraph p;
    p.text = Text::number(i);
    p.font.size = 10;
    p.line_factor = 1;
    flow.paragraphs.push_back(p);
  }
  TextTable table;
  table.columns.resize(1);
  table.rows.resize(5);
  for (size_t r = 0; r < 5; ++r) {
    table.rows[r].height = 20;
    table.rows[r].header = r == 0;
    TextTableCell cell;
    cell.row = r;
    cell.blocks.push_back({TextBlock::Paragraph, r + 1});
    cell.style.padding.fill(2);
    cell.style.background = TextColor{220, 230, 240};
    cell.style.borders.fill({TextTableBorder::Solid, 1, {}, {}});
    table.cells.push_back(cell);
  }
  flow.tables.push_back(table);
  flow.blocks = {{TextBlock::Paragraph, 0},
                 {TextBlock::Table, 0},
                 {TextBlock::Paragraph, 6}};
  return flow;
}
bool warned(const TextPagination &p, const Text &part) {
  return std::any_of(p.unsupported.begin(), p.unsupported.end(),
                     [&](const Text &s) { return s.contains(part); });
}
} // namespace
int main() {
  auto flow = document();
  auto pages = paginate_text(flow, metric);
  assert(pages.pages.size() == 3 && pages.unsupported.empty());
  assert(pages.pages[0].lines[0].paragraph == 0);
  assert(pages.pages[0].cells.size() == 2);
  assert(pages.pages[1].cells.size() == 3);
  assert(pages.pages[2].cells.size() == 2);
  near(pages.pages[0].cells[0].rect.width, 110);
  near(pages.pages[1].cells[0].rect.width, 210);
  near(pages.pages[1].cells[0].rect.y, 5);
  assert(pages.pages[1].cells[0].header && pages.pages[1].cells[0].continued);
  assert(pages.pages[1].lines[0].paragraph == 1);
  assert(pages.pages[2].lines.back().paragraph == 6);
  for (const auto &page : pages.pages) {
    for (const auto &line : page.lines)
      assert(line.y >= page.style.top &&
             line.y + line.height <= page.style.height - page.style.bottom);
    for (const auto &cell : page.cells)
      assert(cell.rect.y >= page.style.top &&
             cell.rect.y + cell.rect.height <=
                 page.style.height - page.style.bottom);
    assert(!page.paints.empty());
  }
  // Merged rows stay together and the repeating header moves with the first
  // body group instead of becoming a lone header at a page's foot.
  flow = document();
  flow.paragraphs[0].line_height = 30;
  auto &table = flow.tables[0];
  table.cells[1].rows = 2;
  table.cells.erase(table.cells.begin() + 2);
  pages = paginate_text(flow, metric);
  assert(pages.pages[0].cells.empty());
  assert(pages.pages[1].cells[1].rows == 2 && pages.pages[1].cells[1].row == 1);
  near(pages.pages[1].cells[1].rect.height, 40);
  // Source hidden paragraphs remain in the flow, never duplicated into pages.
  for (const auto &page : pages.pages)
    for (const auto &line : page.lines)
      assert(line.paragraph != 3);
  // Oversized row groups retain content and report the missing splitting path.
  flow = document();
  flow.tables[0].rows[1].height = 100;
  pages = paginate_text(flow, metric);
  assert(warned(pages, "row-group splitting"));
  assert(std::any_of(
      pages.pages.begin(), pages.pages.end(), [](const TextPage &p) {
        return std::any_of(p.cells.begin(), p.cells.end(),
                           [](const TextCellBox &c) {
                             return c.row == 1 && c.rect.height == 100;
                           });
      }));
  // Explicit breaks and a following master also work for empty-cell tables.
  flow = document();
  flow.tables[0].break_before = true;
  flow.tables[0].break_after = true;
  for (auto &cell : flow.tables[0].cells)
    cell.blocks.clear();
  pages = paginate_text(flow, metric);
  assert(pages.pages[0].cells.empty());
  assert(pages.pages.back().cells.empty());
  assert(pages.pages.back().lines.size() == 1 &&
         pages.pages.back().lines[0].paragraph == 6);
  // Keep-table mode suppresses row boundaries even when no cells span rows.
  flow = document();
  flow.tables[0].may_break_rows = false;
  flow.masters["Narrow"].height = flow.masters["Wide"].height = 130;
  flow.paragraphs[0].line_height = 30;
  pages = paginate_text(flow, metric);
  assert(pages.pages[0].cells.empty() && pages.pages[1].cells.size() == 5);
  // Selection distinguishes columns sharing the same vertical line range and
  // chooses a nested cell before its parent. Padding selects the nearest line.
  TextPage pick;
  pick.cells = {{0, 0, 0, 1, 1, {10, 10, 50, 40}, false, false},
                {0, 0, 1, 1, 1, {60, 10, 50, 40}, false, false}};
  TextLine a, b;
  a.paragraph = 3;
  a.y = b.y = 15;
  a.height = b.height = 10;
  a.clip = TextRect{12, 12, 46, 36};
  b.paragraph = 7;
  b.clip = TextRect{62, 12, 46, 36};
  pick.lines = {a, b};
  assert(text_paragraph_at(pick, 20, 20) == 3);
  assert(text_paragraph_at(pick, 90, 20) == 7);
  assert(text_paragraph_at(pick, 61, 11) == 7);
  assert(!text_paragraph_at(pick, 120, 20));
  pick.cells.push_back({1, 0, 0, 1, 1, {65, 30, 30, 15}, false, false});
  b.paragraph = 9;
  b.y = 31;
  b.clip = TextRect{66, 31, 28, 13};
  pick.lines.push_back(b);
  assert(text_paragraph_at(pick, 80, 34) == 9);
  assert(!text_paragraph_at(pick, NAN, 20));
  NativeTextShaper shaper("DejaVu Sans");
  auto shaped = paginate_text(document(), shaper);
  assert(shaped.pages.size() == 3);
  assert(shaped.pages[1].lines[0].shaped);
  std::cout << "Native table row groups, repeating headers, page-width reflow "
               "and source ordering pass\n";
}
