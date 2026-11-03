// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
namespace DomeOffice {
const NativeSignatures &order_signatures();
Text order_name(const Text &);
class NativeOrderData {
  std::vector<double> original_, sorted_;

public:
  explicit NativeOrderData(std::vector<double>);
  NativeValue evaluate(const Text &, const std::vector<NativeValue> &) const;
};
} // namespace DomeOffice
