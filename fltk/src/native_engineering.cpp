// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_engineering.h"
#include "native_engineering_units.h"
#include <complex>
#include <numbers>
namespace DomeOffice {
const NativeSignatures &engineering_signatures() {
  static const NativeSignatures signatures = {
      {"BASE", {1, 3}},        {"BESSELI", {2, 2}},   {"BESSELJ", {2, 2}},
      {"BESSELK", {2, 2}},     {"BESSELY", {2, 2}},   {"BIN2DEC", {1, 1}},
      {"BIN2OCT", {1, 2}},     {"BIN2HEX", {1, 2}},   {"COMPLEX", {2, 3}},
      {"CONVERT", {3, 3}},     {"DEC2HEX", {1, 2}},   {"DEC2BIN", {1, 2}},
      {"DEC2OCT", {1, 2}},     {"DECIMAL", {2, 2}},   {"DELTA", {1, 2}},
      {"ERF", {1, 2}},         {"ERFC", {1, 2}},      {"GESTEP", {1, 2}},
      {"HEX2BIN", {1, 2}},     {"HEX2DEC", {1, 1}},   {"HEX2OCT", {1, 2}},
      {"IMABS", {1, 1}},       {"IMAGINARY", {1, 1}}, {"IMARGUMENT", {1, 1}},
      {"IMCONJUGATE", {1, 1}}, {"IMCOS", {1, 1}},     {"IMCOSH", {1, 1}},
      {"IMCOT", {1, 1}},       {"IMCSC", {1, 1}},     {"IMCSCH", {1, 1}},
      {"IMDIV", {2, 2}},       {"IMEXP", {1, 1}},     {"IMLN", {1, 1}},
      {"IMLOG2", {1, 1}},      {"IMLOG10", {1, 1}},   {"IMPOWER", {2, 2}},
      {"IMPRODUCT", {1, 255}}, {"IMREAL", {1, 1}},    {"IMSEC", {1, 1}},
      {"IMSECH", {1, 1}},      {"IMSIN", {1, 1}},     {"IMSINH", {1, 1}},
      {"IMSQRT", {1, 1}},      {"IMSUB", {2, 2}},     {"IMSUM", {1, 255}},
      {"IMTAN", {1, 1}},       {"IMTANH", {1, 1}},    {"OCT2BIN", {1, 2}},
      {"OCT2DEC", {1, 1}},     {"OCT2HEX", {1, 2}}};
  return signatures;
}
Text engineering_name(const Text &input) {
  static const std::map<Text, Text> aliases = {
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBESSELI", "BESSELI"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBESSELJ", "BESSELJ"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBESSELK", "BESSELK"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBESSELY", "BESSELY"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBIN2DEC", "BIN2DEC"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBIN2OCT", "BIN2OCT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETBIN2HEX", "BIN2HEX"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETCOMPLEX", "COMPLEX"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETCONVERT", "CONVERT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETDEC2HEX", "DEC2HEX"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETDEC2BIN", "DEC2BIN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETDEC2OCT", "DEC2OCT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETDELTA", "DELTA"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETERF", "ERF"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETERFC", "ERFC"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETGESTEP", "GESTEP"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETHEX2BIN", "HEX2BIN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETHEX2DEC", "HEX2DEC"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETHEX2OCT", "HEX2OCT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMABS", "IMABS"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMAGINARY", "IMAGINARY"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMARGUMENT", "IMARGUMENT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMCONJUGATE", "IMCONJUGATE"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMCOS", "IMCOS"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMDIV", "IMDIV"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMEXP", "IMEXP"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMLN", "IMLN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMLOG2", "IMLOG2"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMLOG10", "IMLOG10"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMPOWER", "IMPOWER"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMPRODUCT", "IMPRODUCT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMREAL", "IMREAL"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMSIN", "IMSIN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMSQRT", "IMSQRT"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMSUB", "IMSUB"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIMSUM", "IMSUM"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETOCT2BIN", "OCT2BIN"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETOCT2DEC", "OCT2DEC"},
      {"COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETOCT2HEX", "OCT2HEX"}};
  if (auto it = aliases.find(input); it != aliases.end())
    return it->second;
  Text name = input;
  if (name.startsWith("_XLFN."))
    name = name.mid(6);
  if (name.startsWith("COM.MICROSOFT."))
    name = name.mid(14);
  return engineering_signatures().contains(name) ? name : input;
}
namespace {
using V = NativeValue;
using C = std::complex<double>;
[[noreturn]] void fail(const char *code) { throw V::error(code); }
double real(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber())
    return v.number;
  if (v.kind == V::Empty)
    return 0;
  if (v.kind == V::Boolean)
    return v.boolean ? 1 : 0;
  bool ok = false;
  double value = v.string.toDouble(&ok);
  if (!ok)
    fail("#VALUE!");
  if (!std::isfinite(value))
    fail("#NUM!");
  return value;
}
Text string(const V &v) {
  if (v.isError())
    throw v;
  return v.display();
}
long long integer(const V &v, long long low, long long high) {
  double n = std::trunc(real(v));
  if (n < double(low) || n > double(high))
    fail("#NUM!");
  return static_cast<long long>(n);
}
struct Complex {
  C value;
  char suffix = 0;
};
Complex complex(const V &v) {
  if (v.isError())
    throw v;
  if (v.isComplex())
    return {{v.number, v.imaginary}, v.complex_suffix};
  if (v.kind != V::String)
    return {{real(v), 0}, 0};
  auto input = v.string.toStdString();
  input.erase(std::remove_if(input.begin(), input.end(),
                             [](char c) {
                               return c == ' ' || c == '\t' || c == '\n' ||
                                      c == '\r';
                             }),
              input.end());
  if (input.size() > 1000000)
    throw V::error("#LIMIT!", true);
  auto parse = [](std::string_view value) {
    if (value.empty())
      fail("#NUM!");
    if (value.front() == '+')
      value.remove_prefix(1);
    double n = 0;
    auto end = std::from_chars(value.data(), value.data() + value.size(), n);
    if (end.ec != std::errc() || end.ptr != value.data() + value.size() ||
        !std::isfinite(n))
      fail("#NUM!");
    return n;
  };
  if (input.empty())
    fail("#NUM!");
  char suffix = input.back();
  if (suffix != 'i' && suffix != 'j')
    return {{parse(input), 0}, 0};
  input.pop_back();
  size_t split = std::string::npos;
  for (size_t i = 1; i < input.size(); ++i)
    if ((input[i] == '+' || input[i] == '-') && input[i - 1] != 'e' &&
        input[i - 1] != 'E')
      split = i;
  auto imag = [](std::string_view text, auto &parse) {
    return text.empty() || text == "+" ? 1 : text == "-" ? -1 : parse(text);
  };
  double a = 0, b = 0;
  if (split == std::string::npos)
    b = imag(input, parse);
  else {
    a = parse(std::string_view(input).substr(0, split));
    b = imag(std::string_view(input).substr(split), parse);
  }
  return {{a, b}, suffix};
}
V result(C value, char suffix = 0) {
  return V::complex(value.real(), value.imag(), suffix ? suffix : 'i');
}
char suffix(char a, char b) { return !a ? b : !b || a == b ? a : 'i'; }
unsigned digit(char32_t c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 10;
  return 99;
}
Text base(uint64_t value, unsigned radix, size_t minimum = 0) {
  Text out;
  do {
    unsigned n = value % radix;
    out = Text(1, char32_t(n < 10 ? '0' + n : 'A' + n - 10)) + out;
    value /= radix;
  } while (value);
  if (minimum > 1000000)
    throw V::error("#LIMIT!", true);
  return Text(minimum > out.size() ? minimum - out.size() : 0, U'0') + out;
}
uint64_t parse_base(const Text &text, unsigned radix) {
  if (text.empty())
    fail("#NUM!");
  uint64_t value = 0;
  for (auto c : text) {
    unsigned d = digit(c);
    if (d >= radix || value > (uint64_t(9007199254740991ULL) - d) / radix)
      fail("#NUM!");
    value = value * radix + d;
  }
  return value;
}
Text digits(const V &v) {
  if (v.kind == V::String)
    return v.string;
  double n = real(v);
  if (n < 0 || std::trunc(n) != n || n > 9007199254740991.)
    fail("#NUM!");
  return base(static_cast<uint64_t>(n), 10);
}
V radix_function(const Text &name, const std::vector<V> &args) {
  unsigned from = name.startsWith("BIN")   ? 2
                  : name.startsWith("OCT") ? 8
                  : name.startsWith("HEX") ? 16
                                           : 10;
  auto destination = name.mid(4);
  unsigned to = destination == "BIN"   ? 2
                : destination == "OCT" ? 8
                : destination == "HEX" ? 16
                                       : 10;
  int64_t number = 0;
  if (from == 10)
    number = integer(args[0], -(1LL << 39), (1LL << 39) - 1);
  else {
    auto text = digits(args[0]);
    if (text.size() > 10)
      fail("#NUM!");
    auto raw = parse_base(text, from);
    unsigned bits = from == 2 ? 10 : from == 8 ? 30 : 40;
    number = raw >= (1ULL << (bits - 1)) ? int64_t(raw) - int64_t(1ULL << bits)
                                         : int64_t(raw);
  }
  if (to == 10)
    return V::numeric(number);
  unsigned bits = to == 2 ? 10 : to == 8 ? 30 : 40;
  if (number < -(1LL << (bits - 1)) || number > (1LL << (bits - 1)) - 1)
    fail("#NUM!");
  if (number < 0)
    return V(base(uint64_t((1LL << bits) + number), to, 10));
  size_t places = args.size() > 1 ? size_t(integer(args[1], 1, 10)) : 0;
  auto out = base(number, to, places);
  if (places && out.size() > places)
    fail("#NUM!");
  return V(out);
}
double erf_interval(double a, double b) {
  if (a >= 0 && b >= 0)
    return std::erfc(a) - std::erfc(b);
  if (a <= 0 && b <= 0)
    return std::erfc(-b) - std::erfc(-a);
  return std::erf(b) - std::erf(a);
}
} // namespace
namespace {
struct Unit {
  double inverse = 0, prefix = 1;
};
Unit unit(const Text &name, const EngineeringUnits &group) {
  if (auto it = group.inverse.find(name); it != group.inverse.end())
    return {it->second, 1};
  static const std::vector<std::pair<Text, double>> prefixes = {
      {"da", 1e1},    {"ki", 0x1p10}, {"Ki", 0x1p10}, {"Mi", 0x1p20},
      {"Gi", 0x1p30}, {"Ti", 0x1p40}, {"Pi", 0x1p50}, {"Ei", 0x1p60},
      {"Zi", 0x1p70}, {"Yi", 0x1p80}, {"Q", 1e30},    {"R", 1e27},
      {"Y", 1e24},    {"Z", 1e21},    {"E", 1e18},    {"P", 1e15},
      {"T", 1e12},    {"G", 1e9},     {"M", 1e6},     {"k", 1e3},
      {"h", 1e2},     {"e", 1e1},     {"d", 1e-1},    {"c", 1e-2},
      {"m", 1e-3},    {"u", 1e-6},    {"µ", 1e-6},    {"μ", 1e-6},
      {"n", 1e-9},    {"p", 1e-12},   {"f", 1e-15},   {"a", 1e-18},
      {"z", 1e-21},   {"y", 1e-24},   {"r", 1e-27},   {"q", 1e-30}};
  // ODF permits decimal prefixes only on the marked metric symbols and
  // binary prefixes only on information units. In particular, "min" must
  // never become milli-inches when paired with a length unit.
  static const std::set<Text> metric = {
      "ang",   "ly",    "lightyear", "m",    "parsec", "pc",  "c",   "cal",
      "e",     "eV",    "ev",        "J",    "Wh",     "wh",  "dyn", "dy",
      "N",     "pond",  "bit",       "byte", "ga",     "T",   "g",   "u",
      "W",     "w",     "at",        "atm",  "mmHg",   "Pa",  "p",   "P",
      "SI_at", "m/h",   "m/hr",      "m/s",  "m/sec",  "sec", "s",   "yr",
      "l",     "L",     "lt",        "ang2", "ang^2",  "ar",  "m2",  "m^2",
      "ang3",  "ang^3", "m3",        "m^3",  "K",      "kel"};
  for (const auto &[prefix, factor] : prefixes)
    if (name.startsWith(prefix)) {
      auto stem = name.mid(prefix.size());
      auto it = group.inverse.find(stem);
      if (it == group.inverse.end() || !metric.contains(stem) ||
          (prefix.endsWith('i') && group.dimension != "info"))
        continue;
      int exponent = (group.dimension == "area" && (stem.endsWith('2')))   ? 2
                     : (group.dimension == "volume" && stem.endsWith('3')) ? 3
                                                                           : 1;
      return {it->second, std::pow(factor, exponent)};
    }
  return {};
}
V convert(const std::vector<V> &args) {
  double value = real(args[0]);
  auto from = string(args[1]), to = string(args[2]);
  // Each pair is the Celsius scale and the reading at Celsius zero.
  // A Celsius anchor avoids cancellation in common C/F conversions.
  static const std::map<Text, std::pair<long double, long double>>
      temperatures = {{"C", {1, 0}},
                      {"cel", {1, 0}},
                      {"F", {5.L / 9, 32}},
                      {"fah", {5.L / 9, 32}},
                      {"K", {1, 273.15L}},
                      {"kel", {1, 273.15L}},
                      {"Rank", {5.L / 9, 491.67L}},
                      {"Reau", {1.25L, 0}}};
  auto temperature = [&](const Text &name) {
    if (auto it = temperatures.find(name); it != temperatures.end())
      return it->second;
    // Kelvin is the only temperature scale permitting SI prefixes.
    EngineeringUnits kelvin{"temperature", {{"K", 1}, {"kel", 1}}};
    auto k = unit(name, kelvin);
    return k.inverse ? std::pair<long double, long double>{k.prefix,
                                                           273.15L / k.prefix}
                     : std::pair<long double, long double>{0, 0};
  };
  auto a = temperature(from), b = temperature(to);
  if (a.first && b.first)
    return V::numeric(double((static_cast<long double>(value) - a.second) *
                                 a.first / b.first +
                             b.second));
  // Exact symbols win globally over a possible prefix interpretation.
  auto exact = [&](const Text &name) {
    for (const auto &group : engineering_units())
      if (group.inverse.contains(name))
        return group.dimension;
    return Text();
  };
  auto from_dimension = exact(from), to_dimension = exact(to);
  for (const auto &group : engineering_units()) {
    if ((!from_dimension.empty() && from_dimension != group.dimension) ||
        (!to_dimension.empty() && to_dimension != group.dimension))
      continue;
    auto x = unit(from, group), y = unit(to, group);
    if (x.inverse && y.inverse)
      return V::numeric(
          double(static_cast<long double>(value) * x.prefix * y.inverse /
                 (static_cast<long double>(x.inverse) * y.prefix)));
  }
  return V::error("#N/A");
}
} // namespace
static NativeValue engineering_impl(const Text &name,
                                    const std::vector<NativeValue> &args) try {
  auto signature = engineering_signatures().find(name);
  if (signature == engineering_signatures().end())
    return V::error("#NAME?", true);
  if (args.size() < signature->second.first ||
      args.size() > signature->second.second)
    return V::error("#VALUE!");
  for (const auto &arg : args)
    if (arg.isError())
      return arg;
  if (name == "CONVERT")
    return convert(args);
  if (name == "BASE") {
    double value = real(args[0]);
    if (value < 0 || value > 9007199254740991.)
      fail("#NUM!");
    unsigned radix = args.size() > 1 ? integer(args[1], 2, 36) : 10;
    size_t places = args.size() > 2 ? size_t(integer(args[2], 0, 1000000)) : 0;
    return V(base(uint64_t(std::trunc(value)), radix, places));
  }
  if (name == "DECIMAL") {
    auto value = string(args[0]);
    unsigned radix = integer(args[1], 2, 36);
    value = value.trimmed();
    if (radix == 16) {
      if (value.startsWith("0x") || value.startsWith("0X"))
        value = value.mid(2);
      else if (value.startsWith('x') || value.startsWith('X'))
        value = value.mid(1);
      if (value.endsWith('h') || value.endsWith('H'))
        value = value.left(value.size() - 1);
    }
    if (radix == 2 && (value.endsWith('b') || value.endsWith('B')))
      value = value.left(value.size() - 1);
    return V::numeric(parse_base(value, radix));
  }
  if (name.contains('2') && (name.startsWith("BIN") || name.startsWith("OCT") ||
                             name.startsWith("HEX") || name.startsWith("DEC")))
    return radix_function(name, args);
  if (name == "DELTA" || name == "GESTEP") {
    if (name == "GESTEP")
      for (const auto &arg : args)
        if (!arg.isNumber())
          fail("#NUM!");
    double a = real(args[0]), b = args.size() > 1 ? real(args[1]) : 0;
    double tolerance = 1e-14 * std::max({1., std::abs(a), std::abs(b)});
    return V::numeric(name == "DELTA"
                          ? std::abs(a - b) <= tolerance
                          : (a > b || std::abs(a - b) <= tolerance));
  }
  if (name == "ERF" || name == "ERFC") {
    double a = real(args[0]);
    if (args.size() == 1)
      return V::numeric(name == "ERF" ? std::erf(a) : std::erfc(a));
    double result = erf_interval(a, real(args[1]));
    return V::numeric(name == "ERF" ? result : -result);
  }
  if (name.startsWith("BESSEL")) {
    double x = real(args[0]), n = real(args[1]);
    if (n < 0)
      fail("#NUM!");
    n = std::trunc(n);
    if (n > 1000000)
      throw V::error("#LIMIT!", true);
    double answer = 0;
    if (name == "BESSELK" || name == "BESSELY") {
      if (x <= 0)
        fail("#NUM!");
      answer =
          name == "BESSELK" ? std::cyl_bessel_k(n, x) : std::cyl_neumann(n, x);
    } else {
      if (x == 0)
        answer = n == 0 ? 1 : 0;
      else
        answer = name == "BESSELI" ? std::cyl_bessel_i(n, std::abs(x))
                                   : std::cyl_bessel_j(n, std::abs(x));
      if (x < 0 && std::fmod(n, 2) == 1)
        answer = -answer;
    }
    return V::numeric(answer);
  }
  if (name == "COMPLEX") {
    char suffix = 'i';
    if (args.size() > 2) {
      auto unit = string(args[2]);
      if (unit != "i" && unit != "j")
        fail("#VALUE!");
      suffix = char(unit[0]);
    }
    return V::complex(real(args[0]), real(args[1]), suffix);
  }
  if (name == "IMSUM" || name == "IMPRODUCT") {
    C out = name == "IMSUM" ? C(0) : C(1);
    char unit = 0;
    size_t count = 0;
    auto append = [&](const V &item) {
      if (++count > 1000000)
        throw V::error("#LIMIT!", true);
      if (item.kind == V::Empty)
        return;
      if (item.isError())
        throw item;
      Complex input;
      try {
        input = complex(item);
      } catch (const V &error) {
        if (name == "IMSUM" && item.kind == V::String && !error.incomplete)
          return;
        throw;
      }
      unit = suffix(unit, input.suffix);
      out = name == "IMSUM" ? out + input.value : out * input.value;
    };
    for (const auto &arg : args) {
      if (arg.isArray())
        for (const auto &value : *arg.items)
          append(value);
      else
        append(arg);
    }
    return result(out, unit);
  }
  auto a = complex(args[0]);
  C z = a.value;
  char unit = a.suffix;
  if (name == "IMREAL")
    return V::numeric(z.real());
  if (name == "IMAGINARY")
    return V::numeric(z.imag());
  if (name == "IMABS")
    return V::numeric(std::abs(z));
  if (name == "IMARGUMENT")
    return V::numeric(std::arg(z));
  if (name == "IMCONJUGATE")
    return result(std::conj(z), unit);
  if (name == "IMSUB" || name == "IMDIV" || name == "IMPOWER") {
    auto b = complex(args[1]);
    unit = suffix(unit, b.suffix);
    if (name == "IMSUB")
      z -= b.value;
    else if (name == "IMDIV") {
      if (b.value == C(0))
        fail("#DIV/0!");
      z /= b.value;
    } else {
      if (z == C(0))
        fail("#NUM!");
      z = std::pow(z, b.value);
    }
  } else if (name == "IMCOS")
    z = std::cos(z);
  else if (name == "IMCOSH")
    z = std::cosh(z);
  else if (name == "IMSIN")
    z = std::sin(z);
  else if (name == "IMSINH")
    z = std::sinh(z);
  else if (name == "IMTAN")
    z = std::tan(z);
  else if (name == "IMTANH")
    z = std::tanh(z);
  else if (name == "IMEXP")
    z = std::exp(z);
  else if (name == "IMSQRT")
    z = std::sqrt(z);
  else if (name == "IMLN" || name == "IMLOG2" || name == "IMLOG10") {
    if (z == C(0))
      fail("#NUM!");
    z = std::log(z);
    if (name == "IMLOG2")
      z /= std::numbers::ln2;
    if (name == "IMLOG10")
      z /= std::numbers::ln10;
  } else {
    C divisor;
    if (name == "IMCSC")
      divisor = std::sin(z);
    else if (name == "IMCSCH")
      divisor = std::sinh(z);
    else if (name == "IMSEC")
      divisor = std::cos(z);
    else if (name == "IMSECH")
      divisor = std::cosh(z);
    else if (name == "IMCOT")
      divisor = std::tan(z);
    else
      return V::error("#NAME?", true);
    if (divisor == C(0))
      fail("#DIV/0!");
    z = 1. / divisor;
  }
  return result(z, unit);
} catch (const NativeValue &failure) {
  return failure;
} catch (const std::domain_error &) {
  return NativeValue::error("#NUM!");
} catch (const std::overflow_error &) {
  return NativeValue::error("#NUM!");
} catch (const std::runtime_error &) {
  // Special-function library non-convergence is a cell error, never a crash.
  return NativeValue::error("#NUM!");
}
NativeValue engineering_function(const Text &name,
                                 const std::vector<NativeValue> &args) {
  auto out = engineering_impl(engineering_name(name.toUpper()), args);
  for (const auto &arg : args)
    out.incomplete = out.incomplete || arg.incomplete;
  return out;
}
} // namespace DomeOffice
