// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
// Absent matrix coordinates are numeric zero, independently of matrix geometry.
// Explicit nonnumeric entries are converted/rejected by the engine collector.
struct NativeRegressionMatrix {
  size_t rows = 1, columns = 1;
  std::map<size_t, double> entries;
  double at(size_t row, size_t column) const {
    auto it = entries.find(row * columns + column);
    return it == entries.end() ? 0 : it->second;
  }
};
const NativeSignatures &regression_signatures();
Text regression_name(const Text &);
class NativeRegressionModel {
  struct Impl;
  std::shared_ptr<Impl> impl_;

public:
  NativeRegressionModel(NativeRegressionMatrix y,
                        std::optional<NativeRegressionMatrix> x, bool intercept,
                        bool logarithmic = false);
  NativeValue coefficients(bool statistics) const;
  NativeValue predict(const std::optional<NativeRegressionMatrix> &) const;
};
} // namespace DomeOffice
