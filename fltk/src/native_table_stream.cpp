// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_stream.h"
#include "native_text_keep.h"
#include <cmath>
#include <numeric>
namespace DomeOffice {
namespace {
void note(std::vector<Text> &values, const Text &value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}
void append(TextBoxLayout &to, TextBoxLayout from, double x, double y) {
  if (from.lines.size() + to.lines.size() > 1000000 ||
      from.cells.size() + to.cells.size() > 1000000 ||
      from.paints.size() + to.paints.size() > 4000000)
    throw std::runtime_error("Streamed table display exceeds native limit");
  offset_text_layout(from, x, y);
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
struct NativeTableStream::Impl {
  const TextFlow &flow;
  TextBlock block;
  NativeBlockLayouter &layouter;
  size_t row, end, depth, pending = 0;
  struct SourceCell {
    size_t index = 0, begin = 0, end = 0;
  };
  std::vector<SourceCell> sources;
  struct Cell {
    SourceCell source;
    std::unique_ptr<NativeTextCursor> cursor;
    double empty_left = 0;
    bool emitted = false;
  };
  std::map<size_t, Cell> active;
  bool initialized = false;
  double minimum_left = 0;
  struct Frame {
    size_t source = 0, first = 0, last = 0;
    double top = 0, bottom = 0, left = 0, width = 0;
    std::array<double, 4> inset{};
    TextBoxLayout text;
    bool filled = false, continued = false, ended = false;
  };
  Impl(const TextFlow &f, const TextBlock &b, size_t begin, size_t e,
       std::vector<size_t> ids, NativeBlockLayouter &l, size_t d)
      : flow(f), block(b), layouter(l), row(begin), end(e), depth(d) {
    const auto &model = block_table(flow, block);
    for (auto id : ids) {
      const auto &cell = model.cells.at(id);
      size_t first = cell.row + cell.rows, last = cell.row;
      for (size_t r = cell.row; r < cell.row + cell.rows; ++r)
        if (model.rows[r].visible &&
            (!model.rows[r].height || *model.rows[r].height > 0)) {
          first = std::min(first, r);
          last = r + 1;
        }
      if (last > first)
        sources.push_back({id, first, last});
    }
    std::sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
      return std::pair{a.begin, a.index} < std::pair{b.begin, b.index};
    });
  }
  double height_left(double width) {
    const auto &model = block_table(flow, block);
    auto geometry = layouter.table(block, width);
    std::vector<double> heights(end - row);
    for (size_t r = row; r < end; ++r)
      if (model.rows[r].visible)
        heights[r - row] =
            r == row && initialized
                ? minimum_left
                : model.rows[r].height.value_or(model.rows[r].minimum);
    struct Constraint {
      size_t begin, end;
      double need;
    };
    std::vector<Constraint> constraints;
    for (const auto &source : sources) {
      if (source.end <= row)
        continue;
      const auto &g = geometry->cells[source.index];
      if (g.box.rect.width <= 0)
        continue;
      auto live = active.find(source.index);
      double body = live == active.end()
                        ? g.text->height
                        : live->second.empty_left +
                              live->second.cursor->height_left(
                                  std::max(.01, g.content_clip.width));
      double padding = g.box.rect.height - g.content_clip.height;
      size_t begin = std::max(row, source.begin);
      if (source.end == begin + 1 && !model.rows[begin].height)
        heights[begin - row] = std::max(heights[begin - row], body + padding);
      else if (source.end > begin + 1)
        constraints.push_back({begin, source.end, body + padding});
    }
    for (const auto &c : constraints) {
      double have = 0;
      size_t flexible = size_t(-1);
      for (size_t r = c.begin; r < c.end; ++r) {
        have += heights[r - row];
        if (model.rows[r].visible && !model.rows[r].height)
          flexible = r;
      }
      if (flexible != size_t(-1) && c.need > have)
        heights[flexible - row] += c.need - have;
    }
    return std::accumulate(heights.begin(), heights.end(), 0.);
  }
  bool has_pending_break() const {
    const auto &model = block_table(flow, block);
    for (const auto &source : sources) {
      if (source.end <= row || model.rows[source.end - 1].height)
        continue;
      auto live = active.find(source.index);
      if (live != active.end()) {
        if (live->second.cursor->has_pending_break())
          return true;
      } else if (text_blocks_force_break(flow, model.cells[source.index].blocks,
                                         depth + 1))
        return true;
    }
    return false;
  }
  TextCursorChunk next(double width, double height, double capacity, bool force,
                       double reserve_after, bool flexible_start) {
    TextCursorChunk out;
    if (!std::isfinite(height) || !std::isfinite(capacity) || capacity <= 0)
      throw std::runtime_error("Invalid streamed table page area");
    const auto &model = block_table(flow, block);
    auto geometry = layouter.table(block, width);
    std::map<size_t, Frame> frames;
    std::vector<std::pair<size_t, double>> rows;
    for (auto &[id, cell] : active)
      cell.cursor->begin_page();
    double y = 0;
    auto frame = [&](size_t id, Cell &cell) -> Frame & {
      auto [it, inserted] = frames.try_emplace(id);
      if (inserted) {
        auto &value = it->second;
        const auto &g = geometry->cells.at(id);
        value.source = id;
        value.first = value.last = rows.size();
        value.top = value.bottom = y;
        value.left = g.box.rect.x;
        value.width = g.box.rect.width;
        value.inset = {g.content_clip.y - g.box.rect.y,
                       g.box.rect.x + g.box.rect.width - g.content_clip.x -
                           g.content_clip.width,
                       g.box.rect.y + g.box.rect.height - g.content_clip.y -
                           g.content_clip.height,
                       g.content_clip.x - g.box.rect.x};
        value.continued = cell.emitted;
      }
      return it->second;
    };
    std::map<size_t, double> ending_heights;
    double highest = 0, row_start = 0;
    bool forced_continuation = false;
    auto fill = [&](Cell &cell, Frame &f, double bottom, bool allow_force) {
      double room = std::max(0., bottom - f.top - f.inset[0] - f.inset[2]);
      if (cell.empty_left > 0) {
        double used = std::min(cell.empty_left, room);
        f.text.height += used;
        cell.empty_left -= used;
      } else if (!cell.cursor->finished()) {
        auto chunk = cell.cursor->fill(
            std::max(.01, f.width - f.inset[1] - f.inset[3]), room,
            std::max(.01, capacity - f.inset[0] - f.inset[2]), allow_force,
            row + 1 == end && end == model.rows.size() &&
                    !model.rows[row].height && !forced_continuation &&
                    highest <= height + 1e-7 &&
                    ending_heights.contains(cell.source.index) &&
                    ending_heights.at(cell.source.index) >= highest - 1e-7 &&
                    minimum_left <= std::max(0., highest - row_start) + 1e-7
                ? reserve_after
                : 0);
        double used = chunk.display.height;
        append(f.text, std::move(chunk.display), 0, 0);
        f.text.height += used;
      }
      f.filled = true;
    };
    while (row < end) {
      const auto &style = model.rows.at(row);
      if (style.keep_together &&
          geometry->row_edges[row + 1] - geometry->row_edges[row] >
              capacity + 1e-7)
        note(out.display.unsupported,
             "An oversized table row cannot honor keep-together");
      if (!initialized) {
        minimum_left = style.visible ? style.height.value_or(style.minimum) : 0;
        initialized = true;
      }
      while (pending < sources.size() && sources[pending].begin <= row) {
        auto source = sources[pending++];
        const auto &g = geometry->cells.at(source.index);
        if (g.box.rect.width <= 0)
          continue;
        const auto &cell_model = model.cells.at(source.index);
        Cell cell{source,
                  std::make_unique<NativeTextCursor>(flow, cell_model.blocks,
                                                     layouter, depth + 1),
                  cell_model.blocks.empty() ? g.text->height : 0, false};
        active.emplace(source.index, std::move(cell));
      }
      double remaining = std::max(0., height - y);
      // Honor fixed/minimum dimensions on a fresh page whenever possible.
      if (!flexible_start && minimum_left > remaining + 1e-7 &&
          minimum_left <= capacity + 1e-7 &&
          (y > 0 || height < capacity - 1e-7))
        break;
      if (style.keep_together &&
          geometry->row_edges[row + 1] - geometry->row_edges[row] <=
              capacity + 1e-7 &&
          geometry->row_edges[row + 1] - geometry->row_edges[row] >
              remaining + 1e-7 &&
          (y > 0 || height < capacity - 1e-7))
        break;
      double min_room = remaining;
      if (row + 1 == end && end == model.rows.size() && reserve_after > 0 &&
          minimum_left <= remaining + 1e-7 &&
          minimum_left + reserve_after > remaining + 1e-7 &&
          !(style.keep_together &&
            geometry->row_edges[row + 1] - geometry->row_edges[row] <=
                capacity + 1e-7))
        min_room = std::max(0., remaining - reserve_after);
      double min_part = std::min(minimum_left, min_room);
      double limit = style.height ? y + min_part : height;
      double bottom = y + min_part;
      bool all_done = true, any_progress = false;
      ending_heights.clear();
      highest = 0;
      forced_continuation = false;
      row_start = y;
      if (reserve_after > 0 && row + 1 == end && end == model.rows.size() &&
          !style.height)
        for (auto &[id, cell] : active)
          if (cell.source.end == row + 1) {
            auto &f = frame(id, cell);
            double h = f.top + f.inset[0] + f.inset[2] + cell.empty_left +
                       cell.cursor->height_left(
                           std::max(.01, f.width - f.inset[1] - f.inset[3]));
            ending_heights[id] = h;
            highest = std::max(highest, h);
            forced_continuation =
                forced_continuation || cell.cursor->has_pending_break();
          }
      for (auto &[id, cell] : active) {
        auto &f = frame(id, cell);
        if (cell.source.end != row + 1)
          continue;
        // On the final exact-height fragment, the author's logical crop
        // takes precedence over unsatisfiable widow/keep constraints. Page
        // boundaries themselves never force a partial glyph line.
        fill(cell, f, limit, style.height && minimum_left <= remaining + 1e-7);
        any_progress = any_progress || f.text.height > 1e-7;
        bool done = cell.cursor->finished() && cell.empty_left <= 1e-7;
        all_done = all_done && done;
        if (!style.height)
          bottom =
              std::max(bottom, f.top + f.inset[0] + f.text.height + f.inset[2]);
      }
      if (!all_done && min_part <= 1e-7 && !any_progress) {
        if (!force || y > 0)
          break;
        for (auto &[id, cell] : active) {
          if (cell.source.end != row + 1)
            continue;
          auto &f = frames.at(id);
          fill(cell, f, limit, true);
          if (!style.height)
            bottom = std::max(bottom,
                              f.top + f.inset[0] + f.text.height + f.inset[2]);
        }
        all_done =
            std::all_of(active.begin(), active.end(), [&](const auto &entry) {
              return entry.second.source.end != row + 1 ||
                     (entry.second.cursor->finished() &&
                      entry.second.empty_left <= 1e-7);
            });
        bool content_progress =
            std::any_of(frames.begin(), frames.end(), [](const auto &entry) {
              return entry.second.filled && entry.second.text.height > 1e-7;
            });
        if (!all_done && !content_progress)
          throw std::runtime_error(
              "Cell padding leaves no room for source content on a fresh page");
      }
      if (!style.visible)
        bottom = y;
      if (!all_done && bottom <= y + 1e-7 && !style.height)
        break;
      double used = std::max(0., bottom - y);
      minimum_left = std::max(0., minimum_left - used);
      bool row_done = minimum_left <= 1e-7 && (style.height || all_done);
      rows.push_back({row, used});
      for (auto &[id, cell] : active) {
        auto &f = frame(id, cell);
        f.last = rows.size() - 1;
        f.bottom = bottom;
        if (row_done && cell.source.end == row + 1)
          f.ended = true;
      }
      y = bottom;
      out.progressed = out.progressed || used > 0 || row_done;
      if (!row_done)
        break;
      for (auto it = active.begin(); it != active.end();)
        if (it->second.source.end == row + 1)
          it = active.erase(it);
        else
          ++it;
      ++row;
      initialized = false;
      if (y >= height - 1e-7 && row < end)
        break;
    }
    // Spanning cells that have not ended do not determine row height. Fill
    // their established page area now, once, so nested table fragments and
    // glyph lines are not committed against successively growing bands.
    for (auto &[id, cell] : active) {
      auto found = frames.find(id);
      if (found == frames.end() || found->second.bottom <= found->second.top)
        continue;
      auto &f = found->second;
      if (!f.filled)
        fill(cell, f, f.bottom, false);
      cell.emitted = true;
    }
    if (rows.empty()) {
      out.finished = row == end;
      return out;
    }
    // Reuse the native table border/background resolver on a display-only
    // fragment grid. No source paragraphs, spans or XML are synthesized.
    TextFlow paint_flow;
    TextTable paint;
    paint.columns = model.columns;
    paint.width = model.width;
    paint.relative_width = model.relative_width;
    paint.left = model.left;
    paint.right = model.right;
    paint.alignment = model.alignment;
    paint.rtl = model.rtl;
    paint.collapsing = model.collapsing;
    paint.background = model.background;
    for (const auto &[source_row, h] : rows) {
      auto r = model.rows.at(source_row);
      r.height = h;
      r.minimum = 0;
      r.visible = true;
      paint.rows.push_back(r);
    }
    std::vector<size_t> visible;
    for (auto &[id, f] : frames) {
      if (f.bottom <= f.top || f.width <= 0)
        continue;
      const auto &original = model.cells.at(id);
      TextTableCell c;
      c.style = original.style;
      c.column = original.column;
      c.columns = original.columns;
      c.row = f.first;
      c.rows = f.last - f.first + 1;
      paint.cells.push_back(std::move(c));
      visible.push_back(id);
    }
    paint_flow.tables.push_back(std::move(paint));
    NativeBlockLayouter painter(
        paint_flow,
        [](const TextBlock &, double) -> std::shared_ptr<const TextBoxLayout> {
          throw std::runtime_error(
              "Display-only table unexpectedly requested source text");
        });
    auto decoration = painter.table({TextBlock::Table, 0}, width);
    out.display.paints = decoration->display.paints;
    for (auto id : visible) {
      auto &f = frames.at(id);
      const auto &original = model.cells.at(id);
      TextRect rect{f.left, f.top, f.width, f.bottom - f.top};
      out.display.cells.push_back(
          {block.index,
           original.row,
           original.column,
           original.rows,
           original.columns,
           rect,
           original.row + original.rows <= geometry->header_rows,
           f.continued,
           {}});
      TextRect clip{rect.x + f.inset[3], rect.y + f.inset[0],
                    std::max(0., rect.width - f.inset[1] - f.inset[3]),
                    std::max(0., rect.height - f.inset[0] - f.inset[2])};
      double extra = f.ended ? std::max(0., clip.height - f.text.height) : 0;
      double align = original.style.vertical == TextCellStyle::Middle
                         ? extra / 2
                     : original.style.vertical == TextCellStyle::Bottom ? extra
                                                                        : 0;
      out.display.cells.back().cell_context =
          enclose_text_cell(f.text, {block.index, original.row, original.column,
                                     original.rows, original.columns});
      offset_text_layout(f.text, clip.x, clip.y + align);
      clip_text_layout(f.text, clip);
      append(out.display, std::move(f.text), 0, 0);
    }
    clip_text_layout(out.display, {-200000, 0, 400000, y});
    out.display.height = y;
    out.finished = row == end;
    return out;
  }
};
NativeTableStream::NativeTableStream(const TextFlow &flow,
                                     const TextBlock &block, size_t begin,
                                     size_t end, std::vector<size_t> cells,
                                     NativeBlockLayouter &layouter,
                                     size_t depth)
    : impl_(std::make_shared<Impl>(flow, block, begin, end, std::move(cells),
                                   layouter, depth)) {}
double NativeTableStream::height_left(double width) {
  return impl_->height_left(width);
}
bool NativeTableStream::has_pending_break() const {
  return impl_->has_pending_break();
}
size_t NativeTableStream::end_row() const { return impl_->end; }
TextCursorChunk NativeTableStream::next(double width, double height,
                                        double capacity, bool force,
                                        double reserve_after,
                                        bool flexible_start) {
  return impl_->next(width, height, capacity, force, reserve_after,
                     flexible_start);
}
} // namespace DomeOffice
