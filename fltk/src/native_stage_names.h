// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <memory>
namespace DomeOffice {
// Reserve generated names against definitions and unresolved references in
// XML, URIs and opaque package parts. Shared by import and detached cloning.
class NativeStageNames {
public:
  explicit NativeStageNames(const OdfPackage &);
  ~NativeStageNames();
  void reserve(const OdfPackage &);
  Text take();

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace DomeOffice
