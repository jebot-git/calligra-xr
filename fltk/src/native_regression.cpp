// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_regression.h"
#include "native_exact_sample.h"
#include <boost/multiprecision/integer.hpp>
#include <numeric>
#include <bit>
namespace DomeOffice {
namespace {
using V = NativeValue;
using Matrix = NativeRegressionMatrix;
using NativeExact::I;
using NativeExact::Q;
using R = boost::multiprecision::cpp_rational;
struct Budget {
  mutable size_t remaining = 50'000'000;
  void charge(size_t n = 1) const {
    if (n > remaining)
      throw V::error("#LIMIT!", true);
    remaining -= n;
  }
  void integer_product(const I &a, const I &b) const {
    charge(a.backend().size() * b.backend().size() + 1);
  }
  size_t limbs(const R &r) const {
    return numerator(r).backend().size() + denominator(r).backend().size();
  }
  void rational(const R &a, const R &b) const {
    size_t n = limbs(a) + limbs(b);
    charge(n * n + 1);
  }
  R add(const R &a, const R &b) const {
    rational(a, b);
    return a + b;
  }
  R sub(const R &a, const R &b) const {
    rational(a, b);
    return a - b;
  }
  R mul(const R &a, const R &b) const {
    rational(a, b);
    return a * b;
  }
  R div(const R &a, const R &b) const {
    if (b == 0)
      throw V::error("#DIV/0!");
    rational(a, b);
    return a / b;
  }
};
R shifted(const R &r, int exponent) {
  if (r == 0 || !exponent)
    return r;
  I n = numerator(r), d = denominator(r);
  if (exponent > 0)
    n <<= exponent;
  else
    d <<= -exponent;
  return R(n, d);
}
// Avoid overflowing numerator and denominator separately when their ratio fits.
template <class Float> Float floating(const R &r) {
  I n = numerator(r), d = denominator(r);
  if (n == 0)
    return 0;
  bool negative = n < 0;
  if (negative)
    n = -n;
  int keep = std::numeric_limits<Float>::digits + 16;
  int ns = std::max(0, int(boost::multiprecision::msb(n)) + 1 - keep);
  int ds = std::max(0, int(boost::multiprecision::msb(d)) + 1 - keep);
  Float value = ldexp(Float(n >> ns) / Float(d >> ds), ns - ds);
  return negative ? -value : value;
}
V numeric(const R &r, int exponent = 0) {
  R value = shifted(r, exponent);
  if (value == 0)
    return V::numeric(0);
  I n = numerator(value), d = denominator(value);
  if (n < 0)
    n = -n;
  int magnitude =
      int(boost::multiprecision::msb(n)) - int(boost::multiprecision::msb(d));
  if (magnitude > 1025)
    return V::error("#NUM!");
  if (magnitude < -1076)
    return V::numeric(0);
  return V::numeric(floating<Q>(value).convert_to<double>());
}
V root(const R &r, int exponent = 0) {
  if (r < 0)
    return V::error("#NUM!");
  if (r == 0)
    return V::numeric(0);
  R value = shifted(r, 2 * exponent);
  I n = numerator(value), d = denominator(value);
  int magnitude =
      int(boost::multiprecision::msb(n)) - int(boost::multiprecision::msb(d));
  if (magnitude > 2050)
    return V::error("#NUM!");
  if (magnitude < -2152)
    return V::numeric(0);
  return V::numeric(sqrt(floating<Q>(value)).convert_to<double>());
}
size_t slots(const Matrix &m) {
  if (!m.rows || !m.columns)
    return 0;
  if (m.rows > std::numeric_limits<size_t>::max() / m.columns)
    throw V::error("#LIMIT!", true);
  size_t n = m.rows * m.columns;
  if (m.entries.size() > 1'000'000)
    throw V::error("#LIMIT!", true);
  for (auto [offset, x] : m.entries) {
    if (offset >= n)
      throw V::error("#VALUE!");
    if (!std::isfinite(x))
      throw V::error("#NUM!");
  }
  return n;
}
struct Column {
  // Ordinal -1 is the fixed intercept column, which must be retained first.
  int ordinal = 0, exponent = 0;
  bool constant = false, generated = false;
  std::map<size_t, I> values;
  I sum = 0, squares = 0;
  I at(size_t observation) const {
    if (constant)
      return 1;
    if (generated)
      return I(observation + 1);
    auto p = values.find(observation);
    return p == values.end() ? I(0) : p->second;
  }
};
// An exact symmetric LDL factorization identifies a maximal independent column
// subset. Positive semidefiniteness makes a zero pivot an exact dependency.
// No floating normal-equation inversion or epsilon rank decision is involved.
struct System {
  size_t dimension = 0;
  const Budget &budget;
  std::vector<R> lower, diagonal;
  std::vector<size_t> selected;
  mutable std::optional<std::vector<R>> inverse_diagonal;
  System(const std::vector<I> &gram, size_t n, const Budget &b)
      : dimension(n), budget(b) {
    if (n && n > 1'000'000 / n)
      throw V::error("#LIMIT!", true);
    lower.resize(n * n);
    diagonal.resize(n);
    std::vector<R> schur(gram.begin(), gram.end());
    for (size_t j = 0; j < n; ++j) {
      budget.charge(n - j);
      R pivot = schur[j * n + j];
      if (pivot < 0)
        throw V::error("#NUM!");
      if (pivot == 0) {
        for (size_t i = j + 1; i < n; ++i)
          if (schur[i * n + j] != 0)
            throw V::error("#NUM!");
        continue;
      }
      selected.push_back(j);
      diagonal[j] = pivot;
      for (size_t i = j + 1; i < n; ++i) {
        budget.charge(n - i + 1);
        if (schur[i * n + j] == 0)
          continue;
        R factor = budget.div(schur[i * n + j], pivot);
        lower[i * n + j] = factor;
        for (size_t k = i; k < n; ++k)
          if (schur[k * n + j] != 0)
            schur[k * n + i] = budget.sub(schur[k * n + i],
                                          budget.mul(factor, schur[k * n + j]));
      }
    }
  }
  std::vector<R> solve(const std::vector<R> &rhs) const {
    std::vector<R> z(dimension), out(dimension);
    for (size_t i : selected) {
      budget.charge(selected.size() + 1);
      z[i] = rhs[i];
      for (size_t k : selected) {
        if (k >= i)
          break;
        if (lower[i * dimension + k] != 0)
          z[i] = budget.sub(z[i], budget.mul(lower[i * dimension + k], z[k]));
      }
    }
    for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
      budget.charge(selected.size() + 1);
      size_t i = *it;
      out[i] = budget.div(z[i], diagonal[i]);
      for (size_t k : selected)
        if (k > i && lower[k * dimension + i] != 0)
          out[i] =
              budget.sub(out[i], budget.mul(lower[k * dimension + i], out[k]));
    }
    return out;
  }
  const std::vector<R> &inverse() const {
    if (!inverse_diagonal) {
      std::vector<R> result(dimension);
      for (size_t i : selected) {
        std::vector<R> unit(dimension);
        unit[i] = 1;
        result[i] = solve(unit)[i];
      }
      inverse_diagonal = std::move(result);
    }
    return *inverse_diagonal;
  }
};
// All candidate integers are binary64 mantissas, at most 2^53-1. Testing the
// first twelve prime Miller-Rabin bases is deterministic in this domain; see
// Sorenson/Webster, https://arxiv.org/abs/1509.00864. Factor products are
// exact.
struct MantissaFactors {
  const Budget &budget;
  std::map<uint64_t, std::map<uint64_t, int>> cache;
  uint64_t multiply(uint64_t a, uint64_t b, uint64_t n) const {
    budget.charge();
    return uint64_t((__uint128_t(a) * b) % n);
  }
  uint64_t power(uint64_t a, uint64_t e, uint64_t n) const {
    uint64_t r = 1;
    while (e) {
      if (e & 1)
        r = multiply(r, a, n);
      e >>= 1;
      if (e)
        a = multiply(a, a, n);
    }
    return r;
  }
  bool prime(uint64_t n) const {
    if (n < 2)
      return false;
    for (uint64_t p : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37}) {
      if (n == p)
        return true;
      if (n % p == 0)
        return false;
    }
    auto shifts = std::countr_zero(n - 1);
    auto odd = (n - 1) >> shifts;
    for (uint64_t a : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37}) {
      auto x = power(a, odd, n);
      if (x == 1 || x == n - 1)
        continue;
      bool witness = true;
      for (int j = 1; j < shifts; ++j) {
        x = multiply(x, x, n);
        if (x == n - 1) {
          witness = false;
          break;
        }
      }
      if (witness)
        return false;
    }
    return true;
  }
  uint64_t divisor(uint64_t n) const {
    for (uint64_t p : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37})
      if (n % p == 0)
        return p;
    // Deterministic Pollard-rho restarts; a failed cycle never labels n prime.
    for (uint64_t seed = 1;; ++seed) {
      uint64_t x = 2, y = 2, g = 1;
      auto step = [&](uint64_t z) { return (multiply(z, z, n) + seed) % n; };
      for (size_t step_count = 0; step_count < 250'000 && g == 1;
           ++step_count) {
        x = step(x);
        y = step(step(y));
        budget.charge();
        g = std::gcd(x > y ? x - y : y - x, n);
      }
      if (g > 1 && g < n)
        return g;
    }
  }
  void split(uint64_t n, std::map<uint64_t, int> &out) const {
    if (n == 1)
      return;
    if (prime(n)) {
      ++out[n];
      return;
    }
    auto d = divisor(n);
    split(d, out);
    split(n / d, out);
  }
  const std::map<uint64_t, int> &operator()(uint64_t n) {
    auto found = cache.find(n);
    if (found != cache.end())
      return found->second;
    std::map<uint64_t, int> out;
    split(n, out);
    uint64_t product = 1;
    for (auto [p, e] : out)
      for (int i = 0; i < e; ++i) {
        budget.charge();
        product *= p;
      }
    if (product != n)
      throw V::error("#NUM!");
    if (cache.size() >= 1'000'000)
      throw V::error("#LIMIT!", true);
    return cache.emplace(n, std::move(out)).first->second;
  }
};
using LogForm = std::map<size_t, R>;
struct LogBasis {
  uint64_t prime = 2;
  std::map<size_t, I> values;
  I sum = 0;
  std::vector<R> fitted;
};
struct LogStats {
  std::vector<LogForm> residual, total, explained;
  std::array<bool, 3> positive = {false, false, false};
};
struct LogEvaluation {
  V value;
  std::array<Q, 3> sums = {0, 0, 0};
  bool valid = true;
};
bool same_result(const V &a, const V &b) {
  if (a.kind != b.kind || a.incomplete || b.incomplete)
    return false;
  if (a.isArray()) {
    if (a.rows != b.rows || a.columns != b.columns)
      return false;
    for (size_t i = 0; i < a.items->size(); ++i)
      if (!same_result((*a.items)[i], (*b.items)[i]))
        return false;
    return true;
  }
  if (a.isNumber())
    return a.number == b.number ||
           std::nextafter(a.number, b.number) == b.number;
  return a.isError() && a.string == b.string;
}
bool same_sums(const LogEvaluation &a, const LogEvaluation &b) {
  if (!a.valid || !b.valid)
    return false;
  for (size_t i = 0; i < a.sums.size(); ++i) {
    if (a.sums[i] == 0 && b.sums[i] == 0)
      continue;
    if (a.sums[i] <= 0 || b.sums[i] <= 0)
      return false;
    if (abs(a.sums[i] - b.sums[i]) > ldexp(std::max(a.sums[i], b.sums[i]), -70))
      return false;
  }
  return true;
}

} // namespace
const NativeSignatures &regression_signatures() {
  static const NativeSignatures names = {{"LINEST", {1, 4}},
                                         {"TREND", {1, 4}},
                                         {"LOGEST", {1, 4}},
                                         {"GROWTH", {1, 4}}};
  return names;
}
Text regression_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  return regression_signatures().contains(base) ? base : name;
}
struct NativeRegressionModel::Impl {
  enum Orientation { Simple, Rows, Columns } orientation = Simple;
  Matrix y;
  size_t observations = 0, predictors = 1;
  bool intercept = true, logarithmic = false;
  mutable Budget budget;
  std::vector<Column> columns;
  std::map<size_t, I> response;
  int ey = 0;
  I sy = 0, syy = 0;
  std::vector<I> gram, rhs;
  std::unique_ptr<System> system;
  std::vector<R> coefficients;
  R residual = 0, total = 0, explained = 0;
  size_t observation(size_t row, size_t column) const {
    return orientation == Simple ? column * y.rows + row
           : orientation == Rows ? row
                                 : column;
  }
  explicit Impl(Matrix yy, std::optional<Matrix> xx, bool constant, bool log)
      : y(std::move(yy)), intercept(constant), logarithmic(log) {
    observations = slots(y);
    if (!observations)
      throw V::error("#N/A");
    if (xx) {
      if (!slots(*xx))
        throw V::error("#N/A");
      if (xx->rows == y.rows && xx->columns == y.columns)
        orientation = Simple;
      else if (y.columns == 1 && xx->rows == y.rows) {
        orientation = Rows;
        predictors = xx->columns;
        observations = y.rows;
      } else if (y.rows == 1 && xx->columns == y.columns) {
        orientation = Columns;
        predictors = xx->rows;
        observations = y.columns;
      } else
        throw V::error("#N/A");
    }
    if (observations <= predictors + (intercept ? 1 : 0))
      throw V::error("#DIV/0!");
    if (predictors >= 1'000'000)
      throw V::error("#LIMIT!", true);
    if (intercept) {
      Column c;
      c.ordinal = -1;
      c.constant = true;
      c.sum = observations;
      c.squares = observations;
      columns.push_back(std::move(c));
    }
    if (!xx) {
      Column c;
      c.generated = true;
      c.sum = I(observations) * (observations + 1) / 2;
      c.squares =
          I(observations) * (observations + 1) * (I(2) * observations + 1) / 6;
      columns.push_back(std::move(c));
    } else {
      std::map<size_t, std::vector<std::pair<size_t, double>>> raw;
      for (auto [offset, value] : xx->entries)
        if (value != 0) {
          budget.charge();
          size_t row = offset / xx->columns, col = offset % xx->columns;
          size_t ordinal = orientation == Simple ? 0
                           : orientation == Rows ? col
                                                 : row;
          raw[ordinal].emplace_back(observation(row, col), value);
        }
      for (auto &[ordinal, values] : raw) {
        Column c;
        c.ordinal = int(ordinal);
        bool initialized = false;
        for (auto [o, x] : values) {
          auto b = NativeExact::binary(x);
          c.exponent =
              initialized ? std::min(c.exponent, b.exponent) : b.exponent;
          initialized = true;
        }
        for (auto [o, x] : values) {
          I v = NativeExact::integer(NativeExact::binary(x), c.exponent);
          budget.integer_product(v, v);
          c.sum += v;
          c.squares += v * v;
          c.values.emplace(o, std::move(v));
        }
        columns.push_back(std::move(c));
      }
    }
    if (!logarithmic) {
      bool nonzero = false;
      for (auto [offset, x] : y.entries)
        if (x != 0) {
          auto b = NativeExact::binary(x);
          ey = nonzero ? std::min(ey, b.exponent) : b.exponent;
          nonzero = true;
        }
      for (auto [offset, x] : y.entries)
        if (x != 0) {
          I v = NativeExact::integer(NativeExact::binary(x), ey);
          budget.integer_product(v, v);
          sy += v;
          syy += v * v;
          response.emplace(observation(offset / y.columns, offset % y.columns),
                           std::move(v));
        }
    }
    size_t d = columns.size();
    if (d && d > 1'000'000 / d)
      throw V::error("#LIMIT!", true);
    gram.resize(d * d);
    rhs.resize(d);
    for (size_t j = 0; j < d; ++j) {
      auto &a = columns[j];
      gram[j * d + j] = a.squares;
      for (auto &[o, v] : response) {
        I x = a.at(o);
        budget.integer_product(x, v);
        rhs[j] += x * v;
      }
      for (size_t i = j + 1; i < d; ++i) {
        const auto &b = columns[i];
        I dot = 0;
        if (a.constant)
          dot = b.sum;
        else if (b.constant)
          dot = a.sum;
        else {
          auto x = a.values.cbegin(), z = b.values.cbegin();
          while (x != a.values.end() && z != b.values.end()) {
            budget.charge();
            if (x->first < z->first)
              ++x;
            else if (z->first < x->first)
              ++z;
            else {
              budget.integer_product(x->second, z->second);
              dot += x->second * z->second;
              ++x;
              ++z;
            }
          }
        }
        gram[i * d + j] = dot;
        gram[j * d + i] = std::move(dot);
      }
    }
    system = std::make_unique<System>(gram, d, budget);
    if (logarithmic) {
      initialize_logs();
      return;
    }
    std::vector<R> b(rhs.begin(), rhs.end());
    coefficients = system->solve(b);
    residual = syy;
    for (size_t i : system->selected)
      residual = budget.sub(residual, budget.mul(coefficients[i], R(rhs[i])));
    if (residual < 0)
      throw V::error("#NUM!");
    total = intercept ? R(syy) - R(sy * sy, I(observations)) : R(syy);
    explained = total - residual;
  }
  std::vector<LogBasis> bases;
  std::optional<LogStats> log_statistics;
  size_t log_records = 0;
  void log_record(size_t n = 1) {
    if (n > 1'000'000 - log_records)
      throw V::error("#LIMIT!", true);
    log_records += n;
  }
  void initialize_logs() {
    // Missing or zero Y slots cannot be dropped from an exponential fit.
    if (y.entries.size() != observations)
      throw V::error("#NUM!");
    MantissaFactors factors{budget, {}};
    std::map<uint64_t, size_t> indices;
    auto put = [&](uint64_t prime, size_t observation, int exponent) {
      if (!exponent)
        return;
      auto [where, inserted] = indices.emplace(prime, bases.size());
      if (inserted) {
        log_record();
        LogBasis b;
        b.prime = prime;
        bases.push_back(std::move(b));
      }
      log_record();
      auto &basis = bases[where->second];
      basis.values.emplace(observation, exponent);
      basis.sum += exponent;
    };
    for (auto [offset, value] : y.entries) {
      budget.charge();
      if (value <= 0)
        throw V::error("#NUM!");
      auto b = NativeExact::binary(value);
      auto trailing = std::countr_zero(b.mantissa);
      auto mantissa = b.mantissa >> trailing;
      auto obs = observation(offset / y.columns, offset % y.columns);
      put(2, obs, b.exponent + trailing);
      for (auto [prime, exponent] : factors(mantissa))
        put(prime, obs, exponent);
    }
    for (auto &basis : bases) {
      std::vector<R> rhs(columns.size());
      for (size_t i = 0; i < columns.size(); ++i)
        for (auto &[o, v] : basis.values) {
          auto x = columns[i].at(o);
          budget.integer_product(x, v);
          rhs[i] += x * v;
        }
      log_record(columns.size());
      basis.fitted = system->solve(rhs);
    }
  }
  void put_log(LogForm &form, size_t index, R value) {
    if (value == 0)
      return;
    log_record();
    form.emplace(index, std::move(value));
  }
  LogForm log_prediction(const std::optional<Matrix> &query, size_t row,
                         size_t column) {
    LogForm result;
    size_t obs = observation(row, column);
    for (size_t k = 0; k < bases.size(); ++k) {
      R sum = 0;
      for (size_t i : system->selected) {
        const auto &c = columns[i];
        R x;
        if (c.constant)
          x = 1;
        else if (!query)
          x = c.at(obs);
        else {
          double q = orientation == Simple ? query->at(row, column)
                     : orientation == Rows
                         ? query->at(row, size_t(c.ordinal))
                         : query->at(size_t(c.ordinal), column);
          x = encoded(q, c.exponent);
        }
        sum = budget.add(sum, budget.mul(bases[k].fitted[i], x));
      }
      put_log(result, k, std::move(sum));
    }
    return result;
  }
  const LogStats &log_stats() {
    if (log_statistics)
      return *log_statistics;
    LogStats out;
    for (size_t o = 0; o < observations; ++o) {
      budget.charge();
      LogForm residual, total, explained;
      for (size_t k = 0; k < bases.size(); ++k) {
        const auto &b = bases[k];
        auto found = b.values.find(o);
        R actual = found == b.values.end() ? R(0) : R(found->second),
          fitted = 0;
        for (size_t i : system->selected)
          fitted =
              budget.add(fitted, budget.mul(b.fitted[i], R(columns[i].at(o))));
        R mean = intercept ? R(b.sum, I(observations)) : R(0);
        put_log(residual, k, budget.sub(actual, fitted));
        put_log(total, k, budget.sub(actual, mean));
        put_log(explained, k, budget.sub(fitted, mean));
      }
      out.positive[0] = out.positive[0] || !residual.empty();
      out.positive[1] = out.positive[1] || !total.empty();
      out.positive[2] = out.positive[2] || !explained.empty();
      if (!residual.empty())
        out.residual.push_back(std::move(residual));
      if (!total.empty())
        out.total.push_back(std::move(total));
      if (!explained.empty())
        out.explained.push_back(std::move(explained));
    }
    log_statistics = std::move(out);
    return *log_statistics;
  }
  int required_bits(const std::vector<LogForm> &forms,
                    const LogStats *stats) const {
    int needed = 128;
    auto inspect = [&](const std::vector<LogForm> &items) {
      for (auto &form : items) {
        budget.charge(form.size() + 1);
        int magnitude = 0;
        for (auto &[index, c] : form) {
          I n = numerator(c);
          if (n < 0)
            n = -n;
          I d = denominator(c);
          magnitude =
              std::max(magnitude, int(boost::multiprecision::msb(n)) -
                                      int(boost::multiprecision::msb(d)) + 1);
        }
        // ln(prime)<37 and all rational coefficients are combined before
        // evaluation. The guard bounds absolute log-form error well below a
        // binary64 ulp near one; statistics additionally require convergence.
        needed = std::max(needed, 96 + 6 + magnitude +
                                      int(std::bit_width(form.size())));
      }
    };
    inspect(forms);
    if (stats) {
      inspect(stats->residual);
      inspect(stats->total);
      inspect(stats->explained);
    }
    return needed;
  }
  template <unsigned Bits>
  LogEvaluation evaluate_logs(const std::vector<LogForm> &forms, size_t rows,
                              size_t width, bool statistics,
                              const LogStats *stats) const {
    using Float =
        boost::multiprecision::number<boost::multiprecision::cpp_bin_float<
            Bits + 32, boost::multiprecision::digit_base_2>>;
    budget.charge(bases.size() * (Bits / 64 + 1) * (Bits / 64 + 1));
    std::vector<Float> logs;
    for (auto &b : bases)
      logs.push_back(log(Float(b.prime)));
    auto linear = [&](const LogForm &form) {
      Float sum = 0;
      for (auto &[k, c] : form) {
        budget.charge(Bits / 64 + 1);
        sum += floating<Float>(c) * logs[k];
      }
      return sum;
    };
    auto number = [](const Float &f) {
      return V::numeric(f.template convert_to<double>());
    };
    std::vector<V> out;
    out.reserve(rows * width);
    for (auto &form : forms) {
      auto value = linear(form);
      // An exponent beyond these bounds is already unrepresentable/underflows
      // binary64; do not ask the multiprecision exp to construct it.
      out.push_back(value > 710    ? V::error("#NUM!")
                    : value < -750 ? V::numeric(0)
                                   : number(exp(value)));
    }
    LogEvaluation result;
    if (statistics) {
      out.resize(rows * width, V::error("#N/A"));
      for (size_t i = 0; i < width; ++i)
        out[width + i] = V::numeric(0);
      auto squares = [&](const std::vector<LogForm> &values) {
        Float sum = 0;
        for (auto &form : values) {
          auto x = linear(form);
          sum += x * x;
        }
        return sum;
      };
      Float residual = squares(stats->residual), total = squares(stats->total),
            explained = squares(stats->explained);
      std::array<Float, 3> metrics{residual, total, explained};
      for (size_t i = 0; i < 3; ++i) {
        result.sums[i] = Q(metrics[i]);
        if (stats->positive[i] && metrics[i] <= 0)
          result.valid = false;
      }
      size_t rank = system->selected.size(), df = observations - rank,
             variables = rank - (intercept ? 1 : 0);
      auto &inverse = system->inverse();
      for (size_t i : system->selected) {
        const auto &c = columns[i];
        size_t slot =
            c.ordinal < 0 ? predictors : predictors - 1 - size_t(c.ordinal);
        out[width + slot] =
            number(sqrt(residual / df *
                        floating<Float>(shifted(inverse[i], -2 * c.exponent))));
      }
      out[2 * width] = !stats->positive[1] ? V::error("#DIV/0!")
                       : total <= 0        ? V::error("#NUM!")
                                           : number(explained / total);
      out[2 * width + 1] = number(sqrt(residual / df));
      out[3 * width] = !variables || !stats->positive[0] ? V::error("#DIV/0!")
                       : residual <= 0
                           ? V::error("#NUM!")
                           : number(explained * df / (residual * variables));
      out[3 * width + 1] = V::numeric(df);
      out[4 * width] = number(explained);
      out[4 * width + 1] = number(residual);
    }
    result.value = V::array(rows, width, std::move(out));
    return result;
  }
  V log_result(bool estimator, bool statistics,
               const std::optional<Matrix> &query) {
    size_t rows = 1, width = predictors + 1;
    std::vector<LogForm> forms;
    if (estimator) {
      if (width > 1'000'000 / (statistics ? 5 : 1))
        return V::error("#LIMIT!", true);
      forms.resize(width);
      for (size_t i : system->selected) {
        const auto &c = columns[i];
        size_t slot =
            c.ordinal < 0 ? predictors : predictors - 1 - size_t(c.ordinal);
        for (size_t k = 0; k < bases.size(); ++k)
          put_log(forms[slot], k, shifted(bases[k].fitted[i], -c.exponent));
      }
      if (statistics)
        rows = 5;
    } else {
      rows = y.rows;
      width = y.columns;
      if (query) {
        if (!slots(*query))
          return V::error("#N/A");
        if (orientation == Rows && query->columns != predictors)
          return V::error("#VALUE!");
        if (orientation == Columns && query->rows != predictors)
          return V::error("#VALUE!");
        rows = orientation == Columns ? 1 : query->rows;
        width = orientation == Rows ? 1 : query->columns;
      }
      if (rows > 1'000'000 / width)
        return V::error("#LIMIT!", true);
      for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < width; ++c)
          forms.push_back(log_prediction(query, r, c));
    }
    const LogStats *stats = statistics ? &log_stats() : nullptr;
    int needed = required_bits(forms, stats);
    if (needed > 4096)
      return V::error("#LIMIT!", true);
    std::optional<LogEvaluation> previous;
    for (int bits : {128, 256, 512, 1024, 2048, 4096}) {
      if (bits * 2 < needed)
        continue;
      LogEvaluation next;
      switch (bits) {
#define LOG_PRECISION(N)                                                       \
  case N:                                                                      \
    next = evaluate_logs<N>(forms, rows, width, statistics, stats);            \
    break;
        LOG_PRECISION(128)
        LOG_PRECISION(256)
        LOG_PRECISION(512) LOG_PRECISION(1024) LOG_PRECISION(2048)
            LOG_PRECISION(4096)
#undef LOG_PRECISION
      }
      if (bits >= needed && previous &&
          same_result(previous->value, next.value) &&
          same_sums(*previous, next))
        return next.value;
      previous = std::move(next);
    }
    return V::error("#LIMIT!", true);
  }

  R encoded(double query, int exponent) const {
    auto b = NativeExact::binary(query);
    if (!b.mantissa)
      return 0;
    return shifted(R(NativeExact::integer(b, b.exponent)),
                   b.exponent - exponent);
  }
  R prediction(const std::optional<Matrix> &query, size_t row,
               size_t column) const {
    R sum = 0;
    size_t obs = observation(row, column);
    for (size_t i : system->selected) {
      const auto &c = columns[i];
      R x;
      if (c.constant)
        x = 1;
      else if (!query)
        x = c.at(obs);
      else {
        double q = orientation == Simple ? query->at(row, column)
                   : orientation == Rows ? query->at(row, size_t(c.ordinal))
                                         : query->at(size_t(c.ordinal), column);
        x = encoded(q, c.exponent);
      }
      sum = budget.add(sum, budget.mul(coefficients[i], x));
    }
    return sum;
  }
};
NativeRegressionModel::NativeRegressionModel(Matrix y, std::optional<Matrix> x,
                                             bool intercept, bool logarithmic)
    : impl_(std::make_shared<Impl>(std::move(y), std::move(x), intercept,
                                   logarithmic)) {}
V NativeRegressionModel::coefficients(bool statistics) const try {
  if (impl_->logarithmic)
    return impl_->log_result(true, statistics, {});
  const auto &d = *impl_;
  size_t width = d.predictors + 1, rows = statistics ? 5 : 1;
  if (width > 1'000'000 / rows)
    return V::error("#LIMIT!", true);
  std::vector<V> out(rows * width, V::error("#N/A"));
  for (size_t i = 0; i < width; ++i) {
    out[i] = V::numeric(0);
    if (statistics)
      out[width + i] = V::numeric(0);
  }
  const std::vector<R> empty;
  const auto &diag = statistics ? d.system->inverse() : empty;
  size_t rank = d.system->selected.size(), df = d.observations - rank;
  for (size_t i : d.system->selected) {
    const auto &c = d.columns[i];
    size_t slot =
        c.ordinal < 0 ? d.predictors : d.predictors - 1 - size_t(c.ordinal);
    out[slot] = numeric(d.coefficients[i], d.ey - c.exponent);
    if (statistics)
      out[width + slot] =
          root(d.budget.mul(d.budget.div(d.residual, R(df)), diag[i]),
               d.ey - c.exponent);
  }
  if (statistics) {
    out[2 * width] = d.total == 0 ? V::error("#DIV/0!")
                                  : numeric(d.budget.div(d.explained, d.total));
    out[2 * width + 1] = root(d.budget.div(d.residual, R(df)), d.ey);
    size_t variables = rank - (d.intercept ? 1 : 0);
    out[3 * width] =
        !variables || d.residual == 0
            ? V::error("#DIV/0!")
            : numeric(d.budget.div(d.budget.mul(d.explained, R(df)),
                                   d.budget.mul(d.residual, R(variables))));
    out[3 * width + 1] = V::numeric(df);
    out[4 * width] = numeric(d.explained, 2 * d.ey);
    out[4 * width + 1] = numeric(d.residual, 2 * d.ey);
  }
  return V::array(rows, width, std::move(out));
} catch (const V &v) {
  return v;
}
V NativeRegressionModel::predict(const std::optional<Matrix> &query) const try {
  if (impl_->logarithmic)
    return impl_->log_result(false, false, query);
  const auto &d = *impl_;
  size_t rows = d.y.rows, columns = d.y.columns;
  if (query) {
    if (!slots(*query))
      return V::error("#N/A");
    if (d.orientation == Impl::Rows && query->columns != d.predictors)
      return V::error("#VALUE!");
    if (d.orientation == Impl::Columns && query->rows != d.predictors)
      return V::error("#VALUE!");
    rows = d.orientation == Impl::Columns ? 1 : query->rows;
    columns = d.orientation == Impl::Rows ? 1 : query->columns;
  }
  if (rows > 1'000'000 / columns)
    return V::error("#LIMIT!", true);
  std::vector<V> out;
  out.reserve(rows * columns);
  for (size_t r = 0; r < rows; ++r)
    for (size_t c = 0; c < columns; ++c)
      out.push_back(numeric(d.prediction(query, r, c), d.ey));
  return V::array(rows, columns, std::move(out));
} catch (const V &v) {
  return v;
}
} // namespace DomeOffice
