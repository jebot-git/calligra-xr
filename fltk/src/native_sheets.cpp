// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_sheets.h"
#include "native_calendar.h"
#include "native_engineering.h"
#include "native_financial.h"
#include "native_time.h"
#include <functional>
#include <numeric>
#include <random>
#include <tuple>
namespace DomeOffice {
namespace {
using Address =
    std::tuple<int, int, int>; // sheet, row, column; one-based cells
struct Area {
  int sheet, row, column, rows, columns;
  size_t size() const { return size_t(rows) * columns; }
};
struct CurrentCell {
  Address &slot, previous;
  CurrentCell(Address &slot, Address next) : slot(slot), previous(slot) {
    slot = next;
  }
  ~CurrentCell() { slot = previous; }
};
struct FlagScope {
  bool &slot;
  bool previous;
  FlagScope(bool &slot, bool enabled) : slot(slot), previous(slot) {
    slot = enabled;
  }
  ~FlagScope() { slot = previous; }
};
using Value = NativeValue;
struct Node {
  enum Kind {
    Literal,
    Reference,
    Range,
    Name,
    Unary,
    Binary,
    Function,
    Array
  } kind = Literal;
  Value literal;
  Text name;
  Address first{}, last{};
  bool explicit_sheet = false;
  unsigned height = 1;
  size_t columns = 0;
  std::vector<std::shared_ptr<Node>> args;
};
using Expr = std::shared_ptr<Node>;
Expr checked(Expr p) {
  for (const auto &child : p->args)
    p->height = std::max(p->height, child->height + 1);
  if (p->height > 256)
    throw Value::error("#DEPTH!", true);
  return p;
}
Expr node(Node::Kind kind, const Text &name = {}) {
  auto p = std::make_shared<Node>();
  p->kind = kind;
  p->name = name;
  return p;
}
Value error(const char *s) {
  Text code(s);
  return Value::error(code, code == "#NAME?" || code == "#PARSE!" ||
                                code == "#LIMIT!" || code == "#DEPTH!");
}
struct DepthGuard {
  unsigned &depth;
  explicit DepthGuard(unsigned &d) : depth(d) {
    if (++depth > 256) {
      --depth;
      throw error("#DEPTH!");
    }
  }
  ~DepthGuard() { --depth; }
};
Value numeric(double n) {
  return std::isfinite(n) ? Value::numeric(n) : error("#NUM!");
}
std::optional<Value> calendar_value(const Text &input,
                                    const NativeCalculationSettings &settings) {
  if (input.size() > 128)
    return {};
  bool iso_date = input.size() >= 10 && input[4] == '-' && input[7] == '-';
  bool iso_duration = input.startsWith('P') || input.startsWith("-P");
  if (!iso_date && !iso_duration)
    return {};
  auto day = Date::fromString(input, ISODate);
  auto time = DateTime::fromString(input, ISODate);
  if (day.valid || time.valid) {
    if (!day.valid)
      day = time.day;
    auto serial =
        (std::chrono::sys_days(day.day) - settings.reference_date()).count();
    Value value =
        numeric(serial + (time.valid ? time.time.ms / 86400000.0 : 0));
    value.format = Value::Date;
    value.string = input;
    return value;
  }
  static const std::regex duration("(-?)P(?:([0-9]+)D)?T(?:([0-9]+)H)?(?:([0-9]"
                                   "+)M)?(?:([0-9]+(?:\\.[0-9]+)?)S)?");
  auto bytes = input.toStdString();
  std::smatch match;
  if (!std::regex_match(bytes, match, duration) ||
      !(match[2].matched || match[3].matched || match[4].matched ||
        match[5].matched))
    return {};
  double seconds = 0;
  for (int i = 2; i <= 5; ++i)
    if (match[i].matched)
      seconds += Text(match[i].str()).toDouble() * (i == 2   ? 86400
                                                    : i == 3 ? 3600
                                                    : i == 4 ? 60
                                                             : 1);
  Value value = numeric(seconds / 86400 * (match[1].str().empty() ? 1 : -1));
  if (value.isError())
    return value;
  value.format = Value::Duration;
  value.string = input;
  return value;
}
double number(const Value &v) {
  if (v.isArray())
    return number(v.scalar());
  if (v.isNumber())
    return v.number;
  if (v.kind == Value::Boolean)
    return v.boolean ? 1 : 0;
  if (v.kind == Value::Empty)
    return 0;
  if (v.kind == Value::Error)
    throw v;
  bool valid = false;
  double n = v.string.toDouble(&valid);
  if (!valid)
    throw error("#VALUE!");
  if (!std::isfinite(n))
    throw error("#NUM!");
  return n;
}
bool truth(const Value &v) {
  if (v.isArray())
    return truth(v.scalar());
  if (v.kind == Value::String) {
    if (v.string.toUpper() == "TRUE")
      return true;
    if (v.string.toUpper() == "FALSE")
      return false;
  }
  return number(v) != 0;
}
Text text(const Value &v) {
  if (v.isError())
    throw v;
  return v.display();
}
Text concatenate(const Text &a, const Text &b) {
  if (a.size() > 1'000'000 || b.size() > 1'000'000 - a.size())
    throw error("#LIMIT!");
  return a + b;
}
template <class Function>
Value map_values(const Value &a, const Value &b, Function fn) {
  auto call = [&](const Value &x, const Value &y) {
    try {
      return fn(x, y);
    } catch (const Value &failure) {
      return failure;
    }
  };
  if (!a.isArray() && !b.isArray())
    return call(a, b);
  if (a.isArray() && b.isArray() &&
      (a.rows != b.rows || a.columns != b.columns))
    return error("#VALUE!");
  size_t rows = std::max(a.rows, b.rows),
         columns = std::max(a.columns, b.columns);
  if (rows > 1'000'000 / columns)
    return error("#LIMIT!");
  std::vector<Value> values;
  values.reserve(rows * columns);
  for (size_t r = 0; r < rows; ++r)
    for (size_t c = 0; c < columns; ++c)
      values.push_back(call(a.at(r, c), b.at(r, c)));
  return Value::array(rows, columns, std::move(values));
}
Value binary(const Text &op, const Value &a, const Value &b) {
  return map_values(a, b, [&](const Value &x, const Value &y) -> Value {
    if (x.isError())
      return x;
    if (y.isError())
      return y;
    if (op == "&")
      return Value(concatenate(text(x), text(y)));
    if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" ||
        op == ">=") {
      int cmp;
      if (x.kind == Value::String || y.kind == Value::String)
        cmp = text(x).compare(text(y), CaseInsensitive);
      else {
        double p = number(x), q = number(y);
        cmp = p < q ? -1 : p > q ? 1 : 0;
      }
      return Value::logical(op == "="    ? cmp == 0
                            : op == "<>" ? cmp != 0
                            : op == "<"  ? cmp < 0
                            : op == ">"  ? cmp > 0
                            : op == "<=" ? cmp <= 0
                                         : cmp >= 0);
    }
    double p = number(x), q = number(y);
    if (op == "+")
      return numeric(p + q);
    if (op == "-")
      return numeric(p - q);
    if (op == "*")
      return numeric(p * q);
    if (op == "/")
      return q == 0 ? error("#DIV/0!") : numeric(p / q);
    if (op == "^")
      return numeric(std::pow(p, q));
    return error("#PARSE!");
  });
}
int lookup_compare(const Value &a, const Value &b) {
  auto numeric_value = [](const Value &v) -> std::optional<double> {
    if (v.isNumber())
      return v.number;
    if (v.kind == Value::Empty)
      return 0;
    if (v.kind == Value::String) {
      bool valid = false;
      double n = v.string.toDouble(&valid);
      if (valid && std::isfinite(n))
        return n;
    }
    return {};
  };
  if (a.isNumber() || b.isNumber()) {
    auto x = numeric_value(a), y = numeric_value(b);
    if (x && y)
      return *x < *y ? -1 : *x > *y ? 1 : 0;
  }
  if (a.kind == Value::Boolean && b.kind == Value::Boolean)
    return a.boolean == b.boolean ? 0 : a.boolean ? 1 : -1;
  return a.display().compare(b.display(), CaseInsensitive);
}
bool lookup_comparable(const Value &a, const Value &b) {
  return a.kind == b.kind || (a.isNumber() && b.kind == Value::Empty) ||
         (b.isNumber() && a.kind == Value::Empty);
}
class Parser {
  Text input;
  size_t at = 0;
  unsigned depth = 0;
  int sheet;
  std::function<int(const Text &)> find_sheet;
  void ws() {
    while (at < input.size() && g_unichar_isspace(input[at]))
      ++at;
  }
  bool take(Char c) {
    ws();
    if (at < input.size() && input[at] == c) {
      ++at;
      return true;
    }
    return false;
  }
  [[noreturn]] void fail() { throw error("#PARSE!"); }
  Text ident() {
    ws();
    size_t begin = at;
    while (at < input.size() &&
           (g_unichar_isalnum(input[at]) || input[at] == '_' ||
            input[at] == '$' || input[at] == '.'))
      ++at;
    return input.mid(begin, at - begin);
  }
  Expr reference(Text name, int target, bool qualified) {
    Text clean = name;
    clean.remove("$");
    size_t i = 0;
    int col = 0;
    while (i < clean.size() && g_unichar_isalpha(clean[i]) && clean[i] < 128) {
      if (col > KS_colMax / 26)
        return {};
      col = col * 26 + g_unichar_toupper(clean[i++]) - 'A' + 1;
    }
    if (i == 0 || i == clean.size())
      return {};
    bool valid = false;
    int row = clean.mid(i).toInt(&valid);
    if (!valid || row < 1 || row > KS_rowMax || col < 1 || col > KS_colMax)
      return {};
    auto out = node(Node::Reference);
    out->first = {target, row, col};
    out->explicit_sheet = qualified;
    return out;
  }
  Expr primary() {
    DepthGuard guard(depth);
    ws();
    if (at >= input.size())
      fail();
    if (take('{')) {
      auto out = node(Node::Array);
      size_t row_width = 0;
      do {
        auto item = parse(0);
        out->args.push_back(item);
        ++row_width;
        if (take(','))
          continue;
        if (!out->columns)
          out->columns = row_width;
        if (row_width != out->columns)
          fail();
        row_width = 0;
        if (take(';'))
          continue;
        if (!take('}'))
          fail();
        break;
      } while (true);
      return checked(out);
    }
    if (take('(')) {
      auto p = parse(0);
      if (!take(')'))
        fail();
      return p;
    }
    if (take('"')) {
      Text s;
      bool end = false;
      while (at < input.size()) {
        Char c = input[at++];
        if (c == '"') {
          if (at < input.size() && input[at] == '"') {
            ++at;
            s += '"';
          } else {
            end = true;
            break;
          }
        } else
          s += c;
      }
      if (!end)
        fail();
      auto p = node(Node::Literal);
      p->literal = Value(s);
      return p;
    }
    if (input[at] == '#') {
      for (const char *code : {"#REF!", "#DIV/0!", "#VALUE!", "#NAME?", "#N/A",
                               "#NUM!", "#NULL!"}) {
        Text value(code);
        if (input.mid(at, value.size()) == value) {
          at += value.size();
          auto out = node(Node::Literal);
          out->literal = Value::error(value);
          return out;
        }
      }
      fail();
    }
    if (input[at] == '+' || input[at] == '-') {
      auto p = node(Node::Unary, Text(1, input[at++]));
      p->args.push_back(parse(45));
      return checked(p);
    }
    if (g_unichar_isdigit(input[at]) ||
        (input[at] == '.' && at + 1 < input.size() &&
         g_unichar_isdigit(input[at + 1]))) {
      size_t begin = at;
      while (at < input.size() &&
             (g_unichar_isdigit(input[at]) || input[at] == '.'))
        ++at;
      if (at < input.size() && (input[at] == 'e' || input[at] == 'E')) {
        ++at;
        if (at < input.size() && (input[at] == '+' || input[at] == '-'))
          ++at;
        while (at < input.size() && g_unichar_isdigit(input[at]))
          ++at;
      }
      bool valid = false;
      double n = input.mid(begin, at - begin).toDouble(&valid);
      if (!valid)
        fail();
      auto p = node(Node::Literal);
      p->literal = numeric(n);
      return p;
    }
    Text name;
    bool quoted = take('\'');
    if (quoted) {
      bool end = false;
      while (at < input.size()) {
        Char c = input[at++];
        if (c == '\'') {
          if (at < input.size() && input[at] == '\'') {
            name += '\'';
            ++at;
          } else {
            end = true;
            break;
          }
        } else
          name += c;
      }
      if (!end)
        fail();
    } else
      name = ident();
    if (name.empty())
      fail();
    int target = sheet;
    bool qualified = take('!');
    if (qualified) {
      target = find_sheet(name);
      if (target < 0)
        throw error("#REF!");
      name = ident();
    } else if (quoted)
      fail();
    ws();
    if (!qualified && take('(')) {
      auto p = node(Node::Function, name.toUpper());
      if (!take(')')) {
        do {
          p->args.push_back(parse(0));
        } while (take(',') || take(';'));
        if (!take(')'))
          fail();
      }
      return checked(p);
    }
    Expr out = reference(name, target, qualified);
    if (out && take(':')) {
      auto last = primary();
      if (last->kind != Node::Reference)
        fail();
      if (!last->explicit_sheet)
        std::get<0>(last->first) = target;
      if (std::get<0>(last->first) != target)
        throw error("#REF!");
      out->kind = Node::Range;
      out->last = last->first;
      return out;
    }
    if (out)
      return out;
    if (!qualified && (name.toUpper() == "TRUE" || name.toUpper() == "FALSE")) {
      auto p = node(Node::Literal);
      p->literal = Value::logical(name.toUpper() == "TRUE");
      return p;
    }
    if (qualified)
      throw error("#REF!");
    return node(Node::Name, name.toCaseFolded());
  }
  Expr parse(int minimum) {
    DepthGuard guard(depth);
    auto left = primary();
    for (;;) {
      ws();
      if (at >= input.size())
        break;
      if (input[at] == '%') {
        if (60 < minimum)
          break;
        ++at;
        auto p = node(Node::Unary, "%");
        p->args = {left};
        left = checked(p);
        continue;
      }
      Text op(1, input[at]);
      if (at + 1 < input.size() && (op == "<" || op == ">")) {
        auto next = input[at + 1];
        if (next == '=' || (op == "<" && next == '>'))
          op += next;
      }
      int priority = op == "=" || op == "<>" || op == "<" || op == ">" ||
                             op == "<=" || op == ">="
                         ? 10
                     : op == "&"              ? 20
                     : op == "+" || op == "-" ? 30
                     : op == "*" || op == "/" ? 40
                     : op == "^"              ? 50
                                              : -1;
      if (priority < minimum)
        break;
      at += op.size();
      auto p = node(Node::Binary, op);
      p->args = {left, parse(priority + (op == "^" ? 0 : 1))};
      left = checked(p);
    }
    return left;
  }

public:
  Parser(const Text &s, int current, std::function<int(const Text &)> resolver)
      : input(s), sheet(current), find_sheet(std::move(resolver)) {
    if (!input.empty() && input[0] == '=')
      at = 1;
  }
  Expr run() {
    if (input.size() > 8192)
      throw error("#LIMIT!");
    auto out = parse(0);
    ws();
    if (at != input.size())
      fail();
    return out;
  }
};
} // namespace
struct SheetEngine::Impl {
  struct Cell {
    Text input;
    Value scalar;
    Expr expression;
  };
  SheetEngine *owner;
  const NativeCalculationSettings settings;
  std::vector<std::unique_ptr<NativeSheet>> sheets;
  std::map<Address, Cell> cells;
  std::map<Text, Expr> names;
  std::vector<MatrixBlock> matrices;
  std::map<Address, Address> matrix_owners;
  mutable std::map<Address, Value> cache;
  mutable std::set<Address> visiting;
  mutable std::set<Text> visiting_names;
  mutable unsigned eval_depth = 0;
  mutable unsigned reference_depth = 0;
  mutable Address current{0, 1, 1};
  mutable bool array_context = false;
  mutable bool calculation_incomplete = false;
  explicit Impl(SheetEngine *p, NativeCalculationSettings context)
      : owner(p), settings(context) {}
  int sheet_index(const Text &name) const {
    for (auto &s : sheets)
      if (s->name.compare(name, CaseInsensitive) == 0)
        return s->index;
    return -1;
  }
  Expr parse(const Text &s, int sheet) const {
    return Parser(s, sheet, [&](const Text &name) { return sheet_index(name); })
        .run();
  }
  Value value(Address pos) const {
    if (auto owner = matrix_owners.find(pos); owner != matrix_owners.end()) {
      auto result = calculate(owner->second);
      if (!result.isArray())
        return result;
      size_t row = std::get<1>(pos) - std::get<1>(owner->second),
             column = std::get<2>(pos) - std::get<2>(owner->second);
      auto cell = row < result.rows && column < result.columns
                      ? result.at(row, column)
                      : Value();
      cell.incomplete = cell.incomplete || result.incomplete;
      return cell;
    }
    return calculate(pos);
  }
  Value calculate(Address pos) const {
    if (auto p = cache.find(pos); p != cache.end())
      return p->second;
    auto p = cells.find(pos);
    if (p == cells.end())
      return {};
    if (!p->second.expression)
      return p->second.scalar;
    if (!visiting.insert(pos).second)
      return error("#CYCLE!");
    Value out;
    CurrentCell context(current, pos);
    FlagScope mode(array_context, matrix_owners.contains(pos));
    FlagScope status(calculation_incomplete, false);
    try {
      if (visiting.size() > 512)
        throw error("#DEPTH!");
      out = evaluate(p->second.expression);
    } catch (const Value &e) {
      out = e;
    } catch (const std::exception &) {
      out = Value::error("#VALUE!", true);
    }
    out.incomplete = out.incomplete || calculation_incomplete;
    visiting.erase(pos);
    cache[pos] = out;
    return out;
  }
  Expr expanded(const Expr &expr) const {
    auto result = expr;
    std::set<Text> chain;
    while (result->kind == Node::Name) {
      if (!chain.insert(result->name).second)
        throw error("#CYCLE!");
      if (chain.size() > 256)
        throw error("#DEPTH!");
      auto it = names.find(result->name);
      if (it == names.end())
        throw error("#NAME?");
      result = it->second;
    }
    return result;
  }
  std::optional<Area> area(const Expr &expr) const;
  Value materialize(const Area &region) const {
    if (region.size() > 1'000'000)
      return error("#LIMIT!");
    if (region.size() == 1)
      return value({region.sheet, region.row, region.column}).scalar();
    std::vector<Value> data;
    data.reserve(region.size());
    for (int r = 0; r < region.rows; ++r)
      for (int c = 0; c < region.columns; ++c)
        data.push_back(
            value({region.sheet, region.row + r, region.column + c}).scalar());
    return Value::array(region.rows, region.columns, std::move(data));
  }
  Value intersect(const Area &region) const {
    int row = region.rows == 1 ? region.row : std::get<1>(current);
    int column = region.columns == 1 ? region.column : std::get<2>(current);
    if (row < region.row || row >= region.row + region.rows ||
        column < region.column || column >= region.column + region.columns)
      return error("#VALUE!");
    return value({region.sheet, row, column}).scalar();
  }
  std::vector<Value> flatten(const Expr &source, bool *collection = nullptr,
                             size_t *slots = nullptr) const {
    auto expr = expanded(source);
    auto region = area(expr);
    if (!region || region->size() == 1) {
      auto result = evaluate(expr);
      if (collection)
        *collection = result.isArray() || region.has_value();
      if (slots)
        *slots = result.isArray() ? result.rows * result.columns : 1;
      if (result.isArray())
        return *result.items;
      return {result};
    }
    if (collection)
      *collection = true;
    if (slots)
      *slots = region->size();
    int s = region->sheet, r = region->row, c = region->column;
    int r2 = r + region->rows - 1, c2 = c + region->columns - 1;
    std::vector<Value> out;
    auto it = cells.lower_bound({s, r, 0});
    auto matrix = matrix_owners.lower_bound({s, r, 0});
    auto eligible = [&](auto iterator, const auto &map) {
      return iterator != map.end() && std::get<0>(iterator->first) == s &&
             std::get<1>(iterator->first) <= r2;
    };
    while (eligible(it, cells) || eligible(matrix, matrix_owners)) {
      Address position;
      if (!eligible(matrix, matrix_owners) ||
          (eligible(it, cells) && it->first < matrix->first)) {
        position = it->first;
        ++it;
      } else if (!eligible(it, cells) || matrix->first < it->first) {
        position = matrix->first;
        ++matrix;
      } else {
        position = it->first;
        ++it;
        ++matrix;
      }
      if (std::get<2>(position) >= c && std::get<2>(position) <= c2)
        out.push_back(value(position).scalar());
    }
    return out;
  }
  Value evaluate(const Expr &e) const;
  Value evaluate_node(const Expr &e) const;
  Value function(const Expr &e) const;
};
std::optional<Area> SheetEngine::Impl::area(const Expr &source) const {
  auto expr = expanded(source);
  if (expr->kind == Node::Reference) {
    auto [s, r, c] = expr->first;
    return Area{s, r, c, 1, 1};
  }
  if (expr->kind == Node::Range) {
    auto [s, r, c] = expr->first;
    auto [last_s, last_r, last_c] = expr->last;
    if (s != last_s)
      throw error("#REF!");
    return Area{s, std::min(r, last_r), std::min(c, last_c),
                std::abs(last_r - r) + 1, std::abs(last_c - c) + 1};
  }
  if (expr->kind != Node::Function)
    return {};
  auto &name = expr->name;
  size_t count = expr->args.size();
  if (name != "INDEX" && name != "OFFSET" && name != "INDIRECT")
    return {};
  DepthGuard guard(reference_depth);
  auto integer = [&](size_t i) {
    if (i >= count)
      throw error("#VALUE!");
    double n = std::trunc(number(evaluate(expr->args[i])));
    if (n < INT32_MIN || n > INT32_MAX)
      throw error("#REF!");
    return int(n);
  };
  if (name == "INDIRECT") {
    if (count < 1 || count > 2)
      throw error("#VALUE!");
    Text reference = text(evaluate(expr->args[0]));
    if (reference.empty() || reference.startsWith('='))
      throw error("#REF!");
    if (count == 2 && !truth(evaluate(expr->args[1]))) {
      auto bytes = reference.toStdString();
      std::smatch match;
      static const std::regex r1c1("R([0-9]+)C([0-9]+)", std::regex::icase);
      if (!std::regex_match(bytes, match, r1c1))
        throw error("#REF!");
      bool ok = false;
      int row = Text(match[1].str()).toInt(&ok);
      if (!ok || row < 1 || row > KS_rowMax)
        throw error("#REF!");
      int col = Text(match[2].str()).toInt(&ok);
      if (!ok || col < 1 || col > KS_colMax)
        throw error("#REF!");
      std::string label;
      for (int c = col; c; c = (c - 1) / 26)
        label.insert(label.begin(), 'A' + (c - 1) % 26);
      reference = Text(label) + Text::number(row);
    }
    Expr target;
    try {
      target = parse(reference, std::get<0>(current));
    } catch (const Value &) {
      throw error("#REF!");
    }
    if (target->kind != Node::Reference && target->kind != Node::Range &&
        target->kind != Node::Name)
      throw error("#REF!");
    auto result = area(target);
    if (!result)
      throw error("#REF!");
    return result;
  }
  if (name == "INDEX") {
    if (count < 2 || count > 3)
      throw error("#VALUE!");
    auto source = area(expr->args[0]);
    if (!source)
      return {};
    int row = integer(1), column = count > 2 ? integer(2) : 1;
    if (count == 2 && source->rows == 1) {
      column = row;
      row = 1;
    }
    if (row < 0 || column < 0 || row > source->rows || column > source->columns)
      throw error("#REF!");
    if (row) {
      source->row += row - 1;
      source->rows = 1;
    }
    if (column) {
      source->column += column - 1;
      source->columns = 1;
    }
    return source;
  }
  if (count < 3 || count > 5)
    throw error("#VALUE!");
  auto result = area(expr->args[0]);
  if (!result)
    throw error("#VALUE!");
  Int64 row = Int64(result->row) + integer(1),
        column = Int64(result->column) + integer(2);
  Int64 rows = count > 3 ? integer(3) : result->rows,
        columns = count > 4 ? integer(4) : result->columns;
  if (rows <= 0 || columns <= 0)
    throw error("#VALUE!");
  if (row < 1 || column < 1 || row + rows - 1 > KS_rowMax ||
      column + columns - 1 > KS_colMax)
    throw error("#REF!");
  return Area{result->sheet, int(row), int(column), int(rows), int(columns)};
}
Value SheetEngine::Impl::evaluate(const Expr &e) const {
  auto result = evaluate_node(e);
  calculation_incomplete = calculation_incomplete || result.incomplete;
  return result;
}
Value SheetEngine::Impl::evaluate_node(const Expr &e) const try {
  DepthGuard guard(eval_depth);
  switch (e->kind) {
  case Node::Literal:
    return e->literal;
  case Node::Reference:
    return value(e->first).scalar();
  case Node::Range:
    return array_context ? materialize(*area(e)) : intersect(*area(e));
  case Node::Array: {
    // Retained Calligra formulas allow scalar expressions in array entries,
    // including DATE calls and cell references. A multi-cell result remains
    // a nested array and is rejected by the typed-array constructor.
    FlagScope elements(array_context, true);
    std::vector<Value> data;
    data.reserve(e->args.size());
    for (const auto &item : e->args)
      data.push_back(evaluate(item));
    size_t rows = data.size() / e->columns;
    return Value::array(rows, e->columns, std::move(data));
  }
  case Node::Name: {
    auto it = names.find(e->name);
    if (it == names.end())
      return error("#NAME?");
    if (!visiting_names.insert(e->name).second)
      return error("#CYCLE!");
    Value out;
    try {
      out = evaluate(it->second);
    } catch (...) {
      visiting_names.erase(e->name);
      throw;
    }
    visiting_names.erase(e->name);
    return out;
  }
  case Node::Unary: {
    auto a = evaluate(e->args[0]);
    return map_values(a, Value{}, [&](const Value &v, const Value &) -> Value {
      double n = number(v);
      return numeric(e->name == "-" ? -n : e->name == "%" ? n / 100 : n);
    });
  }
  case Node::Binary:
    return binary(e->name, evaluate(e->args[0]), evaluate(e->args[1]));
  case Node::Function:
    return function(e);
  }
  return error("#VALUE!");
} catch (const Value &failure) {
  return failure;
}
Value SheetEngine::Impl::function(const Expr &e) const {
  const auto name = calendar_name(financial_name(engineering_name(e->name)));
  auto count = e->args.size();
  static const auto signatures = [] {
    std::map<Text, std::pair<size_t, size_t>> out;
    auto add = [&](const char *names, size_t low, size_t high) {
      for (auto name : Text(names).split(' '))
        out[name] = {low, high};
    };
    add("TRUE FALSE NA PI", 0, 0);
    add("ABS SQRT EXP LN LOG10 SIN COS TAN ASIN ACOS ATAN SINH COSH TANH "
        "DEGREES RADIANS INT SIGN FACT LEN LOWER UPPER TRIM VALUE NOT ISERROR "
        "ISERR ISNA ISNUMBER ISTEXT ISLOGICAL ISBLANK YEAR MONTH DAY HOUR "
        "MINUTE SECOND",
        1, 1);
    add("POWER ATAN2 MOD ROUND ROUNDUP ROUNDDOWN EXACT REPT IFERROR IFNA", 2,
        2);
    add("LOG TRUNC LEFT RIGHT FLOOR CEILING", 1, 2);
    add("MID DATE TIME", 3, 3);
    add("IF FIND SEARCH", 2, 3);
    add("SUBSTITUTE", 3, 4);
    add("SUM PRODUCT MIN MAX COUNT COUNTA CONCAT CONCATENATE SUMSQ", 0, 255);
    add("AVERAGE MEDIAN STDEV STDEV.S STDEVP STDEV.P VAR VAR.S VARP VAR.P AND "
        "OR XOR",
        1, 255);
    add("COUNTBLANK", 1, 1);
    add("CHOOSE", 2, 255);
    add("INDEX MATCH LOOKUP", 2, 3);
    add("VLOOKUP HLOOKUP", 3, 4);
    add("OFFSET", 3, 5);
    add("INDIRECT", 1, 2);
    add("ROW COLUMN SHEET SHEETS", 0, 1);
    add("ROWS COLUMNS TRANSPOSE MDETERM MINVERSE", 1, 1);
    add("MMULT", 2, 2);
    add("SUMPRODUCT", 1, 255);
    for (const auto &[name, arity] : engineering_signatures())
      out[name] = arity;
    for (const auto &[name, arity] : financial_signatures())
      out[name] = arity;
    for (const auto &[name, arity] : calendar_signatures())
      out[name] = arity;
    return out;
  }();
  auto signature = signatures.find(name);
  if (signature == signatures.end())
    return error("#NAME?");
  if (count < signature->second.first || count > signature->second.second)
    return error("#VALUE!");

  static const std::set<Text> accepts_arrays = {
      "SUM",     "PRODUCT", "AVERAGE",     "MIN",       "MAX",
      "COUNT",   "COUNTA",  "COUNTBLANK",  "MEDIAN",    "STDEV",
      "STDEV.S", "STDEVP",  "STDEV.P",     "VAR",       "VAR.S",
      "VARP",    "VAR.P",   "AND",         "OR",        "XOR",
      "SUMSQ",   "CONCAT",  "CONCATENATE", "NPV",       "CHOOSE",
      "INDEX",   "OFFSET",  "INDIRECT",    "ROW",       "COLUMN",
      "SHEET",   "SHEETS",  "ROWS",        "COLUMNS",   "MATCH",
      "LOOKUP",  "VLOOKUP", "HLOOKUP",     "TRANSPOSE", "SUMPRODUCT",
      "MMULT",   "MDETERM", "MINVERSE",    "IMSUM",     "IMPRODUCT",
      "IRR",     "MIRR",    "FVSCHEDULE",  "XIRR",      "XNPV",
      "AMORLINC"};
  bool caller_array_context = array_context;
  FlagScope function_context(array_context,
                             array_context || accepts_arrays.contains(name));
  auto arg = [&](size_t i) -> Value {
    if (i >= count)
      throw error("#VALUE!");
    return evaluate(e->args[i]);
  };
  auto n = [&](size_t i) { return number(arg(i)); };
  auto s = [&](size_t i) { return text(arg(i)); };
  auto integer = [&](size_t i, int low = INT32_MIN, int high = INT32_MAX) {
    double v = std::trunc(n(i));
    if (v < low || v > high)
      throw error("#NUM!");
    return int(v);
  };
  if (name == "IF") {
    if (count < 2 || count > 3)
      return error("#VALUE!");
    auto condition = arg(0);
    if (!condition.isArray())
      return truth(condition) ? arg(1)
             : count == 3     ? arg(2)
                              : Value::logical(false);
    FlagScope conditional(array_context, true);
    std::optional<Value> yes, no;
    std::vector<Value> result;
    result.reserve(condition.items->size());
    for (size_t r = 0; r < condition.rows; ++r)
      for (size_t c = 0; c < condition.columns; ++c) {
        try {
          bool select = truth(condition.at(r, c));
          auto &branch = select ? yes : no;
          if (!branch)
            branch = select       ? arg(1)
                     : count == 3 ? arg(2)
                                  : Value::logical(false);
          result.push_back(branch->isArray() &&
                                   (branch->rows != condition.rows ||
                                    branch->columns != condition.columns)
                               ? error("#VALUE!")
                               : branch->at(r, c));
        } catch (const Value &failure) {
          result.push_back(failure);
        }
      }
    return Value::array(condition.rows, condition.columns, std::move(result));
  }
  if (name == "IFERROR" || name == "IFNA") {
    auto v = arg(0);
    if (v.isArray()) {
      FlagScope fallback(array_context, true);
      std::optional<Value> replacement;
      std::vector<Value> result;
      result.reserve(v.items->size());
      for (size_t r = 0; r < v.rows; ++r)
        for (size_t c = 0; c < v.columns; ++c) {
          auto item = v.at(r, c);
          if (item.isError() && (name == "IFERROR" || item.string == "#N/A")) {
            if (!replacement)
              replacement = arg(1);
            item = replacement->isArray() && (replacement->rows != v.rows ||
                                              replacement->columns != v.columns)
                       ? error("#VALUE!")
                       : replacement->at(r, c);
          }
          result.push_back(item);
        }
      return Value::array(v.rows, v.columns, std::move(result));
    }
    return (name == "IFERROR" ? v.isError() : v.isError() && v.string == "#N/A")
               ? arg(1)
               : v;
  }
  if (name == "CHOOSE") {
    auto select = arg(0);
    auto choose = [&](const Value &item) -> size_t {
      double i = std::trunc(number(item));
      if (i < 1 || i >= count)
        throw error("#VALUE!");
      return size_t(i);
    };
    if (!select.isArray())
      return arg(choose(select));
    FlagScope selection(array_context, true);
    std::vector<std::optional<Value>> choices(count);
    std::vector<Value> result;
    for (size_t r = 0; r < select.rows; ++r)
      for (size_t c = 0; c < select.columns; ++c) {
        try {
          size_t index = choose(select.at(r, c));
          auto &choice = choices[index];
          if (!choice)
            choice = arg(index);
          result.push_back(choice->isArray() &&
                                   (choice->rows != select.rows ||
                                    choice->columns != select.columns)
                               ? error("#VALUE!")
                               : choice->at(r, c));
        } catch (const Value &failure) {
          result.push_back(failure);
        }
      }
    return Value::array(select.rows, select.columns, std::move(result));
  }
  // Scalar functions map typed arrays element by element. Functions which
  // consume whole ranges keep their dimensions and (for sparse aggregates)
  // their extent.

  if (!accepts_arrays.contains(name)) {
    std::vector<Value> args;
    bool any_array = false;
    size_t rows = 1, columns = 1;
    for (auto &expression : e->args) {
      auto v = evaluate(expression);
      if (v.isArray()) {
        if (any_array && (rows != v.rows || columns != v.columns))
          return error("#VALUE!");
        any_array = true;
        rows = v.rows;
        columns = v.columns;
      }
      args.push_back(v);
    }
    if (any_array) {
      std::vector<Value> result;
      result.reserve(rows * columns);
      for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < columns; ++c) {
          auto scalar = node(Node::Function, name);
          for (auto &v : args) {
            auto literal = node(Node::Literal);
            literal->literal = v.at(r, c);
            scalar->args.push_back(literal);
          }
          result.push_back(evaluate(scalar));
        }
      return Value::array(rows, columns, std::move(result));
    }
  }
  if (calendar_signatures().contains(name)) {
    std::vector<Value> args;
    for (size_t i = 0; i < count; ++i)
      args.push_back(arg(i));
    return calendar_function(name, args, settings);
  }
  if (financial_signatures().contains(name)) {
    std::vector<Value> args;
    size_t records = 0;
    for (size_t i = 0; i < count; ++i) {
      bool sequence = (name == "NPV" && i > 0) ||
                      ((name == "IRR" || name == "MIRR") && i == 0) ||
                      (name == "FVSCHEDULE" && i == 1);
      if (!sequence) {
        args.push_back(arg(i));
        continue;
      }
      bool collection = false;
      auto values = flatten(e->args[i], &collection);
      if (values.size() > 1000000 - records)
        return error("#LIMIT!");
      records += values.size();
      if (collection) {
        // Preserve the distinction between a direct scalar and a cell/range:
        // NumberSequence excludes referenced text, Boolean and empty cells.
        if (values.empty())
          values.push_back(Value());
        auto rows = values.size();
        args.push_back(Value::array(rows, 1, std::move(values)));
      } else
        args.push_back(values.front());
    }
    return financial_function(name, args, settings);
  }
  if (engineering_signatures().contains(name)) {
    std::vector<Value> args;
    if (name == "IMSUM" || name == "IMPRODUCT") {
      for (const auto &expression : e->args) {
        bool collection = false;
        auto values = flatten(expression, &collection);
        for (const auto &value : values)
          if (!collection ||
              (value.kind != Value::Empty && value.kind != Value::Boolean))
            args.push_back(value);
        if (args.size() > 1000000)
          return error("#LIMIT!");
      }
      if (args.empty())
        args.push_back(Value());
      // Aggregates accept many values from one range, beyond the syntactic
      // argument-count limit; package them as a single native array.
      auto rows = args.size();
      return engineering_function(name,
                                  {Value::array(rows, 1, std::move(args))});
    }
    for (size_t i = 0; i < count; ++i)
      args.push_back(arg(i));
    return engineering_function(name, args);
  }
  if (name == "ROWS" || name == "COLUMNS") {
    if (auto region = area(e->args[0]))
      return numeric(name == "ROWS" ? region->rows : region->columns);
    auto data = arg(0);
    if (data.isError())
      return data;
    return numeric(name == "ROWS" ? data.rows : data.columns);
  }
  if (name == "ROW" || name == "COLUMN") {
    Area region{std::get<0>(current), std::get<1>(current),
                std::get<2>(current), 1, 1};
    if (count) {
      auto reference = area(e->args[0]);
      if (!reference)
        return error("#VALUE!");
      region = *reference;
    }
    size_t rows = name == "ROW" ? region.rows : 1,
           columns = name == "COLUMN" ? region.columns : 1;
    if (rows == 1 && columns == 1)
      return numeric(name == "ROW" ? region.row : region.column);
    if (rows > 1'000'000 / columns)
      return error("#LIMIT!");
    std::vector<Value> data;
    data.reserve(rows * columns);
    for (size_t r = 0; r < rows; ++r)
      for (size_t c = 0; c < columns; ++c)
        data.push_back(
            numeric(name == "ROW" ? region.row + r : region.column + c));
    return Value::array(rows, columns, std::move(data));
  }
  if (name == "SHEET" || name == "SHEETS") {
    if (!count)
      return numeric(name == "SHEETS" ? sheets.size()
                                      : std::get<0>(current) + 1);
    if (auto reference = area(e->args[0]))
      return numeric(name == "SHEETS" ? 1 : reference->sheet + 1);
    if (name == "SHEETS")
      return error("#VALUE!");
    int sheet = sheet_index(text(arg(0)));
    return sheet < 0 ? error("#N/A") : numeric(sheet + 1);
  }
  if (name == "INDEX" || name == "OFFSET" || name == "INDIRECT") {
    if (auto reference = area(e))
      return caller_array_context ? materialize(*reference)
                                  : intersect(*reference);
    if (name != "INDEX")
      return error("#REF!");
    auto data = arg(0);
    if (data.isError())
      return data;
    int row = integer(1), column = count > 2 ? integer(2) : 1;
    if (count == 2 && data.rows == 1) {
      column = row;
      row = 1;
    }
    if (row < 0 || column < 0 || size_t(row) > data.rows ||
        size_t(column) > data.columns)
      return error("#REF!");
    if (row && column)
      return data.at(row - 1, column - 1);
    size_t rows = row ? 1 : data.rows, columns = column ? 1 : data.columns;
    std::vector<Value> result;
    result.reserve(rows * columns);
    for (size_t r = 0; r < rows; ++r)
      for (size_t c = 0; c < columns; ++c)
        result.push_back(data.at(row ? row - 1 : r, column ? column - 1 : c));
    return Value::array(rows, columns, std::move(result));
  }
  if (name == "TRANSPOSE") {
    auto data = arg(0);
    if (data.isError())
      return data;
    std::vector<Value> result;
    result.reserve(data.rows * data.columns);
    for (size_t c = 0; c < data.columns; ++c)
      for (size_t r = 0; r < data.rows; ++r)
        result.push_back(data.at(r, c));
    return Value::array(data.columns, data.rows, std::move(result));
  }
  if (name == "MATCH" || name == "LOOKUP" || name == "VLOOKUP" ||
      name == "HLOOKUP") {
    auto key = arg(0), data = arg(1);
    if (data.isError())
      return data;
    if (name == "VLOOKUP" || name == "HLOOKUP") {
      auto indices = arg(2);
      bool horizontal = name == "HLOOKUP",
           approximate = count < 4 || truth(arg(3));
      return map_values(
          key, indices, [&](const Value &search, const Value &index) -> Value {
            if (search.isError())
              return search;
            double requested = std::trunc(number(index));
            if (requested < 1)
              return error("#VALUE!");
            size_t bound = horizontal ? data.rows : data.columns;
            if (requested > bound)
              return error("#REF!");
            if (search.kind == Value::Empty && approximate)
              return error("#N/A");
            size_t field = size_t(requested) - 1,
                   items = horizontal ? data.columns : data.rows;
            std::optional<size_t> candidate;
            Value best;
            for (size_t i = 0; i < items; ++i) {
              auto item = horizontal ? data.at(0, i) : data.at(i, 0);
              if (item.isError())
                continue;
              int comparison = lookup_compare(item, search);
              if (comparison == 0)
                return horizontal ? data.at(field, i) : data.at(i, field);
              if (approximate && lookup_comparable(item, search) &&
                  comparison < 0 &&
                  (!candidate || lookup_compare(best, item) < 0)) {
                candidate = i;
                best = item;
              }
            }
            if (!candidate)
              return error("#N/A");
            return horizontal ? data.at(field, *candidate)
                              : data.at(*candidate, field);
          });
    }
    Value returned;
    size_t length = 0;
    bool horizontal = data.rows == 1;
    if (name == "LOOKUP" && count == 2) {
      horizontal = data.columns > data.rows;
      length = horizontal ? data.columns : data.rows;
      std::vector<Value> keys, values;
      keys.reserve(length);
      values.reserve(length);
      for (size_t i = 0; i < length; ++i) {
        keys.push_back(horizontal ? data.at(0, i) : data.at(i, 0));
        values.push_back(horizontal ? data.at(data.rows - 1, i)
                                    : data.at(i, data.columns - 1));
      }
      data = Value::array(length, 1, std::move(keys));
      returned = Value::array(length, 1, std::move(values));
      horizontal = false;
    } else {
      if (data.rows != 1 && data.columns != 1)
        return error("#N/A");
      length = std::max(data.rows, data.columns);
      if (name == "LOOKUP") {
        returned = arg(2);
        if (returned.isError())
          return returned;
        if ((returned.rows != 1 && returned.columns != 1) ||
            std::max(returned.rows, returned.columns) != length)
          return error("#VALUE!");
      }
    }
    int mode = name == "MATCH" && count == 3 ? integer(2) : 1;
    return map_values(
        key, Value{}, [&](const Value &search, const Value &) -> Value {
          if (search.isError())
            return search;
          std::optional<size_t> candidate;
          for (size_t i = 0; i < length; ++i) {
            auto item = horizontal ? data.at(0, i) : data.at(i, 0);
            if (item.isError())
              continue;
            int comparison = lookup_compare(item, search);
            if ((name == "LOOKUP" || mode == 0) && comparison == 0)
              return name == "MATCH"
                         ? numeric(i + 1)
                         : returned.at(returned.rows == 1 ? 0 : i,
                                       returned.columns == 1 ? 0 : i);
            if (mode && lookup_comparable(item, search) &&
                (mode > 0 ? comparison <= 0 : comparison >= 0))
              candidate = i;
          }
          if (!candidate)
            return error("#N/A");
          return name == "MATCH"
                     ? numeric(*candidate + 1)
                     : returned.at(returned.rows == 1 ? 0 : *candidate,
                                   returned.columns == 1 ? 0 : *candidate);
        });
  }
  if (name == "SUMPRODUCT") {
    std::vector<Value> inputs;
    size_t rows = 0, columns = 0;
    for (size_t i = 0; i < count; ++i) {
      auto v = arg(i);
      if (v.isError())
        return v;
      if (i && (rows != v.rows || columns != v.columns))
        return error("#VALUE!");
      rows = v.rows;
      columns = v.columns;
      inputs.push_back(v);
    }
    double total = 0;
    for (size_t r = 0; r < rows; ++r)
      for (size_t c = 0; c < columns; ++c) {
        double product = 1;
        for (auto &input : inputs) {
          auto v = input.at(r, c);
          if (v.isError())
            return v;
          product *= v.isNumber()    ? v.number
                     : v.isBoolean() ? (v.boolean ? 1 : 0)
                                     : 0;
        }
        total += product;
      }
    return numeric(total);
  }
  if (name == "MMULT") {
    auto a = arg(0), b = arg(1);
    if (a.isError())
      return a;
    if (b.isError())
      return b;
    if (a.columns != b.rows)
      return error("#VALUE!");
    if (a.rows > 1'000'000 / b.columns ||
        a.columns > 100'000'000 / a.rows / b.columns)
      return error("#LIMIT!");
    std::vector<Value> result;
    result.reserve(a.rows * b.columns);
    for (size_t r = 0; r < a.rows; ++r)
      for (size_t c = 0; c < b.columns; ++c) {
        double total = 0;
        for (size_t k = 0; k < a.columns; ++k) {
          auto x = a.at(r, k), y = b.at(k, c);
          if (x.isError())
            return x;
          if (y.isError())
            return y;
          if (!x.isNumber() || !y.isNumber())
            return error("#VALUE!");
          total += x.number * y.number;
        }
        result.push_back(numeric(total));
      }
    return Value::array(a.rows, b.columns, std::move(result));
  }
  if (name == "MDETERM" || name == "MINVERSE") {
    auto input = arg(0);
    if (input.isError())
      return input;
    if (input.rows != input.columns)
      return error("#VALUE!");
    size_t width = input.rows;
    if (width > 512)
      return error("#LIMIT!");
    std::vector<double> matrix(width * width), inverse(width * width);
    for (size_t r = 0; r < width; ++r)
      for (size_t c = 0; c < width; ++c) {
        auto v = input.at(r, c);
        if (v.isError())
          return v;
        if (!v.isNumber())
          return error("#VALUE!");
        matrix[r * width + c] = v.number;
        inverse[r * width + c] = r == c ? 1 : 0;
      }
    double determinant = 1;
    for (size_t k = 0; k < width; ++k) {
      size_t pivot = k;
      for (size_t r = k + 1; r < width; ++r)
        if (std::abs(matrix[r * width + k]) >
            std::abs(matrix[pivot * width + k]))
          pivot = r;
      if (matrix[pivot * width + k] == 0)
        return name == "MDETERM" ? numeric(0) : error("#NUM!");
      if (pivot != k) {
        determinant = -determinant;
        for (size_t c = 0; c < width; ++c) {
          std::swap(matrix[k * width + c], matrix[pivot * width + c]);
          std::swap(inverse[k * width + c], inverse[pivot * width + c]);
        }
      }
      double diagonal = matrix[k * width + k];
      determinant *= diagonal;
      for (size_t c = 0; c < width; ++c) {
        matrix[k * width + c] /= diagonal;
        inverse[k * width + c] /= diagonal;
      }
      for (size_t r = 0; r < width; ++r)
        if (r != k) {
          double factor = matrix[r * width + k];
          for (size_t c = 0; c < width; ++c) {
            matrix[r * width + c] -= factor * matrix[k * width + c];
            inverse[r * width + c] -= factor * inverse[k * width + c];
          }
        }
    }
    if (name == "MDETERM")
      return numeric(determinant);
    std::vector<Value> result;
    result.reserve(inverse.size());
    for (double n : inverse)
      result.push_back(numeric(n));
    return Value::array(width, width, std::move(result));
  }
  if (name == "TRUE" || name == "FALSE")
    return count ? error("#VALUE!") : Value::logical(name == "TRUE");
  if (name == "NA")
    return error("#N/A");
  if (name == "PI")
    return numeric(std::acos(-1.0));
  if (name == "ISERROR" || name == "ISERR" || name == "ISNA" ||
      name == "ISNUMBER" || name == "ISTEXT" || name == "ISLOGICAL" ||
      name == "ISBLANK") {
    auto v = arg(0);
    return Value::logical(name == "ISERROR" ? v.isError()
                          : name == "ISERR" ? v.isError() && v.string != "#N/A"
                          : name == "ISNA"  ? v.isError() && v.string == "#N/A"
                          : name == "ISNUMBER"  ? v.isNumber()
                          : name == "ISTEXT"    ? v.kind == Value::String
                          : name == "ISLOGICAL" ? v.isBoolean()
                                                : v.kind == Value::Empty);
  }
  if (name == "NOT")
    return Value::logical(!truth(arg(0)));
  if (name == "AND" || name == "OR" || name == "XOR") {
    bool out = name == "AND";
    int trues = 0;
    bool any = false;
    for (auto &a : e->args) {
      bool collection = false;
      for (auto &v : flatten(a, &collection)) {
        if (collection && (v.kind == Value::Empty || v.kind == Value::String))
          continue;
        bool t = truth(v);
        any = true;
        if (name == "AND")
          out = out && t;
        else if (name == "OR")
          out = out || t;
        else
          trues += t;
      }
    }
    if (!any)
      return error("#VALUE!");
    return Value::logical(name == "XOR" ? trues % 2 : out);
  }
  if (name == "SUM" || name == "PRODUCT" || name == "AVERAGE" ||
      name == "MIN" || name == "MAX" || name == "COUNT" || name == "COUNTA" ||
      name == "COUNTBLANK" || name == "MEDIAN" || name == "STDEV" ||
      name == "STDEV.S" || name == "STDEVP" || name == "STDEV.P" ||
      name == "VAR" || name == "VAR.S" || name == "VARP" || name == "VAR.P") {
    std::vector<double> numbers;
    size_t nonempty = 0, blanks = 0;
    for (auto &a : e->args) {
      bool collection = false;
      size_t slots = 0;
      auto values = flatten(a, &collection, &slots);
      blanks += slots - values.size();
      for (auto &v : values) {
        if (v.kind == Value::Empty ||
            (v.kind == Value::String && v.string.empty()))
          ++blanks;
        if (v.kind != Value::Empty)
          ++nonempty;
        if (v.isError()) {
          if (name == "COUNTA" || name == "COUNTBLANK" ||
              (name == "COUNT" && collection))
            continue;
          throw v;
        }
        if (v.isNumber())
          numbers.push_back(v.number);
        else if (v.isBoolean() && !collection)
          numbers.push_back(v.boolean ? 1 : 0);
        else if (v.kind == Value::String && !collection && name != "COUNTA" &&
                 name != "COUNTBLANK") {
          bool valid = false;
          double scalar = v.string.toDouble(&valid);
          if (valid)
            numbers.push_back(scalar);
          else if (name != "COUNT")
            throw error("#VALUE!");
        }
      }
    }
    if (name == "COUNT")
      return numeric(numbers.size());
    if (name == "COUNTA")
      return numeric(nonempty);
    if (name == "COUNTBLANK")
      return numeric(blanks);
    double sum = std::accumulate(numbers.begin(), numbers.end(), 0.0);
    if (name == "SUM")
      return numeric(sum);
    if (name == "PRODUCT")
      return numeric(numbers.empty()
                         ? 0
                         : std::accumulate(numbers.begin(), numbers.end(), 1.0,
                                           std::multiplies<double>()));
    if (name == "MIN" || name == "MAX")
      return numeric(numbers.empty() ? 0
                     : name == "MIN"
                         ? *std::min_element(numbers.begin(), numbers.end())
                         : *std::max_element(numbers.begin(), numbers.end()));
    if (numbers.empty())
      return error("#DIV/0!");
    if (name == "AVERAGE")
      return numeric(sum / numbers.size());
    if (name == "MEDIAN") {
      std::sort(numbers.begin(), numbers.end());
      size_t m = numbers.size() / 2;
      return numeric(numbers.size() % 2 ? numbers[m]
                                        : (numbers[m - 1] + numbers[m]) / 2);
    }
    bool population = name == "STDEVP" || name == "STDEV.P" || name == "VARP" ||
                      name == "VAR.P";
    if (numbers.size() < (population ? 1u : 2u))
      return error("#DIV/0!");
    double mean = sum / numbers.size(), squares = 0;
    for (double v : numbers)
      squares += (v - mean) * (v - mean);
    double variance = squares / (numbers.size() - (population ? 0 : 1));
    return numeric(name.startsWith("STDEV") ? std::sqrt(variance) : variance);
  }
  if (name == "ABS")
    return numeric(std::abs(n(0)));
  if (name == "SQRT")
    return numeric(std::sqrt(n(0)));
  if (name == "POWER")
    return numeric(std::pow(n(0), n(1)));
  if (name == "EXP")
    return numeric(std::exp(n(0)));
  if (name == "LN")
    return numeric(std::log(n(0)));
  if (name == "LOG10")
    return numeric(std::log10(n(0)));
  if (name == "LOG")
    return numeric(std::log(n(0)) / std::log(count > 1 ? n(1) : 10));
  if (name == "SIN")
    return numeric(std::sin(n(0)));
  if (name == "COS")
    return numeric(std::cos(n(0)));
  if (name == "TAN")
    return numeric(std::tan(n(0)));
  if (name == "ASIN")
    return numeric(std::asin(n(0)));
  if (name == "ACOS")
    return numeric(std::acos(n(0)));
  if (name == "ATAN")
    return numeric(std::atan(n(0)));
  if (name == "ATAN2")
    return numeric(std::atan2(n(1), n(0)));
  if (name == "SINH")
    return numeric(std::sinh(n(0)));
  if (name == "COSH")
    return numeric(std::cosh(n(0)));
  if (name == "TANH")
    return numeric(std::tanh(n(0)));
  if (name == "DEGREES")
    return numeric(n(0) * 180 / std::acos(-1.0));
  if (name == "RADIANS")
    return numeric(n(0) * std::acos(-1.0) / 180);
  if (name == "INT")
    return numeric(std::floor(n(0)));
  if (name == "SIGN") {
    double v = n(0);
    return numeric(v < 0 ? -1 : v > 0 ? 1 : 0);
  }
  if (name == "MOD") {
    double x = n(0), y = n(1);
    return y == 0 ? error("#DIV/0!") : numeric(x - y * std::floor(x / y));
  }
  if (name == "ROUND" || name == "ROUNDUP" || name == "ROUNDDOWN" ||
      name == "TRUNC") {
    double x = n(0), digits = count > 1 ? n(1) : 0;
    if (std::abs(digits) > 308)
      return error("#NUM!");
    double scale = std::pow(10, std::trunc(digits)), v = x * scale;
    if (name == "ROUND")
      v = std::round(v);
    else if (name == "ROUNDUP")
      v = std::copysign(std::ceil(std::abs(v)), v);
    else
      v = std::trunc(v);
    return numeric(v / scale);
  }
  if (name == "FLOOR" || name == "CEILING") {
    double x = n(0), step = count > 1 ? n(1) : 1;
    if (step == 0)
      return numeric(0);
    return numeric(
        (name == "FLOOR" ? std::floor(x / step) : std::ceil(x / step)) * step);
  }
  if (name == "FACT") {
    double v = std::floor(n(0));
    return v < 0 || v > 170 ? error("#NUM!") : numeric(std::tgamma(v + 1));
  }
  if (name == "SUMSQ") {
    double sum = 0;
    for (auto &a : e->args)
      for (auto &v : flatten(a)) {
        if (v.isError())
          throw v;
        if (v.isNumber())
          sum += v.number * v.number;
      }
    return numeric(sum);
  }
  if (name == "LEN")
    return numeric(s(0).size());
  if (name == "LOWER")
    return Value(s(0).toLower());
  if (name == "UPPER")
    return Value(s(0).toUpper());
  if (name == "TRIM") {
    auto in = s(0).trimmed();
    Text out;
    bool space = false;
    for (auto c : in) {
      if (c == ' ') {
        space = true;
        continue;
      }
      if (space && !out.empty())
        out += ' ';
      out += c;
      space = false;
    }
    return Value(out);
  }
  if (name == "CONCATENATE" || name == "CONCAT") {
    Text out;
    for (auto &a : e->args)
      for (auto &v : flatten(a))
        out = concatenate(out, text(v));
    return Value(out);
  }
  if (name == "LEFT" || name == "RIGHT") {
    Text value = s(0);
    int len = count > 1 ? integer(1) : 1;
    if (len < 0)
      return error("#VALUE!");
    return Value(name == "LEFT" ? value.left(len) : value.right(len));
  }
  if (name == "MID") {
    Text value = s(0);
    int pos = integer(1), len = integer(2);
    if (pos < 1 || len < 0)
      return error("#VALUE!");
    return Value(value.mid(pos - 1, len));
  }
  if (name == "EXACT")
    return Value::logical(s(0) == s(1));
  if (name == "VALUE") {
    bool ok = false;
    double v = s(0).toDouble(&ok);
    return ok ? numeric(v) : error("#VALUE!");
  }
  if (name == "REPT") {
    auto value = s(0);
    int times = integer(1);
    if (times < 0 || size_t(times) * value.size() > 1'000'000)
      return error("#VALUE!");
    if (value.empty())
      return Value(Text());
    Text out;
    for (int i = 0; i < times; i++)
      out += value;
    return Value(out);
  }
  if (name == "SUBSTITUTE") {
    auto in = s(0), before = s(1), after = s(2);
    if (before.empty())
      return Value(in);
    int target = count > 3 ? integer(3) : 0, found = 0;
    if (count > 3 && target <= 0)
      return error("#VALUE!");
    size_t at = 0;
    while ((at = in.find(before, at)) != Text::npos) {
      ++found;
      if (!target || target == found) {
        in.std::u32string::replace(at, before.size(), after);
        at += after.size();
      } else
        at += before.size();
      if (in.size() > 1'000'000)
        return error("#LIMIT!");
    }
    return target < 0 ? error("#VALUE!") : Value(in);
  }
  if (name == "FIND" || name == "SEARCH") {
    auto needle = s(0), haystack = s(1);
    int at = count > 2 ? integer(2) : 1;
    if (at < 1)
      return error("#VALUE!");
    if (name == "SEARCH" &&
        (needle.contains('*') || needle.contains('?') || needle.contains('~')))
      return error("#NAME?");
    if (name == "SEARCH") {
      needle = needle.toLower();
      haystack = haystack.toLower();
    }
    int found = haystack.indexOf(needle, at - 1);
    return found < 0 ? error("#VALUE!") : numeric(found + 1);
  }
  if (name == "DATE") {
    int year = integer(0, 0, 9999), month = integer(1, -120000, 120000),
        day = integer(2, -3660000, 3660000);
    if (year >= 0 && year < 1900)
      year += 1900;
    auto base =
        std::chrono::year_month(std::chrono::year(year), std::chrono::January) +
        std::chrono::months(month - 1);
    auto date = std::chrono::sys_days(base / std::chrono::day(1)) +
                std::chrono::days(day - 1);
    double serial = (date - settings.reference_date()).count();
    return numeric(serial);
  }
  if (name == "YEAR" || name == "MONTH" || name == "DAY") {
    auto date = std::chrono::year_month_day(calendar_date(arg(0), settings));
    return numeric(name == "YEAR"    ? int(date.year())
                   : name == "MONTH" ? unsigned(date.month())
                                     : unsigned(date.day()));
  }
  if (name == "TIME") {
    double seconds = n(0) * 3600 + n(1) * 60 + n(2);
    return seconds < 0 ? error("#NUM!")
                       : numeric(std::fmod(seconds, 86400) / 86400);
  }
  if (name == "HOUR" || name == "MINUTE" || name == "SECOND") {
    double serial = n(0);
    if (serial < 0)
      return error("#NUM!");
    int seconds = int(std::floor((serial - std::floor(serial)) * 86400 + 1e-5));
    return numeric(name == "HOUR"     ? seconds / 3600
                   : name == "MINUTE" ? seconds / 60 % 60
                                      : seconds % 60);
  }
  return error("#NAME?");
}
SheetEngine::SheetEngine(NativeCalculationSettings settings)
    : impl_(std::make_unique<Impl>(this, settings)) {}
const NativeCalculationSettings &SheetEngine::calculation_settings() const {
  return impl_->settings;
}
SheetEngine::~SheetEngine() = default;
int SheetEngine::count() const { return impl_->sheets.size(); }
NativeSheet *SheetEngine::sheet(int s) const {
  return impl_->sheets.at(s).get();
}
NativeSheet *SheetEngine::findSheet(const Text &name) const {
  int s = impl_->sheet_index(name);
  return s < 0 ? nullptr : sheet(s);
}
void SheetEngine::addNewSheet(const Text &name) {
  impl_->sheets.push_back(
      std::make_unique<NativeSheet>(NativeSheet{this, count(), name}));
  impl_->cache.clear();
}
Text SheetEngine::input(int s, int c, int r) const {
  auto it = impl_->cells.find({s, r, c});
  return it == impl_->cells.end() ? Text() : it->second.input;
}
NativeValue SheetEngine::value(int s, int c, int r) const {
  return impl_->value({s, r, c});
}
void SheetEngine::set(int s, int c, int r, const Text &value, bool literal) {
  if (s < 0 || s >= count() || c < 1 || r < 1 || c > KS_colMax || r > KS_rowMax)
    throw std::runtime_error("Cell is outside the worksheet");
  if (matrix_child(s, c, r))
    throw std::runtime_error("Edit the top-left cell of the matrix formula");
  if (impl_->matrix_owners.contains({s, r, c}) && !value.startsWith('='))
    throw std::runtime_error("A matrix master requires a formula");
  auto &sheet = *impl_->sheets.at(s);
  sheet.row_count = std::max(sheet.row_count, r);
  sheet.col_count = std::max(sheet.col_count, c);
  Impl::Cell cell;
  cell.input = value;
  if (!literal && value.startsWith('=')) {
    try {
      cell.expression = impl_->parse(value, s);
    } catch (const Value &e) {
      cell.scalar = e;
    }
  } else if (!literal && value.empty())
    cell.scalar = {};
  else if (!literal &&
           (value.toUpper() == "TRUE" || value.toUpper() == "FALSE"))
    cell.scalar = Value::logical(value.toUpper() == "TRUE");
  else {
    bool valid = false;
    double n = value.toDouble(&valid);
    cell.scalar =
        !literal && valid && std::isfinite(n) ? numeric(n) : Value(value);
    if (!literal && !valid)
      if (auto calendar = calendar_value(value, impl_->settings))
        cell.scalar = *calendar;
  }
  impl_->cells[{s, r, c}] = std::move(cell);
  impl_->cache.clear();
}
void SheetEngine::recalculate() {
  impl_->cache.clear();
  impl_->visiting.clear();
  impl_->visiting_names.clear();
  for (const auto &[pos, cell] : impl_->cells)
    if (cell.expression)
      impl_->value(pos);
}
void SheetEngine::addNamedRange(const Text &reference, const Text &name) {
  try {
    impl_->names[name.toCaseFolded()] = impl_->parse(reference, 0);
    impl_->cache.clear();
  } catch (const Value &) {
    throw std::runtime_error("Invalid named range");
  }
}
bool SheetEngine::matrix_child(int sheet, int column, int row) const {
  Address position{sheet, row, column};
  auto found = impl_->matrix_owners.find(position);
  return found != impl_->matrix_owners.end() && found->second != position;
}
const std::vector<MatrixBlock> &SheetEngine::matrix_blocks() const {
  return impl_->matrices;
}
void SheetEngine::define_matrix(int s, int c, int r, int columns, int rows) {
  if (s < 0 || s >= count() || c < 1 || r < 1 || columns < 1 || rows < 1 ||
      Int64(c) + columns - 1 > KS_colMax || Int64(r) + rows - 1 > KS_rowMax)
    throw std::runtime_error("Invalid matrix formula extent");
  if (size_t(rows) > 1'000'000 / size_t(columns) ||
      impl_->matrix_owners.size() + size_t(rows) * columns > 1'000'000)
    throw std::runtime_error("Matrix formulas exceed the cell limit");
  if (!input(s, c, r).startsWith('='))
    throw std::runtime_error("Matrix formula master is missing");
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x)
      if (impl_->matrix_owners.contains({s, r + y, c + x}))
        throw std::runtime_error("Overlapping matrix formulas");
  Address master{s, r, c};
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x)
      impl_->matrix_owners[{s, r + y, c + x}] = master;
  impl_->matrices.push_back({s, c, r, columns, rows});
  auto &target = *impl_->sheets.at(s);
  target.row_count = std::max(target.row_count, r + rows - 1);
  target.col_count = std::max(target.col_count, c + columns - 1);
  impl_->cache.clear();
}
} // namespace DomeOffice
