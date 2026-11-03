// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_shape_drag.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace DomeOffice {
namespace {
constexpr double degrees = 180 / std::numbers::pi;
constexpr double minimum = .001;
void point(ScenePoint p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
      std::abs(p.y) > 1e9)
    throw std::runtime_error("Shape gesture exceeds the coordinate limit");
}
ScenePoint fraction(NativeShapeHandle handle) {
  switch (handle) {
  case NativeShapeHandle::NorthWest:
    return {0, 0};
  case NativeShapeHandle::North:
    return {.5, 0};
  case NativeShapeHandle::NorthEast:
    return {1, 0};
  case NativeShapeHandle::East:
    return {1, .5};
  case NativeShapeHandle::SouthEast:
    return {1, 1};
  case NativeShapeHandle::South:
    return {.5, 1};
  case NativeShapeHandle::SouthWest:
    return {0, 1};
  case NativeShapeHandle::West:
    return {0, .5};
  default:
    throw std::runtime_error("Choose a frame resize handle");
  }
}
double extent(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e7)
    throw std::runtime_error("Shape resize exceeds the frame limit");
  return std::max(minimum, std::abs(value));
}
} // namespace
NativeShapeDrag::NativeShapeDrag(const SceneShape &shape,
                                 NativeShapeHandle handle, ScenePoint pointer)
    : handle_(handle), geometry_(native_shape_geometry(shape)),
      transform_(shape.transform), inverse_(shape.transform.inverse()),
      pointer_(pointer) {
  point(pointer);
  if (geometry_.line) {
    if (handle != NativeShapeHandle::LineStart &&
        handle != NativeShapeHandle::LineEnd)
      throw std::runtime_error("Use the line's endpoint handles");
    if (!geometry_.position_enabled)
      throw std::runtime_error("The line transform is singular");
    first_ = transform_.map(shape.contours.at(0).points.at(0));
    last_ = transform_.map(shape.contours.at(0).points.at(1));
    point(first_);
    point(last_);
  } else {
    center_ = transform_.map({geometry_.width / 2, geometry_.height / 2});
    point(center_);
    if (handle == NativeShapeHandle::Rotate) {
      if (!geometry_.rotation_enabled || !geometry_.rotation)
        throw std::runtime_error("This shape has no editable rotation axis");
      if (pointer.x == center_.x && pointer.y == center_.y)
        throw std::runtime_error("Choose a rotation point outside the center");
    } else {
      auto f = fraction(handle);
      if (!inverse_ || !geometry_.position_enabled ||
          (f.x != .5 && !geometry_.width_enabled) ||
          (f.y != .5 && !geometry_.height_enabled))
        throw std::runtime_error("This frame axis cannot be resized");
    }
  }
}
std::optional<NativeShapeDragProposal>
NativeShapeDrag::update(ScenePoint pointer, bool constrain) const {
  point(pointer);
  const ScenePoint delta{pointer.x - pointer_.x, pointer.y - pointer_.y};
  if (delta.x == 0 && delta.y == 0)
    return {};
  NativeShapeDragProposal result;
  if (geometry_.line) {
    bool start = handle_ == NativeShapeHandle::LineStart;
    auto fixed = start ? last_ : first_, dragged = start ? first_ : last_;
    dragged.x += delta.x;
    dragged.y += delta.y;
    double x = dragged.x - fixed.x, y = dragged.y - fixed.y;
    double length = std::hypot(x, y);
    if (length < minimum) {
      // Keep positive ODF line length at the crossing point; beyond it, the
      // endpoint and direction can pass through the fixed endpoint normally.
      auto old = start ? first_ : last_;
      x = old.x - fixed.x;
      y = old.y - fixed.y;
      length = minimum;
    }
    double angle = x || y ? std::atan2(y, x) * degrees : 0;
    if (constrain)
      angle = std::round(angle / 15) * 15;
    auto radians = angle / degrees;
    dragged = {fixed.x + length * std::cos(radians),
               fixed.y + length * std::sin(radians)};
    auto first = start ? dragged : fixed, last = start ? fixed : dragged;
    result.geometry.x = first.x;
    result.geometry.y = first.y;
    result.geometry.width = length;
    result.geometry.rotation =
        std::atan2(last.y - first.y, last.x - first.x) * degrees;
    return result;
  }
  if (handle_ == NativeShapeHandle::Rotate) {
    ScenePoint a{pointer_.x - center_.x, pointer_.y - center_.y},
        b{pointer.x - center_.x, pointer.y - center_.y};
    // The center has no angle. Keep the original when the pointer reaches it.
    if (b.x == 0 && b.y == 0)
      return {};
    double angle =
        std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y) * degrees;
    if (constrain)
      angle = std::round((*geometry_.rotation + angle) / 15) * 15 -
              *geometry_.rotation;
    angle = std::remainder(angle, 360.);
    if (std::abs(angle) < 1e-12)
      return {};
    double c = std::cos(angle / degrees), s = std::sin(angle / degrees);
    result.action = SceneTransform{c,
                                   s,
                                   -s,
                                   c,
                                   center_.x - c * center_.x + s * center_.y,
                                   center_.y - s * center_.x - c * center_.y};
    return result;
  }
  auto f = fraction(handle_);
  // Only use the inverse's linear part: pointer offsets are vectors, not
  // points.
  ScenePoint local{inverse_->a * delta.x + inverse_->c * delta.y,
                   inverse_->b * delta.x + inverse_->d * delta.y};
  double width = geometry_.width + (f.x == .5  ? 0
                                    : f.x == 0 ? -local.x
                                               : local.x),
         height = geometry_.height + (f.y == .5  ? 0
                                      : f.y == 0 ? -local.y
                                                 : local.y);
  if (constrain && geometry_.width > 0 && geometry_.height > 0) {
    double sx = width / geometry_.width, sy = height / geometry_.height;
    bool horizontal =
        f.y == .5 || (f.x != .5 && std::abs(sx - 1) >= std::abs(sy - 1));
    if (horizontal)
      height = std::copysign(std::abs(sx) * geometry_.height, height);
    else
      width = std::copysign(std::abs(sy) * geometry_.width, width);
    const double scale = std::abs(width) / geometry_.width;
    const double floor =
        std::max(minimum / geometry_.width, minimum / geometry_.height);
    if (scale < floor) {
      width = std::copysign(geometry_.width * floor, width);
      height = std::copysign(geometry_.height * floor, height);
    }
  }
  double w = width == 0 && f.x == .5 ? 0 : extent(width),
         h = height == 0 && f.y == .5 ? 0 : extent(height);
  bool flip_x = std::signbit(width), flip_y = std::signbit(height);
  if (!flip_x && !flip_y && w == geometry_.width && h == geometry_.height)
    return {};
  ScenePoint fixed = transform_.map(
      {(1 - f.x) * geometry_.width, (1 - f.y) * geometry_.height});
  SceneTransform mirror;
  mirror.a = flip_x ? -1 : 1;
  mirror.d = flip_y ? -1 : 1;
  auto changed = transform_ * mirror;
  changed.e = fixed.x - changed.a * (1 - f.x) * w - changed.c * (1 - f.y) * h;
  changed.f = fixed.y - changed.b * (1 - f.x) * w - changed.d * (1 - f.y) * h;
  result.geometry.width = w;
  result.geometry.height = h;
  if (flip_x || flip_y)
    result.action = changed * *inverse_;
  else if (changed.e != transform_.e || changed.f != transform_.f)
    result.action = SceneTransform{
        1, 0, 0, 1, changed.e - transform_.e, changed.f - transform_.f};
  point(changed.map({}));
  return result;
}
} // namespace DomeOffice
