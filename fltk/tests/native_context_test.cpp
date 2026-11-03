// SPDX-License-Identifier: GPL-2.0-or-later
#include "workbook.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace DomeOffice;
namespace {
struct Cwd {
  std::filesystem::path previous = std::filesystem::current_path();
  ~Cwd() {
    std::error_code error;
    std::filesystem::current_path(previous, error);
  }
};
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void text(const NativeValue &v, const Text &want) {
  if (v.kind != NativeValue::String || v.incomplete || v.string != want)
    std::cerr << v.display().toStdString() << " != " << want.toStdString()
              << '\n';
  assert(v.kind == NativeValue::String && !v.incomplete && v.string == want);
}
double number(const NativeValue &v) {
  assert(v.kind == NativeValue::Number && !v.incomplete);
  return v.number;
}
} // namespace
int main() {
  SheetEngine e;
  e.addNewSheet("Context");
  auto eval = [&](const Text &f) {
    e.set(0, 20, 20, "=" + f);
    return e.value(0, 20, 20);
  };
  text(eval("FILENAME()"), "");
  e.set_document_path("/tmp/文書 Ω.ods");
  text(eval("FILENAME()"), "/tmp/文書 Ω.ods");
  assert(eval("INFO(\"missing\")").string == "#VALUE!" &&
         !eval("INFO(\"missing\")").incomplete);
  assert(eval("INFO(#N/A)").string == "#N/A");
  text(eval("INFO(\"recalc\")"), "Automatic");
  assert(!eval("INFO(\"system\")").string.empty());
  assert(!eval("INFO(\"release\")").string.empty());
  auto values = eval("INFO({\"system\",\"recalc\"})");
  assert(values.isArray() && values.columns == 2);
  text(values.at(0, 1), "Automatic");
  auto origin = eval("IFERROR(INFO(\"origin\"),\"headless\")");
  assert(origin.incomplete && origin.string == "headless");
  e.set(0, 4, 1, "=INFO(\"origin\")");
  e.set(0, 5, 1, "=D1");
  e.set_viewport(0, 27, 42);
  text(e.value(0, 5, 1), "$A:$AA$42");
  e.set_viewport(0, 16384, 1048576);
  text(e.value(0, 5, 1), "$A:$XFD$1048576");
  rejects([&] { e.set_viewport(-1, 1, 1); });
  rejects([&] { e.set_viewport(0, 1, 0); });
  text(e.value(0, 5, 1), "$A:$XFD$1048576");
  e.set(0, 1, 1, "=INFO(\"numfile\")");
  e.set(0, 2, 1, "=A1+A1");
  double before = number(e.value(0, 2, 1));
  {
    SheetEngine another;
    for (int i = 0; i < 4; ++i)
      another.addNewSheet(Text("Extra") + Text::number(i));
    assert(number(e.value(0, 2, 1)) == before + 8);
  }
  assert(number(e.value(0, 2, 1)) == before);
  TempDir temporary;
  {
    Cwd restore;
    e.set(0, 6, 1, "=INFO(\"directory\")");
    e.set(0, 7, 1, "=LEN(F1)");
    (void)e.value(0, 7, 1);
    auto directory = std::filesystem::path(temporary.path().toStdString()) /
                     "文書 very long working directory";
    std::filesystem::create_directory(directory);
    std::filesystem::current_path(directory);
    Text expected(directory.string() + "/");
    text(e.value(0, 6, 1), expected);
    assert(number(e.value(0, 7, 1)) == expected.size());
    std::filesystem::remove(directory);
    auto unavailable = e.value(0, 6, 1);
    assert(unavailable.incomplete && unavailable.string == "#VALUE!");
  }
  assert(number(eval("INFO(\"totmem\")")) > 0);
  assert(number(eval("INFO(\"memavail\")")) >= 0);
  e.set(0, 8, 1, "=INFO(\"memused\")");
  e.set(0, 9, 1, "=H1");
  auto used = number(e.value(0, 9, 1));
  std::vector<uint64_t> touched(4 * 1024 * 1024, 42);
  assert(touched.front() == 42 && touched.back() == 42);
  assert(number(e.value(0, 9, 1)) > used + 8 * 1024 * 1024);
  Workbook book;
  book.set(0, 1, 1, "=FILENAME()");
  book.set(0, 2, 1, "=LEN(A1)");
  text(book.value(0, 1, 1), "");
  auto first = temporary.path() + "/first 文書.ods",
       second = temporary.path() + "/second Ω.ods";
  book.save(first);
  text(book.value(0, 1, 1), first);
  assert(book.document_path() == first);
  auto package = OdfPackage(Kind::Sheets);
  package.load(first);
  auto cell =
      package.xml.elementsByTagNameNS(TABLE, "table-cell").at(0).toElement();
  assert(cell.attributeNS(OFFICE, "string-value") == first);
  book.save(second);
  text(book.value(0, 1, 1), second);
  rejects([&] { book.save(temporary.path() + "/missing/fail.ods"); });
  text(book.value(0, 1, 1), second);
  assert(book.document_path() == second);
  Workbook reopened;
  reopened.load(second);
  text(reopened.value(0, 1, 1), second);
  reopened.load(first);
  text(reopened.value(0, 1, 1), first);
  rejects([&] { reopened.load(temporary.path() + "/missing.ods"); });
  text(reopened.value(0, 1, 1), first);
  std::cout << "PASS live INFO context, volatile dependencies, viewport, "
               "memory and atomic document identity\n";
}
