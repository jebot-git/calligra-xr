// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "text_documents.h"
#include <memory>
namespace DomeOffice {
// Detached receiving transaction. Preparing/cancelling cannot alter document,
// selection or redo. Commit validates the exact model instance and revision.
class NativeStagePaste {
public:
  ~NativeStagePaste();
  NativeStagePaste(NativeStagePaste &&) noexcept;
  NativeStagePaste &operator=(NativeStagePaste &&) noexcept;
  NativeStagePaste(const NativeStagePaste &) = delete;
  NativeStagePaste &operator=(const NativeStagePaste &) = delete;

private:
  friend class Presentation;
  struct State;
  std::unique_ptr<State> state_;
  explicit NativeStagePaste(std::unique_ptr<State>);
};
} // namespace DomeOffice
