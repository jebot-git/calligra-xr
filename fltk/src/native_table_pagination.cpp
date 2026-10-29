// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_pagination.h"
#include "native_table_stream.h"
#include "native_text_keep.h"
#include <cmath>
#include <limits>
namespace DomeOffice {
namespace {
void note(std::vector<Text> &values, const Text &value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}
void append(TextBoxLayout &to, TextBoxLayout from, double y) {
  if (from.lines.size() > 1000000 - to.lines.size() ||
      from.cells.size() > 1000000 - to.cells.size() ||
      from.paints.size() > 4000000 - to.paints.size())
    throw std::runtime_error("Paginated table display exceeds native limit");
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
struct NativeTablePaginator::Impl {
  const TextFlow &flow;
  TextBlock block;
  NativeBlockLayouter &layouter;
  size_t row = 0, depth = 0;
  struct Group {
    size_t begin = 0, end = 0;
    double y = 0, height = 0;
    TextBoxLayout display;
    std::vector<size_t> source_cells;
    bool page_breaks = false;
  };
  struct Prepared {
    std::shared_ptr<const NativeTableLayout> layout;
    std::vector<Group> groups;
    size_t header_groups = 0;
    double header_height = 0;
    std::vector<Text> unsupported;
  };
  std::map<double, Prepared> cache;
  size_t indexed = 0;
  std::unique_ptr<NativeTableStream> stream;
  Impl(const TextFlow &f, const TextBlock &b, NativeBlockLayouter &l, size_t d)
      : flow(f), block(b), layouter(l), depth(d) {
    if (depth >= 32)
      throw std::runtime_error("Excessive table pagination depth");
  }
  Prepared &prepare(double width) {
    if (auto found = cache.find(width); found != cache.end())
      return found->second;
    auto &model = block_table(flow, block);
    Prepared p;
    p.layout = layouter.table(block, width);
    for (const auto &warning : p.layout->display.unsupported)
      if (warning != "Page and keep settings in unlinked text frames" &&
          warning != "Page and keep settings in unlinked tables")
        note(p.unsupported, warning);
    auto rows = model.rows.size();
    if (rows > 6000000 - indexed)
      throw std::runtime_error("Paginated table index exceeds native limit");
    indexed += rows;
    std::vector<size_t> ends(rows);
    for (size_t r = 0; r < rows; ++r)
      ends[r] = r + 1;
    for (const auto &cell : model.cells)
      ends.at(cell.row) = std::max(ends[cell.row], cell.row + cell.rows);
    if (!model.may_break_rows && rows)
      ends[0] = rows;
    for (size_t r = 0; r < rows;) {
      size_t end = ends[r];
      for (size_t s = r + 1; s < end; ++s)
        end = std::max(end, ends[s]);
      double y = p.layout->row_edges[r];
      p.groups.push_back({r, end, y, p.layout->row_edges[end] - y, {}});
      r = end;
    }
    size_t visits = 0;
    std::map<const TextTable *, bool> break_cache;
    std::function<bool(std::span<const TextBlock>, size_t)> has_breaks =
        [&](std::span<const TextBlock> blocks, size_t depth) {
          if (depth >= 32)
            throw std::runtime_error("Excessive table cell block depth");
          bool found = false;
          for (const auto &b : blocks) {
            if (++visits > 1000000)
              throw std::runtime_error(
                  "Table cell break inspection exceeds limit");
            if (b.kind == TextBlock::Paragraph) {
              const auto &paragraph = block_paragraph(flow, b);
              found = found || paragraph.break_before || paragraph.break_after;
              if (!paragraph.master.empty())
                note(p.unsupported, "Page master changes inside table cells");
            } else {
              const auto &table = block_table(flow, b);
              auto entry = break_cache.find(&table);
              bool value = false;
              if (entry != break_cache.end())
                value = entry->second;
              else {
                value = table.break_before || table.break_after;
                for (const auto &cell : table.cells)
                  value = has_breaks(cell.blocks, depth + 1) || value;
                break_cache.emplace(&table, value);
              }
              found = found || value;
            }
          }
          return found;
        };
    for (size_t id = 0; id < model.cells.size(); ++id) {
      auto group = std::lower_bound(
          p.groups.begin(), p.groups.end(), model.cells[id].row,
          [](const Group &g, size_t r) { return g.end <= r; });
      if (group == p.groups.end())
        throw std::runtime_error("Native table cell has no row group");
      group->source_cells.push_back(id);
      group->page_breaks =
          has_breaks(model.cells[id].blocks, 0) || group->page_breaks;
    }
    for (const auto &group : p.groups) {
      if (group.end <= p.layout->header_rows) {
        ++p.header_groups;
        p.header_height += group.height;
      } else if (group.begin < p.layout->header_rows) {
        p.header_groups = 0;
        p.header_height = 0;
        if (model.may_break_rows)
          note(p.unsupported,
               "A vertical cell span crosses the repeating header boundary");
        break;
      }
    }
    // Distribute by geometric intervals once per width, rather than scanning
    // the complete table for each page. Vertical spans already unite groups;
    // only backgrounds/borders at shared boundaries need multiple references.
    auto distribute = [&](double low, double high, auto add) {
      if (high < low)
        return;
      auto first = std::lower_bound(
          p.groups.begin(), p.groups.end(), low,
          [](const Group &g, double y) { return g.y + g.height < y; });
      for (auto it = first; it != p.groups.end() && it->y <= high; ++it) {
        if (++indexed > 6000000)
          throw std::runtime_error(
              "Paginated table index exceeds native limit");
        add(*it);
      }
    };
    for (const auto &line : p.layout->display.lines) {
      auto interval =
          line.clip.value_or(TextRect{line.x, line.y, line.width, line.height});
      if (interval.height <= 0)
        continue;
      distribute(interval.y, interval.y + interval.height, [&](Group &g) {
        // A row group's edge is not content belonging to the adjacent row.
        if (interval.y + interval.height <= g.y || interval.y >= g.y + g.height)
          return;
        auto copy = line;
        TextRect band{-200000, g.y, 400000, g.height};
        copy.clip = copy.clip ? intersect_text_rect(*copy.clip, band) : band;
        g.display.lines.push_back(std::move(copy));
      });
    }
    for (const auto &paint : p.layout->display.paints) {
      auto interval = paint.clip ? intersect_text_rect(paint.rect, *paint.clip)
                                 : paint.rect;
      if (interval.height <= 0)
        continue;
      distribute(interval.y, interval.y + interval.height, [&](Group &g) {
        auto copy = paint;
        TextRect band{-200000, g.y, 400000, g.height};
        copy.clip = copy.clip ? intersect_text_rect(*copy.clip, band) : band;
        g.display.paints.push_back(std::move(copy));
      });
    }
    for (const auto &cell : p.layout->display.cells)
      distribute(cell.rect.y, cell.rect.y + cell.rect.height, [&](Group &g) {
        if (cell.rect.y + cell.rect.height <= g.y ||
            cell.rect.y >= g.y + g.height)
          return;
        auto copy = cell;
        copy.rect =
            intersect_text_rect(copy.rect, {-200000, g.y, 400000, g.height});
        g.display.cells.push_back(std::move(copy));
      });
    for (auto &g : p.groups) {
      offset_text_layout(g.display, 0, -g.y);
      g.display.height = g.height;
    }
    return cache.emplace(width, std::move(p)).first->second;
  }
  double height_left(double width) {
    auto &p = prepare(width);
    size_t after = stream ? stream->end_row() : row;
    double height = p.layout->height - p.layout->row_edges.at(after);
    if (stream)
      height += stream->height_left(width);
    if (row && row >= p.layout->header_rows && p.header_groups)
      height += p.header_height;
    return height;
  }
  bool has_pending_break() const {
    const auto &model = block_table(flow, block);
    if (stream && stream->has_pending_break())
      return true;
    size_t after = stream ? stream->end_row() : row;
    for (const auto &cell : model.cells) {
      if (cell.row < after)
        continue;
      size_t end = cell.row + cell.rows;
      while (end > cell.row && !model.rows[end - 1].visible)
        --end;
      if (end > cell.row && !model.rows[end - 1].height &&
          text_blocks_force_break(flow, cell.blocks, depth + 1))
        return true;
    }
    return false;
  }
  TablePageFragment next(double width, double available, double capacity,
                         bool empty, bool repeat_header, double reserve_after,
                         bool keep_before) {
    if (!std::isfinite(available) || !std::isfinite(capacity) || capacity <= 0)
      throw std::runtime_error("Invalid table page capacity");
    auto &p = prepare(width);
    TablePageFragment out;
    out.display.unsupported = p.unsupported;
    if (!std::isfinite(reserve_after) || reserve_after < 0)
      throw std::runtime_error("Invalid following keep extent");
    if (reserve_after >= capacity - 1e-7) {
      note(out.display.unsupported,
           "A table keep-with-next boundary cannot fit a fresh page");
      reserve_after = 0;
    }
    auto first =
        std::lower_bound(p.groups.begin(), p.groups.end(), row,
                         [](const Group &g, size_t r) { return g.begin < r; });
    if (first == p.groups.end()) {
      out.finished = true;
      return out;
    }
    bool repeat = repeat_header && (row || stream) && p.header_groups &&
                  first >= p.groups.begin() + p.header_groups;
    double header = repeat ? p.header_height : 0;
    if (header >= capacity - 1e-7 && repeat) {
      repeat = false;
      header = 0;
      note(out.display.unsupported,
           "Repeating table header leaves no room for body content");
    }
    // Fixed dimensions/explicit whole-table keeps move intact when possible.
    const auto &model = block_table(flow, block);
    bool fixed_group = !model.may_break_rows;
    if (first->end == first->begin + 1)
      fixed_group = fixed_group ||
                    model.rows[first->begin].height.has_value() ||
                    model.rows[first->begin].keep_together;
    if (!keep_before && !stream && !first->page_breaks && fixed_group &&
        first->height + header <= capacity + 1e-7 &&
        first->height + header > available + 1e-7 && !empty) {
      out.advance = true;
      return out;
    }
    if (!keep_before && !row && !stream && p.header_groups &&
        p.header_groups < p.groups.size()) {
      double required = p.header_height + p.groups[p.header_groups].height;
      if (required <= capacity + 1e-7 && required > available + 1e-7 &&
          !empty) {
        out.advance = true;
        return out;
      }
    }
    double y = 0;
    auto headers = [&] {
      for (size_t i = 0; i < p.header_groups; ++i) {
        auto display = p.groups[i].display;
        for (auto &cell : display.cells)
          cell.continued = true;
        append(out.display, std::move(display), y);
        y += p.groups[i].height;
      }
    };
    if (repeat)
      headers();
    bool only_initial_headers = !row;
    for (auto it = first; it != p.groups.end(); ++it) {
      double room = available - y;
      double tail = it + 1 == p.groups.end() ? reserve_after : 0;
      bool tail_needs_room = tail > 0 && it->height + tail > room + 1e-7;
      bool row_keep =
          it->end == it->begin + 1 && model.rows[it->begin].keep_together;
      bool explicit_keep = !model.may_break_rows || row_keep;
      if (tail_needs_room && explicit_keep &&
          it->height + tail + header > capacity + 1e-7 &&
          it->height + header <= capacity + 1e-7) {
        note(out.display.unsupported,
             "A table keep-with-next boundary conflicts with a row/table keep");
        tail = 0;
        tail_needs_room = false;
      }
      if (!stream && tail_needs_room &&
          it->height + tail + header <= capacity + 1e-7 &&
          (y > header + 1e-7 || !empty))
        break;
      if (!stream && !it->page_breaks && it->height <= room + 1e-7 &&
          !tail_needs_room) {
        append(out.display, it->display, y);
        y += it->height;
        row = it->end;
        only_initial_headers =
            only_initial_headers && row <= p.layout->header_rows;
        continue;
      }
      bool fixed = !model.may_break_rows;
      if (it->end == it->begin + 1)
        fixed = fixed || model.rows[it->begin].height.has_value() ||
                model.rows[it->begin].keep_together;
      if (!stream && !it->page_breaks && fixed && !tail_needs_room &&
          !keep_before && it->height <= capacity - header + 1e-7)
        break;
      if (!stream) {
        if (!model.may_break_rows)
          note(out.display.unsupported,
               "An oversized table cannot honor keep-between-rows");
        stream = std::make_unique<NativeTableStream>(
            flow, block, it->begin, it->end, it->source_cells, layouter, depth);
      }
      auto chunk = stream->next(width, std::max(0., room),
                                std::max(.01, capacity - header), false, tail,
                                keep_before || tail > 0);
      if (chunk.display.height <= 1e-7 && !chunk.finished) {
        if ((y > header + 1e-7 || !empty)) {
          if (only_initial_headers && !empty) {
            row = 0;
            stream.reset();
            out.display = {};
            out.advance = true;
          } else if (y <= 1e-7)
            out.advance = true;
          break;
        }
        if (repeat) {
          out.display = {};
          out.display.unsupported = p.unsupported;
          y = header = 0;
          repeat = false;
          note(out.display.unsupported,
               "Repeating table header leaves no room for the next cell line");
          chunk = stream->next(width, std::max(0., available), capacity, false,
                               tail, keep_before || tail > 0);
        }
        if (chunk.display.height <= 1e-7 && !chunk.finished)
          chunk = stream->next(width, std::max(0., available - y),
                               std::max(.01, capacity - header), true, tail,
                               keep_before || tail > 0);
        if (chunk.display.height <= 1e-7 && !chunk.finished)
          throw std::runtime_error(
              "Native table cell cursors did not progress");
      }
      double used = chunk.display.height;
      append(out.display, std::move(chunk.display), y);
      y += used;
      only_initial_headers = false;
      if (!chunk.finished)
        break;
      row = it->end;
      stream.reset();
    }
    out.display.height = y;
    out.finished = row == model.rows.size() && !stream;
    if (y <= 1e-7 && !out.finished && !empty)
      out.advance = true;
    return out;
  }
};
std::optional<size_t> text_paragraph_at(const TextPage &page, double x,
                                        double y) {
  if (!std::isfinite(x) || !std::isfinite(y))
    return {};
  auto contains = [&](const TextRect &r) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
  };
  const TextCellBox *cell = nullptr;
  for (const auto &candidate : page.cells)
    if (contains(candidate.rect) &&
        (!cell || candidate.rect.width * candidate.rect.height <
                      cell->rect.width * cell->rect.height))
      cell = &candidate;
  std::optional<size_t> source;
  double distance = std::numeric_limits<double>::infinity();
  for (const auto &line : page.lines) {
    if (!cell) {
      if (!line.clip && y >= line.y && y < line.y + line.height)
        return line.paragraph;
      continue;
    }
    // The smallest containing cell wins for nested tables. Its source lines
    // have a clip contained by that cell, unlike surrounding parent text.
    if (!line.clip || line.clip->width <= 0 || line.clip->height <= 0 ||
        line.clip->x < cell->rect.x - 1e-7 ||
        line.clip->y < cell->rect.y - 1e-7 ||
        line.clip->x + line.clip->width >
            cell->rect.x + cell->rect.width + 1e-7 ||
        line.clip->y + line.clip->height >
            cell->rect.y + cell->rect.height + 1e-7)
      continue;
    double top = std::max(line.y, line.clip->y);
    double bottom =
        std::min(line.y + line.height, line.clip->y + line.clip->height);
    if (bottom <= top)
      continue;
    double d = y < top ? top - y : y >= bottom ? y - bottom : 0;
    if (d < distance) {
      distance = d;
      source = line.paragraph;
    }
  }
  return source;
}
NativeTablePaginator::NativeTablePaginator(const TextFlow &flow,
                                           const TextBlock &block,
                                           NativeBlockLayouter &layouter,
                                           size_t depth)
    : impl_(std::make_shared<Impl>(flow, block, layouter, depth)) {}
double NativeTablePaginator::height_left(double width) {
  return impl_->height_left(width);
}
bool NativeTablePaginator::has_pending_break() const {
  return impl_->has_pending_break();
}
TablePageFragment NativeTablePaginator::next(double width, double available,
                                             double capacity, bool page_empty,
                                             bool repeat_header,
                                             double reserve_after,
                                             bool keep_before) {
  return impl_->next(width, available, capacity, page_empty, repeat_header,
                     reserve_after, keep_before);
}
} // namespace DomeOffice
