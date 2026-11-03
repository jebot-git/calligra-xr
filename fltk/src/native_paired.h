// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &paired_signatures();
Text paired_name(const Text &);
// Dataset pairs retain argument order. Regression interprets first as Y,
// second as X; covariance/correlation are symmetric.
class NativePairedData {
  struct Impl;
  std::shared_ptr<Impl> impl_;

public:
  explicit NativePairedData(std::vector<std::pair<double, double>>);
  NativeValue evaluate(const Text &, const NativeValue &query = {}) const;
};
} // namespace DomeOffice
