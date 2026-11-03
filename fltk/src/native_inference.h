// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &inference_signatures();
Text inference_name(const Text &);
class NativeHypothesisData {
  struct Impl;
  std::shared_ptr<Impl> impl_;

public:
  NativeHypothesisData(
      std::vector<double>, std::vector<double>,
      std::optional<std::vector<std::pair<double, double>>> = {});
  NativeValue evaluate(const Text &,
                       const std::vector<NativeValue> & = {}) const;
};
class NativeProbabilityData {
  std::vector<double> values_;
  std::vector<long double> tree_;
  size_t base_ = 1;

public:
  NativeProbabilityData(std::vector<double>, std::vector<double>);
  NativeValue evaluate(const NativeValue &, const NativeValue &) const;
};
NativeValue native_frequency(std::vector<double>, const std::vector<double> &);
NativeValue native_chi_test(const std::vector<std::pair<double, double>> &,
                            size_t rows, size_t columns);
} // namespace DomeOffice
