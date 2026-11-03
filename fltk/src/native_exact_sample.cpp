// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_exact_sample.h"
#include <array>
#include <bit>
#include <limits>
namespace DomeOffice::NativeExact {
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53);
Binary binary(double value) {
  if (!std::isfinite(value))
    throw NativeValue::error("#NUM!");
  uint64_t bits = std::bit_cast<uint64_t>(value);
  int biased = (bits >> 52) & 2047;
  uint64_t mantissa =
      (bits & ((uint64_t(1) << 52) - 1)) | (biased ? uint64_t(1) << 52 : 0);
  int exponent = biased ? biased - 1023 - 52 : -1074;
  if (mantissa) {
    int shift = std::countr_zero(mantissa);
    mantissa >>= shift;
    exponent += shift;
  }
  return {mantissa, exponent, bool(bits >> 63)};
}
I integer(Binary value, int exponent) {
  if (!value.mantissa)
    return 0;
  I result = value.mantissa;
  result <<= value.exponent - exponent;
  return value.negative ? -result : result;
}
namespace {
template <class Range, class Decode>
void gather(Sample &sample, const Range &values, Decode decode) {
  sample.count = values.size();
  if (sample.count > 1'000'000)
    throw NativeValue::error("#LIMIT!", true);
  bool nonzero = false;
  int top = 0;
  for (const auto &item : values)
    for (auto b : decode(item))
      if (b.mantissa) {
        int next_top = b.exponent + 64 - std::countl_zero(b.mantissa) + 1;
        sample.exponent =
            nonzero ? std::min(sample.exponent, b.exponent) : b.exponent;
        top = nonzero ? std::max(top, next_top) : next_top;
        nonzero = true;
      }
  sample.limbs = nonzero ? (top - sample.exponent + 63) / 64 : 1;
  sample.charge(sample.count * (sample.limbs * sample.limbs + 4));
  for (const auto &item : values) {
    I next = 0;
    bool first = true;
    for (auto b : decode(item)) {
      I term = integer(b, sample.exponent);
      if (first)
        next = std::move(term);
      else
        next -= term;
      first = false;
    }
    sample.sum += next;
    sample.squares += next * next;
  }
  sample.centered = I(sample.count) * sample.squares - sample.sum * sample.sum;
}
} // namespace
Sample::Sample(const std::vector<double> &values) {
  gather(*this, values, [](double x) { return std::array{binary(x)}; });
}
Sample::Sample(const std::vector<std::pair<double, double>> &values) {
  gather(*this, values,
         [](auto p) { return std::array{binary(p.first), binary(p.second)}; });
}
void Sample::charge(size_t work) const {
  if (work > remaining)
    throw NativeValue::error("#LIMIT!", true);
  remaining -= work;
}
Q Sample::variance() const {
  if (count < 2)
    throw NativeValue::error("#DIV/0!");
  charge(limbs * limbs + 8);
  return ldexp(Q(centered) / Q(I(count) * (count - 1)), 2 * exponent);
}
Q Sample::difference(const Sample &other) const {
  if (!count || !other.count)
    throw NativeValue::error("#DIV/0!");
  int common = std::min(exponent, other.exponent);
  charge((limbs + other.limbs) * 4 + 8);
  I numerator = (sum * other.count << (exponent - common)) -
                (other.sum * count << (other.exponent - common));
  return ldexp(Q(numerator) / Q(I(count) * other.count), common);
}
Q Sample::difference(double query) const {
  if (!count)
    throw NativeValue::error("#DIV/0!");
  auto b = binary(query);
  int common = b.mantissa ? std::min(exponent, b.exponent) : exponent;
  charge((size_t(std::abs(exponent - common)) / 64 + 1) * limbs + 8);
  I numerator = (sum << (exponent - common)) - integer(b, common) * count;
  return ldexp(Q(numerator) / count, common);
}
} // namespace DomeOffice::NativeExact
