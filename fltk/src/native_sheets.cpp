// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_sheets.h"
#include "native_time.h"
#include <functional>
#include <numeric>
#include <random>
#include <tuple>
namespace DomeOffice {
namespace {
using Address =
    std::tuple<int, int, int>; // sheet, row, column; one-based cells
using Value = NativeValue;
struct Node {
  enum Kind {
    Literal,
    Reference,
    Range,
    Name,
    Unary,
    Binary,
    Function
  } kind = Literal;
  Value literal;
  Text name;
  Address first{}, last{};
  bool explicit_sheet = false;
  unsigned height = 1;
  std::vector<std::shared_ptr<Node>> args;
};
using Expr = std::shared_ptr<Node>;
Expr checked(Expr p) {
  for (const auto &child : p->args)
    p->height = std::max(p->height, child->height + 1);
  if (p->height > 256)
    throw Value::error("#DEPTH!");
  return p;
}
Expr node(Node::Kind kind, const Text &name = {}) {
  auto p = std::make_shared<Node>();
  p->kind = kind;
  p->name = name;
  return p;
}
Value error(const char *s) { return Value::error(s); }
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
std::optional<Value> calendar_value(const Text &input) {
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
    auto serial = (std::chrono::sys_days(day.day) -
                   std::chrono::sys_days(std::chrono::year(1899) / 12 / 30))
                      .count();
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
  if (v.kind == Value::Number)
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
  std::vector<std::unique_ptr<NativeSheet>> sheets;
  std::map<Address, Cell> cells;
  std::map<Text, Expr> names;
  mutable std::map<Address, Value> cache;
  mutable std::set<Address> visiting;
  mutable std::set<Text> visiting_names;
  mutable unsigned eval_depth = 0;
  explicit Impl(SheetEngine *p) : owner(p) {}
  int sheet_index(const Text &name) const {
    for (auto &s : sheets)
      if (s->name.compare(name, CaseInsensitive) == 0)
        return s->index;
    return -1;
  }
  Expr parse(const Text &s, int sheet) {
    return Parser(s, sheet, [&](const Text &name) { return sheet_index(name); })
        .run();
  }
  Value value(Address pos) const {
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
    try {
      if (visiting.size() > 512)
        throw error("#DEPTH!");
      out = evaluate(p->second.expression);
    } catch (const Value &e) {
      out = e;
    } catch (const std::exception &) {
      out = error("#VALUE!");
    }
    visiting.erase(pos);
    cache[pos] = out;
    return out;
  }
  Expr expanded(const Expr &expr) const {
    if (expr->kind != Node::Name)
      return expr;
    auto it = names.find(expr->name);
    if (it == names.end())
      throw error("#NAME?");
    return it->second;
  }
  std::vector<Value> flatten(const Expr &source) const {
    auto expr = expanded(source);
    if (expr->kind != Node::Range)
      return {evaluate(expr)};
    auto [s, r, c] = expr->first;
    auto [s2, r2, c2] = expr->last;
    if (r > r2)
      std::swap(r, r2);
    if (c > c2)
      std::swap(c, c2);
    std::vector<Value> out;
    for (auto it = cells.lower_bound({s, r, 0});
         it != cells.end() && std::get<0>(it->first) == s &&
         std::get<1>(it->first) <= r2;
         ++it)
      if (std::get<2>(it->first) >= c && std::get<2>(it->first) <= c2)
        out.push_back(value(it->first));
    return out;
  }
  Value evaluate(const Expr &e) const;
  Value function(const Expr &e) const;
};
Value SheetEngine::Impl::evaluate(const Expr &e) const try {
  DepthGuard guard(eval_depth);
  switch (e->kind) {
  case Node::Literal:
    return e->literal;
  case Node::Reference:
    return value(e->first);
  case Node::Range:
    return value(e->first);
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
    double n = number(a);
    return numeric(e->name == "-" ? -n : e->name == "%" ? n / 100 : n);
  }
  case Node::Binary: {
    auto a = evaluate(e->args[0]), b = evaluate(e->args[1]);
    if (a.isError())
      return a;
    if (b.isError())
      return b;
    auto op = e->name;
    if (op == "&")
      return Value(concatenate(text(a), text(b)));
    if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" ||
        op == ">=") {
      int cmp;
      if (a.kind == Value::String || b.kind == Value::String)
        cmp = text(a).compare(text(b), CaseInsensitive);
      else {
        double x = number(a), y = number(b);
        cmp = x < y ? -1 : x > y ? 1 : 0;
      }
      return Value::logical(op == "="    ? cmp == 0
                            : op == "<>" ? cmp != 0
                            : op == "<"  ? cmp < 0
                            : op == ">"  ? cmp > 0
                            : op == "<=" ? cmp <= 0
                                         : cmp >= 0);
    }
    double x = number(a), y = number(b);
    if (op == "+")
      return numeric(x + y);
    if (op == "-")
      return numeric(x - y);
    if (op == "*")
      return numeric(x * y);
    if (op == "/")
      return y == 0 ? error("#DIV/0!") : numeric(x / y);
    if (op == "^")
      return numeric(std::pow(x, y));
    return error("#PARSE!");
  }
  case Node::Function:
    return function(e);
  }
  return error("#VALUE!");
} catch (const Value &failure) {
  return failure;
}
Value SheetEngine::Impl::function(const Expr &e) const {
  const auto &name = e->name;
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
    add("PMT PV FV", 3, 5);
    add("SUM PRODUCT MIN MAX COUNT COUNTA CONCAT CONCATENATE SUMSQ", 0, 255);
    add("AVERAGE MEDIAN STDEV STDEV.S STDEVP STDEV.P VAR VAR.S VARP VAR.P AND "
        "OR XOR",
        1, 255);
    add("COUNTBLANK", 1, 1);
    add("NPV", 2, 255);
    return out;
  }();
  auto signature = signatures.find(name);
  if (signature == signatures.end())
    return error("#NAME?");
  if (count < signature->second.first || count > signature->second.second)
    return error("#VALUE!");

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
    return truth(arg(0)) ? arg(1) : count == 3 ? arg(2) : Value::logical(false);
  }
  if (name == "IFERROR" || name == "IFNA") {
    auto v = arg(0);
    return (name == "IFERROR" ? v.isError() : v.isError() && v.string == "#N/A")
               ? arg(1)
               : v;
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
    for (auto &a : e->args)
      for (auto &v : flatten(a)) {
        bool t = truth(v);
        if (name == "AND")
          out = out && t;
        else if (name == "OR")
          out = out || t;
        else
          trues += t;
      }
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
      auto expression = expanded(a);
      auto values = flatten(a);
      if (expression->kind == Node::Range) {
        auto [sh, r, c] = expression->first;
        auto [sh2, r2, c2] = expression->last;
        blanks += size_t(std::abs(r2 - r) + 1) * (std::abs(c2 - c) + 1) -
                  values.size();
      }
      for (auto &v : values) {
        if (v.kind == Value::Empty ||
            (v.kind == Value::String && v.string.empty()))
          ++blanks;
        if (v.kind != Value::Empty)
          ++nonempty;
        if (v.isError()) {
          if (name == "COUNTA" || name == "COUNTBLANK" ||
              (name == "COUNT" && (expression->kind == Node::Range ||
                                   expression->kind == Node::Reference)))
            continue;
          throw v;
        }
        if (v.isNumber())
          numbers.push_back(v.number);
        else if (v.isBoolean() && expression->kind != Node::Range &&
                 expression->kind != Node::Reference)
          numbers.push_back(v.boolean ? 1 : 0);
        else if (v.kind == Value::String && expression->kind != Node::Range &&
                 expression->kind != Node::Reference && name != "COUNTA" &&
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
  if (name == "PMT" || name == "PV" || name == "FV") {
    double rate = n(0), periods = n(1), a = n(2), b = count > 3 ? n(3) : 0,
           type = count > 4 ? n(4) : 0;
    if (type != 0 && type != 1)
      return error("#NUM!");
    double factor = std::pow(1 + rate, periods);
    if (name == "PMT")
      return periods == 0
                 ? error("#DIV/0!")
                 : numeric(rate == 0 ? -(a + b) / periods
                                     : -rate * (a * factor + b) /
                                           ((factor - 1) * (1 + rate * type)));
    if (name == "PV")
      return numeric(rate == 0
                         ? -b - a * periods
                         : -(b + a * (1 + rate * type) * (factor - 1) / rate) /
                               factor);
    return numeric(rate == 0 ? -b - a * periods
                             : -(b * factor +
                                 a * (1 + rate * type) * (factor - 1) / rate));
  }
  if (name == "NPV") {
    double rate = n(0), sum = 0;
    int period = 1;
    for (size_t i = 1; i < count; i++)
      for (auto &v : flatten(e->args[i])) {
        if (v.isError())
          throw v;
        if (v.isNumber())
          sum += v.number / std::pow(1 + rate, period++);
      }
    return numeric(sum);
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
    double serial =
        (date - std::chrono::sys_days(std::chrono::year(1899) / 12 / 30))
            .count();
    return numeric(serial);
  }
  if (name == "YEAR" || name == "MONTH" || name == "DAY") {
    double serial = n(0);
    if (serial < 0 || serial > 2958465)
      return error("#NUM!");
    auto date = std::chrono::year_month_day(
        std::chrono::sys_days(std::chrono::year(1899) / 12 / 30) +
        std::chrono::days(int(serial)));
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
SheetEngine::SheetEngine() : impl_(std::make_unique<Impl>(this)) {}
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
}
Text SheetEngine::input(int s, int c, int r) const {
  auto it = impl_->cells.find({s, r, c});
  return it == impl_->cells.end() ? Text() : it->second.input;
}
NativeValue SheetEngine::value(int s, int c, int r) const {
  return impl_->value({s, r, c});
}
void SheetEngine::set(int s, int c, int r, const Text &value, bool literal) {
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
      if (auto calendar = calendar_value(value))
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
  } catch (const Value &) {
    throw std::runtime_error("Invalid named range");
  }
}
} // namespace DomeOffice
