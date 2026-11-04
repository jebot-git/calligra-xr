// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_fragment.h"
#include <memory>
namespace DomeOffice {
// Preparing a cut never changes the live document or its history. Publish the
// clipboard first; only commit after the clipboard owner reports success.
// Destruction cancels the draft. A moved, foreign or stale draft cannot commit.
class NativeStageCut {
public:
  ~NativeStageCut();
  NativeStageCut(NativeStageCut &&) noexcept;
  NativeStageCut &operator=(NativeStageCut &&) noexcept;
  NativeStageCut(const NativeStageCut &) = delete;
  NativeStageCut &operator=(const NativeStageCut &) = delete;
  const NativeStageFragment &fragment() const;

private:
  friend class Presentation;
  struct State;
  std::unique_ptr<State> state_;
  NativeStageCut(std::unique_ptr<State>);
};
} // namespace DomeOffice
