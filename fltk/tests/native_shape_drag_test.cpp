// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_shape_drag.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace DomeOffice;
namespace {
constexpr double degrees = 180 / std::numbers::pi;
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
void near(ScenePoint a, ScenePoint b) {
  near(a.x, b.x);
  near(a.y, b.y);
}
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
SceneShape frame(SceneTransform transform) {
  SceneShape s;
  s.source =
      read_document_xml(
          "<draw:rect "
          "xmlns:draw='urn:oasis:names:tc:opendocument:xmlns:drawing:1.0'/>")
          .documentElement();
  s.kind = SceneShape::Rectangle;
  s.transform = transform;
  s.width = 100;
  s.height = 80;
  return s;
}
ScenePoint moved(SceneTransform t, ScenePoint start, ScenePoint delta) {
  return {start.x + t.a * delta.x + t.c * delta.y,
          start.y + t.b * delta.x + t.d * delta.y};
}
} // namespace
int main() {
  std::array<ScenePoint, 8> handles{
      {{0, 0}, {.5, 0}, {1, 0}, {1, .5}, {1, 1}, {.5, 1}, {0, 1}, {0, .5}}};
  // General affine cases include rotation, nonuniform stretch, shear and an
  // existing reflection. The oracle follows handle/fixed points directly.
  std::array<SceneTransform, 4> transforms{{{},
                                            {0, 2, -3, 0, 140, 30},
                                            {1.3, .7, .4, 1.9, -20, 60},
                                            {-2, .3, .6, 1.2, 500, 200}}};
  size_t cases = 0;
  for (auto t : transforms) {
    auto s = frame(t);
    for (size_t h = 0; h < handles.size(); ++h) {
      auto f = handles[h];
      // Grab slightly away from the center of the handle, as a real pointer
      // does; the offset must not become an initial geometry jump.
      auto start = t.map({f.x * 100 + 2, f.y * 80 - 3});
      NativeShapeDrag drag(s, NativeShapeHandle(h), start);
      assert(!drag.update(start));
      for (auto delta : {ScenePoint{20, 30}, {-150, -110}, {175, 160}}) {
        auto p = moved(t, start, delta);
        auto proposal = drag.update(p);
        assert(proposal && proposal->geometry.width &&
               proposal->geometry.height);
        auto w = *proposal->geometry.width, height = *proposal->geometry.height;
        assert(w > 0 && height > 0);
        auto changed = proposal->action.value_or(SceneTransform{}) * t;
        near(changed.map({(1 - f.x) * w, (1 - f.y) * height}),
             t.map({(1 - f.x) * 100, (1 - f.y) * 80}));
        near(changed.map({f.x * w, f.y * height}),
             t.map({f.x * 100 + (f.x == .5 ? 0 : delta.x),
                    f.y * 80 + (f.y == .5 ? 0 : delta.y)}));
        near(std::abs(changed.a * changed.d - changed.b * changed.c),
             std::abs(t.a * t.d - t.b * t.c));
        auto aspect = drag.update(p, true);
        assert(aspect);
        near(*aspect->geometry.width / *aspect->geometry.height, 1.25);
        auto constrained = aspect->action.value_or(SceneTransform{}) * t;
        near(constrained.map({(1 - f.x) * *aspect->geometry.width,
                              (1 - f.y) * *aspect->geometry.height}),
             t.map({(1 - f.x) * 100, (1 - f.y) * 80}));
        assert(!drag.update(start)); // Updates never accumulate rounding.
        ++cases;
      }
    }
    auto center = t.map({50, 40});
    ScenePoint start{center.x + 80, center.y};
    NativeShapeDrag rotation(s, NativeShapeHandle::Rotate, start);
    for (double angle : {-155., -34.1, 22.3, 91., 170.}) {
      ScenePoint p{center.x + 80 * std::cos(angle / degrees),
                   center.y + 80 * std::sin(angle / degrees)};
      auto proposal = rotation.update(p);
      assert(proposal && proposal->action && !proposal->geometry.width);
      auto changed = *proposal->action * t;
      near(changed.map({50, 40}), center);
      near(std::hypot(changed.a, changed.b), std::hypot(t.a, t.b));
      near(std::hypot(changed.c, changed.d), std::hypot(t.c, t.d));
      near(changed.a * changed.c + changed.b * changed.d,
           t.a * t.c + t.b * t.d);
      auto snap = rotation.update(p, true);
      assert(snap);
      auto snapped = snap->action.value_or(SceneTransform{}) * t;
      double direction = std::atan2(snapped.b, snapped.a) * degrees;
      near(direction / 15, std::round(direction / 15));
      ++cases;
    }
    assert(!rotation.update(center) && !rotation.update(start));
  }
  // Line endpoints use slide-space distances, even in affine groups. Moving
  // one endpoint keeps the other exactly fixed and supports crossing it.
  for (auto t : transforms) {
    auto s = frame(t);
    s.kind = SceneShape::Line;
    s.contours = {{{{3, 5}, {83, 35}}, false}};
    auto a = t.map({3, 5}), b = t.map({83, 35});
    for (auto h : {NativeShapeHandle::LineStart, NativeShapeHandle::LineEnd}) {
      auto original = h == NativeShapeHandle::LineStart ? a : b;
      ScenePoint start{original.x + 2, original.y - 4};
      NativeShapeDrag line(s, h, start);
      ScenePoint p{start.x + 130, start.y - 80};
      for (bool snap : {false, true}) {
        auto proposal = line.update(p, snap);
        assert(proposal && !proposal->action);
        auto &g = proposal->geometry;
        auto angle = *g.rotation / degrees;
        ScenePoint first{*g.x, *g.y}, last{*g.x + *g.width * std::cos(angle),
                                           *g.y + *g.width * std::sin(angle)};
        near(h == NativeShapeHandle::LineStart ? last : first,
             h == NativeShapeHandle::LineStart ? b : a);
        if (snap)
          near(*g.rotation / 15, std::round(*g.rotation / 15));
        else
          near(h == NativeShapeHandle::LineStart ? first : last,
               ScenePoint{original.x + 130, original.y - 80});
        ++cases;
      }
      assert(!line.update(start));
    }
  }
  auto s = frame({});
  NativeShapeDrag side(s, NativeShapeHandle::East, {100, 40});
  assert(!side.update({100, 60}));
  auto minimum_aspect = side.update({0, 40}, true);
  assert(minimum_aspect);
  near(*minimum_aspect->geometry.width / *minimum_aspect->geometry.height,
       1.25);
  rejects([&] { side.update({NAN, 1}); });
  rejects([&] { side.update({1e9 + 1, 0}); });
  rejects([&] { side.update({1e8, 40}); });
  rejects([&] { NativeShapeDrag d(s, NativeShapeHandle::LineStart, {}); });
  rejects([&] { NativeShapeDrag d(s, NativeShapeHandle::Rotate, {50, 40}); });
  s.master = true;
  rejects([&] { NativeShapeDrag d(s, NativeShapeHandle::East, {}); });
  s.master = false;
  s.transform = {1, 0, 0, 0};
  rejects([&] { NativeShapeDrag d(s, NativeShapeHandle::East, {}); });
  std::cout << "PASS " << cases
            << " independent affine resize/rotation/line "
               "gesture cases, fixed anchors, crossings, aspect/snap, limits "
               "and exact return-to-start\n";
}
