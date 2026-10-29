// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_cursor.h"
#include "native_table_pagination.h"
#include "native_text_keep.h"
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
  NativeKeepPlanner keeps;
  size_t depth, block = 0, source = 0, line = 0, last_visible = size_t(-1);
  double width = 0, after = 0;
  bool started = false, page_has_text = false, blocked = false,
       nested_written = false;
  std::shared_ptr<const TextBoxLayout> layout;
  std::vector<double> heights;
  std::unique_ptr<NativeTablePaginator> nested;
  std::map<double, std::vector<double>> suffixes;
  size_t measurement_records = 0;
  void prepare(double w) {
    if (!layout || width != w) {
      layout = layouter.paragraph(blocks[block], w, source);
      width = w;
      line = 0;
      heights = {0};
      for (const auto &item : layout->lines)
        heights.push_back(heights.back() + item.height);
    }
  }
  double height_left(double w) {
    if (block == blocks.size())
      return 0;
    auto found = suffixes.find(w);
    if (found == suffixes.end()) {
      if (blocks.size() > 1000000 - measurement_records)
        throw std::runtime_error("Cell keep measurements exceed native limit");
      measurement_records += blocks.size();
      std::vector<double> values(blocks.size());
      size_t next = size_t(-1);
      for (size_t i = blocks.size(); i-- > 0;) {
        auto traits = text_block_traits(flow, blocks[i]);
        if (!traits.visible)
          continue;
        double h = blocks[i].kind == TextBlock::Paragraph
                       ? layouter.paragraph(blocks[i], w)->height
                       : layouter.table(blocks[i], w)->height;
        values[i] =
            h + (next == size_t(-1)
                     ? traits.after
                     : std::max(traits.after,
                                text_block_traits(flow, blocks[next]).before) +
                           values[next]);
        next = i;
      }
      found = suffixes.emplace(w, std::move(values)).first;
    }
    size_t current = block;
    while (current < blocks.size() &&
           !text_block_traits(flow, blocks[current]).visible)
      ++current;
    if (current == blocks.size())
      return 0;
    const auto &b = blocks[current];
    auto traits = text_block_traits(flow, b);
    double full = b.kind == TextBlock::Paragraph
                      ? layouter.paragraph(b, w)->height
                      : layouter.table(b, w)->height;
    double remaining = full;
    if (current == block) {
      if (b.kind == TextBlock::Paragraph) {
        prepare(w);
        remaining = heights.back() - heights[line];
      } else if (nested)
        remaining = nested->height_left(w);
    }
    return remaining + found->second[current] - full +
           (current == block && started ? 0 : std::max(after, traits.before));
  }
  bool has_pending_break() const {
    std::optional<TextBlockTraits> previous;
    for (size_t i = block; i < blocks.size(); ++i) {
      const auto &b = blocks[i];
      auto traits = text_block_traits(flow, b);
      if (!traits.visible)
        continue;
      if (previous && (previous->break_after || traits.break_before))
        return true;
      if (b.kind == TextBlock::Table) {
        if (i == block && nested) {
          if (nested->has_pending_break())
            return true;
        } else if (text_blocks_force_break(flow, blocks.subspan(i, 1),
                                           depth + 1))
          return true;
      }
      previous = traits;
    }
    return false;
  }
  Impl(const TextFlow &f, std::span<const TextBlock> b, NativeBlockLayouter &l,
       size_t d)
      : flow(f), blocks(b), layouter(l), keeps(f, l), depth(d) {
    for (size_t i = blocks.size(); i-- > 0;)
      if (text_block_traits(flow, blocks[i]).visible) {
        last_visible = i;
        break;
      }
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
  TextCursorChunk fill(double w, double height, double capacity, bool force,
                       double reserve_after) {
    if (!std::isfinite(w) || w <= 0 || !std::isfinite(height) ||
        !std::isfinite(capacity) || capacity <= 0)
      throw std::runtime_error("Invalid cell cursor area");
    TextCursorChunk out;
    double y = 0;
    if (blocked)
      return out;
    while (block < blocks.size()) {
      const auto &b = blocks[block];
      auto keep = keeps.following(blocks, block, w, capacity);
      double extra =
          keep.linked ? keep.extra
                      : (block == last_visible && reserve_after > 0
                             ? reserve_after + text_block_traits(flow, b).after
                             : 0);
      if (!started && keep.whole_fits &&
          keep.whole + std::max(after, text_block_traits(flow, b).before) >
              height - y + 1e-7 &&
          !force)
        break;
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
        auto fragment = nested->next(
            w, height - y - gap, capacity, force && !page_has_text,
            !nested_written, extra, !started && keeps.preceding(blocks, block));
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
      if (!p.master.empty())
        note(out.display.unsupported, "Page master changes inside table cells");
      prepare(w);
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
      size_t kept_fit = fit;
      if (extra > 0) {
        if (p.keep_together && paragraph_height <= capacity &&
            paragraph_height + gap + extra > capacity)
          note(out.display.unsupported,
               "A keep-with-next boundary conflicts with keep-together");
        else
          kept_fit =
              fit_kept_tail(std::span<const double>(heights.data() + line,
                                                    heights.size() - line),
                            fit, height - y - gap,
                            std::max(.01, capacity - (!started ? p.before : 0)),
                            extra, p.widows, out.display.unsupported);
      }
      fit = kept_fit;
      if (fit < remaining) {
        if (fit && remaining - fit < size_t(p.widows))
          fit = remaining > size_t(p.widows) ? remaining - p.widows : 0;
        if (!started && fit < size_t(p.orphans))
          fit = 0;
      }
      if (!fit && force && !page_has_text && remaining) {
        fit = kept_fit ? kept_fit : raw_fit ? raw_fit : 1;
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
                                       double capacity, bool force,
                                       double reserve_after) {
  return impl_->fill(width, height, capacity, force, reserve_after);
}
double NativeTextCursor::height_left(double width) {
  return impl_->height_left(width);
}
bool NativeTextCursor::has_pending_break() const {
  return impl_->has_pending_break();
}
bool NativeTextCursor::finished() const {
  return impl_->block == impl_->blocks.size();
}
} // namespace DomeOffice
