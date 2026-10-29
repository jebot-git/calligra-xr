// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_image.h"
#include <atomic>
namespace DomeOffice {
std::shared_ptr<const ImageAsset> make_image_asset(const Text &source,
                                                   Bytes bytes) {
  return make_image_asset(source,
                          std::make_shared<const Bytes>(std::move(bytes)));
}
std::shared_ptr<const ImageAsset>
make_image_asset(const Text &source, std::shared_ptr<const Bytes> bytes) {
  if (!bytes || bytes->empty() || bytes->size() > 16 * 1024 * 1024)
    return {};
  static std::atomic<uint64_t> next{1};
  return std::make_shared<const ImageAsset>(ImageAsset{
      next.fetch_add(1, std::memory_order_relaxed), source, std::move(bytes)});
}
std::optional<Text> package_image_path(const Text &reference) {
  if (reference.empty() || reference.contains('?') || reference.contains('#') ||
      reference.contains('\\'))
    return {};
  auto slash = reference.indexOf('/'), colon = reference.indexOf(':');
  if (colon >= 0 && (slash < 0 || colon < slash))
    return {};
  Text decoded;
  try {
    decoded = Url::fromPercentEncoding(reference.toUtf8());
  } catch (const std::exception &) {
    return {};
  }
  if (decoded.empty() || decoded.startsWith('/') || decoded.contains('\\') ||
      decoded.contains(Char(0)))
    return {};
  TextList parts;
  for (const auto &part : decoded.split('/')) {
    if (part.empty() || part == ".")
      continue;
    if (part == "..") {
      if (parts.empty())
        return {};
      parts.pop_back();
    } else
      parts.push_back(part);
  }
  if (parts.empty())
    return {};
  return parts.join('/');
}
std::optional<Bytes> inline_image_data(const Text &text) {
  if (text.size() > 24 * 1024 * 1024)
    return {};
  Bytes encoded;
  for (auto c : text) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
      continue;
    if (c > 127 ||
        !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '='))
      return {};
    encoded.push_back(char(c));
  }
  if (encoded.empty() || encoded.size() % 4)
    return {};
  auto padding = encoded.find('=');
  if (padding != Bytes::npos) {
    if (encoded.size() - padding > 2)
      return {};
    for (size_t i = padding; i < encoded.size(); ++i)
      if (encoded[i] != '=')
        return {};
  }
  auto bytes = Bytes::fromBase64(encoded.c_str());
  if (bytes.empty() || bytes.size() > 16 * 1024 * 1024)
    return {};
  return bytes;
}
} // namespace DomeOffice
