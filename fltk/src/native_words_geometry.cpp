// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_geometry.h"
#include <numeric>
#include <cmath>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
} // namespace
RefinedAxis::RefinedAxis(size_t count, size_t begin, size_t span,
                         size_t number) {
  auto common = std::gcd(span, number);
  factor = number / common;
  auto group = span / common;
  edges.push_back(0);
  for (size_t i = 0; i < count; ++i)
    edges.push_back(edges.back() +
                    (i >= begin && i < begin + span ? factor : 1));
  for (size_t i = 0; i <= number; ++i)
    parts.push_back(edges[begin] + i * group);
}
RefinedAxis::RefinedAxis(const std::vector<double> &source, size_t begin,
                         size_t span, size_t number) {
  std::vector<double> old{0};
  for (double value : source)
    old.push_back(old.back() + value);
  double total = old.at(begin + span) - old.at(begin);
  if (!std::isfinite(total) || total <= 1e-8)
    fail("The selected cell has no physical extent");
  auto tolerance = std::min(1e-7, total / number * 1e-5);
  std::vector<double> points{0};
  size_t cut = 1;
  edges.push_back(0);
  for (size_t i = 0; i < source.size(); ++i) {
    while (cut < number &&
           old[begin] + total * cut / number <= old[i] + tolerance)
      ++cut;
    while (cut < number &&
           old[begin] + total * cut / number < old[i + 1] - tolerance) {
      if (i >= begin && i < begin + span)
        points.push_back(old[begin] + total * cut / number);
      ++cut;
    }
    points.push_back(old[i + 1]);
    edges.push_back(points.size() - 1);
  }
  parts.push_back(edges[begin]);
  for (size_t i = 1; i < number; ++i) {
    double position = old[begin] + total * i / number;
    auto it = std::upper_bound(points.begin() + edges[begin],
                               points.begin() + edges[begin + span] + 1,
                               position + tolerance);
    if (it != points.begin() + edges[begin])
      --it;
    if (it == points.end() || std::abs(*it - position) > tolerance)
      fail("Cannot preserve the physical subdivision boundary");
    parts.push_back(size_t(it - points.begin()));
  }
  parts.push_back(edges[begin + span]);
  for (size_t i = 1; i < points.size(); ++i)
    lengths.push_back(points[i] - points[i - 1]);
}
void inspect_words_table_geometry(const TextFlow &flow,
                                  const std::vector<TextCellAddress> &path,
                                  double body_width, NativeTextShaper &shaper,
                                  const InspectTableGeometry &inspect) {
  NativeBlockLayouter layouter(flow, table_paragraph_layouter(flow, shaper));
  const std::vector<TextBlock> *blocks = &flow.blocks;
  for (size_t i = 0; i < path.size(); ++i) {
    auto found =
        std::find_if(blocks->begin(), blocks->end(), [&](const auto &b) {
          return b.kind == TextBlock::Table && b.index == path[i].table;
        });
    if (found == blocks->end())
      fail("The table geometry context is missing");
    auto layout = layouter.table(*found, body_width);
    const auto &table = block_table(flow, *found);
    if (i + 1 == path.size()) {
      inspect(layouter, *layout, table);
      return;
    }
    auto cell = std::find_if(
        layout->cells.begin(), layout->cells.end(), [&](const auto &c) {
          return c.box.row == path[i].row && c.box.column == path[i].column;
        });
    if (cell == layout->cells.end() || cell->content_clip.width <= .01)
      fail("The enclosing cell has no text area");
    auto model = std::find_if(
        table.cells.begin(), table.cells.end(), [&](const auto &c) {
          return c.row == path[i].row && c.column == path[i].column;
        });
    if (model == table.cells.end())
      fail("The enclosing cell source is missing");
    blocks = &model->blocks;
    body_width = cell->content_clip.width;
  }
}
WordsTableGeometry measure_words_table_geometry(
    const TextFlow &flow, const std::vector<TextCellAddress> &path,
    std::optional<double> body_width, NativeTextShaper &shaper) {
  WordsTableGeometry result;
  if (body_width)
    result.body_width = *body_width;
  else {
    const auto &style = flow.masters.at(flow.first_master);
    result.body_width = style.width - style.left - style.right;
    auto pages = paginate_text(flow, shaper);
    bool found = false;
    for (const auto &page : pages.pages) {
      for (const auto &cell : page.cells) {
        std::vector<TextCellAddress> context;
        for (auto p = cell.cell_context; p; p = p->nested)
          context.push_back(p->address);
        if (context == path) {
          result.body_width =
              page.style.width - page.style.left - page.style.right;
          found = true;
          break;
        }
      }
      if (found)
        break;
    }
  }
  if (!std::isfinite(result.body_width) || result.body_width <= .01 ||
      result.body_width > 100000)
    fail("Invalid page text width");
  inspect_words_table_geometry(
      flow, path, result.body_width, shaper,
      [&](auto &, const auto &layout, const auto &) {
        result.available_width = layout.available_width;
        for (size_t i = 1; i < layout.column_edges.size(); ++i)
          result.columns.push_back(layout.column_edges[i] -
                                   layout.column_edges[i - 1]);
        for (size_t i = 1; i < layout.row_edges.size(); ++i)
          result.rows.push_back(layout.row_edges[i] - layout.row_edges[i - 1]);
      });
  return result;
}
} // namespace DomeOffice
