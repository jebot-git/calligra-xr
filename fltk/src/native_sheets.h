// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_calculation_settings.h"
#include "native_limits.h"
#include "native_types.h"
#include <cmath>
namespace DomeOffice {
using NativeSignatures = std::map<Text, std::pair<size_t, size_t>>;
struct NativeValue {
  enum Kind { Empty, Number, Boolean, String, Error, Array, Complex };
  Kind kind = Empty;
  enum NumberFormat { General, Date, Duration };
  NumberFormat format = General;
  double number = 0;
  double imaginary = 0;
  char complex_suffix = 'i';
  bool boolean = false;
  bool incomplete = false;
  Text string;
  size_t rows = 1, columns = 1;
  std::shared_ptr<const std::vector<NativeValue>> items;
  NativeValue() = default;
  explicit NativeValue(const Text &s) : kind(String), string(s) {}
  static NativeValue numeric(double n) {
    if (!std::isfinite(n))
      return error("#NUM!");
    NativeValue v;
    v.kind = Number;
    v.number = n;
    return v;
  }
  static NativeValue logical(bool b) {
    NativeValue v;
    v.kind = Boolean;
    v.boolean = b;
    return v;
  }
  static NativeValue complex(double real, double imag, char suffix = 'i') {
    if (!std::isfinite(real) || !std::isfinite(imag))
      return error("#NUM!");
    NativeValue v;
    v.kind = Complex;
    v.number = real;
    v.imaginary = imag;
    v.complex_suffix = suffix;
    if (real == 0)
      real = 0;
    if (imag == 0)
      imag = 0;
    v.string = Text::number(real, 'g', 17) + (imag < 0 ? Text() : Text("+")) +
               Text::number(imag, 'g', 17) + Text(1, char32_t(suffix));
    return v;
  }
  static NativeValue error(const Text &s, bool incomplete = false) {
    NativeValue v(s);
    v.kind = Error;
    v.incomplete = incomplete;
    return v;
  }
  static NativeValue array(size_t rows, size_t columns,
                           std::vector<NativeValue> values) {
    if (!rows || !columns || rows > 1'000'000 / columns)
      return error("#LIMIT!", true);
    if (values.size() != rows * columns ||
        std::any_of(values.begin(), values.end(),
                    [](const auto &v) { return v.isArray(); }))
      return error("#VALUE!");
    NativeValue v;
    v.kind = Array;
    v.rows = rows;
    v.columns = columns;
    v.incomplete =
        std::any_of(values.begin(), values.end(),
                    [](const auto &value) { return value.incomplete; });
    v.items =
        std::make_shared<const std::vector<NativeValue>>(std::move(values));
    return v;
  }
  bool isArray() const { return kind == Array; }
  NativeValue at(size_t row, size_t column) const {
    if (!isArray())
      return *this; // Scalar broadcast.
    if (!items || row >= rows || column >= columns)
      return error("#REF!");
    auto result = items->at(row * columns + column);
    result.incomplete = result.incomplete || incomplete;
    return result;
  }
  NativeValue scalar() const { return isArray() ? at(0, 0) : *this; }
  bool isNumber() const { return kind == Number || kind == Complex; }
  bool isComplex() const { return kind == Complex; }
  bool isBoolean() const { return kind == Boolean; }
  bool isError() const { return kind == Error; }
  double asFloat() const { return number; }
  bool asBoolean() const { return boolean; }
  Text display() const {
    if (isArray())
      return scalar().display();
    return kind == Number
               ? (format == General ? Text::number(number, 'g', 15) : string)
           : kind == Boolean ? Text(boolean ? "TRUE" : "FALSE")
                             : string;
  }
};
class SheetEngine;
struct NativeSheet {
  SheetEngine *engine;
  int index;
  Text name;
  int row_count = 0, col_count = 0;
  NativeSheet *cellStorage() { return this; }
  int rows(bool) const { return row_count; }
  int columns(bool) const { return col_count; }
  Text sheetName() const { return name; }
};
struct MatrixBlock {
  int sheet, column, row, columns, rows;
};
class SheetEngine {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit SheetEngine(NativeCalculationSettings settings = {});
  const NativeCalculationSettings &calculation_settings() const;
  ~SheetEngine();
  int count() const;
  NativeSheet *sheet(int s) const;
  NativeSheet *findSheet(const Text &name) const;
  void addNewSheet(const Text &name);
  Text input(int s, int c, int r) const;
  NativeValue value(int s, int c, int r) const;
  void set(int s, int c, int r, const Text &text, bool literal = false);
  void recalculate();
  void addNamedRange(const Text &reference, const Text &name);
  void define_matrix(int sheet, int column, int row, int columns, int rows);
  bool matrix_child(int sheet, int column, int row) const;
  const std::vector<MatrixBlock> &matrix_blocks() const;
};
class NativeCell {
  NativeSheet *sheet_;
  int col_, row_;

public:
  NativeCell(NativeSheet *s, int c, int r) : sheet_(s), col_(c), row_(r) {}
  Text userInput() const {
    return sheet_->engine->input(sheet_->index, col_, row_);
  }
  NativeValue value() const {
    return sheet_->engine->value(sheet_->index, col_, row_);
  }
  void parseUserInput(const Text &v) {
    sheet_->engine->set(sheet_->index, col_, row_, v);
  }
  void setCellValue(const NativeValue &v) {
    sheet_->engine->set(sheet_->index, col_, row_, v.string, true);
  }
};
} // namespace DomeOffice
