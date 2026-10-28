// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_formula.h"
#include "native_limits.h"
#include <functional>
namespace DomeOffice::Formula {
namespace {
const std::regex ref(
    R"((?:(('(?:[^']|'')+'|[^\s+*/():,;=<>&!]+)!)?)(\$?[A-Z]{1,4}\$?[1-9][0-9]*)(?::(('(?:[^']|'')+'|[^\s+*/():,;=<>&!]+)!)?(\$?[A-Z]{1,4}\$?[1-9][0-9]*))?)",
    std::regex::icase);
bool valid_reference(std::string_view value) {
  size_t i = 0;
  int column = 0, row = 0;
  if (value[i] == '$')
    ++i;
  while (i < value.size()) {
    char c = value[i];
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c < 'A' || c > 'Z')
      break;
    if (column > KS_colMax / 26)
      return false;
    column = column * 26 + c - 'A' + 1;
    ++i;
  }
  if (i < value.size() && value[i] == '$')
    ++i;
  if (i == value.size())
    return false;
  for (; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9' || row > KS_rowMax / 10)
      return false;
    row = row * 10 + value[i] - '0';
  }
  return column >= 1 && column <= KS_colMax && row >= 1 && row <= KS_rowMax;
}
size_t unquoted_char(const std::string &text, char token, size_t begin = 0) {
  bool quoted = false;
  for (size_t i = begin; i < text.size(); ++i) {
    if (text[i] == '\'') {
      if (quoted && i + 1 < text.size() && text[i + 1] == '\'') {
        ++i;
        continue;
      }
      quoted = !quoted;
    } else if (!quoted && text[i] == token)
      return i;
  }
  return std::string::npos;
}
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
    if (!literal &&
        (i == 0 || !isalnum(static_cast<unsigned char>(bytes[i - 1]))) &&
        std::regex_search(bytes.cbegin() + i, bytes.cend(), m, ref,
                          std::regex_constants::match_continuous) &&
        valid_reference(m[3].str()) &&
        (!m[6].matched || valid_reference(m[6].str())) &&
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
  if (!b.starts_with('=') && colon != std::string::npos)
    b = b.substr(colon + 1);
  if (!b.starts_with('='))
    b = '=' + b;
  std::string out;
  bool literal = false;
  bool sheet_quote = false;
  int array_depth = 0;
  for (size_t i = 0; i < b.size(); i++) {
    char c = b[i];
    if (c == '"' && !sheet_quote) {
      if (literal && i + 1 < b.size() && b[i + 1] == '"') {
        out += "\"\"";
        ++i;
        continue;
      }
      literal = !literal;
    }
    if (!literal && c == '\'')
      sheet_quote = !sheet_quote;
    if (!literal && !sheet_quote && c == '[') {
      auto end = unquoted_char(b, ']', i + 1);
      if (end == std::string::npos)
        throw std::runtime_error("Unclosed ODF formula reference");
      auto range = b.substr(i + 1, end - i - 1);
      auto sep = unquoted_char(range, ':');
      out += unodf(range.substr(0, sep));
      if (sep != std::string::npos)
        out += ':' + unodf(range.substr(sep + 1));
      i = end;
    } else {
      if (!literal && !sheet_quote && c == '{')
        ++array_depth;
      if (!literal && !sheet_quote && c == '}')
        --array_depth;
      out += !literal && !sheet_quote && c == ';'                      ? ','
             : !literal && !sheet_quote && c == '|' && array_depth > 0 ? ';'
                                                                       : c;
    }
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
  bool sheet_quote = false;
  int array_depth = 0, reference_depth = 0;
  for (auto &c : out) {
    if (c == '"' && !sheet_quote)
      literal = !literal;
    else if (!literal) {
      if (c == '\'')
        sheet_quote = !sheet_quote;
      if (sheet_quote)
        continue;
      if (c == '[')
        ++reference_depth;
      if (c == ']')
        --reference_depth;
      if (reference_depth)
        continue;
      if (c == '{')
        ++array_depth;
      if (c == '}')
        --array_depth;
      if (c == ',')
        c = ';';
      else if (c == ';' && array_depth > 0)
        c = '|';
    }
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
      while (i < r.size() &&
             ((r[i] >= 'A' && r[i] <= 'Z') || (r[i] >= 'a' && r[i] <= 'z'))) {
        char c = r[i++];
        if (c >= 'a' && c <= 'z')
          c -= 32;
        col = col * 26 + c - 'A' + 1;
      }
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
