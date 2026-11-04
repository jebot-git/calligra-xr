// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment.h"
#include <array>
#include <numeric>
#include <span>
namespace DomeOffice {
namespace {
constexpr size_t payload_limit = 64 * 1024 * 1024;
constexpr size_t object_limit = 100000, path_index_limit = 1000000;
constexpr std::array<unsigned char, 8> magic{'C', 'S', 't', 'a',
                                             'g', 'e', 0,   1};
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
std::vector<size_t> order(const std::vector<NativeStageObjectPath> &paths) {
  if (paths.empty() || paths.size() > object_limit)
    fail("Invalid clipboard object count");
  size_t indices = 0;
  for (const auto &path : paths) {
    if (path.empty() || path.size() > 256 ||
        path.size() > path_index_limit - indices)
      fail("Clipboard object paths exceed the limit");
    indices += path.size();
    for (auto rank : path)
      if (rank > path_index_limit)
        fail("Clipboard object child index exceeds the limit");
  }
  std::vector<size_t> indices_order(paths.size());
  std::iota(indices_order.begin(), indices_order.end(), 0);
  std::sort(indices_order.begin(), indices_order.end(),
            [&](size_t a, size_t b) { return paths[a] < paths[b]; });
  for (size_t i = 1; i < indices_order.size(); ++i) {
    const auto &a = paths[indices_order[i - 1]], &b = paths[indices_order[i]];
    if (a.size() <= b.size() && std::equal(a.begin(), a.end(), b.begin()))
      fail("Clipboard object paths overlap or repeat");
  }
  return indices_order;
}
XmlElement sole_page(const OdfPackage &package) {
  auto pages = children(package.body(), DRAW, "page");
  if (package.kind() != Kind::Stage || pages.size() != 1)
    fail("An object clipboard requires exactly one presentation page");
  return pages[0];
}
NativeStageFragment canonical(const Bytes &archive,
                              const std::vector<NativeStageObjectPath> &paths) {
  if (archive.empty() || archive.size() > payload_limit)
    fail("Clipboard ODP payload exceeds the limit");
  // Validate metadata before decompressing or laying out untrusted input.
  (void)order(paths);
  OdfPackage package(Kind::Stage);
  package.load_bytes(archive);
  auto nodes = stage_fragment_nodes(package, paths);
  auto scenes = read_presentation_scenes(package);
  auto refs = native_object_references(scenes.at(0), nodes);
  if (refs.size() != nodes.size())
    fail("Clipboard selection is not a native slide object");
  // Do not trust text fallbacks, archive metadata or unused parts supplied by a
  // clipboard peer. Re-extract only the explicitly selected dependency closure.
  auto result = native_stage_fragment(package, scenes.at(0), refs);
  if (result.archive.size() > payload_limit)
    fail("Clipboard ODP payload exceeds the limit");
  return result;
}
void append(Bytes &out, uint64_t value, size_t bytes) {
  for (size_t i = bytes; i; --i)
    out.push_back(char(value >> ((i - 1) * 8)));
}
struct Reader {
  std::span<const unsigned char> bytes;
  explicit Reader(const Bytes &input)
      : bytes(reinterpret_cast<const unsigned char *>(input.data()),
              input.size()) {}
  std::span<const unsigned char> take(size_t n) {
    if (n > bytes.size())
      fail("Truncated object clipboard envelope");
    auto out = bytes.first(n);
    bytes = bytes.subspan(n);
    return out;
  }
  uint64_t number(size_t n) {
    uint64_t value = 0;
    for (auto b : take(n))
      value = (value << 8) | b;
    return value;
  }
};
} // namespace

std::vector<XmlElement>
stage_fragment_nodes(const OdfPackage &package,
                     const std::vector<NativeStageObjectPath> &paths) {
  const auto sorted = order(paths);
  auto page = sole_page(package);
  struct Frame {
    XmlElement parent;
    XmlNode cursor;
    uint32_t rank = 0;
  };
  std::vector<Frame> frames{{page, page.firstChild(), 0}};
  std::vector<XmlElement> result(paths.size());
  NativeStageObjectPath previous;
  size_t visits = 0;
  for (auto at : sorted) {
    const auto &path = paths[at];
    size_t common = 0;
    while (common < previous.size() && common < path.size() &&
           previous[common] == path[common])
      ++common;
    frames.resize(common + 1);
    for (size_t depth = common; depth < path.size(); ++depth) {
      auto &frame = frames.back();
      XmlElement found;
      if (path[depth] < frame.rank)
        fail("Invalid clipboard path order");
      while (!frame.cursor.isNull()) {
        if (++visits > path_index_limit)
          fail("Clipboard object path traversal exceeds the limit");
        auto node = frame.cursor;
        frame.cursor = node.nextSibling();
        if (!node.isElement())
          continue;
        if (frame.rank++ == path[depth]) {
          found = node;
          break;
        }
      }
      if (found.isNull())
        fail("Clipboard object path is outside the page");
      frames.push_back({found, found.firstChild(), 0});
    }
    result[at] = frames.back().parent;
    previous = path;
  }
  return result;
}

Bytes encode_stage_fragment(const NativeStageFragment &fragment) {
  auto safe = canonical(fragment.archive, fragment.selection);
  if (safe.archive.size() > payload_limit - 20)
    fail("Object clipboard envelope exceeds the limit");
  size_t size = 20 + safe.archive.size();
  for (const auto &path : safe.selection) {
    if (2 + 4 * path.size() > payload_limit - size)
      fail("Object clipboard envelope exceeds the limit");
    size += 2 + 4 * path.size();
  }
  if (size > payload_limit)
    fail("Object clipboard envelope exceeds the limit");
  Bytes out;
  out.reserve(size);
  out.append(reinterpret_cast<const char *>(magic.data()), magic.size());
  append(out, safe.archive.size(), 8);
  append(out, safe.selection.size(), 4);
  for (const auto &path : safe.selection) {
    append(out, path.size(), 2);
    for (auto rank : path)
      append(out, rank, 4);
  }
  out += safe.archive;
  return out;
}
NativeStageFragment decode_stage_fragment(const Bytes &payload) {
  if (payload.size() > payload_limit)
    fail("Object clipboard envelope exceeds the limit");
  Reader input(payload);
  auto header = input.take(magic.size());
  if (!std::equal(header.begin(), header.end(), magic.begin()))
    fail("Unsupported object clipboard version");
  auto size = input.number(8), count = input.number(4);
  if (!size || size > payload_limit || !count || count > object_limit)
    fail("Invalid object clipboard lengths");
  std::vector<NativeStageObjectPath> paths;
  size_t indices = 0;
  for (size_t i = 0; i < count; ++i) {
    auto length = input.number(2);
    if (!length || length > 256 || length > path_index_limit - indices)
      fail("Clipboard object paths exceed the limit");
    indices += size_t(length);
    NativeStageObjectPath path;
    path.reserve(size_t(length));
    for (size_t j = 0; j < length; ++j) {
      auto rank = input.number(4);
      if (rank > path_index_limit)
        fail("Clipboard object child index exceeds the limit");
      path.push_back(uint32_t(rank));
    }
    paths.push_back(std::move(path));
  }
  auto archive = input.take(size_t(size));
  if (!input.bytes.empty())
    fail("Trailing object clipboard data");
  return canonical(
      Bytes(reinterpret_cast<const char *>(archive.data()), archive.size()),
      paths);
}
} // namespace DomeOffice
