// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
namespace DomeOffice {
// Immutable encoded data can outlive its source package and is shared by all
// labels/frames using that entry. IDs change when an entry is replaced.
struct ImageAsset {
  uint64_t id;
  Text source;
  std::shared_ptr<const Bytes> data;
  // Encoded-content identity survives source-model rebuilds and new archives.
  std::string fingerprint;
};
std::shared_ptr<const ImageAsset> make_image_asset(const Text &, Bytes);
std::shared_ptr<const ImageAsset>
make_image_asset(const Text &, std::shared_ptr<const Bytes>);
std::optional<Text> package_image_path(const Text &);
std::optional<Bytes> inline_image_data(const Text &);
} // namespace DomeOffice
