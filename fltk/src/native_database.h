// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_criteria.h"
#include <functional>
namespace DomeOffice {
struct NativeDatabaseSource {
  size_t rows = 1, columns = 1;
  std::function<NativeValue(size_t, size_t)> read;
  std::function<bool(size_t, size_t)> blank;
  std::function<std::vector<size_t>()> occupied_rows;
  using Predicate = std::function<NativeValue(size_t)>;
  std::function<Predicate(size_t, size_t)> predicate;
};
const NativeSignatures &database_signatures();
Text database_name(const Text &);
class NativeDatabaseModel {
  struct Impl;
  std::shared_ptr<Impl> impl_;

public:
  NativeDatabaseModel(NativeDatabaseSource, NativeDatabaseSource,
                      NativeCalculationSettings, CriteriaBudget &);
  NativeValue calculate(const Text &, const NativeValue &,
                        bool omitted = false) const;
};
} // namespace DomeOffice
