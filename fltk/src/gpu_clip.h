// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <span>
namespace DomeOffice {
struct ClipPoint {
  double x = 0, y = 0;
};
struct ClipPolygon {
  std::array<ClipPoint, 64> points;
  size_t size = 0;
};
inline ClipPolygon clip_polygon(std::span<const ClipPoint> input,
                                const std::optional<TextRect> &clip) {
  if (input.size() > 60)
    throw std::runtime_error("Native clip polygon exceeds limit");
  ClipPolygon result;
  result.size = input.size();
  std::copy(input.begin(), input.end(), result.points.begin());
  if (!clip)
    return result;
  if (clip->width <= 0 || clip->height <= 0)
    return {};
  for (size_t side = 0; side < 4 && result.size; ++side) {
    ClipPolygon next;
    bool vertical = side < 2;
    double limit = side == 0   ? clip->x
                   : side == 1 ? clip->x + clip->width
                   : side == 2 ? clip->y
                               : clip->y + clip->height;
    auto coordinate = [&](ClipPoint p) { return vertical ? p.x : p.y; };
    auto inside = [&](ClipPoint p) {
      return side == 0 || side == 2 ? coordinate(p) >= limit
                                    : coordinate(p) <= limit;
    };
    auto previous = result.points[result.size - 1];
    bool before = inside(previous);
    for (size_t i = 0; i < result.size; ++i) {
      auto p = result.points[i];
      bool after = inside(p);
      if (before != after) {
        double t = (limit - coordinate(previous)) /
                   (coordinate(p) - coordinate(previous));
        if (next.size >= next.points.size())
          throw std::runtime_error("Native clip polygon exceeds limit");
        next.points[next.size++] = {previous.x + t * (p.x - previous.x),
                                    previous.y + t * (p.y - previous.y)};
      }
      if (after) {
        if (next.size >= next.points.size())
          throw std::runtime_error("Native clip polygon exceeds limit");
        next.points[next.size++] = p;
      }
      previous = p;
      before = after;
    }
    result = next;
  }
  return result;
}
inline bool clip_segment(ClipPoint &a, ClipPoint &b,
                         const std::optional<TextRect> &clip) {
  if (!clip)
    return true;
  if (clip->width <= 0 || clip->height <= 0)
    return false;
  double lo = 0, hi = 1, dx = b.x - a.x, dy = b.y - a.y;
  auto axis = [&](double start, double delta, double low, double high) {
    if (delta == 0)
      return start >= low && start <= high;
    double p = (low - start) / delta, q = (high - start) / delta;
    if (p > q)
      std::swap(p, q);
    lo = std::max(lo, p);
    hi = std::min(hi, q);
    return lo <= hi;
  };
  if (!axis(a.x, dx, clip->x, clip->x + clip->width) ||
      !axis(a.y, dy, clip->y, clip->y + clip->height))
    return false;
  auto start = a;
  a = {start.x + lo * dx, start.y + lo * dy};
  b = {start.x + hi * dx, start.y + hi * dy};
  return true;
}
inline std::optional<TextRect> relative_text_clip(std::optional<TextRect> clip,
                                                  double x, double y) {
  if (clip) {
    clip->x -= x;
    clip->y -= y;
  }
  return clip;
}
} // namespace DomeOffice
