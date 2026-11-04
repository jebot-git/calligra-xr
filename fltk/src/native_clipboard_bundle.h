// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace DomeOffice {
struct NativeClipboardFormat {
  std::string mime;
  std::vector<uint8_t> data;
  bool operator==(const NativeClipboardFormat &) const = default;
};
using NativeClipboardBundle = std::vector<NativeClipboardFormat>;
// Limits apply before allocating a declared payload. The process transport is
// deliberately separate from the ODF fragment format carried by a MIME type.
inline constexpr size_t clipboard_payload_limit = 64 * 1024 * 1024;
inline constexpr size_t clipboard_format_limit = 16;
inline constexpr size_t clipboard_mime_limit = 256;
inline constexpr size_t clipboard_wire_limit =
    clipboard_payload_limit + 32 +
    clipboard_format_limit * (clipboard_mime_limit + 12);
bool native_clipboard_mime(const std::string &);
std::vector<uint8_t> encode_clipboard_bundle(const NativeClipboardBundle &);
NativeClipboardBundle decode_clipboard_bundle(std::span<const uint8_t>);
} // namespace DomeOffice
