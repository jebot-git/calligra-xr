// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_sheets.h"
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
namespace DomeOffice::NativeExact {
using I = boost::multiprecision::cpp_int;
using Q = boost::multiprecision::cpp_bin_float_quad;
struct Binary {
  uint64_t mantissa;
  int exponent;
  bool negative;
};
Binary binary(double);
I integer(Binary, int exponent);
// Exact totals precede mean/variance subtraction. Conversion to quad is only
// performed after integer cancellation, preserving tiny differences of means.
struct Sample {
  size_t count = 0, limbs = 1;
  int exponent = 0;
  I sum = 0, squares = 0, centered = 0;
  mutable size_t remaining = 50'000'000;
  explicit Sample(const std::vector<double> &);
  explicit Sample(const std::vector<std::pair<double, double>> &differences);
  void charge(size_t) const;
  Q variance() const;
  Q difference(const Sample &) const;
  Q difference(double) const;
};
} // namespace DomeOffice::NativeExact
