// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
namespace DomeOffice {
// Encoded bytes and intrinsic dimensions validated by the shared image decoder.
// The document engine consumes this value without depending on UI image types.
class NativeImageInput {
  std::shared_ptr<const Bytes> data_;
  Text media_, extension_;
  int width_, height_;
  NativeImageInput(Bytes data, Text media, Text extension, int width,
                   int height)
      : data_(std::make_shared<const Bytes>(std::move(data))),
        media_(std::move(media)), extension_(std::move(extension)),
        width_(width), height_(height) {}
  friend NativeImageInput prepare_image_input(Bytes);

public:
  NativeImageInput(const NativeImageInput &) = default;
  NativeImageInput &operator=(const NativeImageInput &) = default;
  const Bytes &data() const { return *data_; }
  const Text &media_type() const { return media_; }
  const Text &extension() const { return extension_; }
  int width() const { return width_; }
  int height() const { return height_; }
};
NativeImageInput prepare_image_input(Bytes);
NativeImageInput load_image_input(const Text &path);
} // namespace DomeOffice
