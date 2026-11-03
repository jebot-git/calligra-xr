// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_inference.h"
#include "native_exact_sample.h"
#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/fisher_f.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <cmath>
#include <limits>
#include <numeric>
namespace DomeOffice {
namespace {
using V = NativeValue;
using NativeExact::Q;
using NativeExact::Sample;
namespace M = boost::math;
namespace P = boost::math::policies;
using Policy =
    P::policy<P::underflow_error<P::ignore_error>,
              P::denorm_error<P::ignore_error>,
              P::max_series_iterations<100000>, P::max_root_iterations<256>>;
template <template <class, class> class D> using Dist = D<long double, Policy>;
V numeric(long double x) { return V::numeric(static_cast<double>(x)); }
double number(const V &v) {
  if (v.isError())
    throw v;
  bool ok = true;
  double n = v.isNumber()          ? v.number
             : v.isBoolean()       ? (v.boolean ? 1 : 0)
             : v.kind == V::Empty  ? 0
             : v.kind == V::String ? v.string.toDouble(&ok)
                                   : (ok = false, 0);
  if (!ok)
    throw V::error("#VALUE!");
  if (!std::isfinite(n))
    throw V::error("#NUM!");
  return n;
}
struct Sum {
  long double sum = 0, correction = 0;
  void add(long double next) {
    long double total = sum + next;
    correction += std::abs(sum) >= std::abs(next) ? (sum - total) + next
                                                  : (next - total) + sum;
    sum = total;
  }
  long double get() const { return sum + correction; }
};
} // namespace
const NativeSignatures &inference_signatures() {
  static const NativeSignatures names = {
      {"FREQUENCY", {2, 2}}, {"PROB", {3, 4}},   {"FTEST", {2, 2}},
      {"TTEST", {4, 4}},     {"T.TEST", {4, 4}}, {"ZTEST", {2, 3}},
      {"Z.TEST", {2, 3}},    {"CHITEST", {2, 2}}};
  return names;
}
Text inference_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  static const std::map<Text, Text> aliases = {{"F.TEST", "FTEST"},
                                               {"CHISQ.TEST", "CHITEST"},
                                               {"LEGACY.CHITEST", "CHITEST"},
                                               {"LEGACYCHITEST", "CHITEST"}};
  if (auto i = aliases.find(base); i != aliases.end())
    return i->second;
  return inference_signatures().contains(base) ? base : name;
}
struct NativeHypothesisData::Impl {
  std::vector<double> x, y;
  std::optional<std::vector<std::pair<double, double>>> pairs;
  mutable std::optional<Sample> sx, sy, difference, paired_x, paired_y;
  Impl(std::vector<double> a, std::vector<double> b,
       std::optional<std::vector<std::pair<double, double>>> p)
      : x(std::move(a)), y(std::move(b)), pairs(std::move(p)) {}
  const Sample &first(bool paired = false) const {
    if (!paired) {
      if (!sx)
        sx.emplace(x);
      return *sx;
    }
    if (!paired_x) {
      std::vector<double> a;
      for (auto [x, y] : *pairs)
        a.push_back(x);
      paired_x.emplace(a);
    }
    return *paired_x;
  }
  const Sample &second(bool paired = false) const {
    if (!paired) {
      if (!sy)
        sy.emplace(y);
      return *sy;
    }
    if (!paired_y) {
      std::vector<double> b;
      for (auto [x, y] : *pairs)
        b.push_back(y);
      paired_y.emplace(b);
    }
    return *paired_y;
  }
};
NativeHypothesisData::NativeHypothesisData(
    std::vector<double> a, std::vector<double> b,
    std::optional<std::vector<std::pair<double, double>>> p)
    : impl_(std::make_shared<Impl>(std::move(a), std::move(b), std::move(p))) {}
V NativeHypothesisData::evaluate(const Text &name,
                                 const std::vector<V> &args) const try {
  for (const auto &v : args)
    if (v.isError())
      return v;
  const auto &d = *impl_;
  if (name == "ZTEST" || name == "Z.TEST") {
    if (args.empty() || args.size() > 2)
      return V::error("#VALUE!");
    const auto &s = d.first();
    if (!s.count)
      return V::error("#N/A");
    if (s.count < 2 && (name == "ZTEST" || args.size() < 2))
      return V::error("#DIV/0!");
    Q sd = args.size() == 2 ? Q(number(args[1])) : sqrt(s.variance());
    if (sd <= 0)
      return V::error(args.size() == 2 ? "#NUM!" : "#DIV/0!");
    Q z = s.difference(number(args[0])) * sqrt(Q(s.count)) / sd;
    return numeric(M::cdf(M::complement(Dist<M::normal_distribution>(),
                                        z.convert_to<long double>())));
  }
  if (name == "FTEST") {
    const auto &a = d.first(), &b = d.second();
    if (a.count < 2 || b.count < 2)
      return V::error("#DIV/0!");
    Q va = a.variance(), vb = b.variance();
    if (va == 0 || vb == 0)
      return V::error("#DIV/0!");
    auto ratio = (va / vb).convert_to<long double>();
    Dist<M::fisher_f_distribution> f(a.count - 1, b.count - 1);
    return numeric(2 *
                   std::min(M::cdf(f, ratio), M::cdf(M::complement(f, ratio))));
  }
  if (name == "TTEST" || name == "T.TEST") {
    if (args.size() != 2)
      return V::error("#VALUE!");
    double tails = std::trunc(number(args[0])),
           type = std::trunc(number(args[1]));
    if ((tails != 1 && tails != 2) || (type < 1 || type > 3))
      return V::error("#NUM!");
    Q t, df;
    if (type == 1) {
      if (!d.pairs)
        return V::error("#N/A");
      if (!d.difference)
        d.difference.emplace(*d.pairs);
      const auto &s = *d.difference;
      if (s.count < 2)
        return V::error("#DIV/0!");
      Q variance = s.variance();
      if (variance == 0)
        return V::error("#DIV/0!");
      t = abs(s.difference(0)) * sqrt(Q(s.count) / variance);
      df = s.count - 1;
    } else {
      // OpenFormula pairs nonnumeric filtering at corresponding positions for
      // equal-shaped arrays. Modern independent tests filter samples
      // separately; unequal-shaped independent arrays are also accepted as an
      // ODF extension.
      bool pair_filter = name == "TTEST" && d.pairs.has_value();
      const auto &a = d.first(pair_filter), &b = d.second(pair_filter);
      if (a.count < 2 || b.count < 2)
        return V::error("#DIV/0!");
      Q va = a.variance(), vb = b.variance(), denominator;
      if (type == 2) {
        df = a.count + b.count - 2;
        denominator = ((a.count - 1) * va + (b.count - 1) * vb) / df *
                      (Q(1) / a.count + Q(1) / b.count);
      } else {
        Q aa = va / a.count, bb = vb / b.count;
        denominator = aa + bb;
        if (denominator == 0)
          return V::error("#DIV/0!");
        df = denominator * denominator /
             (aa * aa / (a.count - 1) + bb * bb / (b.count - 1));
      }
      if (denominator == 0)
        return V::error("#DIV/0!");
      t = abs(a.difference(b)) / sqrt(denominator);
    }
    // Welch's df is deliberately fractional; the scalar TDIST function
    // truncates its argument and cannot be used to evaluate a Welch test.
    return numeric(tails *
                   M::cdf(M::complement(Dist<M::students_t_distribution>(
                                            df.convert_to<long double>()),
                                        t.convert_to<long double>())));
  }
  return V::error("#NAME?", true);
} catch (const V &v) {
  return v;
} catch (const M::evaluation_error &) {
  return V::error("#LIMIT!", true);
} catch (const M::rounding_error &) {
  return V::error("#LIMIT!", true);
} catch (const std::domain_error &) {
  return V::error("#NUM!");
} catch (const std::overflow_error &) {
  return V::error("#NUM!");
}

NativeProbabilityData::NativeProbabilityData(
    std::vector<double> values, std::vector<double> probabilities) {
  if (values.empty() || values.size() != probabilities.size())
    throw V::error("#N/A");
  if (values.size() > 1'000'000)
    throw V::error("#LIMIT!", true);
  std::vector<std::pair<double, double>> sorted;
  Sum total;
  for (size_t i = 0; i < values.size(); ++i) {
    double x = values[i], p = probabilities[i];
    if (!std::isfinite(x) || !std::isfinite(p) || p < 0 || p > 1)
      throw V::error("#NUM!");
    sorted.emplace_back(x, p);
    total.add(p);
  }
  // Permit only input-rounding noise, rather than the old broad 1e-7 threshold.
  if (std::abs(total.get() - 1) > 8 * std::numeric_limits<double>::epsilon())
    throw V::error("#NUM!");
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](auto a, auto b) { return a.first < b.first; });
  while (base_ < sorted.size())
    base_ *= 2;
  tree_.resize(base_ * 2);
  for (size_t i = 0; i < sorted.size(); ++i) {
    values_.push_back(sorted[i].first);
    tree_[base_ + i] = sorted[i].second;
  }
  for (size_t i = base_ - 1; i > 0; --i)
    tree_[i] = tree_[i * 2] + tree_[i * 2 + 1];
}
V NativeProbabilityData::evaluate(const V &lower, const V &upper) const try {
  if (lower.isError())
    return lower;
  if (upper.isError())
    return upper;
  double a = number(lower), b = number(upper);
  if (b < a)
    return numeric(0);
  size_t left = std::lower_bound(values_.begin(), values_.end(), a) -
                values_.begin() + base_;
  size_t right = std::upper_bound(values_.begin(), values_.end(), b) -
                 values_.begin() + base_;
  // Range sums never subtract nearly equal prefix totals: an isolated tiny
  // probability after a large mass must retain all its representable digits.
  Sum total;
  while (left < right) {
    if (left & 1)
      total.add(tree_[left++]);
    if (right & 1)
      total.add(tree_[--right]);
    left /= 2;
    right /= 2;
  }
  return numeric(std::clamp(total.get(), 0.L, 1.L));
} catch (const V &v) {
  return v;
}
V native_frequency(std::vector<double> data, const std::vector<double> &bins) {
  if (data.size() > 1'000'000 || bins.size() >= 1'000'000)
    return V::error("#LIMIT!", true);
  for (double x : data)
    if (!std::isfinite(x))
      return V::error("#NUM!");
  for (double x : bins)
    if (!std::isfinite(x))
      return V::error("#NUM!");
  std::sort(data.begin(), data.end());
  std::vector<size_t> order(bins.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&](auto a, auto b) { return bins[a] < bins[b]; });
  std::vector<V> out(bins.size() + 1, V::numeric(0));
  auto start = data.begin();
  for (size_t i : order) {
    auto end = std::upper_bound(start, data.end(), bins[i]);
    out[i] = V::numeric(end - start);
    start = end;
  }
  out.back() = V::numeric(data.end() - start);
  auto rows = out.size();
  return V::array(rows, 1, std::move(out));
}
V native_chi_test(const std::vector<std::pair<double, double>> &pairs,
                  size_t rows, size_t columns) try {
  size_t df =
      rows > 1 && columns > 1 ? (rows - 1) * (columns - 1) : rows * columns - 1;
  if (!rows || !columns || rows * columns < 2 || !df)
    return V::error("#N/A");
  if (pairs.size() > 1'000'000)
    return V::error("#LIMIT!", true);
  Sum sum;
  for (auto [observed, expected] : pairs) {
    if (!std::isfinite(observed) || !std::isfinite(expected))
      return V::error("#NUM!");
    if (expected == 0)
      return V::error("#DIV/0!");
    if (expected < 0)
      return V::error("#NUM!");
    long double delta = static_cast<long double>(observed) - expected;
    sum.add(delta * delta / expected);
  }
  return numeric(
      M::cdf(M::complement(Dist<M::chi_squared_distribution>(df), sum.get())));
} catch (const M::evaluation_error &) {
  return V::error("#LIMIT!", true);
} catch (const M::rounding_error &) {
  return V::error("#LIMIT!", true);
} catch (const std::domain_error &) {
  return V::error("#NUM!");
} catch (const std::overflow_error &) {
  return V::error("#NUM!");
}
} // namespace DomeOffice
