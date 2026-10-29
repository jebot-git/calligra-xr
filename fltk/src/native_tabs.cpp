// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_tabs.h"
#include <numbers>
namespace DomeOffice {
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
    double unit = std::max(.2, width), y = -std::max(.5, tab.font.size * .08);
    auto stroke = [&](double x1, double y1, double x2, double y2) {
      if (++pieces > 100000)
        throw std::runtime_error(
            "Tab leader display list exceeds native limit");
      tab.strokes.push_back({x1, y1, x2, y2, width});
    };
    for (int line = 0; line < (stop.double_line ? 2 : 1); ++line) {
      double base = y + line * 3 * unit;
      if (stop.line_style == TextTabStop::Solid) {
        stroke(left, base, right, base);
        continue;
      }
      if (stop.line_style == TextTabStop::Wave) {
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
      switch (stop.line_style) {
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
  }
}
} // namespace DomeOffice
