// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_layout.h"
#include <numeric>
#include <set>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
void note(std::vector<Text> &notes, const Text &text) {
  if (std::find(notes.begin(), notes.end(), text) == notes.end())
    notes.push_back(text);
}
void shift(TextRect &r, double x, double y) {
  r.x += x;
  r.y += y;
}
double border_width(const TextTableBorder &b) {
  if (b.style == TextTableBorder::None)
    return 0;
  if (b.style == TextTableBorder::Double && b.double_widths)
    return std::accumulate(b.double_widths->begin(), b.double_widths->end(),
                           0.);
  return b.width;
}
void append(TextBoxLayout &to, TextBoxLayout from, double x, double y) {
  offset_text_layout(from, x, y);
  if (to.lines.size() + from.lines.size() > 1000000 ||
      to.paints.size() + from.paints.size() > 4000000 ||
      to.cells.size() + from.cells.size() > 1000000)
    fail("Expanded table display exceeds native limit");
  to.lines.insert(to.lines.end(), std::make_move_iterator(from.lines.begin()),
                  std::make_move_iterator(from.lines.end()));
  to.paints.insert(to.paints.end(),
                   std::make_move_iterator(from.paints.begin()),
                   std::make_move_iterator(from.paints.end()));
  to.cells.insert(to.cells.end(), std::make_move_iterator(from.cells.begin()),
                  std::make_move_iterator(from.cells.end()));
  for (const auto &warning : from.unsupported)
    note(to.unsupported, warning);
}
struct Edge {
  bool vertical = false;
  double position = 0, begin = 0, end = 0;
  TextTableBorder border;
  size_t side = 0, serial = 0;
};
int priority(const TextTableBorder &b) {
  return b.style == TextTableBorder::Double   ? 9
         : b.style == TextTableBorder::Solid  ? 8
         : b.style == TextTableBorder::Dashed ? 7
         : b.style == TextTableBorder::Dotted ? 6
                                              : int(b.style);
}
TextColor shade(TextColor c, bool light) {
  auto f = [&](unsigned char n) {
    return static_cast<unsigned char>(light ? n + (255 - n) * .4 : n * .6);
  };
  return {f(c.red), f(c.green), f(c.blue), c.alpha};
}
void rectangle(std::vector<TextPaint> &paints, TextRect rect, TextColor c,
               TextPaint::Coverage coverage = TextPaint::Fill,
               bool ellipse = false) {
  if (rect.width <= 0 || rect.height <= 0 || c.alpha <= 0)
    return;
  if (paints.size() >= 4000000)
    fail("Table paint exceeds native limit");
  paints.push_back({rect, c, coverage, ellipse, {}});
}
void paint_edge(std::vector<TextPaint> &paints, const Edge &edge) {
  double w = border_width(edge.border);
  if (w <= 0 || edge.end <= edge.begin)
    return;
  auto strip = [&](double center, double thickness, TextColor color, double a,
                   double b, bool dot = false) {
    rectangle(paints,
              edge.vertical
                  ? TextRect{center - thickness / 2, a, thickness, b - a}
                  : TextRect{a, center - thickness / 2, b - a, thickness},
              color,
              edge.vertical ? TextPaint::Vertical : TextPaint::Horizontal, dot);
  };
  auto color = edge.border.color;
  using B = TextTableBorder;
  bool positive = edge.side == 0 || edge.side == 3;
  if (edge.border.style == B::Double) {
    auto widths = edge.border.double_widths.value_or(
        std::array<double, 3>{w / 3, w / 3, w / 3});
    double minus = positive ? widths[2] : widths[0],
           plus = positive ? widths[0] : widths[2];
    strip(edge.position - w / 2 + minus / 2, minus, color, edge.begin,
          edge.end);
    strip(edge.position + w / 2 - plus / 2, plus, color, edge.begin, edge.end);
  } else if (edge.border.style == B::Dashed || edge.border.style == B::Dotted) {
    double ink = edge.border.style == B::Dotted ? w : 3 * w,
           period = edge.border.style == B::Dotted ? 3 * w : 6 * w;
    double start = std::floor(edge.begin / period) * period;
    size_t count = 0;
    for (double at = start; at < edge.end; at += period) {
      if (++count > 1000000)
        fail("Table border pattern exceeds native limit");
      double a = std::max(at, edge.begin), b = std::min(at + ink, edge.end);
      if (b > a)
        strip(edge.position, w, color, a, b, edge.border.style == B::Dotted);
    }
  } else if (edge.border.style == B::Groove || edge.border.style == B::Ridge) {
    bool light = (edge.border.style == B::Ridge) == positive;
    strip(edge.position - w / 4, w / 2, shade(color, light), edge.begin,
          edge.end);
    strip(edge.position + w / 4, w / 2, shade(color, !light), edge.begin,
          edge.end);
  } else {
    if (edge.border.style == B::Inset || edge.border.style == B::Outset)
      color = shade(color, (edge.border.style == B::Outset) == positive);
    strip(edge.position, w, color, edge.begin, edge.end);
  }
}
// Sweep coincident edges. This handles partial spans and collapsed hidden rows
// without quadratic comparisons or painting adjacent translucent borders twice.
void collapsed_edges(std::vector<TextPaint> &paints,
                     const std::vector<Edge> &edges) {
  std::map<std::pair<bool, double>, std::vector<size_t>> groups;
  for (size_t i = 0; i < edges.size(); ++i)
    if (border_width(edges[i].border) > 0 && edges[i].end > edges[i].begin)
      groups[{edges[i].vertical, edges[i].position}].push_back(i);
  for (const auto &[axis, ids] : groups) {
    struct Event {
      double at;
      size_t index;
      bool start;
    };
    std::vector<Event> events;
    for (auto id : ids) {
      events.push_back({edges[id].begin, id, true});
      events.push_back({edges[id].end, id, false});
    }
    std::sort(events.begin(), events.end(),
              [](const auto &a, const auto &b) { return a.at < b.at; });
    auto compare = [&](size_t a, size_t b) {
      return std::tuple{border_width(edges[a].border),
                        priority(edges[a].border), edges[a].serial} <
             std::tuple{border_width(edges[b].border),
                        priority(edges[b].border), edges[b].serial};
    };
    std::set<size_t, decltype(compare)> active(compare);
    size_t at = 0;
    double previous = events.front().at;
    while (at < events.size()) {
      double pos = events[at].at;
      if (pos > previous && !active.empty()) {
        auto edge = edges[*active.rbegin()];
        edge.begin = previous;
        edge.end = pos;
        paint_edge(paints, edge);
      }
      size_t end = at;
      while (end < events.size() && events[end].at == pos)
        ++end;
      for (size_t i = at; i < end; ++i)
        if (!events[i].start)
          active.erase(events[i].index);
      for (size_t i = at; i < end; ++i)
        if (events[i].start)
          active.insert(events[i].index);
      at = end;
      previous = pos;
    }
  }
}
} // namespace
const TextParagraph &block_paragraph(const TextFlow &flow,
                                     const TextBlock &block) {
  return block.formatted ? *block.formatted : flow.paragraphs.at(block.index);
}
const TextTable &block_table(const TextFlow &flow, const TextBlock &block) {
  return block.table ? *block.table : flow.tables.at(block.index);
}
TextRect intersect_text_rect(TextRect a, TextRect b) {
  double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  return {x, y, std::max(0., std::min(a.x + a.width, b.x + b.width) - x),
          std::max(0., std::min(a.y + a.height, b.y + b.height) - y)};
}
void offset_text_layout(TextBoxLayout &value, double x, double y) {
  for (auto &line : value.lines) {
    line.x += x;
    line.y += y;
    line.baseline += y;
    if (line.label)
      line.label->x += x;
    if (line.clip)
      shift(*line.clip, x, y);
  }
  for (auto &paint : value.paints) {
    shift(paint.rect, x, y);
    if (paint.clip)
      shift(*paint.clip, x, y);
  }
  for (auto &cell : value.cells)
    shift(cell.rect, x, y);
}
void clip_text_layout(TextBoxLayout &value, TextRect clip) {
  for (auto &line : value.lines)
    line.clip = line.clip ? intersect_text_rect(*line.clip, clip) : clip;
  for (auto &paint : value.paints)
    paint.clip = paint.clip ? intersect_text_rect(*paint.clip, clip) : clip;
  for (auto &cell : value.cells)
    cell.rect = intersect_text_rect(cell.rect, clip);
}
struct NativeBlockLayouter::Impl {
  const TextFlow &flow;
  LayoutParagraph paragraph;
  using Key = std::pair<const TextTable *, double>;
  std::map<Key, std::shared_ptr<const NativeTableLayout>> tables;
  size_t depth = 0, work = 0;
  Impl(const TextFlow &f, LayoutParagraph p)
      : flow(f), paragraph(std::move(p)) {}
  struct Scope {
    size_t &n;
    Scope(size_t &n) : n(n) {
      if (n >= 64)
        fail("Excessive table layout depth");
      ++n;
    }
    ~Scope() { --n; }
  };
  TextBoxLayout blocks(std::span<const TextBlock> blocks, double width) {
    Scope scope(depth);
    TextBoxLayout result;
    double y = 0, after = 0;
    for (const auto &block : blocks) {
      if (block.kind == TextBlock::Paragraph) {
        const auto &p = block_paragraph(flow, block);
        if (p.break_before || p.break_after || p.keep_next || p.keep_together ||
            !p.master.empty())
          note(result.unsupported,
               "Page and keep settings in unlinked text frames");
        y += std::max(after, std::max(0., p.before));
        auto box = paragraph(block, std::max(.01, width));
        append(result, *box, 0, y);
        y += box->height;
        after = std::max(0., p.after);
      } else {
        const auto &model = block_table(flow, block);
        if (!model.visible)
          continue;
        if (model.break_before || model.break_after || model.keep_next ||
            !model.may_break_rows || !model.master.empty())
          note(result.unsupported, "Page and keep settings in unlinked tables");
        y += std::max(after, std::max(0., model.before));
        auto box = table(block, width);
        append(result, box->display, 0, y);
        y += box->height;
        after = std::max(0., model.after);
      }
      if (!std::isfinite(y) || y > 100000000)
        fail("Table text height exceeds native limit");
    }
    result.height = y + after;
    return result;
  }
  std::shared_ptr<const NativeTableLayout> table(const TextBlock &block,
                                                 double available) {
    const auto &model = block_table(flow, block);
    Key key{&model, available};
    if (auto it = tables.find(key); it != tables.end())
      return it->second;
    Scope scope(depth);
    size_t columns = model.columns.size(), rows = model.rows.size();
    if (!std::isfinite(available) || available <= 0 || available > 100000 ||
        columns > 1024 || rows > 100000 ||
        (columns && rows > 1000000 / columns) ||
        model.cells.size() > 1000000 - work)
      fail("Table geometry exceeds native limit");
    work += model.cells.size();
    auto dimension = [](double n) {
      return std::isfinite(n) && n >= 0 && n <= 100000;
    };
    auto color_valid = [](const std::optional<TextColor> &c) {
      return !c || (std::isfinite(c->alpha) && c->alpha >= 0 && c->alpha <= 1);
    };
    for (double n : {model.left, model.right, model.before, model.after})
      if (!std::isfinite(n) || std::abs(n) > 100000)
        fail("Invalid native table margin");
    if ((model.width && !dimension(*model.width)) ||
        (model.relative_width &&
         (!std::isfinite(*model.relative_width) || *model.relative_width <= 0 ||
          *model.relative_width > 100)) ||
        !color_valid(model.background))
      fail("Invalid native table style");
    for (const auto &c : model.columns)
      if ((c.width && !dimension(*c.width)) || !std::isfinite(c.relative) ||
          c.relative < 0 || c.relative > 1000000)
        fail("Invalid native table column width");
    for (const auto &r : model.rows)
      if ((r.height && !dimension(*r.height)) || !dimension(r.minimum) ||
          !color_valid(r.background))
        fail("Invalid native table row style");
    for (const auto &cell : model.cells) {
      if (!color_valid(cell.style.background))
        fail("Invalid native table cell color");
      for (double p : cell.style.padding)
        if (!dimension(p))
          fail("Invalid native cell padding");
      for (const auto &b : cell.style.borders) {
        if (!dimension(b.width) || !color_valid(b.color))
          fail("Invalid native cell border");
        if (b.double_widths) {
          double sum = 0;
          for (double w : *b.double_widths) {
            if (!dimension(w))
              fail("Invalid native double border");
            sum += w;
          }
          if (sum > 100000)
            fail("Invalid native double border");
        }
      }
    }
    auto out = std::make_shared<NativeTableLayout>();
    double nominal =
        model.relative_width
            ? available * *model.relative_width
            : model.width.value_or(available - model.left - model.right);
    if (model.alignment == TextTable::Full ||
        model.alignment == TextTable::Margins)
      nominal = available - model.left - model.right;
    if (!std::isfinite(nominal) || nominal < 0 || nominal > 100000)
      fail("Invalid resolved table width");
    std::vector<double> widths(columns);
    double fixed = 0, weights = 0;
    for (size_t c = 0; c < columns; ++c) {
      const auto &col = model.columns[c];
      if (!col.visible)
        continue;
      if (col.relative > 0 || !col.width)
        weights += col.relative > 0 ? col.relative : 1;
      else {
        widths[c] = *col.width;
        fixed += widths[c];
      }
    }
    double space = std::max(0., nominal - fixed);
    // Preserve fixed column widths when the author specified a table width.
    // Otherwise follow Calligra's equal-decrement, zero-clamped shrink rule.
    if (fixed > nominal && !model.width && !model.relative_width) {
      size_t n = 0;
      for (size_t c = 0; c < columns; ++c)
        if (model.columns[c].visible && model.columns[c].width &&
            model.columns[c].relative <= 0)
          ++n;
      double excess = n ? (fixed - nominal) / n : 0;
      for (size_t c = 0; c < columns; ++c)
        if (model.columns[c].visible && model.columns[c].width &&
            model.columns[c].relative <= 0)
          widths[c] = std::max(0., widths[c] - excess);
    }
    for (size_t c = 0; c < columns; ++c)
      if (model.columns[c].visible &&
          (model.columns[c].relative > 0 || !model.columns[c].width))
        widths[c] = weights ? space *
                                  (model.columns[c].relative > 0
                                       ? model.columns[c].relative
                                       : 1) /
                                  weights
                            : 0;
    out->width = std::accumulate(widths.begin(), widths.end(), 0.);
    out->x = model.left +
             (model.alignment == TextTable::Right
                  ? available - model.left - model.right - out->width
              : model.alignment == TextTable::Center
                  ? (available - model.left - model.right - out->width) / 2
                  : 0);
    out->column_edges = {out->x};
    for (double w : widths)
      out->column_edges.push_back(out->column_edges.back() + w);
    if (out->width > available + 1e-6)
      note(out->display.unsupported,
           "Fixed table columns exceed available width");
    if (std::any_of(model.columns.begin(), model.columns.end(),
                    [](const auto &c) { return c.optimal; }))
      note(out->display.unsupported,
           "Optimal table column sizing uses declared widths");
    // RTL reverses physical column positions without changing source indices.
    auto column_box = [&](const TextTableCell &cell) {
      double a = out->column_edges[cell.column],
             b = out->column_edges[cell.column + cell.columns];
      if (model.rtl) {
        double end = out->x + out->width;
        return std::pair{end - (b - out->x), b - a};
      }
      return std::pair{a, b - a};
    };
    std::vector<size_t> grid(rows * columns, size_t(-1));
    for (size_t i = 0; i < model.cells.size(); ++i) {
      const auto &cell = model.cells[i];
      if (!cell.rows || !cell.columns || cell.row >= rows ||
          cell.column >= columns || cell.rows > rows - cell.row ||
          cell.columns > columns - cell.column)
        fail("Invalid native table cell span");
      for (size_t r = cell.row; r < cell.row + cell.rows; ++r)
        for (size_t c = cell.column; c < cell.column + cell.columns; ++c) {
          auto &slot = grid[r * columns + c];
          if (slot != size_t(-1))
            fail("Overlapping native table cells");
          slot = i;
        }
    }
    std::vector<std::array<double, 4>> insets(model.cells.size());
    for (size_t i = 0; i < model.cells.size(); ++i) {
      const auto &cell = model.cells[i];
      for (size_t side = 0; side < 4; ++side) {
        double w = border_width(cell.style.borders[side]);
        if (model.collapsing) {
          if (side == 0 || side == 2) {
            auto shown_row = [&](size_t r) {
              return model.rows[r].visible &&
                     (!model.rows[r].height || *model.rows[r].height > 0);
            };
            std::optional<size_t> neighbor;
            if (side == 0) {
              size_t r = cell.row;
              while (r) {
                --r;
                if (shown_row(r)) {
                  neighbor = r;
                  break;
                }
              }
            } else {
              size_t r = cell.row + cell.rows;
              while (r < rows) {
                if (shown_row(r)) {
                  neighbor = r;
                  break;
                }
                ++r;
              }
            }
            if (neighbor)
              for (size_t c = cell.column; c < cell.column + cell.columns;
                   ++c) {
                auto n = grid[*neighbor * columns + c];
                if (n != size_t(-1))
                  w = std::max(
                      w, border_width(
                             model.cells[n].style.borders[side == 0 ? 2 : 0]));
              }
          }
          if (side == 1 || side == 3) {
            const bool logical_before = (side == 3) != model.rtl;
            std::optional<size_t> neighbor;
            if (logical_before) {
              size_t c = cell.column;
              while (c) {
                --c;
                if (widths[c] > 0) {
                  neighbor = c;
                  break;
                }
              }
            } else {
              size_t c = cell.column + cell.columns;
              while (c < columns) {
                if (widths[c] > 0) {
                  neighbor = c;
                  break;
                }
                ++c;
              }
            }
            if (neighbor)
              for (size_t r = cell.row; r < cell.row + cell.rows; ++r) {
                auto n = grid[r * columns + *neighbor];
                if (n != size_t(-1))
                  w = std::max(
                      w, border_width(
                             model.cells[n].style.borders[side == 3 ? 1 : 3]));
              }
          }
          w /= 2;
        }
        insets[i][side] = w + cell.style.padding[side];
      }
    }
    std::vector<std::shared_ptr<const TextBoxLayout>> content(
        model.cells.size());
    std::vector<double> heights(rows);
    for (size_t r = 0; r < rows; ++r)
      if (model.rows[r].visible)
        heights[r] = model.rows[r].height.value_or(model.rows[r].minimum);
    for (size_t i = 0; i < model.cells.size(); ++i) {
      const auto &cell = model.cells[i];
      auto [x, w] = column_box(cell);
      if (w <= 1e-9) {
        content[i] = std::make_shared<TextBoxLayout>();
        continue;
      }
      double inner = std::max(.01, w - insets[i][1] - insets[i][3]);
      auto text = blocks(cell.blocks, inner);
      if (cell.blocks.empty() && cell.empty_format) {
        TextBlock strut{
            TextBlock::Paragraph, size_t(-1), cell.empty_format, {}};
        text.height = paragraph(strut, inner)->height;
      }
      content[i] = std::make_shared<const TextBoxLayout>(std::move(text));
      if (cell.rows == 1 && model.rows[cell.row].visible &&
          !model.rows[cell.row].height)
        heights[cell.row] =
            std::max(heights[cell.row],
                     content[i]->height + insets[i][0] + insets[i][2]);
    }
    // A spanning cell constrains the sum of its rows; grow the last flexible
    // row.
    for (size_t i = 0; i < model.cells.size(); ++i) {
      const auto &cell = model.cells[i];
      if (cell.rows <= 1)
        continue;
      double have = 0;
      size_t flexible = size_t(-1);
      for (size_t r = cell.row; r < cell.row + cell.rows; ++r) {
        have += heights[r];
        if (model.rows[r].visible && !model.rows[r].height)
          flexible = r;
      }
      double need = content[i]->height + insets[i][0] + insets[i][2];
      if (need > have && flexible != size_t(-1))
        heights[flexible] += need - have;
    }
    out->row_edges = {0};
    for (double h : heights) {
      if (!std::isfinite(h) || h < 0 || out->row_edges.back() + h > 100000000)
        fail("Invalid resolved table row height");
      out->row_edges.push_back(out->row_edges.back() + h);
    }
    out->height = out->row_edges.back();
    while (out->header_rows < rows && model.rows[out->header_rows].header)
      ++out->header_rows;
    rectangle(out->display.paints, {out->x, 0, out->width, out->height},
              model.background.value_or(TextColor{0, 0, 0, 0}));
    for (size_t r = 0; r < rows; ++r)
      if (model.rows[r].background)
        rectangle(out->display.paints,
                  {out->x, out->row_edges[r], out->width, heights[r]},
                  *model.rows[r].background);
    std::vector<Edge> edges;
    for (size_t i = 0; i < model.cells.size(); ++i) {
      const auto &cell = model.cells[i];
      auto [x, w] = column_box(cell);
      double y = out->row_edges[cell.row],
             h = out->row_edges[cell.row + cell.rows] - y;
      NativeCellLayout value;
      value.box = {block.index,
                   cell.row,
                   cell.column,
                   cell.rows,
                   cell.columns,
                   {x, y, w, h},
                   cell.row + cell.rows <= out->header_rows,
                   false};
      value.text = content[i];
      value.content_clip = {x + insets[i][3], y + insets[i][0],
                            std::max(0., w - insets[i][1] - insets[i][3]),
                            std::max(0., h - insets[i][0] - insets[i][2])};
      double extra =
          std::max(0., value.content_clip.height - content[i]->height);
      value.text_x = value.content_clip.x;
      value.text_y = value.content_clip.y +
                     (cell.style.vertical == TextCellStyle::Middle   ? extra / 2
                      : cell.style.vertical == TextCellStyle::Bottom ? extra
                                                                     : 0);
      if (w > 0 && h > 0) {
        if (cell.style.background)
          rectangle(out->display.paints, {x, y, w, h}, *cell.style.background);
        out->display.cells.push_back(value.box);
        auto text = *content[i];
        offset_text_layout(text, value.text_x, value.text_y);
        clip_text_layout(text, value.content_clip);
        append(out->display, std::move(text), 0, 0);
        for (size_t side = 0; side < 4; ++side) {
          double bw = border_width(cell.style.borders[side]);
          if (bw <= 0)
            continue;
          bool vertical = side == 1 || side == 3;
          double pos = side == 0   ? y
                       : side == 2 ? y + h
                       : side == 3 ? x
                                   : x + w;
          if (!model.collapsing)
            pos += (side == 0 || side == 3 ? 1 : -1) * bw / 2;
          edges.push_back({vertical, pos, vertical ? y : x,
                           vertical ? y + h : x + w, cell.style.borders[side],
                           side, edges.size()});
        }
      }
      out->cells.push_back(std::move(value));
    }
    if (model.collapsing)
      collapsed_edges(out->display.paints, edges);
    else
      for (const auto &edge : edges)
        paint_edge(out->display.paints, edge);
    for (const auto &warning : model.unsupported)
      note(out->display.unsupported, warning);
    out->display.height = out->height;
    tables.emplace(key, out);
    return out;
  }
};
NativeBlockLayouter::NativeBlockLayouter(const TextFlow &flow,
                                         LayoutParagraph p)
    : impl_(std::make_shared<Impl>(flow, std::move(p))) {}
TextBoxLayout NativeBlockLayouter::blocks(std::span<const TextBlock> blocks,
                                          double width) {
  return impl_->blocks(blocks, width);
}
std::shared_ptr<const NativeTableLayout>
NativeBlockLayouter::table(const TextBlock &block, double width) {
  return impl_->table(block, width);
}
} // namespace DomeOffice
