// SPDX-License-Identifier: LGPL-2.0-only
#pragma once
#include "native_sheets.h"
namespace DomeOffice::FinancialValues {
using V = NativeValue;
using Real = long double;
[[noreturn]] inline void fail(const char *code) { throw V::error(code); }
inline Real number(const V &v) {
  if (v.isError())
    throw v;
  if (v.isNumber())
    return v.number;
  if (v.kind == V::Empty)
    return 0;
  if (v.isBoolean())
    return v.boolean;
  if (v.isArray())
    fail("#VALUE!");
  bool ok = false;
  auto n = v.string.toDouble(&ok);
  if (!ok)
    fail("#VALUE!");
  if (!std::isfinite(n))
    fail("#NUM!");
  return n;
}
inline Text text(const V &v) {
  if (v.isError())
    throw v;
  if (v.isArray())
    fail("#VALUE!");
  return v.display();
}
inline bool logical(const V &v) {
  if (v.kind == V::String) {
    auto s = v.string.toUpper();
    if (s == "TRUE")
      return true;
    if (s == "FALSE")
      return false;
  }
  return number(v) != 0;
}
inline Real divide(Real a, Real b) {
  if (b == 0)
    fail("#DIV/0!");
  return a / b;
}
inline int integer(const V &v, int low, int high) {
  auto n = std::trunc(number(v));
  if (n < low || n > high)
    fail("#VALUE!");
  return int(n);
}
} // namespace DomeOffice::FinancialValues
