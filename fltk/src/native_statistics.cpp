// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_statistics.h"
#include <algorithm>
#include <cmath>
namespace DomeOffice {
namespace {
// Compensated sums and an origin near the observations retain small deviations
// in data with a large offset. Extended intermediates also avoid overflowing a
// square or reciprocal when the final mean or standard deviation fits a cell.
struct Sum {
  long double value = 0, correction = 0;
  void add(long double next) {
    long double total = value + next;
    correction += std::abs(value) >= std::abs(next) ? (value - total) + next
                                                    : (next - total) + value;
    value = total;
  }
  long double get() const { return value + correction; }
};
NativeValue numeric(long double number) {
  return NativeValue::numeric(static_cast<double>(number));
}
} // namespace
const NativeSignatures &statistics_signatures() {
  static const NativeSignatures names = [] {
    NativeSignatures out;
    for (auto name : Text("AVERAGEA AVEDEV DEVSQ DEVSQA GEOMEAN HARMEAN "
                          "STDEVA STDEVPA VARA VARPA SKEW SKEWP KURT KURTP")
                         .split(' '))
      out[name] = {1, 255};
    return out;
  }();
  return names;
}
Text statistics_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  static const std::map<Text, Text> aliases = {
      {"SKEW.P", "SKEWP"},   {"VARIANCE", "VAR"}, {"STDEV.S", "STDEV"},
      {"STDEV.P", "STDEVP"}, {"VAR.S", "VAR"},    {"VAR.P", "VARP"}};
  if (auto found = aliases.find(base); found != aliases.end())
    return found->second;
  return statistics_signatures().contains(base) || base == "STANDARDIZE" ? base
                                                                         : name;
}
bool statistics_includes_text(const Text &name) {
  return name == "AVERAGEA" || name == "DEVSQA" || name == "STDEVA" ||
         name == "STDEVPA" || name == "VARA" || name == "VARPA";
}
NativeValue statistics_function(const Text &name,
                                const std::vector<double> &values) {
  if (!statistics_signatures().contains(name))
    return NativeValue::error("#NAME?", true);
  bool sample = name == "STDEVA" || name == "VARA";
  if (values.size() < (sample ? 2u : 1u))
    return NativeValue::error("#DIV/0!");
  if (values.size() > 1'000'000)
    return NativeValue::error("#LIMIT!", true);
  for (double value : values)
    if (!std::isfinite(value))
      return NativeValue::error("#NUM!");
  if (name == "SKEW" || name == "SKEWP" || name == "KURT" || name == "KURTP") {
    bool skew = name.startsWith("SKEW");
    if (values.size() < (skew ? 3u : 4u))
      return NativeValue::error("#DIV/0!");
    // Shift before scaling to preserve small differences around a large origin.
    // Normalized deviations keep cubed/fourth powers bounded even at extreme
    // double magnitudes; compensated passes retain cancellation in odd moments.
    long double origin = values.front(), scale = 0, n = values.size();
    for (long double value : values)
      scale = std::max(scale, std::abs(value - origin));
    if (!scale)
      return NativeValue::error("#DIV/0!");
    Sum offsets;
    for (long double value : values)
      offsets.add((value - origin) / scale);
    long double mean = offsets.get() / n;
    Sum second, third, fourth;
    for (long double value : values) {
      long double delta = (value - origin) / scale - mean,
                  square = delta * delta;
      second.add(square);
      third.add(square * delta);
      fourth.add(square * square);
    }
    long double m2 = second.get() / n, m3 = third.get() / n,
                m4 = fourth.get() / n;
    if (m2 <= 0)
      return NativeValue::error("#DIV/0!");
    if (skew) {
      long double result = m3 / (m2 * std::sqrt(m2));
      if (name == "SKEW")
        result *= std::sqrt(n * (n - 1)) / (n - 2);
      return numeric(result);
    }
    long double excess = m4 / (m2 * m2) - 3;
    return numeric(name == "KURTP" ? excess
                                   : (n - 1) / ((n - 2) * (n - 3)) *
                                         ((n + 1) * excess + 6));
  }
  if (name == "GEOMEAN" || name == "HARMEAN") {
    long double minimum = *std::min_element(values.begin(), values.end());
    if (minimum <= 0)
      return NativeValue::error("#NUM!");
    Sum total;
    for (long double value : values)
      total.add(name == "GEOMEAN" ? std::log(value) : minimum / value);
    return numeric(name == "GEOMEAN" ? std::exp(total.get() / values.size())
                                     : minimum * (values.size() / total.get()));
  }
  if (name == "AVERAGEA") {
    Sum total;
    for (long double value : values)
      total.add(value);
    return numeric(total.get() / values.size());
  }
  long double origin = values.front();
  Sum offsets;
  for (long double value : values)
    offsets.add(value - origin);
  long double mean_offset = offsets.get() / values.size();
  Sum deviations;
  for (long double value : values) {
    long double delta = (value - origin) - mean_offset;
    deviations.add(name == "AVEDEV" ? std::abs(delta) : delta * delta);
  }
  if (name == "AVEDEV")
    return numeric(deviations.get() / values.size());
  if (name == "DEVSQ" || name == "DEVSQA")
    return numeric(deviations.get());
  long double variance = deviations.get() / (values.size() - (sample ? 1 : 0));
  return numeric(name.startsWith("STDEV") ? std::sqrt(variance) : variance);
}
} // namespace DomeOffice
