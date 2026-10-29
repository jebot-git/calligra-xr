// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_keep.h"
#include <cmath>
namespace DomeOffice {
namespace {
void note(std::vector<Text> &values, const Text &value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}
} // namespace
TextBlockTraits text_block_traits(const TextFlow &flow,
                                  const TextBlock &block) {
  if (block.kind == TextBlock::Paragraph) {
    const auto &p = block_paragraph(flow, block);
    return {std::max(0., p.before), std::max(0., p.after), true,    p.keep_next,
            p.break_before,         p.break_after,         p.master};
  }
  const auto &t = block_table(flow, block);
  return {std::max(0., t.before), std::max(0., t.after), t.visible, t.keep_next,
          t.break_before,         t.break_after,         t.master};
}
bool text_blocks_force_break(const TextFlow &flow,
                             std::span<const TextBlock> blocks, size_t depth) {
  if (depth >= 32)
    throw std::runtime_error("Excessive forced-break inspection depth");
  std::optional<TextBlockTraits> previous;
  for (const auto &block : blocks) {
    auto traits = text_block_traits(flow, block);
    if (!traits.visible)
      continue;
    if (previous && (previous->break_after || traits.break_before))
      return true;
    if (block.kind == TextBlock::Table) {
      const auto &table = block_table(flow, block);
      for (const auto &cell : table.cells) {
        size_t end = cell.row + cell.rows;
        while (end > cell.row && !table.rows[end - 1].visible)
          --end;
        if (end > cell.row && !table.rows[end - 1].height &&
            text_blocks_force_break(flow, cell.blocks, depth + 1))
          return true;
      }
    }
    previous = traits;
  }
  return false;
}
struct NativeKeepPlanner::Impl {
  const TextFlow &flow;
  NativeBlockLayouter &layouter;
  size_t work = 0, depth = 0;
  struct Extent {
    double height = 0, prefix = 0;
    bool coherent = true;
  };
  struct Chain {
    double whole = 0, prefix = 0;
    bool one_page = true;
    size_t next = size_t(-1);
  };
  using ExtentKey = std::tuple<uintptr_t, bool, double, double>;
  using ChainKey = std::tuple<uintptr_t, size_t, size_t, double, double>;
  std::map<ExtentKey, Extent> extents;
  std::map<ChainKey, Chain> chains;
  std::map<const TextTable *, bool> break_cache;
  Impl(const TextFlow &f, NativeBlockLayouter &l) : flow(f), layouter(l) {}
  void step(size_t count = 1) {
    if (count > 10000000 - work)
      throw std::runtime_error("Native keep planning exceeds work limit");
    work += count;
  }
  bool linked(const TextBlockTraits &a, const TextBlockTraits &b) const {
    // Every explicit master assignment inserts a page break, including an
    // assignment of the current master. Forced breaks override keeps.
    return a.keep_next && !a.break_after && !b.break_before && b.master.empty();
  }
  bool has_breaks(const TextTable &table, size_t level = 0) {
    if (auto entry = break_cache.find(&table); entry != break_cache.end())
      return entry->second;
    if (level >= 32)
      throw std::runtime_error("Excessive keep inspection depth");
    bool value = false;
    for (const auto &cell : table.cells)
      for (const auto &block : cell.blocks) {
        step();
        auto traits = text_block_traits(flow, block);
        value = value || traits.break_before || traits.break_after ||
                !traits.master.empty();
        if (block.kind == TextBlock::Table)
          value = has_breaks(block_table(flow, block), level + 1) || value;
      }
    break_cache.emplace(&table, value);
    return value;
  }
  Extent extent(const TextBlock &block, double width, double capacity) {
    const void *identity =
        block.kind == TextBlock::Paragraph
            ? static_cast<const void *>(&block_paragraph(flow, block))
            : static_cast<const void *>(&block_table(flow, block));
    ExtentKey key{reinterpret_cast<uintptr_t>(identity),
                  block.kind == TextBlock::Table, width, capacity};
    if (auto entry = extents.find(key); entry != extents.end())
      return entry->second;
    step();
    if (++depth > 32)
      throw std::runtime_error("Excessive keep planning depth");
    struct Scope {
      size_t &n;
      ~Scope() { --n; }
    } scope{depth};
    Extent out;
    if (block.kind == TextBlock::Paragraph) {
      const auto &p = block_paragraph(flow, block);
      auto box = layouter.paragraph(block, width);
      out.height = box->height;
      if (p.keep_together && out.height <= capacity + 1e-7)
        out.prefix = out.height;
      else {
        size_t count =
            std::min(box->lines.size(), size_t(std::max(1, p.orphans)));
        if (box->lines.size() > count &&
            box->lines.size() - count < size_t(std::max(1, p.widows)))
          count = box->lines.size();
        for (size_t i = 0; i < count; ++i)
          out.prefix += box->lines[i].height;
      }
    } else {
      const auto &t = block_table(flow, block);
      if (!t.visible)
        return extents.emplace(key, out).first->second;
      auto box = layouter.table(block, width);
      out.height = box->height;
      // Cell break/master boundaries preclude treating the whole table as one
      // unbroken object; its first normal fragment still supplies a prefix.
      out.coherent = !has_breaks(t);
      if (!t.may_break_rows && out.coherent && out.height <= capacity + 1e-7)
        out.prefix = out.height;
      else {
        size_t header = box->header_rows < t.rows.size() ? box->header_rows : 0;
        double head = box->row_edges.at(header);
        double minimum = 0;
        std::vector<std::vector<size_t>> ending(t.rows.size());
        step(t.cells.size());
        for (size_t id = 0; id < t.cells.size(); ++id)
          ending.at(t.cells[id].row + t.cells[id].rows - 1).push_back(id);
        for (size_t r = header; r < t.rows.size(); ++r) {
          step();
          if (!t.rows[r].visible)
            continue;
          double row_min = t.rows[r].minimum;
          if (t.rows[r].keep_together &&
              box->row_edges[r + 1] - box->row_edges[r] <=
                  capacity - head + 1e-7)
            row_min =
                std::max(row_min, box->row_edges[r + 1] - box->row_edges[r]);
          // Exact/minimum dimensions can continue; only an explicit row keep
          // requires a complete row before a preceding block may join it.
          if (row_min > capacity - head)
            row_min = 1;
          minimum += row_min;
          for (size_t id : ending[r]) {
            const auto &cell = t.cells[id];
            const auto &g = box->cells[id];
            if (g.box.rect.width <= 0)
              continue;
            double top = g.content_clip.y - g.box.rect.y;
            double bottom = g.box.rect.y + g.box.rect.height -
                            g.content_clip.y - g.content_clip.height;
            double body =
                cell.blocks.empty()
                    ? g.text->height
                    : prefix(cell.blocks, std::max(.01, g.content_clip.width),
                             std::max(.01, capacity - head - top - bottom));
            minimum = std::max(minimum, top + body + bottom);
          }
          if (minimum > 1e-7)
            break;
        }
        out.prefix = head + minimum;
        if (out.prefix <= 1e-7 && out.height > 0)
          out.prefix = std::min(1., out.height);
      }
    }
    if (!std::isfinite(out.height) || !std::isfinite(out.prefix))
      throw std::runtime_error("Invalid native keep extent");
    return extents.emplace(key, out).first->second;
  }
  const Chain &plan(std::span<const TextBlock> blocks, size_t start,
                    double width, double capacity) {
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(capacity) ||
        capacity <= 0)
      throw std::runtime_error("Invalid keep planning area");
    auto key = [&](size_t index) {
      return ChainKey{reinterpret_cast<uintptr_t>(blocks.data()), blocks.size(),
                      index, width, capacity};
    };
    if (auto entry = chains.find(key(start)); entry != chains.end())
      return entry->second;
    auto next_visible = [&](size_t index) {
      while (++index < blocks.size()) {
        step();
        if (text_block_traits(flow, blocks[index]).visible)
          return index;
      }
      return size_t(-1);
    };
    std::vector<size_t> visits;
    size_t index = start;
    while (true) {
      if (chains.contains(key(index)))
        break;
      step();
      visits.push_back(index);
      size_t next = next_visible(index);
      if (next == size_t(-1) || !linked(text_block_traits(flow, blocks[index]),
                                        text_block_traits(flow, blocks[next])))
        break;
      index = next;
    }
    for (auto at = visits.rbegin(); at != visits.rend(); ++at) {
      size_t i = *at;
      auto measure = extent(blocks[i], width, capacity);
      auto a = text_block_traits(flow, blocks[i]);
      Chain value;
      value.whole = std::min(capacity + 1, measure.height);
      value.prefix = measure.prefix;
      value.one_page = measure.coherent && measure.height <= capacity + 1e-7;
      size_t next = next_visible(i);
      if (next != size_t(-1) &&
          linked(a, text_block_traits(flow, blocks[next]))) {
        const auto &child = chains.at(key(next));
        value.next = next;
        double gap =
            std::max(a.after, text_block_traits(flow, blocks[next]).before);
        value.whole =
            std::min(capacity + 1, measure.height + gap + child.whole);
        value.one_page =
            value.one_page && child.one_page && value.whole <= capacity + 1e-7;
        if (measure.coherent &&
            measure.height + gap + child.prefix <= capacity + 1e-7)
          value.prefix = measure.height + gap + child.prefix;
      }
      chains.emplace(key(i), value);
    }
    return chains.at(key(start));
  }
  double prefix(std::span<const TextBlock> blocks, double width,
                double capacity) {
    for (size_t i = 0; i < blocks.size(); ++i)
      if (text_block_traits(flow, blocks[i]).visible)
        return plan(blocks, i, width, capacity).prefix;
    return 0;
  }
};
NativeKeepPlanner::NativeKeepPlanner(const TextFlow &flow,
                                     NativeBlockLayouter &layouter)
    : impl_(std::make_shared<Impl>(flow, layouter)) {}
TextKeepLink NativeKeepPlanner::following(std::span<const TextBlock> blocks,
                                          size_t index, double width,
                                          double capacity) {
  const auto &a = text_block_traits(impl_->flow, blocks[index]);
  if (!a.visible || !a.keep_next || a.break_after)
    return {};
  const auto &current = impl_->plan(blocks, index, width, capacity);
  if (current.next == size_t(-1))
    return {};
  auto next = current.next;
  const auto &child = impl_->plan(blocks, next, width, capacity);
  double gap =
      std::max(a.after, text_block_traits(impl_->flow, blocks[next]).before);
  return {true, current.one_page, current.whole, gap + child.prefix};
}
bool NativeKeepPlanner::preceding(std::span<const TextBlock> blocks,
                                  size_t index) const {
  while (index) {
    const auto &previous = text_block_traits(impl_->flow, blocks[--index]);
    if (!previous.visible)
      continue;
    size_t next = index + 1;
    while (next < blocks.size() &&
           !text_block_traits(impl_->flow, blocks[next]).visible)
      ++next;
    return next < blocks.size() &&
           impl_->linked(previous,
                         text_block_traits(impl_->flow, blocks[next]));
  }
  return false;
}
double NativeKeepPlanner::prefix(std::span<const TextBlock> blocks,
                                 double width, double capacity) {
  return impl_->prefix(blocks, width, capacity);
}
size_t fit_kept_tail(std::span<const double> heights, size_t fit,
                     double available, double capacity, double extra,
                     int widows, std::vector<Text> &notes) {
  size_t count = heights.empty() ? 0 : heights.size() - 1;
  if (extra <= 0 || fit != count || !count)
    return fit;
  if (heights.back() - heights.front() + extra <= available + 1e-7)
    return fit;
  size_t tail = std::min(count, size_t(std::max(1, widows)));
  while (tail &&
         heights.back() - heights[count - tail] + extra > capacity + 1e-7)
    --tail;
  if (!tail) {
    note(notes, "A keep-with-next boundary cannot fit a fresh page");
    return fit;
  }
  if (tail < size_t(std::max(1, widows)))
    note(notes, "A keep-with-next boundary conflicts with widow/orphan rules");
  return count - tail;
}
} // namespace DomeOffice
