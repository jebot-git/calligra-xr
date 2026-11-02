// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_selection.h"
#include "native_table_layout.h"
#include <algorithm>
namespace DomeOffice {
WordsDisplayMap::Key
WordsDisplayMap::key(size_t paragraph,
                     const std::vector<TextCellAddress> &cells) {
  Key result{paragraph, {}};
  for (const auto &c : cells)
    result.second.push_back({c.table, c.row, c.column, c.rows, c.columns});
  return result;
}
WordsDisplayMap::WordsDisplayMap(const TextFlow &flow) {
  std::vector<size_t> starts;
  size_t total = 0;
  for (const auto &p : flow.paragraphs) {
    starts.push_back(total);
    total += p.text.size() + 1;
  }
  std::vector<TextCellAddress> path;
  auto walk = [&](auto &&self, const std::vector<TextBlock> &blocks,
                  size_t depth) -> void {
    if (depth > 16)
      throw std::runtime_error("Excessive selection table depth");
    for (const auto &block : blocks) {
      if (block.kind == TextBlock::Paragraph) {
        if (paragraphs_.size() >= 1000000)
          throw std::runtime_error(
              "Displayed selection exceeds native paragraph limit");
        auto k = key(block.index, path);
        if (!indices_.emplace(k, paragraphs_.size()).second)
          throw std::runtime_error("Ambiguous displayed paragraph identity");
        paragraphs_.push_back({block.index, starts.at(block.index),
                               flow.paragraphs.at(block.index).text.size(),
                               path});
      } else {
        const auto &table = block_table(flow, block);
        for (const auto &cell : table.cells) {
          bool row = false, column = false;
          for (size_t r = cell.row; r < cell.row + cell.rows; ++r)
            row = row || table.rows.at(r).visible;
          for (size_t c = cell.column; c < cell.column + cell.columns; ++c)
            column = column || table.columns.at(c).visible;
          if (!row || !column)
            continue;
          path.push_back(
              {block.index, cell.row, cell.column, cell.rows, cell.columns});
          self(self, cell.blocks, depth + 1);
          path.pop_back();
        }
      }
    }
  };
  walk(walk, flow.blocks, 0);
}
size_t WordsDisplayMap::locate(const WordsTextPoint &point) const {
  for (size_t i = 0; i < paragraphs_.size(); ++i) {
    const auto &p = paragraphs_[i];
    if (point.position >= p.begin && point.position <= p.begin + p.length &&
        point.cells == p.cells)
      return i;
  }
  throw std::runtime_error(
      "The displayed selection endpoint is stale or incomplete");
}
WordsDisplaySpan WordsDisplayMap::span(const WordsTextPoint &a,
                                       const WordsTextPoint &b) const {
  auto first = locate(a), last = locate(b);
  bool reverse = first > last || (first == last && a.position > b.position);
  return reverse ? WordsDisplaySpan{last, first, b.position, a.position, true}
                 : WordsDisplaySpan{first, last, a.position, b.position, false};
}
std::optional<std::pair<size_t, size_t>>
WordsDisplayMap::portion(size_t paragraph,
                         const std::vector<TextCellAddress> &path,
                         const WordsDisplaySpan &span) const {
  auto it = indices_.find(key(paragraph, path));
  if (it == indices_.end() || it->second < span.first || it->second > span.last)
    return {};
  const auto &p = paragraphs_.at(it->second);
  return std::pair(it->second == span.first ? span.begin : p.begin,
                   it->second == span.last ? span.end : p.begin + p.length + 1);
}
std::vector<WordsRangeSlice>
WordsDisplayMap::slices(const WordsDisplaySpan &span) const {
  std::vector<WordsRangeSlice> result;
  for (size_t i = span.first; i <= span.last; ++i) {
    const auto &p = paragraphs_.at(i);
    result.push_back(
        {p.paragraph, i == span.first ? span.begin : p.begin,
         i == span.last ? span.end : p.begin + p.length, p.cells,
         !(i == span.last && i > span.first && span.end == p.begin)});
  }
  return result;
}
} // namespace DomeOffice
