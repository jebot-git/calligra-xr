// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native document types: UTF-32 text, UTF-8/binary bytes and STL containers.
// No Qt headers, objects or binary ABI are used.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <iomanip>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
using Int64 = int64_t;
using UInt64 = uint64_t;
using Char = char32_t;
enum TextOption { CaseSensitive, CaseInsensitive, ISODate, ISODateWithMs, UTC };
class Text;
class TextList;
class Regex;
template <class T> struct List : std::vector<T> {
  using std::vector<T>::vector;
  bool isEmpty() const { return this->empty(); }
  T &first() { return this->front(); }
  const T &first() const { return this->front(); }
  T &last() { return this->back(); }
  const T &last() const { return this->back(); }
  List &operator<<(const T &v) {
    this->push_back(v);
    return *this;
  }
  void append(const T &v) { this->push_back(v); }
  bool contains(const T &v) const {
    return std::find(this->begin(), this->end(), v) != this->end();
  }
  void removeAt(int i) { this->erase(this->begin() + i); }
  void removeLast() { this->pop_back(); }
  T value(int i, const T &fallback = {}) const {
    return i >= 0 && size_t(i) < this->size() ? this->at(i) : fallback;
  }
};
class Bytes : public std::string {
public:
  using std::string::string;
  Bytes() = default;
  Bytes(const std::string &s) : std::string(s) {}
  const char *constData() const { return c_str(); }
  bool isEmpty() const { return empty(); }
  std::string toStdString() const { return *this; }
  bool contains(const char *s) const { return find(s) != npos; }
  bool startsWith(const Bytes &s) const { return starts_with(s); }
  Bytes &append(char c) {
    push_back(c);
    return *this;
  }
  using std::string::append;
  static Bytes fromHex(const char *s) {
    Bytes out;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
      char b[3] = {s[i], s[i + 1], 0};
      out.push_back(char(strtoul(b, nullptr, 16)));
    }
    return out;
  }
  Bytes &replace(const Bytes &before, const Bytes &after) {
    size_t i = 0;
    if (before.empty())
      return *this;
    while ((i = find(before, i)) != npos) {
      std::string::replace(i, before.size(), after);
      i += after.size();
    }
    return *this;
  }
  Bytes toUpper() const {
    Bytes out = *this;
    for (char &c : out)
      c = g_ascii_toupper(c);
    return out;
  }
  static Bytes fromBase64(const char *s) {
    gsize n = 0;
    auto *p = g_base64_decode(s, &n);
    Bytes out(reinterpret_cast<char *>(p), n);
    g_free(p);
    return out;
  }
  Bytes toBase64() const {
    char *p = g_base64_encode(reinterpret_cast<const guchar *>(data()), size());
    Bytes out(p);
    g_free(p);
    return out;
  }
};
class Text : public std::u32string {
  static std::u32string decode(const char *p, size_t n) {
    if (!n)
      return {};
    glong count = 0;
    GError *error = nullptr;
    auto *v = g_utf8_to_ucs4(p, n, nullptr, &count, &error);
    if (!v) {
      if (error)
        g_error_free(error);
      throw std::runtime_error("Invalid UTF-8 text");
    }
    std::u32string out(reinterpret_cast<char32_t *>(v), count);
    g_free(v);
    return out;
  }

public:
  using std::u32string::u32string;
  Text() = default;
  Text(const char *s) : std::u32string(decode(s ? s : "", s ? strlen(s) : 0)) {}
  Text(const std::string &s) : std::u32string(decode(s.data(), s.size())) {}
  Text(const std::u32string &s) : std::u32string(s) {}
  Text(char c) : std::u32string(1, char32_t(static_cast<unsigned char>(c))) {}
  static Text fromUtf8(const char *s) { return Text(s); }
  static Text fromUtf8(const Bytes &s) { return Text(std::string(s)); }
  static Text fromUtf8(const char *s, int n) { return Text(std::string(s, n)); }
  Bytes toUtf8() const {
    glong n = 0;
    auto *p = g_ucs4_to_utf8(reinterpret_cast<const gunichar *>(data()), size(),
                             nullptr, &n, nullptr);
    if (!p)
      throw std::runtime_error("Invalid Unicode text");
    Bytes out(p, n);
    g_free(p);
    return out;
  }
  std::string toStdString() const { return toUtf8(); }
  bool isEmpty() const { return empty(); }
  Text mid(int at, int n = -1) const {
    if (at < 0)
      at = 0;
    if (size_t(at) >= size())
      return {};
    return substr(at, n < 0 ? npos : size_t(n));
  }
  Text left(int n) const {
    return substr(0, std::min(size_t(std::max(n, 0)), size()));
  }
  void chop(int n) { resize(n > int(size()) ? 0 : size() - n); }
  Text right(int n) const { return mid(std::max(0, int(size()) - n)); }
  Text trimmed() const {
    size_t a = 0, b = size();
    while (a < b && g_unichar_isspace(at(a)))
      ++a;
    while (b > a && g_unichar_isspace(at(b - 1)))
      --b;
    return substr(a, b - a);
  }
  Text toUpper() const {
    auto bytes = toUtf8();
    char *p = g_utf8_strup(bytes.data(), bytes.size());
    Text out(p);
    g_free(p);
    return out;
  }
  Text toLower() const {
    auto bytes = toUtf8();
    char *p = g_utf8_strdown(bytes.data(), bytes.size());
    Text out(p);
    g_free(p);
    return out;
  }
  Text toCaseFolded() const { return folded(); }
  Text folded() const {
    auto bytes = toUtf8();
    char *p = g_utf8_casefold(bytes.data(), bytes.size());
    Text out(p);
    g_free(p);
    return out;
  }
  int compare(const Text &o, TextOption option = CaseSensitive) const {
    return option == CaseInsensitive
               ? folded().std::u32string::compare(o.folded())
               : std::u32string::compare(o);
  }
  bool contains(const Text &o, TextOption option = CaseSensitive) const {
    return option == CaseInsensitive ? folded().find(o.folded()) != npos
                                     : find(o) != npos;
  }
  bool contains(Char c) const { return find(c) != npos; }
  bool contains(const Regex &r) const;
  bool startsWith(const Text &o, TextOption option = CaseSensitive) const {
    return option == CaseInsensitive ? folded().starts_with(o.folded())
                                     : starts_with(o);
  }
  bool startsWith(Char c) const { return !empty() && front() == c; }
  bool endsWith(Char c) const { return !empty() && back() == c; }
  bool endsWith(const Text &o, TextOption option = CaseSensitive) const {
    return option == CaseInsensitive ? folded().ends_with(o.folded())
                                     : ends_with(o);
  }
  int indexOf(const Text &o, int at = 0) const {
    auto p = find(o, at);
    return p == npos ? -1 : int(p);
  }
  int indexOf(Char c, int at = 0) const {
    auto p = find(c, at);
    return p == npos ? -1 : int(p);
  }
  int lastIndexOf(Char c) const {
    auto p = rfind(c);
    return p == npos ? -1 : int(p);
  }
  TextList split(Char c) const;
  TextList split(const Text &s) const;
  Text &replace(const Text &before, const Text &after) {
    if (before.empty())
      return *this;
    size_t i = 0;
    while ((i = find(before, i)) != npos) {
      std::u32string::replace(i, before.size(), after);
      i += after.size();
    }
    return *this;
  }
  Text &replace(Char before, Char after) {
    std::replace(begin(), end(), before, after);
    return *this;
  }
  Text &remove(const Regex &);
  Text &remove(const Text &s) { return replace(s, {}); }
  Text &remove(int at, int n) {
    erase(at, n);
    return *this;
  }
  Text &insert(int at, const Text &s) {
    std::u32string::insert(at, s);
    return *this;
  }
  Text &append(const Text &s) {
    *this += s;
    return *this;
  }
  Text &append(Char c) {
    push_back(c);
    return *this;
  }
  Text &operator+=(const Text &s) {
    std::u32string::append(s);
    return *this;
  }
  Text &operator+=(Char c) {
    push_back(c);
    return *this;
  }
  Text &operator+=(const char *s) { return *this += Text(s); }
  static Text number(double v, char format = 'g', int precision = 6) {
    char buffer[128];
    auto f =
        format == 'f' ? std::chars_format::fixed : std::chars_format::general;
    auto [p, e] = std::to_chars(buffer, buffer + 128, v, f, precision);
    if (e != std::errc{})
      throw std::runtime_error("Number formatting failed");
    return Text(std::string(buffer, p));
  }
  template <class T>
    requires std::is_integral_v<T>
  static Text number(T v) {
    return Text(std::to_string(v));
  }
  long long toLongLong(bool *ok = nullptr) const {
    auto b = trimmed().toUtf8();
    char *end = nullptr;
    errno = 0;
    long long v = strtoll(b.c_str(), &end, 10);
    bool valid = !b.empty() && end == b.data() + b.size() && errno != ERANGE;
    if (ok)
      *ok = valid;
    return valid ? v : 0;
  }
  int toInt(bool *ok = nullptr) const {
    bool valid;
    auto v = toLongLong(&valid);
    valid = valid && v >= INT32_MIN && v <= INT32_MAX;
    if (ok)
      *ok = valid;
    return valid ? int(v) : 0;
  }
  double toDouble(bool *ok = nullptr) const {
    auto b = trimmed().toUtf8();
    char *end = nullptr;
    errno = 0;
    double v = strtod(b.c_str(), &end);
    bool valid = !b.empty() && end == b.data() + b.size() && errno != ERANGE;
    if (ok)
      *ok = valid;
    return valid ? v : 0;
  }
  Text arg(const Text &value) const {
    Text out = *this;
    int number = INT32_MAX;
    for (size_t i = 0; i + 1 < size(); i++)
      if (at(i) == U'%' && at(i + 1) >= U'1' && at(i + 1) <= U'9')
        number = std::min(number, int(at(i + 1) - U'0'));
    if (number != INT32_MAX)
      out.replace(Text("%") + Text::number(number), value);
    return out;
  }
  template <class T>
    requires std::is_arithmetic_v<T>
  Text arg(T v) const {
    return arg(Text::number(v));
  }
  template <class A, class B> Text arg(const A &a, const B &b) const {
    return arg(a).arg(b);
  }
  friend bool operator==(const Text &a, const Text &b) {
    return static_cast<const std::u32string &>(a) ==
           static_cast<const std::u32string &>(b);
  }
  friend auto operator<=>(const Text &a, const Text &b) {
    return static_cast<const std::u32string &>(a) <=>
           static_cast<const std::u32string &>(b);
  }
  friend Text operator+(const Text &a, const Text &b) {
    Text r = a;
    r += b;
    return r;
  }
  friend Text operator+(const Text &a, const char *b) { return a + Text(b); }
  friend Text operator+(const char *a, const Text &b) { return Text(a) + b; }
  friend Text operator+(const Text &a, Char b) {
    Text r = a;
    r += b;
    return r;
  }
  friend Text operator+(Char a, const Text &b) {
    Text r(1, a);
    r += b;
    return r;
  }
};
class TextList : public List<Text> {
public:
  using List<Text>::List;
  Text join(const Text &s) const {
    Text out;
    for (const auto &v : *this) {
      if (&v != &front())
        out += s;
      out += v;
    }
    return out;
  }
  Text join(Char c) const { return join(Text(1, c)); }
};
inline TextList Text::split(Char c) const { return split(Text(1, c)); }
inline TextList Text::split(const Text &s) const {
  TextList out;
  if (s.empty()) {
    out << *this;
    return out;
  }
  size_t pos = 0, next;
  while ((next = find(s, pos)) != npos) {
    out << Text(substr(pos, next - pos));
    pos = next + s.size();
  }
  out << Text(substr(pos));
  return out;
}
struct RegexMatch {
  bool valid = false;
  List<Text> groups;
  bool hasMatch() const { return valid; }
  Text captured(int i = 0) const { return groups.value(i); }
  int capturedStart(int = 0) const { return 0; }
};
class Regex {
public:
  enum Option { CaseInsensitiveOption };
  std::regex expression;
  explicit Regex(const char *s) : expression(s) {}
  Regex(const char *s, Option) : expression(s, std::regex::icase) {}
  RegexMatch match(const Text &s) const {
    auto b = s.toUtf8();
    std::smatch m;
    bool valid =
        std::regex_search(static_cast<const std::string &>(b), m, expression);
    RegexMatch out;
    out.valid = valid;
    for (auto &v : m)
      out.groups << Text(v.str());
    return out;
  }
};
inline bool Text::contains(const Regex &r) const {
  return r.match(*this).hasMatch();
}
inline Text &Text::remove(const Regex &r) {
  *this = Text(std::regex_replace(toStdString(), r.expression, ""));
  return *this;
}
template <class K, class V> class Map {
  std::map<K, V> data_;
  template <class I> struct Iter {
    I i;
    auto &operator*() const { return i->second; }
    auto *operator->() const { return &i->second; }
    Iter &operator++() {
      ++i;
      return *this;
    }
    bool operator!=(const Iter &o) const { return i != o.i; }
    bool operator==(const Iter &o) const { return i == o.i; }
    const K &key() const { return i->first; }
    auto &value() const { return i->second; }
  };

public:
  Map() = default;
  Map(std::initializer_list<std::pair<const K, V>> items) : data_(items) {}
  bool contains(const K &key) const { return data_.contains(key); }
  bool isEmpty() const { return data_.empty(); }
  void clear() { data_.clear(); }
  size_t size() const { return data_.size(); }
  V &operator[](const K &key) { return data_[key]; }
  V value(const K &key, const V &fallback = {}) const {
    auto i = data_.find(key);
    return i == data_.end() ? fallback : i->second;
  }
  void insert(const K &k, const V &v) { data_[k] = v; }
  void remove(const K &k) { data_.erase(k); }
  List<K> keys() const {
    List<K> r;
    for (auto &[k, v] : data_)
      r << k;
    return r;
  }
  auto begin() { return Iter{data_.begin()}; }
  auto end() { return Iter{data_.end()}; }
  auto begin() const { return Iter{data_.begin()}; }
  auto end() const { return Iter{data_.end()}; }
  auto cbegin() const { return begin(); }
  auto cend() const { return end(); }
};
template <class T> struct Set : std::set<T> {
  using std::set<T>::set;
  void remove(const T &t) { this->erase(t); }
  bool isEmpty() const { return this->empty(); }
};
template <class A, class B> using Pair = std::pair<A, B>;
struct FileInfo {
  std::filesystem::path p;
  FileInfo(const Text &s) : p(s.toStdString()) {}
  Text suffix() const {
    auto s = p.extension().string();
    return Text(s.empty() ? s : s.substr(1));
  }
  Text path() const {
    return Text(p.parent_path().empty() ? "." : p.parent_path().string());
  }
  Text absolutePath() const {
    return Text(std::filesystem::absolute(p).parent_path().string());
  }
  Text fileName() const { return Text(p.filename().string()); }
  Text completeBaseName() const { return Text(p.stem().string()); }
};
struct Dir {
  Text base;
  Dir() = default;
  Dir(const Text &s) : base(s) {}
  static Text cleanPath(const Text &s) {
    return Text(
        std::filesystem::path(s.toStdString()).lexically_normal().string());
  }
  bool mkpath(const Text &s) {
    std::error_code ec;
    std::filesystem::create_directories(s.toStdString(), ec);
    return !ec;
  }
  Text filePath(const Text &s) const {
    return Text(
        (std::filesystem::path(base.toStdString()) / s.toStdString()).string());
  }
};
struct IO {
  enum Mode { ReadOnly = 1, WriteOnly = 2, Truncate = 4 };
};
class File {
  int fd_ = -1;
  Text path_;

public:
  File(const Text &s) : path_(s) {}
  ~File() { close(); }
  bool open(int mode) {
    close();
    fd_ = ::open(path_.toUtf8().c_str(),
                 mode & IO::WriteOnly
                     ? O_WRONLY | O_CREAT | (mode & IO::Truncate ? O_TRUNC : 0)
                     : O_RDONLY,
                 0600);
    return fd_ >= 0;
  }
  void close() {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
  }
  int handle() const { return fd_; }
  Bytes readAll() {
    Bytes out;
    char b[8192];
    ssize_t n;
    while ((n = ::read(fd_, b, sizeof b)) > 0)
      out.append(b, n);
    return out;
  }
  Int64 write(const Bytes &s) {
    size_t done = 0;
    while (done < s.size()) {
      auto n = ::write(fd_, s.data() + done, s.size() - done);
      if (n <= 0)
        return -1;
      done += n;
    }
    return done;
  }
  static Bytes encodeName(const Text &s) { return s.toUtf8(); }
  static bool remove(const Text &s) {
    std::error_code ec;
    return std::filesystem::remove(s.toStdString(), ec);
  }
  static bool copy(const Text &a, const Text &b) {
    std::error_code ec;
    return std::filesystem::copy_file(a.toStdString(), b.toStdString(), ec);
  }
};
class TempDir {
  Text p;

public:
  TempDir() {
    std::string pattern =
        std::string(g_get_tmp_dir()) + "/calligra-native-XXXXXX";
    if (char *v = mkdtemp(pattern.data()))
      p = Text(v);
  }
  ~TempDir() {
    if (!p.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(p.toStdString(), ec);
    }
  }
  bool isValid() const { return !p.empty(); }
  Text path() const { return p; }
};
class TempFile {
  Text p;
  int fd = -1;
  bool created = false;

public:
  TempFile(const Text &s) : p(s) {}
  ~TempFile() {
    close();
    if (created && !p.empty())
      ::unlink(p.toUtf8().c_str());
  }
  bool open() {
    auto b = p.toUtf8();
    fd = mkstemp(b.data());
    if (fd >= 0) {
      p = Text::fromUtf8(b);
      created = true;
    }
    return fd >= 0;
  }
  void close() {
    if (fd >= 0)
      ::close(fd);
    fd = -1;
  }
  Text fileName() const { return p; }
};
struct Url {
  static Text fromPercentEncoding(const Bytes &b) {
    gchar *p = g_uri_unescape_string(b.c_str(), nullptr);
    if (!p)
      throw std::runtime_error("Invalid percent-encoded document path");
    Text out(p);
    g_free(p);
    return out;
  }
};
