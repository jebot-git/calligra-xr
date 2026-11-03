// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_database.h"
#include "native_exact_sample.h"
#include <bit>
namespace DomeOffice {
namespace {
using V = NativeValue;
using NativeExact::I;
using NativeExact::Q;
V number(const Q &x) { return V::numeric(x.convert_to<double>()); }
V product(const std::vector<double> &values, CriteriaBudget &budget) {
  if (values.empty())
    return V::numeric(0);
  int64_t exponent = 0, lower = 0, upper = 0;
  bool negative = false, zero = false;
  std::vector<uint64_t> factors;
  for (double x : values) {
    budget.charge(1);
    auto b = NativeExact::binary(x);
    negative = negative != b.negative;
    if (!b.mantissa) {
      zero = true;
      continue;
    }
    int bits = int(std::bit_width(b.mantissa)) - 1;
    exponent += b.exponent;
    lower += b.exponent + bits;
    upper += b.exponent + bits + (b.mantissa != 1);
    if (b.mantissa != 1)
      factors.push_back(b.mantissa);
  }
  if (zero)
    return V::numeric(0);
  if (lower >= 1024)
    return V::error("#NUM!");
  if (upper < -1075)
    return V::numeric(0);
  I exact = 1;
  for (auto factor : factors) {
    budget.charge(exact.backend().size() + 1);
    exact *= factor;
  }
  int bits = boost::multiprecision::msb(exact) + 1;
  int64_t magnitude = exponent + bits - 1;
  if (magnitude > 1024)
    return V::error("#NUM!");
  if (magnitude < -1076)
    return V::numeric(0);
  int trim = std::max(0, bits - (std::numeric_limits<Q>::digits + 16));
  Q result = ldexp(Q(exact >> trim), int(exponent + trim));
  return number(negative ? -result : result);
}
V aggregate(const Text &name, const std::vector<double> &values,
            CriteriaBudget &budget) {
  if (name == "DMIN" || name == "DMAX")
    return V::numeric(values.empty() ? 0
                      : name == "DMIN"
                          ? *std::min_element(values.begin(), values.end())
                          : *std::max_element(values.begin(), values.end()));
  if (name == "DPRODUCT")
    return product(values, budget);
  if (name == "DSUM" && values.empty())
    return V::numeric(0);
  if (values.empty())
    return V::error("#DIV/0!");
  NativeExact::Sample sample(values);
  budget.charge(50'000'000 - sample.remaining);
  if (name == "DSUM" || name == "DAVERAGE")
    return number(
        ldexp(Q(sample.sum) / (name == "DAVERAGE" ? Q(sample.count) : Q(1)),
              sample.exponent));
  bool population = name == "DVARP" || name == "DSTDEVP";
  if (sample.count < (population ? 1u : 2u))
    return V::error("#DIV/0!");
  Q variance =
      ldexp(Q(sample.centered) /
                Q(I(sample.count) * (sample.count - (population ? 0 : 1))),
            2 * sample.exponent);
  return number(name == "DSTDEV" || name == "DSTDEVP" ? sqrt(variance)
                                                      : variance);
}
} // namespace
const NativeSignatures &database_signatures() {
  static const NativeSignatures names = {
      {"DAVERAGE", {3, 3}}, {"DCOUNT", {3, 3}}, {"DCOUNTA", {3, 3}},
      {"DGET", {3, 3}},     {"DMAX", {3, 3}},   {"DMIN", {3, 3}},
      {"DPRODUCT", {3, 3}}, {"DSTDEV", {3, 3}}, {"DSTDEVP", {3, 3}},
      {"DSUM", {3, 3}},     {"DVAR", {3, 3}},   {"DVARP", {3, 3}}};
  return names;
}
Text database_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  return database_signatures().contains(base) ? base : name;
}
struct NativeDatabaseModel::Impl {
  NativeDatabaseSource data, criteria;
  NativeCalculationSettings settings;
  CriteriaBudget &budget;
  std::map<Text, size_t> fields;
  bool incomplete = false, has_predicates = false;
  struct Condition {
    size_t field = 0;
    std::unique_ptr<NativeCriterion> matcher;
    NativeDatabaseSource::Predicate predicate;
  };
  std::vector<std::vector<Condition>> alternatives;
  bool selected = false;
  std::optional<V> selection_error;
  std::vector<size_t> selected_rows;
  size_t selected_blank = 0;
  V read(const NativeDatabaseSource &source, size_t row, size_t column) {
    budget.charge(1);
    auto v = source.read(row, column);
    incomplete = incomplete || v.incomplete;
    if (v.isError() && v.string == "#CYCLE!")
      throw v;
    return v;
  }
  size_t field(const V &value) const {
    if (value.isError())
      throw value;
    if (value.isNumber()) {
      double n = std::trunc(value.number);
      if (n < 1 || n > double(data.columns))
        throw V::error("#VALUE!");
      return size_t(n) - 1;
    }
    if (value.kind != V::String)
      throw V::error("#VALUE!");
    auto p = fields.find(value.string.toCaseFolded());
    if (p == fields.end())
      throw V::error("#VALUE!");
    return p->second;
  }
  Impl(NativeDatabaseSource db, NativeDatabaseSource cr,
       NativeCalculationSettings s, CriteriaBudget &b)
      : data(std::move(db)), criteria(std::move(cr)), settings(s), budget(b) {
    if (!data.rows || !data.columns || criteria.rows < 2 || !criteria.columns)
      throw V::error("#VALUE!");
    if (criteria.columns > 1'000'000 ||
        criteria.rows > 1'000'000 / criteria.columns)
      throw V::error("#LIMIT!", true);
    for (size_t col = 0; col < data.columns; ++col) {
      auto h = read(data, 0, col);
      if (h.isError())
        throw h;
      fields.emplace(h.display().toCaseFolded(), col);
    }
    alternatives.resize(criteria.rows - 1);
    auto match_settings = settings.criteria();
    match_settings.case_sensitive = false;
    for (size_t col = 0; col < criteria.columns; ++col) {
      auto header = read(criteria, 0, col);
      std::optional<size_t> index;
      try {
        index = field(header);
      } catch (const V &v) {
        if (v.incomplete || v.string != "#VALUE!")
          throw;
      }
      bool has_condition = false;
      for (size_t row = 1; row < criteria.rows; ++row) {
        budget.charge(1);
        if (criteria.blank && criteria.blank(row, col))
          continue;
        has_condition = true;
        Condition condition;
        if (index) {
          condition.field = *index;
          auto query = read(criteria, row, col);
          if (query.kind == V::String && query.string.empty())
            continue;
          if (query.kind == V::Complex)
            throw V::error("#VALUE!", true);
          condition.matcher = std::make_unique<NativeCriterion>(
              query, match_settings, budget, settings.reference_date());
        } else {
          condition.predicate = criteria.predicate
                                    ? criteria.predicate(row, col)
                                    : NativeDatabaseSource::Predicate{};
          if (!condition.predicate)
            throw V::error("#VALUE!");
          has_predicates = true;
        }
        alternatives[row - 1].push_back(std::move(condition));
      }
      if (!index && !has_condition && header.kind != V::Empty &&
          !(header.kind == V::String && header.string.empty()))
        throw V::error("#VALUE!");
    }
    // Calc ignores unused OR rows when other rows contain conditions. A wholly
    // blank criteria area denotes the complete database column.
    if (std::any_of(alternatives.begin(), alternatives.end(),
                    [](const auto &row) { return !row.empty(); }))
      alternatives.erase(
          std::remove_if(alternatives.begin(), alternatives.end(),
                         [](const auto &row) { return row.empty(); }),
          alternatives.end());
  }
  bool matches(size_t row, bool blank) {
    bool any = false;
    for (auto &alternative : alternatives) {
      bool all = true;
      for (auto &condition : alternative) {
        budget.charge(1);
        bool match;
        if (condition.predicate) {
          auto v = condition.predicate(row);
          incomplete = incomplete || v.incomplete;
          if (v.isError())
            throw v;
          if (v.isBoolean())
            match = v.boolean;
          else if (v.isNumber())
            match = v.number != 0;
          else
            throw V::error("#VALUE!");
        } else {
          auto v = blank ? V() : read(data, row, condition.field);
          match = condition.matcher->matches(v, true);
        }
        all = match && all;
      }
      any = all || any;
    }
    return any;
  }
  void select() {
    if (selected)
      return;
    if (selection_error)
      throw *selection_error;
    try {
      if (has_predicates) {
        for (size_t row = 1; row < data.rows; ++row) {
          budget.charge(1);
          if (matches(row, false))
            selected_rows.push_back(row);
          if (selected_rows.size() > 1'000'000)
            throw V::error("#LIMIT!", true);
        }
      } else {
        auto occupied = data.occupied_rows();
        std::sort(occupied.begin(), occupied.end());
        occupied.erase(std::unique(occupied.begin(), occupied.end()),
                       occupied.end());
        size_t records = 0;
        for (size_t row : occupied)
          if (row && row < data.rows) {
            budget.charge(1);
            ++records;
            if (matches(row, false))
              selected_rows.push_back(row);
          }
        if (records < data.rows - 1 && matches(0, true))
          selected_blank = data.rows - 1 - records;
      }
      selected = true;
    } catch (const V &failure) {
      // A prepared query has one selection for all requested output fields.
      // Discard partial rows and retain its failure instead of retrying work.
      selected_rows.clear();
      selected_blank = 0;
      selection_error = failure;
      throw;
    }
  }
  V compute(const Text &name, const V &selector, bool omitted) {
    bool counting = name == "DCOUNT" || name == "DCOUNTA";
    incomplete = incomplete || selector.incomplete;
    if (selector.isError())
      throw selector;
    bool no_field = counting && (omitted || selector.kind == V::Empty ||
                                 (selector.isNumber() && selector.number == 0));
    if (omitted && !counting)
      return V::error("#VALUE!");
    std::optional<size_t> column;
    if (!no_field)
      column = field(selector);
    select();
    size_t records = selected_rows.size() + selected_blank;
    if (no_field)
      return V::numeric(records);
    if (name == "DGET") {
      if (!records)
        return V::error("#VALUE!");
      if (records > 1)
        return V::error("#NUM!");
      auto v = selected_blank ? V() : read(data, selected_rows[0], *column);
      return v.kind == V::Empty ? V::error("#VALUE!") : v;
    }
    size_t count = 0;
    std::vector<double> values;
    for (size_t row : selected_rows) {
      auto v = read(data, row, *column);
      if (name == "DCOUNTA") {
        count += v.kind != V::Empty;
        continue;
      }
      if (name == "DCOUNT") {
        if (v.kind == V::Complex)
          throw V::error("#VALUE!", true);
        count += v.isNumber() || v.isBoolean();
        continue;
      }
      if (v.isError())
        throw v;
      if (v.kind == V::Complex)
        throw V::error("#VALUE!", true);
      if (v.isNumber() || v.isBoolean())
        values.push_back(v.isBoolean() ? double(v.boolean) : v.number);
      if (values.size() > 1'000'000)
        throw V::error("#LIMIT!", true);
    }
    return counting ? V::numeric(count) : aggregate(name, values, budget);
  }
};
NativeDatabaseModel::NativeDatabaseModel(NativeDatabaseSource db,
                                         NativeDatabaseSource cr,
                                         NativeCalculationSettings settings,
                                         CriteriaBudget &budget)
    : impl_(std::make_shared<Impl>(std::move(db), std::move(cr), settings,
                                   budget)) {}
V NativeDatabaseModel::calculate(const Text &name, const V &field,
                                 bool omitted) const {
  V out;
  try {
    out = impl_->compute(name, field, omitted);
  } catch (const V &v) {
    out = v;
  }
  out.incomplete = out.incomplete || impl_->incomplete;
  return out;
}
} // namespace DomeOffice
