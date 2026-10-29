// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_layout.h"
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace DomeOffice;
namespace {
TextMetrics metric(const Text &s, const TextFont &f) {
  return {s.size() * f.size / 2, f.size * .8, f.size * .2};
}
TextParagraph paragraph(const Text &text) {
  TextParagraph p;
  p.text = text;
  p.font.size = 10;
  p.line_factor = 1;
  return p;
}
TextFlow document() {
  TextFlow f;
  TextPageStyle page;
  page.width = 150;
  page.height = 90;
  page.left = page.right = page.top = page.bottom = 5;
  page.next = "Wide";
  f.masters["Narrow"] = page;
  page.width = 250;
  f.masters["Wide"] = page;
  f.first_master = "Narrow";
  f.paragraphs = {paragraph("Left"), paragraph("Right"), paragraph(""),
                  paragraph("short")};
  for (int i = 0; i < 30; ++i)
    f.paragraphs[2].text += "alpha beta gamma ";
  TextTable table;
  table.columns.resize(2);
  table.rows.resize(2);
  table.rows[0].header = true;
  table.rows[0].height = 18;
  for (size_t i = 0; i < 4; ++i) {
    TextTableCell cell;
    cell.row = i / 2;
    cell.column = i % 2;
    cell.style.padding.fill(2);
    cell.style.background = TextColor{240, 225, 200};
    cell.style.borders.fill({TextTableBorder::Solid, 1, {}, {}});
    cell.blocks.push_back({TextBlock::Paragraph, i});
    table.cells.push_back(cell);
  }
  f.tables.push_back(table);
  f.blocks = {{TextBlock::Table, 0}};
  return f;
}
void coverage(const TextFlow &flow, const TextPagination &pages,
              size_t source) {
  size_t end = 0, count = 0;
  for (const auto &page : pages.pages)
    for (const auto &line : page.lines)
      if (line.paragraph == source) {
        if (line.begin != end)
          std::cerr << "source " << source << " begin " << line.begin
                    << " expected " << end << '\n';
        assert(line.begin == end && line.end >= line.begin);
        end = line.end;
        ++count;
      }
  assert(count && end == flow.paragraphs[source].text.size());
}
void bounds(const TextPagination &pages) {
  for (const auto &page : pages.pages) {
    for (const auto &cell : page.cells) {
      assert(cell.rect.y >= page.style.top - 1e-7);
      if (cell.rect.y + cell.rect.height >
          page.style.height - page.style.bottom + 1e-7)
        std::cerr << "cell overflow " << cell.rect.y << " + "
                  << cell.rect.height << '\n';
      assert(cell.rect.y + cell.rect.height <=
             page.style.height - page.style.bottom + 1e-7);
    }
    for (const auto &line : page.lines) {
      assert(line.y >= page.style.top - 1e-7 &&
             line.y + line.height <=
                 page.style.height - page.style.bottom + 1e-7);
      if (line.clip) {
        assert(line.y >= line.clip->y - 1e-7);
        assert(line.y + line.height <= line.clip->y + line.clip->height + 1e-7);
      }
    }
  }
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--inspect") {
    TextDocument document;
    document.load(Text(argv[2]));
    auto flow = document.flow();
    NativeTextShaper shaper("DejaVu Sans");
    auto pagination = paginate_text(flow, shaper);
    using Json = nlohmann::json;
    Json data = {{"paragraphs", Json::array()},
                 {"pages", Json::array()},
                 {"unsupported", Json::array()}};
    for (const auto &p : flow.paragraphs)
      data["paragraphs"].push_back(p.text.toStdString());
    for (const auto &warning : pagination.unsupported)
      data["unsupported"].push_back(warning.toStdString());
    for (const auto &page : pagination.pages) {
      Json p = {{"width", page.style.width}, {"height", page.style.height},
                {"top", page.style.top},     {"bottom", page.style.bottom},
                {"lines", Json::array()},    {"cells", Json::array()}};
      for (const auto &line : page.lines) {
        Json l = {{"source", line.paragraph},
                  {"begin", line.begin},
                  {"end", line.end},
                  {"x", line.x},
                  {"y", line.y},
                  {"height", line.height},
                  {"missing", line.shaped ? line.shaped->missing_glyphs : 0}};
        if (line.clip)
          l["clip"] = {line.clip->x, line.clip->y, line.clip->width,
                       line.clip->height};
        p["lines"].push_back(l);
      }
      for (const auto &cell : page.cells)
        p["cells"].push_back(
            {{"table", cell.table},
             {"row", cell.row},
             {"column", cell.column},
             {"rows", cell.rows},
             {"columns", cell.columns},
             {"rect",
              {cell.rect.x, cell.rect.y, cell.rect.width, cell.rect.height}},
             {"header", cell.header},
             {"continued", cell.continued}});
      data["pages"].push_back(p);
    }
    std::cout << data.dump() << '\n';
    return 0;
  }
  auto flow = document();
  auto pages = paginate_text(flow, metric);
  assert(pages.pages.size() >= 3 && pages.unsupported.empty());
  coverage(flow, pages, 2);
  coverage(flow, pages, 3);
  bounds(pages);
  size_t short_lines = 0, continuing = 0, headers = 0;
  for (const auto &page : pages.pages) {
    for (const auto &line : page.lines)
      if (line.paragraph == 3)
        ++short_lines;
    for (const auto &cell : page.cells) {
      continuing += cell.continued && !cell.header;
      headers += cell.header;
    }
  }
  assert(short_lines == 1 && continuing >= 2 &&
         headers == pages.pages.size() * 2);
  // Auto-row minimum height is consumed across fragments even after both
  // text cursors finish; source text is not re-emitted on blank continuations.
  flow = document();
  flow.paragraphs[2].text = "x";
  flow.paragraphs[3].text = "y";
  flow.tables[0].rows[1].minimum = 200;
  pages = paginate_text(flow, metric);
  double minimum_height = 0;
  for (const auto &page : pages.pages)
    for (const auto &cell : page.cells)
      if (!cell.header && cell.column == 0)
        minimum_height += cell.rect.height;
  assert(std::abs(minimum_height - 200) < 1e-7);
  coverage(flow, pages, 2);
  coverage(flow, pages, 3);
  bounds(pages);
  flow = document();
  flow.tables[0].rows[1].keep_together = true;
  pages = paginate_text(flow, metric);
  assert(std::any_of(
      pages.unsupported.begin(), pages.unsupported.end(),
      [](const Text &s) { return s.contains("cannot honor keep-together"); }));
  coverage(flow, pages, 2);
  bounds(pages);
  // Paragraph boundaries, trailing hard breaks and zero-length lines retain
  // source offsets when the next page changes its wrapping width.
  flow = document();
  flow.paragraphs[2].text = "first\n\n" + flow.paragraphs[2].text + "\n";
  flow.paragraphs.push_back(paragraph("tail"));
  flow.tables[0].cells[2].blocks.push_back({TextBlock::Paragraph, 4});
  pages = paginate_text(flow, metric);
  coverage(flow, pages, 2);
  coverage(flow, pages, 4);
  bounds(pages);
  // A merged cell advances independently from the cells ending in each row.
  flow = document();
  flow.tables[0].rows.push_back({});
  flow.tables[0].cells[2].rows = 2;
  flow.paragraphs[3].text = "right before";
  flow.paragraphs.push_back(paragraph("right after right after right after"));
  TextTableCell later = flow.tables[0].cells[3];
  later.row = 2;
  later.blocks = {{TextBlock::Paragraph, 4}};
  flow.tables[0].cells.push_back(later);
  pages = paginate_text(flow, metric);
  coverage(flow, pages, 2);
  coverage(flow, pages, 3);
  coverage(flow, pages, 4);
  bounds(pages);
  assert(std::any_of(
      pages.pages.back().cells.begin(), pages.pages.back().cells.end(),
      [](const TextCellBox &c) { return c.rows == 2 && c.continued; }));
  // Nested tables use the same source cursors and never slice child glyphs.
  flow = document();
  TextTable nested;
  nested.columns.resize(1);
  nested.rows.resize(1);
  TextTableCell child;
  child.blocks = {{TextBlock::Paragraph, 2}};
  child.style.padding.fill(1);
  nested.cells.push_back(child);
  flow.tables.push_back(nested);
  flow.tables[0].cells[2].blocks = {{TextBlock::Table, 1}};
  pages = paginate_text(flow, metric);
  coverage(flow, pages, 2);
  bounds(pages);
  assert(std::any_of(
      pages.pages[1].cells.begin(), pages.pages[1].cells.end(),
      [](const TextCellBox &c) { return c.table == 1 && c.continued; }));
  // An explicit break inside a short cell still creates a continuation.
  flow = document();
  flow.paragraphs[2] = paragraph("before");
  flow.paragraphs[2].break_after = true;
  flow.paragraphs.push_back(paragraph("after"));
  flow.tables[0].cells[2].blocks.push_back({TextBlock::Paragraph, 4});
  pages = paginate_text(flow, metric);
  assert(pages.pages.size() == 2 && pages.unsupported.empty());
  coverage(flow, pages, 2);
  coverage(flow, pages, 4);
  bounds(pages);
  // Port the retained TestTableLayout::testMergedRowsSpansAreas/Areas2
  // topology: three columns, adjacent pairs of merged rows, two original
  // paragraphs per merged cell, and small 200 x 90/110 root areas.
  for (size_t row_count : {size_t(6), size_t(10)}) {
    TextFlow original;
    TextPageStyle area;
    area.width = 200;
    area.height = row_count == 6 ? 90 : 110;
    area.left = area.right = area.top = area.bottom = 0;
    area.next = "Area";
    original.masters["Area"] = area;
    original.first_master = "Area";
    auto before = paragraph("");
    before.font.size = 12;
    before.line_height = 13.2;
    before.after = 6;
    original.paragraphs.push_back(before);
    for (size_t r = 0; r < row_count; ++r)
      for (size_t c = 0; c < 3; ++c) {
        auto p = paragraph("Cell " + Text::number(r) + ", " + Text::number(c));
        p.font.size = 12;
        p.line_height = 13.2;
        original.paragraphs.push_back(p);
      }
    TextTable merged;
    merged.rows.resize(row_count);
    merged.columns.resize(3);
    for (size_t r = 0; r < row_count; r += 2)
      for (size_t c = 0; c < 3; ++c) {
        TextTableCell cell;
        cell.row = r;
        cell.column = c;
        cell.rows = 2;
        cell.blocks = {{TextBlock::Paragraph, 1 + r * 3 + c},
                       {TextBlock::Paragraph, 1 + (r + 1) * 3 + c}};
        merged.cells.push_back(cell);
      }
    original.tables.push_back(merged);
    original.blocks = {{TextBlock::Paragraph, 0}, {TextBlock::Table, 0}};
    auto result = paginate_text(original, metric);
    if (row_count == 6)
      assert(result.pages.size() == 2);
    else
      assert(result.pages.size() <= 5);
    for (size_t source = 1; source < original.paragraphs.size(); ++source)
      coverage(original, result, source);
    bounds(result);
    assert(text_paragraph_at(result.pages[0], 10, 25));
    assert(!result.pages.back().cells.empty());
  }
  // Impossible vertical padding must fail immediately rather than emitting
  // thousands of decoration-only pages without advancing the source cursor.
  flow = document();
  flow.tables[0].cells[2].blocks.clear();
  flow.tables[0].cells[2].empty_format =
      std::make_shared<const TextParagraph>(paragraph(""));
  flow.tables[0].cells[2].style.padding[0] = 100;
  flow.tables[0].cells[2].style.padding[2] = 100;
  bool rejected = false;
  try {
    paginate_text(flow, metric);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("padding leaves no room") !=
               std::string::npos;
  }
  assert(rejected);
  // Real shaping retains global Unicode ranges and only one list label.
  flow = document();
  flow.paragraphs[2].text =
      "العربية עברית office e\u0301 " + flow.paragraphs[2].text;
  TextListLabel list;
  list.text = "1.";
  list.min_width = 14;
  flow.paragraphs[2].list = list;
  NativeTextShaper shaper("DejaVu Sans");
  pages = paginate_text(flow, shaper);
  coverage(flow, pages, 2);
  bounds(pages);
  size_t labels = 0;
  for (const auto &page : pages.pages)
    for (const auto &line : page.lines)
      if (line.paragraph == 2 && line.label)
        ++labels;
  assert(labels == 1);
  std::cout << "Independent cell source cursors, split/merged rows, headers, "
               "nested tables, width reflow and whole glyph lines pass\n";
}
