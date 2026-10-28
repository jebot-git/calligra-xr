// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_limits.h"
#include "native_types.h"
namespace DomeOffice {
struct NativeValue {
  enum Kind { Empty, Number, Boolean, String, Error };
  Kind kind = Empty;
  enum NumberFormat { General, Date, Duration };
  NumberFormat format = General;
  double number = 0;
  bool boolean = false;
  Text string;
  NativeValue() = default;
  explicit NativeValue(const Text &s) : kind(String), string(s) {}
  static NativeValue numeric(double n) {
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
  static NativeValue error(const Text &s) {
    NativeValue v(s);
    v.kind = Error;
    return v;
  }
  bool isNumber() const { return kind == Number; }
  bool isBoolean() const { return kind == Boolean; }
  bool isError() const { return kind == Error; }
  double asFloat() const { return number; }
  bool asBoolean() const { return boolean; }
  Text display() const {
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
class SheetEngine {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  SheetEngine();
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
