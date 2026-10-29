// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_layout.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
TextMetrics metric(const Text &s, const TextFont &f) {
  return {s.size() * f.size / 2, f.size * .8, f.size * .2};
}
TextParagraph paragraph(const Text &s) {
  TextParagraph p;
  p.text = s;
  p.font.size = 10;
  p.line_factor = 1;
  p.orphans = p.widows = 1;
  return p;
}
TextFlow base() {
  TextFlow f;
  TextPageStyle page;
  page.width = 200;
  page.height = 75;
  page.top = page.bottom = page.left = page.right = 5;
  page.next = "Page";
  f.masters["Page"] = page;
  f.first_master = "Page";
  f.paragraphs = {paragraph("1\n2\n3\n4"), paragraph("Heading"),
                  paragraph("body"), paragraph("after\nafter2")};
  f.paragraphs[3].keep_together = true;
  TextTable t;
  t.columns.resize(1);
  t.rows.resize(1);
  t.rows[0].height = 20;
  TextTableCell c;
  c.blocks = {{TextBlock::Paragraph, 2}};
  t.cells.push_back(c);
  f.tables.push_back(t);
  return f;
}
size_t page_of(const TextPagination &pages, size_t paragraph,
               bool last = false) {
  size_t found = size_t(-1);
  for (size_t p = 0; p < pages.pages.size(); ++p)
    for (const auto &line : pages.pages[p].lines)
      if (line.paragraph == paragraph) {
        if (!last)
          return p;
        found = p;
      }
  assert(found != size_t(-1));
  return found;
}
void coverage(const TextFlow &flow, const TextPagination &pages,
              size_t source) {
  size_t end = 0, count = 0;
  for (const auto &page : pages.pages)
    for (const auto &line : page.lines)
      if (line.paragraph == source) {
        assert(line.begin == end);
        end = line.end;
        ++count;
      }
  assert(count && end == flow.paragraphs[source].text.size());
}
bool warned(const TextPagination &p, const Text &part) {
  return std::any_of(p.unsupported.begin(), p.unsupported.end(),
                     [&](const Text &s) { return s.contains(part); });
}
void check(const TextFlow &flow, const TextPagination &p,
           std::initializer_list<size_t> sources) {
  assert(p.pages.size() < 30);
  for (auto source : sources)
    coverage(flow, p, source);
  for (const auto &page : p.pages)
    for (const auto &line : page.lines)
      assert(line.y >= page.style.top - 1e-7 &&
             line.y + line.height <=
                 page.style.height - page.style.bottom + 1e-7);
}
} // namespace
int main() {
  auto f = base();
  f.paragraphs[1].keep_next = true;
  f.blocks = {{TextBlock::Paragraph, 0},
              {TextBlock::Paragraph, 1},
              {TextBlock::Table, 0},
              {TextBlock::Paragraph, 3}};
  auto p = paginate_text(f, metric);
  assert(p.pages.size() == 2 && p.pages[0].cells.empty());
  assert(page_of(p, 1) == page_of(p, 2) && p.unsupported.empty());
  check(f, p, {0, 1, 2, 3});
  f = base();
  f.tables[0].keep_next = true;
  f.blocks = {{TextBlock::Paragraph, 0},
              {TextBlock::Table, 0},
              {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(page_of(p, 2) == page_of(p, 3) && page_of(p, 2) == 1 &&
         p.unsupported.empty());
  check(f, p, {0, 2, 3});
  // A chain can cross both boundaries and hidden tables without losing links.
  f = base();
  f.paragraphs[1].keep_next = true;
  f.tables[0].keep_next = true;
  TextTable hidden = f.tables[0];
  hidden.visible = false;
  f.tables.push_back(hidden);
  f.blocks = {{TextBlock::Paragraph, 0},
              {TextBlock::Paragraph, 1},
              {TextBlock::Table, 1},
              {TextBlock::Table, 0},
              {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(page_of(p, 1) == page_of(p, 2) && page_of(p, 2) == page_of(p, 3));
  check(f, p, {0, 1, 2, 3});
  // Long tables keep only their trailing fragment with the following prefix.
  f = base();
  f.tables[0].keep_next = true;
  f.tables[0].rows[0].height.reset();
  f.paragraphs[2].text = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(page_of(p, 2, true) == page_of(p, 3) && p.unsupported.empty());
  check(f, p, {2, 3});
  // Exact-height blank continuations retain the full dimension before a
  // caption.
  f = base();
  f.tables[0].keep_next = true;
  f.tables[0].rows[0].height = 120;
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(page_of(p, 3) == p.pages.size() - 1);
  assert(!p.pages.back().cells.empty() && p.unsupported.empty());
  double height = 0;
  for (const auto &page : p.pages)
    for (const auto &cell : page.cells)
      height += cell.rect.height;
  assert(std::abs(height - 120) < 1e-7);
  check(f, p, {2, 3});
  // The same planner applies within a source cell, including nested tables.
  for (int kind = 0; kind < 3; ++kind) {
    f = base();
    auto inner = f.tables[0];
    f.tables[0].rows[0].height.reset();
    if (kind == 0) {
      f.paragraphs[1].keep_next = true;
      f.tables[0].cells[0].blocks = {{TextBlock::Paragraph, 0},
                                     {TextBlock::Paragraph, 1},
                                     {TextBlock::Paragraph, 3}};
    } else if (kind == 1) {
      f.paragraphs[1].keep_next = true;
      f.tables.push_back(inner);
      f.tables[0].cells[0].blocks = {{TextBlock::Paragraph, 0},
                                     {TextBlock::Paragraph, 1},
                                     {TextBlock::Table, 1}};
    } else {
      inner.keep_next = true;
      f.tables.push_back(inner);
      f.tables[0].cells[0].blocks = {{TextBlock::Paragraph, 0},
                                     {TextBlock::Table, 1},
                                     {TextBlock::Paragraph, 3}};
    }
    f.blocks = {{TextBlock::Table, 0}};
    p = paginate_text(f, metric);
    assert(p.unsupported.empty());
    if (kind == 0) {
      assert(page_of(p, 1) == page_of(p, 3));
      check(f, p, {0, 1, 3});
    }
    if (kind == 1) {
      assert(page_of(p, 1) == page_of(p, 2));
      check(f, p, {0, 1, 2});
    }
    if (kind == 2) {
      assert(page_of(p, 2) == page_of(p, 3));
      check(f, p, {0, 2, 3});
    }
  }
  // A short cell must finish naturally while a different cell continues.
  // Reserving the caption in every cell would unnecessarily delay the short
  // paragraph or violate its keep/widow constraints.
  f = base();
  f.tables[0].keep_next = true;
  f.tables[0].rows[0].height.reset();
  f.tables[0].columns.resize(2);
  f.paragraphs[1].text = "short\nshort\nshort\nshort\nshort\nshort";
  f.paragraphs[1].keep_together = true;
  TextTableCell short_cell;
  short_cell.column = 1;
  short_cell.blocks = {{TextBlock::Paragraph, 1}};
  f.tables[0].cells.push_back(short_cell);
  f.paragraphs[2].text = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(p.unsupported.empty());
  assert(page_of(p, 1) == 0 && page_of(p, 1, true) == 0);
  assert(page_of(p, 2, true) == page_of(p, 3));
  check(f, p, {1, 2, 3});
  // A heading can join the first fragment of a long exact-height table.
  f = base();
  f.paragraphs[1].keep_next = true;
  f.tables[0].rows[0].height = 120;
  f.blocks = {{TextBlock::Paragraph, 0},
              {TextBlock::Paragraph, 1},
              {TextBlock::Table, 0}};
  p = paginate_text(f, metric);
  assert(page_of(p, 1) == page_of(p, 2) && p.unsupported.empty());
  check(f, p, {0, 1, 2});
  // A long paragraph keeps its last lines with the following table's prefix.
  f = base();
  f.paragraphs[1].text = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
  f.paragraphs[1].keep_next = true;
  f.blocks = {{TextBlock::Paragraph, 1}, {TextBlock::Table, 0}};
  p = paginate_text(f, metric);
  assert(page_of(p, 1, true) == page_of(p, 2) && p.unsupported.empty());
  check(f, p, {1, 2});
  // A forced break in a sibling cell guarantees a later fragment: it must not
  // impose a virtual caption reservation on an already complete short cell.
  f = base();
  f.tables[0].keep_next = true;
  f.tables[0].rows[0].height.reset();
  f.tables[0].columns.resize(2);
  f.paragraphs[2].text = "0\n1\n2\n3\n4\n5";
  f.paragraphs[2].keep_together = true;
  f.paragraphs[1].text = "before";
  f.paragraphs[1].break_after = true;
  f.paragraphs.push_back(paragraph("next"));
  short_cell.column = 1;
  short_cell.blocks = {{TextBlock::Paragraph, 1}, {TextBlock::Paragraph, 4}};
  f.tables[0].cells.push_back(short_cell);
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(p.unsupported.empty());
  assert(page_of(p, 2, true) == 0 && page_of(p, 4) == page_of(p, 3));
  check(f, p, {1, 2, 3, 4});
  // Remaining-height probes traverse active nested tables, not the original
  // complete geometry, when choosing which cell controls the final fragment.
  f = base();
  auto nested = f.tables[0];
  nested.rows[0].height.reset();
  f.tables.push_back(nested);
  f.tables[0].rows[0].height.reset();
  f.tables[0].keep_next = true;
  f.tables[0].columns.resize(2);
  f.tables[0].cells[0].blocks = {{TextBlock::Table, 1}};
  f.paragraphs[2].text = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
  f.paragraphs[1].text = "0\n1\n2\n3\n4\n5";
  f.paragraphs[1].keep_together = true;
  short_cell.blocks = {{TextBlock::Paragraph, 1}};
  f.tables[0].cells.push_back(short_cell);
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(p.unsupported.empty());
  assert(page_of(p, 1, true) == 0 && page_of(p, 2, true) == page_of(p, 3));
  check(f, p, {1, 2, 3});
  // Explicit page breaks override a keep at the same boundary.
  f = base();
  f.tables[0].keep_next = true;
  f.paragraphs[3].break_before = true;
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(page_of(p, 2) < page_of(p, 3) && p.unsupported.empty());
  check(f, p, {2, 3});
  // An explicit master assignment starts a page even when its name matches
  // the current master. That forced boundary takes precedence over a keep.
  for (int kind = 0; kind < 3; ++kind) {
    f = base();
    if (kind == 0) {
      f.paragraphs[1].keep_next = true;
      f.tables[0].master = "Page";
      f.blocks = {{TextBlock::Paragraph, 1}, {TextBlock::Table, 0}};
    } else if (kind == 1) {
      f.tables[0].keep_next = true;
      f.paragraphs[3].master = "Page";
      f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
    } else {
      f.paragraphs[1].keep_next = true;
      f.paragraphs[3].master = "Page";
      f.blocks = {{TextBlock::Paragraph, 1}, {TextBlock::Paragraph, 3}};
    }
    p = paginate_text(f, metric);
    assert(p.pages.size() == 2 && p.unsupported.empty());
    if (kind == 0) {
      assert(page_of(p, 1) < page_of(p, 2));
      check(f, p, {1, 2});
    } else if (kind == 1) {
      assert(page_of(p, 2) < page_of(p, 3));
      check(f, p, {2, 3});
    } else {
      assert(page_of(p, 1) < page_of(p, 3));
      check(f, p, {1, 3});
    }
  }
  // Equal-strength constraints can be impossible; retain source and progress.
  f = base();
  f.tables[0].keep_next = true;
  f.tables[0].rows[0].height = 55;
  f.tables[0].rows[0].keep_together = true;
  f.blocks = {{TextBlock::Table, 0}, {TextBlock::Paragraph, 3}};
  p = paginate_text(f, metric);
  assert(warned(p, "conflicts with a row/table keep"));
  check(f, p, {2, 3});
  std::cout << "Native paragraph/table/cell keep boundaries, long "
               "continuations, dimensions and break precedence pass\n";
}
