// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_formula.h"
#include <functional>
namespace DomeOffice::Formula {
namespace {
const std::regex ref(
    R"((?:(('(?:[^']|'')+'|[^\s+*/():,;=<>&!]+)!)?)(\$?[A-Z]{1,3}\$?[1-9][0-9]*)(?::(('(?:[^']|'')+'|[^\s+*/():,;=<>&!]+)!)?(\$?[A-Z]{1,3}\$?[1-9][0-9]*))?)");
Text convert(const Text &formula,
             const std::function<std::string(const std::smatch &)> &fn) {
  auto bytes = formula.toStdString();
  std::string out;
  bool literal = false;
  for (size_t i = 0; i < bytes.size();) {
    char c = bytes[i];
    if (c == '"') {
      out += c;
      ++i;
      if (literal && i < bytes.size() && bytes[i] == '"') {
        out += bytes[i++];
        continue;
      }
      literal = !literal;
      continue;
    }
    std::smatch m;
    std::string tail = bytes.substr(i);
    if (!literal &&
        (i == 0 || !isalnum(static_cast<unsigned char>(bytes[i - 1]))) &&
        std::regex_search(tail, m, ref,
                          std::regex_constants::match_continuous) &&
        !(i + m.length() < bytes.size() && bytes[i + m.length()] == '(')) {
      out += fn(m);
      i += m.length();
    } else {
      out += c;
      ++i;
    }
  }
  return Text(out);
}
std::string unodf(std::string s) {
  if (s.starts_with('$'))
    s.erase(0, 1);
  bool quoted = false;
  size_t dot = std::string::npos;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\'')
      quoted = !quoted;
    else if (s[i] == '.' && !quoted)
      dot = i;
  }
  if (dot != std::string::npos) {
    if (dot == 0)
      s.erase(0, 1);
    else
      s[dot] = '!';
  }
  return s;
}
} // namespace
Text from_excel(const Text &f) { return f.startsWith('=') ? f : Text("=") + f; }
Text decode(const Text &formula) {
  auto b = formula.toStdString();
  auto colon = b.find(":=");
  if (colon != std::string::npos)
    b = b.substr(colon + 1);
  if (!b.starts_with('='))
    b = '=' + b;
  std::string out;
  bool literal = false;
  for (size_t i = 0; i < b.size(); i++) {
    char c = b[i];
    if (c == '"') {
      if (literal && i + 1 < b.size() && b[i + 1] == '"') {
        out += "\"\"";
        ++i;
        continue;
      }
      literal = !literal;
    }
    if (!literal && c == '[') {
      auto end = b.find(']', i);
      if (end == std::string::npos)
        throw std::runtime_error("Unclosed ODF formula reference");
      auto range = b.substr(i + 1, end - i - 1);
      auto sep = range.find(':');
      out += unodf(range.substr(0, sep));
      if (sep != std::string::npos)
        out += ':' + unodf(range.substr(sep + 1));
      i = end;
    } else
      out += (!literal && c == ';') ? ',' : c;
  }
  return Text(out);
}
Text encode(const Text &formula) {
  Text out = convert(formula, [](const std::smatch &m) {
    auto left = m[2].matched ? m[2].str() + "." : ".";
    left += m[3].str();
    if (m[6].matched)
      left += ':' + (m[5].matched ? m[5].str() + "." : ".") + m[6].str();
    return '[' + left + ']';
  });
  bool literal = false;
  for (auto &c : out) {
    if (c == '"')
      literal = !literal;
    else if (c == ',' && !literal)
      c = ';';
  }
  return out;
}
Text save_region(const Text &range) {
  auto encoded = encode(Text("=") + range).mid(1);
  if (encoded.startsWith('[') && encoded.endsWith(']'))
    return encoded.mid(1, int(encoded.size()) - 2);
  return encoded;
}
Text load_region(const Text &range) {
  return decode(Text("=[") + range + "]").mid(1);
}
Text shift(const Text &formula, int columns, int rows) {
  return convert(formula, [&](const std::smatch &m) {
    auto adjust = [&](std::string r) {
      size_t i = 0;
      bool abscol = r[i] == '$';
      if (abscol)
        ++i;
      int col = 0;
      while (i < r.size() && r[i] >= 'A' && r[i] <= 'Z')
        col = col * 26 + r[i++] - 'A' + 1;
      bool absrow = i < r.size() && r[i] == '$';
      if (absrow)
        ++i;
      int row = std::stoi(r.substr(i));
      if (!abscol)
        col += columns;
      if (!absrow)
        row += rows;
      if (col < 1 || col > 16384 || row < 1 || row > 1048576)
        return std::string("#REF!");
      std::string s;
      for (int c = col; c; c = (c - 1) / 26)
        s.insert(s.begin(), 'A' + (c - 1) % 26);
      return (abscol ? "$" : "") + s + (absrow ? "$" : "") +
             std::to_string(row);
    };
    std::string out = m[1].str() + adjust(m[3]);
    if (m[6].matched)
      out += ':' + m[4].str() + adjust(m[6]);
    return out;
  });
}
} // namespace DomeOffice::Formula
