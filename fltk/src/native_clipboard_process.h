// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_clipboard_bundle.h"
#include <memory>
#include <optional>

namespace DomeOffice {
// Poll from the UI event loop. A successful offer remains owned by the helper
// until replaced, including after this controller/application is destroyed.
// Cancelling a pending operation never kills a previously acknowledged owner.
class NativeClipboardProcess {
public:
  struct Result {
    bool success = false;
    NativeClipboardBundle formats;
    std::string error;
  };
  explicit NativeClipboardProcess(std::string helper);
  ~NativeClipboardProcess();
  NativeClipboardProcess(const NativeClipboardProcess &) = delete;
  NativeClipboardProcess &operator=(const NativeClipboardProcess &) = delete;
  void offer(const NativeClipboardBundle &);
  void receive(const std::vector<std::string> &preferred);
  std::optional<Result> poll();
  void cancel();
  bool busy() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Resolve only beside the executable (build) or its installed libexec sibling.
std::string native_clipboard_helper();
} // namespace DomeOffice
