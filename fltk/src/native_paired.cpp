// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_paired.h"
#include <bit>
#include <limits>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
namespace DomeOffice {
namespace {
using V = NativeValue;
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53);
using I = boost::multiprecision::cpp_int;
using Q = boost::multiprecision::cpp_bin_float_quad;
struct Binary {
  uint64_t mantissa;
  int exponent;
  bool negative;
};
Binary binary(double value) {
  if (!std::isfinite(value))
    throw V::error("#NUM!");
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
V scaled(const I &numerator, const I &denominator, int exponent) {
  Q value = ldexp(Q(numerator) / Q(denominator), exponent);
  return V::numeric(value.convert_to<double>());
}
double number(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber()) {
    if (!std::isfinite(v.number))
      throw V::error("#NUM!");
    return v.number;
  }
  if (v.isBoolean())
    return v.boolean ? 1 : 0;
  if (v.kind == V::Empty)
    return 0;
  bool ok = false;
  double value = v.string.toDouble(&ok);
  if (!ok)
    throw V::error("#VALUE!");
  if (!std::isfinite(value))
    throw V::error("#NUM!");
  return value;
}
} // namespace
const NativeSignatures &paired_signatures() {
  static const NativeSignatures names = [] {
    NativeSignatures out;
    for (auto name : Text("CORREL COVAR COVARIANCE.S SLOPE INTERCEPT RSQ STEYX "
                          "SUMX2MY2 SUMX2PY2 SUMXMY2")
                         .split(' '))
      out[name] = {2, 2};
    out["FORECAST"] = {3, 3};
    return out;
  }();
  return names;
}
Text paired_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  static const std::map<Text, Text> aliases = {{"PEARSON", "CORREL"},
                                               {"COVARIANCE.P", "COVAR"},
                                               {"FORECAST.LINEAR", "FORECAST"},
                                               {"SUM2XMY", "SUMXMY2"}};
  if (auto found = aliases.find(base); found != aliases.end())
    return found->second;
  return paired_signatures().contains(base) ? base : name;
}
struct NativePairedData::Impl {
  size_t count = 0, limbs = 1;
  int ex = 0, ey = 0;
  I sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, vx = 0, vy = 0, cov = 0,
    intercept = 0;
  mutable size_t remaining = 50'000'000;
  void charge(size_t units) const {
    if (units > remaining)
      throw V::error("#LIMIT!", true);
    remaining -= units;
  }
  explicit Impl(const std::vector<std::pair<double, double>> &values)
      : count(values.size()) {
    if (count > 1'000'000)
      throw V::error("#LIMIT!", true);
    if (!count)
      return;
    bool nonzero_x = false, nonzero_y = false;
    int max_x = 0, max_y = 0;
    for (auto [y, x] : values) {
      auto a = binary(x), b = binary(y);
      if (a.mantissa) {
        int top = a.exponent + 64 - std::countl_zero(a.mantissa);
        ex = nonzero_x ? std::min(ex, a.exponent) : a.exponent;
        max_x = nonzero_x ? std::max(max_x, top) : top;
        nonzero_x = true;
      }
      if (b.mantissa) {
        int top = b.exponent + 64 - std::countl_zero(b.mantissa);
        ey = nonzero_y ? std::min(ey, b.exponent) : b.exponent;
        max_y = nonzero_y ? std::max(max_y, top) : top;
        nonzero_y = true;
      }
    }
    size_t lx = nonzero_x ? (max_x - ex + 63) / 64 : 1,
           ly = nonzero_y ? (max_y - ey + 63) / 64 : 1;
    limbs = std::max(lx, ly);
    // Estimate limb multiplication work before processing an exceptionally wide
    // mixed-exponent dataset. Limits retain incomplete provenance, never zeros.
    charge(count * (lx * lx + ly * ly + lx * ly + 4));
    // Every finite double is an integer times a power of two. Accumulate exact
    // binary sums/products, then cancel integers before any floating division.
    // This retains tiny intercepts and zero residuals under extreme offsets.
    for (auto [y, x] : values) {
      I a = integer(binary(x), ex), b = integer(binary(y), ey);
      sx += a;
      sy += b;
      sxx += a * a;
      syy += b * b;
      sxy += a * b;
    }
    vx = I(count) * sxx - sx * sx;
    vy = I(count) * syy - sy * sy;
    cov = I(count) * sxy - sx * sy;
    intercept = sy * sxx - sx * sxy;
  }
};
NativePairedData::NativePairedData(
    std::vector<std::pair<double, double>> values)
    : impl_(std::make_shared<Impl>(values)) {}
V NativePairedData::evaluate(const Text &name, const V &query) const try {
  if (!paired_signatures().contains(name))
    return V::error("#NAME?", true);
  const auto &d = *impl_;
  size_t n = d.count;
  d.charge(d.limbs * d.limbs * 8 + 8);
  if (name.startsWith("SUMX")) {
    int exponent = std::min(2 * d.ex, 2 * d.ey);
    I first = d.syy << (2 * d.ey - exponent),
      second = d.sxx << (2 * d.ex - exponent);
    I total;
    if (name == "SUMX2MY2")
      total = first - second;
    else if (name == "SUMX2PY2")
      total = first + second;
    else {
      exponent = std::min(exponent, d.ex + d.ey);
      first <<= std::min(2 * d.ex, 2 * d.ey) - exponent;
      second <<= std::min(2 * d.ex, 2 * d.ey) - exponent;
      total = first + second - (I(2) * d.sxy << (d.ex + d.ey - exponent));
    }
    return scaled(total, I(1), exponent);
  }
  double forecast = 0;
  if (name == "FORECAST")
    forecast = number(query);
  if (!n)
    return V::error(name == "FORECAST" ? "#N/A" : "#DIV/0!");
  if (name == "COVAR" || name == "COVARIANCE.S") {
    if (name == "COVARIANCE.S" && n < 2)
      return V::error("#DIV/0!");
    return scaled(d.cov, I(n) * (name == "COVAR" ? n : n - 1), d.ex + d.ey);
  }
  if (d.vx == 0)
    return V::error("#DIV/0!");
  if (name == "CORREL" || name == "RSQ") {
    if (d.vy == 0)
      return V::error("#DIV/0!");
    I square = d.cov * d.cov, denominator = d.vx * d.vy;
    Q result = Q(square) / Q(denominator);
    if (name == "CORREL") {
      result = sqrt(result);
      if (d.cov < 0)
        result = -result;
    }
    return V::numeric(result.convert_to<double>());
  }
  if (name == "SLOPE")
    return scaled(d.cov, d.vx, d.ey - d.ex);
  if (name == "INTERCEPT")
    return scaled(d.intercept, d.vx, d.ey);
  if (name == "STEYX") {
    if (n < 3)
      return V::error("#DIV/0!");
    I determinant = d.vx * d.vy - d.cov * d.cov;
    I denominator = I(n) * d.vx * (n - 2);
    Q result = ldexp(sqrt(Q(determinant) / Q(denominator)), d.ey);
    return V::numeric(result.convert_to<double>());
  }
  if (name == "FORECAST") {
    auto q = binary(forecast);
    I numerator = d.intercept, denominator = d.vx;
    if (q.mantissa) {
      I term = d.cov * integer(q, q.exponent);
      int shift = q.exponent - d.ex;
      d.charge((size_t(std::abs(shift)) / 64 + 1) * d.limbs + 8);
      if (shift >= 0)
        numerator += term << shift;
      else {
        numerator = (numerator << -shift) + term;
        denominator <<= -shift;
      }
    }
    auto out = scaled(numerator, denominator, d.ey);
    out.incomplete = query.incomplete;
    return out;
  }
  return V::error("#NAME?", true);
} catch (const V &failure) {
  return failure;
}
} // namespace DomeOffice
