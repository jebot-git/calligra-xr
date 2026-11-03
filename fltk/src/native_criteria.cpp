// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_criteria.h"
#include <unicode/regex.h>
namespace DomeOffice {
namespace {
using V = NativeValue;
enum Compare { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
bool compare(Compare op, int sign) {
  switch (op) {
  case Equal:
    return sign == 0;
  case NotEqual:
    return sign != 0;
  case Less:
    return sign < 0;
  case LessEqual:
    return sign <= 0;
  case Greater:
    return sign > 0;
  case GreaterEqual:
    return sign >= 0;
  }
  return false;
}
icu::UnicodeString unicode(const Text &text) {
  auto bytes = text.toStdString();
  return icu::UnicodeString::fromUTF8(icu::StringPiece(bytes));
}
Text wildcard_pattern(const Text &input) {
  Text pattern;
  auto literal = [&](char32_t c) {
    if (Text("\\.^$|()[]{}*+?").contains(c))
      pattern += '\\';
    pattern += c;
  };
  for (size_t i = 0; i < input.size(); ++i) {
    auto c = input[i];
    if (c == '~' && i + 1 < input.size() &&
        (input[i + 1] == '~' || input[i + 1] == '*' || input[i + 1] == '?'))
      literal(input[++i]);
    else if (c == '*')
      pattern += ".*";
    else if (c == '?')
      pattern += '.';
    else
      literal(c);
  }
  return pattern;
}
} // namespace
void CriteriaBudget::charge(uint64_t amount) {
  if (amount > remaining) {
    remaining = 0;
    throw V::error("#LIMIT!", true);
  }
  remaining -= amount;
}
const NativeSignatures &conditional_signatures() {
  static const NativeSignatures names = {
      {"COUNTIF", {2, 2}},  {"COUNTIFS", {2, 254}}, {"SUMIF", {2, 3}},
      {"SUMIFS", {3, 255}}, {"AVERAGEIF", {2, 3}},  {"AVERAGEIFS", {3, 255}}};
  return names;
}
bool conditional_function(const Text &name) {
  return conditional_signatures().contains(name);
}
Text conditional_name(const Text &name) {
  auto base = name;
  if (base.startsWith("_XLFN."))
    base = base.mid(6);
  if (base.startsWith("COM.MICROSOFT."))
    base = base.mid(14);
  return conditional_function(base) ? base : name;
}
struct NativeCriterion::Impl {
  Compare op = Equal;
  bool numeric = false, empty = false, text_criterion = false;
  double number = 0;
  Text operand;
  NativeCriteriaSettings settings;
  CriteriaBudget &budget;
  std::unique_ptr<icu::UnicodeString> input;
  std::unique_ptr<icu::RegexPattern> pattern;
  std::unique_ptr<icu::RegexMatcher> matcher;
  int32_t previous_steps = 0;
  explicit Impl(CriteriaBudget &budget) : budget(budget) {}
  static UBool U_CALLCONV step(const void *context, int32_t steps) {
    auto &self = *const_cast<Impl *>(static_cast<const Impl *>(context));
    uint64_t cost = uint64_t(std::max(0, steps - self.previous_steps)) * 10'000;
    self.previous_steps = steps;
    if (cost > self.budget.remaining) {
      self.budget.remaining = 0;
      return false;
    }
    self.budget.remaining -= cost;
    return true;
  }
};
NativeCriterion::NativeCriterion(const V &value,
                                 const NativeCriteriaSettings &settings,
                                 CriteriaBudget &budget,
                                 std::chrono::sys_days epoch)
    : impl_(std::make_unique<Impl>(budget)) {
  auto &p = *impl_;
  p.settings = settings;
  budget.charge(1);
  if (value.isError())
    throw value;
  if (value.isArray())
    throw V::error("#VALUE!");
  if (value.kind != V::String) {
    p.numeric = true;
    p.number = value.isBoolean() ? (value.boolean ? 1 : 0) : value.number;
    return;
  }
  p.operand = value.string;
  p.text_criterion = true;
  if (p.operand.size() > 8192)
    throw V::error("#LIMIT!", true);
  for (const auto &[token, op] :
       std::vector<std::pair<Text, Compare>>{{"<=", LessEqual},
                                             {">=", GreaterEqual},
                                             {"<>", NotEqual},
                                             {"=", Equal},
                                             {"<", Less},
                                             {">", Greater}}) {
    if (p.operand.startsWith(token)) {
      p.op = op;
      p.operand = p.operand.mid(token.size());
      break;
    }
  }
  p.empty = p.operand.empty();
  if (!p.empty) {
    bool valid = false;
    p.number = p.operand.toDouble(&valid);
    p.numeric = valid && std::isfinite(p.number);
    // Deterministic ISO and US English civil-date criteria, using the same
    // workbook epoch as date cells. General locale parsing remains separate.
    auto parts = p.operand.split(p.operand.contains('/') ? '/' : '-');
    if (!p.numeric && parts.size() == 3) {
      bool y_ok = false, m_ok = false, d_ok = false;
      bool iso = parts[0].size() == 4 && p.operand.contains('-');
      int year = parts[iso ? 0 : 2].toInt(&y_ok);
      int month = parts[iso ? 1 : 0].toInt(&m_ok);
      int day = parts[iso ? 2 : 1].toInt(&d_ok);
      auto date = std::chrono::year(year) / std::chrono::month(month) /
                  std::chrono::day(day);
      if (y_ok && m_ok && d_ok && year >= 1 && year <= 9999 && date.ok()) {
        p.number = (std::chrono::sys_days(date) - epoch).count();
        p.numeric = true;
      }
    }
  }
  if (!p.numeric && !p.empty && (p.op == Equal || p.op == NotEqual) &&
      (settings.wildcards || settings.regular_expressions)) {
    Text expression =
        settings.wildcards ? wildcard_pattern(p.operand) : p.operand;
    budget.charge(expression.size());
    UErrorCode status = U_ZERO_ERROR;
    uint32_t flags = settings.case_sensitive ? 0 : UREGEX_CASE_INSENSITIVE;
    if (settings.wildcards)
      flags |= UREGEX_DOTALL;
    p.pattern.reset(
        icu::RegexPattern::compile(unicode(expression), flags, status));
    if (status == U_MEMORY_ALLOCATION_ERROR ||
        status == U_REGEX_STACK_OVERFLOW || status == U_REGEX_PATTERN_TOO_BIG ||
        status == U_REGEX_NUMBER_TOO_BIG)
      throw V::error("#LIMIT!", true);
    if (status == U_REGEX_UNIMPLEMENTED || status == U_INTERNAL_PROGRAM_ERROR)
      throw V::error("#VALUE!", true);
    if (U_FAILURE(status)) {
      // Malformed regular expressions are literal criteria, as in Calc.
      p.pattern.reset();
      return;
    }
    p.matcher.reset(p.pattern->matcher(status));
    if (U_FAILURE(status) || !p.matcher)
      throw V::error("#LIMIT!", true);
    p.matcher->setStackLimit(256 * 1024, status);
    p.matcher->setMatchCallback(Impl::step, &p, status);
    if (U_FAILURE(status))
      throw V::error("#LIMIT!", true);
  }
}
NativeCriterion::~NativeCriterion() = default;
NativeCriterion::NativeCriterion(NativeCriterion &&) noexcept = default;
NativeCriterion &
NativeCriterion::operator=(NativeCriterion &&) noexcept = default;
bool NativeCriterion::matches(const V &value) const {
  auto &p = *impl_;
  p.budget.charge(1);
  if (value.isError()) {
    bool same = p.text_criterion && p.operand == value.string;
    return p.op == Equal ? same : p.op == NotEqual && !same;
  }
  if (value.kind == V::Empty) {
    if (p.numeric)
      return p.op == NotEqual;
    return p.op == Equal ? p.empty : p.op == NotEqual && !p.empty;
  }
  if (p.numeric) {
    double number = value.isBoolean() ? (value.boolean ? 1 : 0) : value.number;
    if (value.kind == V::String) {
      p.budget.charge(value.string.size());
      if (!p.text_criterion || (p.op != Equal && p.op != NotEqual))
        return p.op == NotEqual;
      if (p.text_criterion && !p.settings.whole_cell &&
          (p.op == Equal || p.op == NotEqual)) {
        bool found = value.string.contains(p.operand, p.settings.case_sensitive
                                                          ? CaseSensitive
                                                          : CaseInsensitive);
        return p.op == Equal ? found : !found;
      }
      bool valid = false;
      number = value.string.toDouble(&valid);
      if (!valid || !std::isfinite(number))
        return p.op == NotEqual;
    } else if (!value.isNumber() && !value.isBoolean())
      return p.op == NotEqual;
    return compare(p.op, number < p.number ? -1 : number > p.number ? 1 : 0);
  }
  auto text = value.kind == V::Number && value.format != V::General
                  ? Text::number(value.number, 'g', 15)
                  : value.display();
  p.budget.charge(text.size() + p.operand.size());
  if (p.matcher) {
    auto input = std::make_unique<icu::UnicodeString>(unicode(text));
    p.matcher->reset(*input);
    p.input = std::move(input);
    p.previous_steps = 0;
    UErrorCode status = U_ZERO_ERROR;
    bool found = p.settings.whole_cell ? p.matcher->matches(status)
                                       : p.matcher->find(status);
    if (U_FAILURE(status))
      throw V::error("#LIMIT!", true);
    return p.op == NotEqual ? !found : found;
  }
  if (p.op == Equal || p.op == NotEqual) {
    if (!p.settings.case_sensitive) {
      text = text.toCaseFolded();
      auto operand = p.operand.toCaseFolded();
      bool found =
          p.settings.whole_cell ? text == operand : text.contains(operand);
      return p.op == NotEqual ? !found : found;
    }
    bool found =
        p.settings.whole_cell ? text == p.operand : text.contains(p.operand);
    return p.op == NotEqual ? !found : found;
  }
  return compare(p.op, text.compare(p.operand, p.settings.case_sensitive
                                                   ? CaseSensitive
                                                   : CaseInsensitive));
}
} // namespace DomeOffice
