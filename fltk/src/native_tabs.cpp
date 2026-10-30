// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_tabs.h"
#include <numbers>
#include <unicode/uchar.h>
namespace DomeOffice {
bool TextDecoration::valid() const {
  return style >= None && style <= Wave && width_unit >= Auto &&
         width_unit <= Points && std::isfinite(width) && width > 0 &&
         width <= 1000 &&
         (!color || (std::isfinite(color->alpha) && color->alpha >= 0 &&
                     color->alpha <= 1));
}
std::vector<TextLeaderStroke> text_line_strokes(TextDecoration::LineStyle style,
                                                bool double_line, double left,
                                                double right, double y,
                                                double width) {
  std::vector<TextLeaderStroke> result;
  if (style == TextDecoration::None || right <= left)
    return result;
  if (style < TextDecoration::None || style > TextDecoration::Wave ||
      !std::isfinite(left) || !std::isfinite(right) || !std::isfinite(y) ||
      !std::isfinite(width) || width <= 0 || width > 1000)
    throw std::runtime_error("Invalid native text line geometry");
  const double unit = std::max(.2, width);
  auto stroke = [&](double x1, double y1, double x2, double y2) {
    if (result.size() >= 100000)
      throw std::runtime_error("Text line display list exceeds native limit");
    result.push_back({x1, y1, x2, y2, width});
  };
  for (int line = 0; line < (double_line ? 2 : 1); ++line) {
    double base = y + line * 3 * unit;
    if (style == TextTabStop::Solid) {
      stroke(left, base, right, base);
      continue;
    }
    if (style == TextTabStop::Wave) {
      double period = std::max(2., 8 * unit), step = period / 12;
      double a = left;
      while (a < right) {
        double b = std::min(right, a + step);
        auto wave = [&](double x) {
          return base +
                 unit * std::sin((x - left) * 2 * std::numbers::pi / period);
        };
        stroke(a, wave(a), b, wave(b));
        a = b;
      }
      continue;
    }
    std::vector<double> pattern;
    switch (style) {
    case TextTabStop::Dotted:
      pattern = {1, 2};
      break;
    case TextTabStop::Dash:
      pattern = {4, 3};
      break;
    case TextTabStop::LongDash:
      pattern = {8, 3};
      break;
    case TextTabStop::DotDash:
      pattern = {1, 2, 4, 2};
      break;
    default:
      pattern = {1, 2, 1, 2, 4, 2};
      break;
    }
    double x = left;
    size_t i = 0;
    while (x < right) {
      double end = std::min(right, x + pattern[i % pattern.size()] * unit);
      if (i % 2 == 0)
        stroke(x, base, end, base);
      x = end;
      ++i;
    }
  }
  return result;
}
TextFont TextFontPatch::apply(TextFont value) const {
  if (family)
    value.family = *family;
  if (language)
    value.language = *language;
  if (weight) {
    value.weight = *weight;
    value.bold = *weight >= 600;
  }
  if (italic)
    value.italic = *italic;
  value.size = size.value_or(value.size) * scale;
  if (!std::isfinite(value.size) || value.size <= 0 || value.size > 1000)
    throw std::runtime_error("Invalid tab leader font size");
  return value;
}
TabAdvance next_tab(const TextParagraph &p, double current, double origin,
                    double width, double delimiter, bool rtl) {
  TabAdvance result;
  auto it = std::upper_bound(
      p.tabs.begin(), p.tabs.end(), current - origin + 1e-7,
      [](double at, const TextTabStop &stop) { return at < stop.position; });
  if (it != p.tabs.end()) {
    result.stop = *it;
    result.position = origin + it->position;
  } else {
    double from = current - origin;
    if (!p.tabs.empty())
      from = std::max(from, p.tabs.back().position);
    result.position =
        origin +
        std::max(0., std::floor(from / p.default_tab) + 1) * p.default_tab;
    result.stop.type = rtl ? TextTabStop::Right : TextTabStop::Left;
  }
  double offset = result.stop.type == TextTabStop::Center      ? width / 2
                  : result.stop.type == TextTabStop::Character ? delimiter
                  : ((result.stop.type == TextTabStop::Right) != rtl) ? width
                                                                      : 0;
  result.offset = offset;
  result.clamped = result.position < current + offset - 1e-7;
  result.advance = std::max(0., result.position - current - offset);
  if (!std::isfinite(result.advance) || !std::isfinite(result.position))
    throw std::runtime_error("Invalid native tab advance");
  return result;
}
std::vector<TextTabPlacement> text_decoration_lines(const ShapedText &text) {
  struct Run {
    double left, right;
    ShapedDecoration line;
  };
  std::vector<TextTabPlacement> result;
  size_t total = 0;
  for (bool strike : {false, true}) {
    std::vector<Run> runs;
    if (!text.decorations)
      return result;
    for (const auto &cluster : text.clusters) {
      if (cluster.metrics.width <= 0)
        continue;
      auto first = std::lower_bound(
          text.decorations->begin(), text.decorations->end(), cluster.begin,
          [](const auto &span, size_t at) { return span.end <= at; });
      for (auto span = first;
           span != text.decorations->end() && span->begin < cluster.end;
           ++span) {
        const auto &line = strike ? span->paint.strike : span->paint.underline;
        if (line.format.style == TextDecoration::None)
          continue;
        size_t begin = std::max(cluster.begin, span->begin),
               end = std::min(cluster.end, span->end);
        if (!text.source || begin >= end || end > text.source->size())
          throw std::runtime_error("Invalid text decoration source extent");
        auto point = [&](size_t offset) {
          auto caret = std::lower_bound(
              cluster.carets.begin(), cluster.carets.end(), offset,
              [](const auto &caret, size_t at) { return caret.offset < at; });
          if (caret == cluster.carets.end() || caret->offset != offset)
            throw std::runtime_error(
                "Decoration boundary is not a shaping grapheme");
          return cluster.x + caret->fraction * cluster.metrics.width;
        };
        // Skip whitespace at grapheme granularity, including tabs after their
        // position-dependent advances have been installed by layout.
        for (auto at = begin; at < end;) {
          auto caret =
              std::upper_bound(cluster.carets.begin(), cluster.carets.end(), at,
                               [](size_t offset, const auto &caret) {
                                 return offset < caret.offset;
                               });
          size_t next = caret == cluster.carets.end()
                            ? end
                            : std::min(end, caret->offset);
          if (next <= at)
            throw std::runtime_error("Invalid decoration grapheme order");
          bool space = std::all_of(text.source->begin() + at,
                                   text.source->begin() + next,
                                   [](auto c) { return u_isUWhiteSpace(c); });
          if (!line.format.skip_space || !space) {
            double a = point(at), b = point(next);
            runs.push_back({std::min(a, b), std::max(a, b), line});
          }
          at = next;
        }
      }
    }
    std::sort(runs.begin(), runs.end(),
              [](const auto &a, const auto &b) { return a.left < b.left; });
    size_t n = 0;
    for (const auto &run : runs) {
      if (n && runs[n - 1].line == run.line &&
          std::abs(runs[n - 1].right - run.left) < 1e-7)
        runs[n - 1].right = run.right;
      else
        runs[n++] = run;
    }
    runs.resize(n);
    for (const auto &run : runs) {
      TextTabPlacement paint;
      paint.color = run.line.color;
      double y = run.line.position;
      if (strike && run.line.format.double_line)
        y -= 1.5 * std::max(.2, run.line.width);
      paint.strokes =
          text_line_strokes(run.line.format.style, run.line.format.double_line,
                            run.left, run.right, y, run.line.width);
      total += paint.strokes.size();
      if (total > 2000000)
        throw std::runtime_error("Text decorations exceed native limit");
      result.push_back(std::move(paint));
    }
  }
  return result;
}
void prepare_tab_leaders(std::vector<TextTabPlacement> &tabs,
                         NativeTextShaper *shaper) {
  size_t pieces = 0;
  for (auto &tab : tabs) {
    auto &stop = tab.stop;
    double margin =
        std::min(tab.font.size / 6, std::max(0., tab.x2 - tab.x1) / 4);
    double left = tab.x1 + margin, right = tab.x2 - margin;
    if (right <= left)
      continue;
    if (!stop.leader.empty()) {
      if (!shaper)
        continue;
      auto font = stop.leader_font.apply(tab.font);
      auto leader =
          std::make_shared<ShapedText>(shaper->shape(stop.leader, font));
      if (stop.leader_font.color_set)
        for (auto &glyph : leader->glyphs)
          glyph.color = stop.leader_font.color;
      double step = leader->metrics.width;
      if (step < .01 || !std::isfinite(step))
        continue;
      // Whole glyphs fit the gap; their original text is never added to the
      // body.
      for (double at = left; at + step <= right + 1e-7; at += step) {
        if (++pieces > 100000)
          throw std::runtime_error(
              "Tab leader display list exceeds native limit");
        tab.leader_positions.push_back(at);
      }
      auto repeated = std::make_shared<ShapedText>();
      repeated->metrics = leader->metrics;
      repeated->metrics.width = right - left;
      for (double at : tab.leader_positions)
        for (auto glyph : leader->glyphs) {
          glyph.x += at;
          glyph.begin = tab.source;
          glyph.end = tab.source + 1;
          repeated->glyphs.push_back(std::move(glyph));
        }
      tab.leader = std::move(repeated);
      continue;
    }
    if (stop.line_style == TextTabStop::None)
      continue;
    double width = stop.width.value_or(tab.font.size * stop.width_scale);
    double y = -std::max(.5, tab.font.size * .08);
    tab.strokes = text_line_strokes(stop.line_style, stop.double_line, left,
                                    right, y, width);
    pieces += tab.strokes.size();
    if (pieces > 100000)
      throw std::runtime_error("Tab leader display list exceeds native limit");
  }
}
} // namespace DomeOffice
