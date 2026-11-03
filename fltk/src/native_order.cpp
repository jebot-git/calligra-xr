// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_order.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace DomeOffice {
namespace {
using V = NativeValue;
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
  bool valid = false;
  double n = v.string.toDouble(&valid);
  if (!valid)
    throw V::error("#VALUE!");
  if (!std::isfinite(n))
    throw V::error("#NUM!");
  return n;
}
double interpolate(double a, double b, long double fraction) {
  return static_cast<double>(std::lerp(static_cast<long double>(a),
                                       static_cast<long double>(b), fraction));
}
// Exact decimal truncation of an integer rank fraction avoids dropping a full
// last decimal place because a representable rational rounded downward.
double truncate_fraction(size_t numerator, size_t denominator, int digits) {
  size_t remainder = numerator % denominator;
  uint64_t result = numerator / denominator, scale = 1;
  for (int i = 0; i < digits; ++i) {
    remainder *= 10;
    result = result * 10 + remainder / denominator;
    remainder %= denominator;
    scale *= 10;
  }
  return static_cast<double>(result) / scale;
}
double decimal_rank(long double rank, double digits) {
  // Beyond the smallest double's decimal place, truncation cannot change the
  // stored result. Check before converting an arbitrarily large significance.
  if (digits > 324)
    return static_cast<double>(rank);
  long double scale = std::pow(10.L, int(digits));
  if (!std::isfinite(scale))
    return static_cast<double>(rank);
  long double scaled = rank * scale;
  // Recover a decimal boundary obscured by input-double roundoff (the
  // published 5.43 example is 0.381, not 0.380). The cap keeps this from
  // rounding a meaningful fraction of the requested last decimal place.
  long double nearest = std::round(scaled);
  long double tolerance = std::min(
      1e-7L, 16 * std::numeric_limits<double>::epsilon() * std::abs(scaled));
  if (std::abs(scaled - nearest) <= tolerance)
    scaled = nearest;
  return static_cast<double>(std::floor(scaled) / scale);
}

} // namespace
const NativeSignatures &order_signatures() {
  static const NativeSignatures names = {
      {"SMALL", {2, 2}},       {"LARGE", {2, 2}},
      {"RANK", {2, 3}},        {"RANK.AVG", {2, 3}},
      {"PERCENTILE", {2, 2}},  {"PERCENTILE.EXC", {2, 2}},
      {"QUARTILE", {2, 2}},    {"QUARTILE.EXC", {2, 2}},
      {"PERCENTRANK", {2, 3}}, {"PERCENTRANK.EXC", {2, 3}},
      {"MODE", {1, 255}},      {"MODE.SNGL", {1, 255}},
      {"MODE.MULT", {1, 255}}, {"MEDIAN", {1, 255}},
      {"TRIMMEAN", {2, 2}}};
  return names;
}
Text order_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  static const std::map<Text, Text> aliases = {
      {"RANK.EQ", "RANK"},
      {"PERCENTILE.INC", "PERCENTILE"},
      {"QUARTILE.INC", "QUARTILE"},
      {"PERCENTRANK.INC", "PERCENTRANK"}};
  if (auto alias = aliases.find(base); alias != aliases.end())
    return alias->second;
  return order_signatures().contains(base) ? base : name;
}
NativeOrderData::NativeOrderData(std::vector<double> values)
    : original_(std::move(values)) {
  if (original_.size() > 1'000'000)
    throw V::error("#LIMIT!", true);
  if (std::any_of(original_.begin(), original_.end(),
                  [](double x) { return !std::isfinite(x); }))
    throw V::error("#NUM!");
  sorted_ = original_;
  std::sort(sorted_.begin(), sorted_.end());
}
V NativeOrderData::evaluate(const Text &name,
                            const std::vector<V> &parameters) const try {
  if (!order_signatures().contains(name))
    return V::error("#NAME?", true);
  bool aggregate = name == "MEDIAN" || name.startsWith("MODE");
  bool optional = name.startsWith("RANK") || name.startsWith("PERCENTRANK");
  if ((aggregate && !parameters.empty()) ||
      (!aggregate &&
       (parameters.empty() || parameters.size() > (optional ? 2u : 1u))))
    return V::error("#VALUE!");
  const auto &data = sorted_;
  size_t n = data.size();
  if (!n)
    return V::error(name.startsWith("RANK") || name.startsWith("MODE") ||
                            name.startsWith("PERCENTRANK")
                        ? "#N/A"
                        : "#NUM!");
  if (name == "MEDIAN") {
    size_t m = n / 2;
    return V::numeric(n % 2 ? data[m] : interpolate(data[m - 1], data[m], .5L));
  }
  if (name.startsWith("MODE")) {
    std::vector<double> modes;
    size_t maximum = 1;
    for (size_t first = 0; first < n;) {
      size_t end =
          std::upper_bound(data.begin() + first, data.end(), data[first]) -
          data.begin();
      size_t count = end - first;
      if (count > maximum) {
        maximum = count;
        modes.clear();
      }
      if (count == maximum && count > 1)
        modes.push_back(data[first]);
      first = end;
    }
    if (modes.empty())
      return V::error("#N/A");
    // OpenFormula MODE chooses the smallest tied value. OOXML MODE.SNGL
    // and MODE.MULT retain the input encounter order instead.
    if (name == "MODE")
      return V::numeric(modes.front());
    std::vector<V> result;
    std::vector<bool> emitted(modes.size(), false);
    for (double value : original_) {
      auto found = std::lower_bound(modes.begin(), modes.end(), value);
      if (found == modes.end() || *found != value)
        continue;
      if (name == "MODE.SNGL")
        return V::numeric(value);
      size_t index = found - modes.begin();
      if (!emitted[index]) {
        result.push_back(V::numeric(value));
        emitted[index] = true;
      }
    }
    size_t size = result.size();
    return V::array(size, 1, std::move(result));
  }
  if (parameters.empty() || parameters.size() > 2)
    return V::error("#VALUE!");
  double x = number(parameters[0]);
  if (name == "TRIMMEAN") {
    if (x < 0 || x >= 1)
      return V::error("#NUM!");
    size_t trim =
        static_cast<size_t>(std::floor(static_cast<double>(n) * x / 2));
    long double total = 0, correction = 0;
    for (size_t i = trim; i < n - trim; ++i) {
      long double next = data[i], sum = total + next;
      correction += std::abs(total) >= std::abs(next) ? (total - sum) + next
                                                      : (next - sum) + total;
      total = sum;
    }
    return V::numeric(
        static_cast<double>((total + correction) / (n - 2 * trim)));
  }
  if (name == "SMALL" || name == "LARGE") {
    x = std::trunc(x);
    if (x < 1 || x > static_cast<double>(n))
      return V::error("#NUM!");
    return V::numeric(data[name == "SMALL" ? size_t(x) - 1 : n - size_t(x)]);
  }
  if (name == "RANK" || name == "RANK.AVG") {
    bool ascending = parameters.size() > 1 && number(parameters[1]) != 0;
    auto low = std::lower_bound(data.begin(), data.end(), x);
    if (low == data.end() || *low != x)
      return V::error("#N/A");
    auto high = std::upper_bound(low, data.end(), x);
    size_t below = low - data.begin(), above = data.end() - high;
    double rank = 1 + (ascending ? below : above);
    if (name == "RANK.AVG")
      rank += static_cast<double>(high - low - 1) / 2;
    return V::numeric(rank);
  }
  bool exclusive = name.endsWith(".EXC");
  if (name.startsWith("QUARTILE") || name.startsWith("PERCENTILE")) {
    if (name.startsWith("QUARTILE")) {
      x = std::trunc(x);
      if (x < 0 || x > 4)
        return V::error("#NUM!");
      x /= 4;
    }
    if (exclusive) {
      double lower = 1. / (n + 1), upper = static_cast<double>(n) / (n + 1);
      if (x <= 0 || x >= 1 || x < lower || x > upper)
        return V::error("#NUM!");
    } else if (x < 0 || x > 1)
      return V::error("#NUM!");
    long double position = exclusive ? static_cast<long double>(x) * (n + 1) - 1
                                     : static_cast<long double>(x) * (n - 1);
    position = std::clamp(position, 0.L, static_cast<long double>(n - 1));
    size_t first = static_cast<size_t>(position);
    if (first == n - 1)
      return V::numeric(data.back());
    return V::numeric(
        interpolate(data[first], data[first + 1], position - first));
  }
  if (name.startsWith("PERCENTRANK")) {
    double significance =
        parameters.size() > 1 ? std::trunc(number(parameters[1])) : 3;
    if (significance < 1)
      return V::error("#NUM!");
    if (x < data.front() || x > data.back())
      return V::error("#N/A");
    if (n == 1)
      return V::numeric(exclusive ? .5 : 1);
    auto upper = std::lower_bound(data.begin(), data.end(), x);
    size_t index = upper - data.begin();
    size_t denominator = exclusive ? n + 1 : n - 1;
    if (*upper == x) {
      size_t numerator = index + (exclusive ? 1 : 0);
      return V::numeric(
          significance <= 18
              ? truncate_fraction(numerator, denominator, int(significance))
              : decimal_rank(static_cast<long double>(numerator) / denominator,
                             significance));
    }
    size_t lower = index - 1;
    long double fraction =
        (static_cast<long double>(x) - data[lower]) /
        (static_cast<long double>(data[index]) - data[lower]);
    long double rank = (lower + (exclusive ? 1 : 0) + fraction) / denominator;
    return V::numeric(decimal_rank(rank, significance));
  }
  return V::error("#NAME?", true);
} catch (const V &failure) {
  return failure;
}
} // namespace DomeOffice
