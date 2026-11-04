// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_clipboard_bundle.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace DomeOffice;
namespace {
void rejects(const std::function<void()> &action) {
  bool failed = false;
  try {
    action();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  assert(failed);
}
void set64(std::vector<uint8_t> &data, size_t at, uint64_t value) {
  for (size_t i = 0; i < 8; ++i)
    data[at + 7 - i] = uint8_t(value >> (i * 8));
}
} // namespace
int main() {
  // Independent wire fixture: versioned header, big-endian lengths, embedded
  // NUL/invalid UTF-8 bytes and an empty second representation.
  std::vector<uint8_t> wire{
      'C', 'a', 'l', 'l', 'i', 'g', 'r', 'a', 'C', 'l', 'i', 'p', 0,   1,
      0,   0,   0,   0,   0,   2,   0,   0,   0,   9,   0,   0,   0,   0,
      0,   0,   0,   4,   'i', 'm', 'a', 'g', 'e', '/', 'p', 'n', 'g', 0,
      255, 3,   128, 0,   0,   0,   10,  0,   0,   0,   0,   0,   0,   0,
      0,   't', 'e', 'x', 't', '/', 'p', 'l', 'a', 'i', 'n'};
  NativeClipboardBundle expected{{"image/png", {0, 255, 3, 128}},
                                 {"text/plain", {}}};
  assert(decode_clipboard_bundle(wire) == expected);
  assert(encode_clipboard_bundle(expected) == wire);
  for (size_t n = 0; n < wire.size(); ++n)
    rejects([&] { decode_clipboard_bundle(std::span(wire).first(n)); });
  for (size_t at :
       {size_t(0), size_t(12), size_t(13), size_t(14), size_t(15)}) {
    auto broken = wire;
    broken[at] ^= 1;
    rejects([&] { decode_clipboard_bundle(broken); });
  }
  auto broken = wire;
  broken.push_back(0);
  rejects([&] { decode_clipboard_bundle(broken); });
  broken = wire;
  broken[19] = 0;
  rejects([&] { decode_clipboard_bundle(broken); });
  broken = wire;
  broken[19] = 17;
  rejects([&] { decode_clipboard_bundle(broken); });
  broken = wire;
  broken[23] = 0;
  rejects([&] { decode_clipboard_bundle(broken); });
  broken = wire;
  broken[22] = 2;
  rejects([&] { decode_clipboard_bundle(broken); });
  for (uint64_t size : {uint64_t(clipboard_payload_limit + 1), UINT64_MAX}) {
    broken = wire;
    set64(broken, 24, size);
    rejects([&] { decode_clipboard_bundle(broken); });
  }
  broken = wire;
  set64(broken, 49, clipboard_payload_limit);
  rejects([&] { decode_clipboard_bundle(broken); });
  rejects([] { encode_clipboard_bundle({}); });
  rejects([] {
    encode_clipboard_bundle({{"text/plain", {}}, {"text/plain", {1}}});
  });
  auto duplicate =
      encode_clipboard_bundle({{"image/png", {1}}, {"image/gif", {2}}});
  std::copy_n("image/png", 9, duplicate.end() - 10);
  rejects([&] { decode_clipboard_bundle(duplicate); });
  auto many = NativeClipboardBundle(17, {"text/plain", {}});
  rejects([&] { encode_clipboard_bundle(many); });
  for (std::string type : std::vector<std::string>{
           "", "text", "/plain", "text/", "text/plain\n", "text /plain",
           "text/plain/extra", std::string(257, 'x')})
    rejects([&] { encode_clipboard_bundle({{type, {}}}); });
  assert(
      native_clipboard_mime("application/vnd.oasis.opendocument.presentation"));
  assert(native_clipboard_mime("text/plain;charset=utf-8"));
  assert(native_clipboard_mime("text/plain; charset=\"utf-8\""));
  NativeClipboardBundle large{
      {"application/octet-stream", std::vector<uint8_t>(200000)}};
  for (size_t i = 0; i < large[0].data.size(); ++i)
    large[0].data[i] = uint8_t(i * 37);
  assert(decode_clipboard_bundle(encode_clipboard_bundle(large)) == large);
  std::cout << "Native clipboard: independent binary framing, malformed input "
               "and quotas passed\n";
}
