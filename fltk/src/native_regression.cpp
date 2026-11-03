// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_regression.h"
#include "native_exact_sample.h"
#include <boost/multiprecision/integer.hpp>
#include <numeric>
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
} // namespace
const NativeSignatures &regression_signatures() {
  static const NativeSignatures names = {{"LINEST", {1, 4}}, {"TREND", {1, 4}}};
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
  bool intercept = true;
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
  explicit Impl(Matrix yy, std::optional<Matrix> xx, bool constant)
      : y(std::move(yy)), intercept(constant) {
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
                                             bool intercept)
    : impl_(std::make_shared<Impl>(std::move(y), std::move(x), intercept)) {}
V NativeRegressionModel::coefficients(bool statistics) const try {
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
