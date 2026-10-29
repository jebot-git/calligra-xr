// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_cursor.h"
#include "native_table_pagination.h"
#include <cmath>
namespace DomeOffice {
namespace {
void note(std::vector<Text> &values, const Text &value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}
void append(TextBoxLayout &to, TextBoxLayout from, double y) {
  if (from.lines.size() + to.lines.size() > 1000000 ||
      from.cells.size() + to.cells.size() > 1000000 ||
      from.paints.size() + to.paints.size() > 4000000)
    throw std::runtime_error("Native cell cursor display exceeds limit");
  offset_text_layout(from, 0, y);
  to.lines.insert(to.lines.end(), std::make_move_iterator(from.lines.begin()),
                  std::make_move_iterator(from.lines.end()));
  to.cells.insert(to.cells.end(), std::make_move_iterator(from.cells.begin()),
                  std::make_move_iterator(from.cells.end()));
  to.paints.insert(to.paints.end(),
                   std::make_move_iterator(from.paints.begin()),
                   std::make_move_iterator(from.paints.end()));
  for (const auto &warning : from.unsupported)
    note(to.unsupported, warning);
}
} // namespace
struct NativeTextCursor::Impl {
  const TextFlow &flow;
  std::span<const TextBlock> blocks;
  NativeBlockLayouter &layouter;
  size_t depth, block = 0, source = 0, line = 0;
  double width = 0, after = 0;
  bool started = false, page_has_text = false, blocked = false,
       nested_written = false;
  std::shared_ptr<const TextBoxLayout> layout;
  std::vector<double> heights;
  std::unique_ptr<NativeTablePaginator> nested;
  Impl(const TextFlow &f, std::span<const TextBlock> b, NativeBlockLayouter &l,
       size_t d)
      : flow(f), blocks(b), layouter(l), depth(d) {
    if (depth >= 32)
      throw std::runtime_error("Excessive native text cursor depth");
  }
  void next() {
    ++block;
    source = line = 0;
    started = false;
    layout.reset();
    nested.reset();
    nested_written = false;
  }
  TextCursorChunk fill(double w, double height, double capacity, bool force) {
    if (!std::isfinite(w) || w <= 0 || !std::isfinite(height) ||
        !std::isfinite(capacity) || capacity <= 0)
      throw std::runtime_error("Invalid cell cursor area");
    TextCursorChunk out;
    double y = 0;
    if (blocked)
      return out;
    while (block < blocks.size()) {
      const auto &b = blocks[block];
      if (b.kind == TextBlock::Table) {
        const auto &table = block_table(flow, b);
        if (!table.visible) {
          next();
          out.progressed = true;
          continue;
        }
        if (!nested) {
          if (table.break_before && page_has_text) {
            blocked = true;
            break;
          }
          nested = std::make_unique<NativeTablePaginator>(flow, b, layouter,
                                                          depth + 1);
        }
        double gap = started ? 0 : std::max(after, std::max(0., table.before));
        auto fragment = nested->next(w, height - y - gap, capacity,
                                     force && !page_has_text, !nested_written);
        for (const auto &warning : fragment.display.unsupported)
          note(out.display.unsupported, warning);
        if (fragment.advance)
          break;
        bool completed = fragment.finished;
        double used = fragment.display.height;
        append(out.display, std::move(fragment.display), y + gap);
        y += gap + used;
        out.progressed = true;
        page_has_text = true;
        nested_written = true;
        started = true;
        if (!completed)
          break;
        after = std::max(0., table.after);
        bool stop = table.break_after;
        next();
        if (stop) {
          blocked = true;
          break;
        }
        continue;
      }
      const auto &p = block_paragraph(flow, b);
      if (!started && p.break_before && page_has_text) {
        blocked = true;
        break;
      }
      if (p.keep_next)
        note(out.display.unsupported,
             "Keep-with-next between table cell blocks is not implemented");
      if (!p.master.empty())
        note(out.display.unsupported, "Page master changes inside table cells");
      if (!layout || width != w) {
        layout = layouter.paragraph(b, w, source);
        width = w;
        line = 0;
        heights = {0};
        for (const auto &item : layout->lines)
          heights.push_back(heights.back() + item.height);
      }
      for (const auto &warning : layout->unsupported)
        note(out.display.unsupported, warning);
      double gap = started ? 0 : std::max(after, std::max(0., p.before));
      size_t remaining = layout->lines.size() - line, fit = 0;
      double used = gap;
      while (fit < remaining &&
             y + used + layout->lines[line + fit].height <= height + 1e-7)
        used += layout->lines[line + fit++].height;
      size_t raw_fit = fit;
      double paragraph_height = heights.back() - heights[line];
      if (!started && p.keep_together &&
          paragraph_height + gap <= capacity + 1e-7 && fit < remaining)
        fit = 0;
      if (fit < remaining) {
        if (fit && remaining - fit < size_t(p.widows))
          fit = remaining > size_t(p.widows) ? remaining - p.widows : 0;
        if (!started && fit < size_t(p.orphans))
          fit = 0;
      }
      if (!fit && force && !page_has_text && remaining) {
        fit = raw_fit ? raw_fit : 1;
        note(out.display.unsupported,
             raw_fit ? "Unsatisfiable cell paragraph keep/widow/orphan rule"
                     : "A cell line or paragraph margin exceeds the page text "
                       "height");
      }
      if (!fit)
        break;
      y += gap;
      for (size_t i = 0; i < fit; ++i) {
        auto copy = layout->lines[line + i];
        double shift = y - copy.y;
        copy.y += shift;
        copy.baseline += shift;
        if (copy.clip)
          copy.clip->y += shift;
        out.display.lines.push_back(std::move(copy));
        y += layout->lines[line + i].height;
      }
      line += fit;
      started = true;
      page_has_text = true;
      out.progressed = true;
      if (line < layout->lines.size()) {
        source = layout->lines[line].begin;
        break;
      }
      after = std::max(0., p.after);
      bool stop = p.break_after;
      next();
      if (stop) {
        blocked = true;
        break;
      }
    }
    if (block == blocks.size()) {
      y += std::min(std::max(0., height - y), after);
      after = 0;
      out.finished = true;
    }
    out.display.height = y;
    return out;
  }
};
NativeTextCursor::NativeTextCursor(const TextFlow &flow,
                                   std::span<const TextBlock> blocks,
                                   NativeBlockLayouter &layouter, size_t depth)
    : impl_(std::make_shared<Impl>(flow, blocks, layouter, depth)) {}
void NativeTextCursor::begin_page() {
  impl_->page_has_text = impl_->blocked = impl_->nested_written = false;
  impl_->after = 0;
}
TextCursorChunk NativeTextCursor::fill(double width, double height,
                                       double capacity, bool force) {
  return impl_->fill(width, height, capacity, force);
}
bool NativeTextCursor::finished() const {
  return impl_->block == impl_->blocks.size();
}
} // namespace DomeOffice
