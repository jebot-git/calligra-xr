// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include <cmath>
#include <limits>

namespace DomeOffice {
std::vector<TextCaret> text_line_carets(const TextLine &line) {
  std::vector<TextCaret> result;
  if (!line.shaped)
    return result;
  const auto &shape = *line.shaped;
  auto add = [&](size_t offset, TextCaret::Affinity affinity, bool rtl,
                 double x, const TextMetrics &metrics) {
    TextCaret caret;
    caret.paragraph = line.paragraph;
    caret.offset = offset;
    caret.affinity = affinity;
    caret.rtl = rtl;
    caret.rect = {line.x + x, line.baseline - metrics.ascent, 0,
                  metrics.ascent + metrics.descent};
    result.push_back(caret);
  };
  if (shape.clusters.empty())
    add(shape.empty_offset, TextCaret::Following, shape.rtl, 0, shape.metrics);
  for (const auto &cluster : shape.clusters)
    for (const auto &stop : cluster.carets) {
      const double x = cluster.x + stop.fraction * cluster.metrics.width;
      if (stop.offset < cluster.end)
        add(stop.offset, TextCaret::Following, cluster.rtl, x, cluster.metrics);
      if (stop.offset > cluster.begin)
        add(stop.offset, TextCaret::Preceding, cluster.rtl, x, cluster.metrics);
    }
  std::stable_sort(result.begin(), result.end(),
                   [](const TextCaret &a, const TextCaret &b) {
                     return a.rect.x < b.rect.x;
                   });
  return result;
}
std::optional<TextCaretHit> text_caret_at(const TextPage &page, double x,
                                          double y) {
  auto index = text_line_at(page, x, y);
  if (!index)
    return {};
  const auto &line = page.lines[*index];
  std::optional<TextCaret> best;
  double distance = std::numeric_limits<double>::infinity();
  for (auto caret : text_line_carets(line)) {
    if (line.clip && (caret.rect.x < line.clip->x - 1e-7 ||
                      caret.rect.x > line.clip->x + line.clip->width + 1e-7))
      continue;
    double d = std::abs(caret.rect.x - x);
    auto priority = [&](const TextCaret &c) {
      return std::pair(c.rtl != line.shaped->rtl,
                       c.affinity != TextCaret::Following);
    };
    if (d < distance - 1e-7 || (std::abs(d - distance) <= 1e-7 && best &&
                                priority(caret) < priority(*best))) {
      best = caret;
      distance = d;
    }
  }
  if (!best)
    return {};
  return TextCaretHit{*index, *best};
}
} // namespace DomeOffice
