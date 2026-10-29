// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_pagination.h"
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
  size_t row = 0;
  struct Group {
    size_t begin = 0, end = 0;
    double y = 0, height = 0;
    TextBoxLayout display;
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
  Impl(const TextFlow &f, const TextBlock &b, NativeBlockLayouter &l)
      : flow(f), block(b), layouter(l) {}
  Prepared &prepare(double width) {
    if (auto found = cache.find(width); found != cache.end())
      return found->second;
    auto &model = block_table(flow, block);
    Prepared p;
    p.layout = layouter.table(block, width);
    p.unsupported = p.layout->display.unsupported;
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
  TablePageFragment next(double width, double available, double capacity,
                         bool empty) {
    if (!std::isfinite(available) || !std::isfinite(capacity) || capacity <= 0)
      throw std::runtime_error("Invalid table page capacity");
    auto &p = prepare(width);
    TablePageFragment out;
    out.display.unsupported = p.unsupported;
    auto first =
        std::lower_bound(p.groups.begin(), p.groups.end(), row,
                         [](const Group &g, size_t r) { return g.begin < r; });
    if (first == p.groups.end()) {
      out.finished = true;
      return out;
    }
    bool repeat =
        row && p.header_groups && first >= p.groups.begin() + p.header_groups;
    double header = repeat ? p.header_height : 0;
    if (header + first->height > capacity + 1e-7 && repeat) {
      repeat = false;
      header = 0;
      note(out.display.unsupported,
           "Repeating table header leaves no room for the next row group");
    }
    if (header + first->height > available + 1e-7 && !empty) {
      out.advance = true;
      return out;
    }
    if (!row && p.header_groups && p.header_groups < p.groups.size()) {
      double required = p.header_height + p.groups[p.header_groups].height;
      if (required <= capacity + 1e-7 && required > available + 1e-7 &&
          !empty) {
        out.advance = true;
        return out;
      }
      if (required > capacity + 1e-7)
        note(out.display.unsupported,
             "Table header cannot share a page with its first body group");
    }
    double y = 0;
    if (repeat)
      for (size_t i = 0; i < p.header_groups; ++i) {
        auto display = p.groups[i].display;
        for (auto &cell : display.cells)
          cell.continued = true;
        append(out.display, std::move(display), y);
        y += p.groups[i].height;
      }
    for (auto it = first; it != p.groups.end(); ++it) {
      if (it != first && y + it->height > available + 1e-7)
        break;
      if (y + it->height > available + 1e-7)
        note(out.display.unsupported,
             "Table row-group splitting is not implemented; an oversized group "
             "exceeds the page");
      append(out.display, it->display, y);
      y += it->height;
      row = it->end;
    }
    out.display.height = y;
    out.finished = row == block_table(flow, block).rows.size();
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
                                           NativeBlockLayouter &layouter)
    : impl_(std::make_shared<Impl>(flow, block, layouter)) {}
TablePageFragment NativeTablePaginator::next(double width, double available,
                                             double capacity, bool page_empty) {
  return impl_->next(width, available, capacity, page_empty);
}
} // namespace DomeOffice
