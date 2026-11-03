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
                          "STDEVA STDEVPA VARA VARPA")
                         .split(' '))
      out[name] = {1, 255};
    return out;
  }();
  return names;
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
