// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_enhanced_geometry.h"
#include <charconv>
#include <numbers>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
double finite(double n) {
  if (!std::isfinite(n))
    fail("Non-finite enhanced geometry formula result");
  return n;
}
struct Node {
  enum Kind {
    Number,
    Constant,
    Modifier,
    Reference,
    Unary,
    Binary,
    Function
  } kind = Number;
  double number = 0;
  std::string name;
  std::vector<size_t> children;
};
using Expression = std::vector<Node>;
struct Parser {
  std::string source;
  size_t at = 0;
  Expression nodes;
  const EnhancedEvaluator &owner;
  Parser(const Text &text, const EnhancedEvaluator &e)
      : source(text.toStdString()), owner(e) {
    if (source.size() > 65536)
      fail("Enhanced formula exceeds the input limit");
  }
  void space() {
    while (at < source.size() &&
           std::isspace(static_cast<unsigned char>(source[at])))
      ++at;
  }
  bool take(char c) {
    space();
    if (at < source.size() && source[at] == c) {
      ++at;
      return true;
    }
    return false;
  }
  size_t append(Node n) {
    if (nodes.size() >= 8192)
      fail("Enhanced formula exceeds the expression limit");
    nodes.push_back(std::move(n));
    return nodes.size() - 1;
  }
  size_t expression(int depth = 0) {
    auto left = product(depth + 1);
    while (true) {
      space();
      if (at == source.size() || (source[at] != '+' && source[at] != '-'))
        return left;
      char op = source[at++];
      auto right = product(depth + 1);
      left = append({Node::Binary, 0, std::string(1, op), {left, right}});
    }
  }
  size_t product(int depth) {
    auto left = unary(depth + 1);
    while (true) {
      space();
      if (at == source.size() || (source[at] != '*' && source[at] != '/'))
        return left;
      char op = source[at++];
      auto right = unary(depth + 1);
      left = append({Node::Binary, 0, std::string(1, op), {left, right}});
    }
  }
  size_t unary(int depth) {
    if (depth > 128)
      fail("Enhanced formula nesting exceeds the limit");
    space();
    if (at < source.size() && (source[at] == '+' || source[at] == '-')) {
      char op = source[at++];
      auto child = unary(depth + 1);
      return append({Node::Unary, 0, std::string(1, op), {child}});
    }
    return atom(depth + 1);
  }
  size_t atom(int depth) {
    space();
    if (at == source.size())
      fail("Incomplete enhanced geometry formula");
    if (take('(')) {
      auto n = expression(depth + 1);
      if (!take(')'))
        fail("Unclosed enhanced geometry formula");
      return n;
    }
    if (source[at] == '?') {
      ++at;
      auto n = owner.reference_length(std::string_view(source).substr(at));
      if (!n)
        fail("Unknown enhanced equation reference");
      auto name = source.substr(at, n);
      at += n;
      return append({Node::Reference, 0, name, {}});
    }
    if (source[at] == '$') {
      ++at;
      size_t start = at;
      while (at < source.size() &&
             std::isdigit(static_cast<unsigned char>(source[at])))
        ++at;
      if (at == start)
        fail("Invalid enhanced modifier reference");
      return append({Node::Modifier, 0, source.substr(start, at - start), {}});
    }
    if (std::isdigit(static_cast<unsigned char>(source[at])) ||
        source[at] == '.') {
      double n;
      auto p =
          std::from_chars(source.data() + at, source.data() + source.size(), n);
      if (p.ec != std::errc() || p.ptr == source.data() + at)
        fail("Invalid enhanced formula number");
      at = p.ptr - source.data();
      return append({Node::Number, finite(n), {}, {}});
    }
    size_t start = at;
    while (at < source.size() &&
           (std::isalpha(static_cast<unsigned char>(source[at])) ||
            std::isdigit(static_cast<unsigned char>(source[at])) ||
            source[at] == '_'))
      ++at;
    if (start == at)
      fail("Invalid enhanced formula token");
    auto name = source.substr(start, at - start);
    if (!take('('))
      return append({Node::Constant, 0, name, {}});
    std::vector<size_t> args;
    if (!take(')'))
      while (true) {
        args.push_back(expression(depth + 1));
        if (take(')'))
          break;
        if (!take(','))
          fail("Invalid enhanced function argument list");
        if (args.size() >= 3)
          fail("Too many enhanced function arguments");
      }
    size_t required = name == "if"                                          ? 3
                      : (name == "min" || name == "max" || name == "atan2") ? 2
                      : (name == "abs" || name == "sqrt" || name == "sin" ||
                         name == "cos" || name == "tan" || name == "atan")
                          ? 1
                          : 0;
    if (!required || args.size() != required)
      fail("Invalid enhanced function or arity");
    return append({Node::Function, 0, name, std::move(args)});
  }
  Expression parse() {
    expression();
    space();
    if (at != source.size())
      fail("Unexpected enhanced formula token");
    return std::move(nodes);
  }
};
} // namespace
struct EnhancedEvaluator::Impl {
  EnhancedContext context;
  mutable std::map<Text, Expression> expressions;
  mutable std::map<Text, double> values;
  mutable std::set<Text> active;
  mutable size_t operations = 0;
  struct RefNode {
    std::map<unsigned char, size_t> edges;
    bool terminal = false;
  };
  std::vector<RefNode> names{1};
  mutable size_t compiled_nodes = 0;
  explicit Impl(EnhancedContext c) : context(std::move(c)) {
    for (double n :
         {context.viewbox.x, context.viewbox.y, context.viewbox.width,
          context.viewbox.height, context.width, context.height})
      finite(n);
    if (context.viewbox.width <= 0 || context.viewbox.height <= 0 ||
        context.equations.size() > 4096 || context.modifiers.size() > 4096)
      fail("Invalid enhanced geometry context");
    for (double n : context.modifiers)
      finite(n);
    size_t name_bytes = 0, formula_bytes = 0;
    for (const auto &[name, formula] : context.equations) {
      if (name.empty() || name.size() > 512 || formula.size() > 65536)
        fail("Invalid enhanced equation declaration");
      auto utf8 = name.toUtf8();
      name_bytes += utf8.size();
      formula_bytes += formula.toUtf8().size();
      if (name_bytes > 1048576 || formula_bytes > 4194304)
        fail("Enhanced equation declarations exceed the limit");
      size_t node = 0;
      for (unsigned char c : utf8) {
        auto found = names[node].edges.find(c);
        size_t next;
        if (found != names[node].edges.end())
          next = found->second;
        else {
          next = names.size();
          names[node].edges[c] = next;
          names.emplace_back();
        }
        node = next;
      }
      names[node].terminal = true;
    }
  }
  double constant(const std::string &name) const {
    const auto &v = context.viewbox;
    if (name == "pi")
      return std::numbers::pi;
    if (name == "left")
      return v.x;
    if (name == "top")
      return v.y;
    if (name == "right")
      return v.x + v.width;
    if (name == "bottom")
      return v.y + v.height;
    if (name == "width")
      return v.width;
    if (name == "height")
      return v.height;
    if (name == "hasfill")
      return context.has_fill ? 1 : 0;
    if (name == "hasstroke")
      return context.has_stroke ? 1 : 0;
    if (name == "logwidth")
      return context.width * 2540 / 72;
    if (name == "logheight")
      return context.height * 2540 / 72;
    if (name == "xstretch")
      return context.xstretch.value_or(0);
    if (name == "ystretch")
      return context.ystretch.value_or(0);
    fail("Unknown enhanced geometry identifier");
  }
  double reference(const Text &name, const EnhancedEvaluator &owner) const {
    if (auto found = values.find(name); found != values.end())
      return found->second;
    auto declaration = context.equations.find(name);
    if (declaration == context.equations.end())
      fail("Unknown enhanced equation reference");
    if (active.size() >= 128 || !active.insert(name).second)
      fail("Cyclic or excessive enhanced equation references");
    struct Erase {
      std::set<Text> &s;
      Text name;
      ~Erase() { s.erase(name); }
    } erase{active, name};
    auto found = expressions.find(name);
    if (found == expressions.end()) {
      auto expression = Parser(declaration->second, owner).parse();
      compiled_nodes += expression.size();
      if (compiled_nodes > 262144)
        fail("Enhanced compiled expressions exceed the limit");
      found = expressions.emplace(name, std::move(expression)).first;
    }
    double result = eval(found->second, found->second.size() - 1, owner, 0);
    values[name] = result;
    return result;
  }
  double eval(const Expression &e, size_t index, const EnhancedEvaluator &owner,
              int depth) const {
    if (++operations > 1000000 || depth > 256)
      fail("Enhanced formula evaluation exceeds the limit");
    const auto &n = e.at(index);
    auto value = [&](size_t i) {
      return eval(e, n.children.at(i), owner, depth + 1);
    };
    double result = 0;
    if (n.kind == Node::Number)
      return n.number;
    if (n.kind == Node::Constant)
      return finite(constant(n.name));
    if (n.kind == Node::Reference)
      return reference(Text::fromUtf8(n.name), owner);
    if (n.kind == Node::Modifier) {
      size_t i = 0;
      auto p = std::from_chars(n.name.data(), n.name.data() + n.name.size(), i);
      if (p.ec != std::errc() || p.ptr != n.name.data() + n.name.size() ||
          i >= context.modifiers.size())
        fail("Enhanced modifier index is out of range");
      return context.modifiers[i];
    }
    if (n.kind == Node::Unary)
      return finite(n.name == "-" ? -value(0) : value(0));
    if (n.kind == Node::Binary) {
      auto a = value(0), b = value(1);
      if (n.name == "+")
        result = a + b;
      else if (n.name == "-")
        result = a - b;
      else if (n.name == "*")
        result = a * b;
      else {
        if (b == 0)
          fail("Division by zero in enhanced geometry");
        result = a / b;
      }
      return finite(result);
    }
    if (n.name == "if")
      return value(0) > 0 ? value(1) : value(2);
    auto a = value(0);
    if (n.name == "abs")
      result = std::abs(a);
    else if (n.name == "sqrt")
      result = std::sqrt(a);
    else if (n.name == "sin")
      result = std::sin(a);
    else if (n.name == "cos")
      result = std::cos(a);
    else if (n.name == "tan")
      result = std::tan(a);
    else if (n.name == "atan")
      result = std::atan(a);
    else {
      auto b = value(1);
      if (n.name == "min")
        result = std::min(a, b);
      else if (n.name == "max")
        result = std::max(a, b);
      else
        result = std::atan2(a, b);
    }
    return finite(result);
  }
};
EnhancedEvaluator::EnhancedEvaluator(EnhancedContext c)
    : impl_(std::make_shared<Impl>(std::move(c))) {}
const EnhancedContext &EnhancedEvaluator::context() const {
  return impl_->context;
}
size_t EnhancedEvaluator::reference_length(std::string_view input) const {
  size_t result = 0, node = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    auto found =
        impl_->names[node].edges.find(static_cast<unsigned char>(input[i]));
    if (found == impl_->names[node].edges.end())
      break;
    node = found->second;
    if (impl_->names[node].terminal)
      result = i + 1;
  }
  return result;
}
double EnhancedEvaluator::evaluate(const Text &formula) const {
  impl_->operations = 0;
  auto e = Parser(formula, *this).parse();
  return impl_->eval(e, e.size() - 1, *this, 0);
}
double EnhancedEvaluator::reference(const Text &name) const {
  impl_->operations = 0;
  return impl_->reference(name, *this);
}
} // namespace DomeOffice
