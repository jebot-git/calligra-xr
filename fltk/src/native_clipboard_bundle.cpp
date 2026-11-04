// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_clipboard_bundle.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace DomeOffice {
namespace {
constexpr std::array<uint8_t, 16> magic{'C', 'a', 'l', 'l', 'i', 'g', 'r', 'a',
                                        'C', 'l', 'i', 'p', 0,   1,   0,   0};
[[noreturn]] void invalid() {
  throw std::runtime_error("Invalid or oversized native clipboard bundle");
}
void append(std::vector<uint8_t> &out, uint64_t value, size_t bytes) {
  for (size_t i = bytes; i; --i)
    out.push_back(uint8_t(value >> ((i - 1) * 8)));
}
struct Reader {
  std::span<const uint8_t> remaining;
  std::span<const uint8_t> take(size_t count) {
    if (count > remaining.size())
      invalid();
    auto result = remaining.first(count);
    remaining = remaining.subspan(count);
    return result;
  }
  uint64_t number(size_t bytes) {
    uint64_t result = 0;
    for (auto byte : take(bytes))
      result = (result << 8) | byte;
    return result;
  }
};
void validate(const NativeClipboardBundle &bundle) {
  if (bundle.empty() || bundle.size() > clipboard_format_limit)
    invalid();
  size_t total = 0;
  std::set<std::string> types;
  for (const auto &format : bundle) {
    if (!native_clipboard_mime(format.mime) ||
        !types.insert(format.mime).second ||
        format.data.size() > clipboard_payload_limit - total)
      invalid();
    total += format.data.size();
  }
}
} // namespace

bool native_clipboard_mime(const std::string &mime) {
  // MIME parameters (including quoted values) are passed through verbatim.
  // Control characters, whitespace-only types and protocol line injection are
  // rejected; MIME names themselves must have a nonempty type and subtype.
  if (mime.empty() || mime.size() > clipboard_mime_limit ||
      std::any_of(mime.begin(), mime.end(),
                  [](unsigned char c) { return c < 32 || c > 126; }))
    return false;
  auto end = mime.find(';');
  auto name = mime.substr(0, end);
  auto slash = name.find('/');
  auto token = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
  };
  return slash != std::string::npos && slash && slash + 1 < name.size() &&
         std::all_of(name.begin(), name.begin() + slash, token) &&
         std::all_of(name.begin() + slash + 1, name.end(), token);
}

std::vector<uint8_t>
encode_clipboard_bundle(const NativeClipboardBundle &bundle) {
  validate(bundle);
  std::vector<uint8_t> out(magic.begin(), magic.end());
  append(out, bundle.size(), 4);
  for (const auto &format : bundle) {
    append(out, format.mime.size(), 4);
    append(out, format.data.size(), 8);
    out.insert(out.end(), format.mime.begin(), format.mime.end());
    out.insert(out.end(), format.data.begin(), format.data.end());
  }
  return out;
}

NativeClipboardBundle decode_clipboard_bundle(std::span<const uint8_t> bytes) {
  if (bytes.size() > clipboard_wire_limit)
    invalid();
  Reader input{bytes};
  auto header = input.take(magic.size());
  if (!std::equal(header.begin(), header.end(), magic.begin()))
    invalid();
  auto count = input.number(4);
  if (!count || count > clipboard_format_limit)
    invalid();
  NativeClipboardBundle result;
  size_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    auto mime_size = input.number(4), data_size = input.number(8);
    if (!mime_size || mime_size > clipboard_mime_limit ||
        data_size > clipboard_payload_limit - total)
      invalid();
    auto mime = input.take(size_t(mime_size));
    std::string type(mime.begin(), mime.end());
    if (!native_clipboard_mime(type) ||
        std::any_of(result.begin(), result.end(),
                    [&](const auto &f) { return f.mime == type; }))
      invalid();
    auto data = input.take(size_t(data_size));
    result.push_back({std::move(type), {data.begin(), data.end()}});
    total += size_t(data_size);
  }
  if (!input.remaining.empty())
    invalid();
  return result;
}
} // namespace DomeOffice
