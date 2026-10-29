// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_print.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F call) {
  bool caught = false;
  try {
    call();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
void valid(const Text &path) {
  File file(path);
  assert(file.open(IO::ReadOnly));
  auto data = file.readAll();
  assert(data.startsWith("%PDF-") && data.contains("%%EOF") &&
         data.size() > 500);
}
} // namespace
int main() {
  TempDir directory;
  TextDocument words;
  words.set_text(
      "Native print baseline\nUnicode: Český 日本語 العربية\nEnd of document");
  auto w = directory.path() + "/words.pdf";
  auto result = print_pdf(words, w);
  assert(result.pages >= 1);
  valid(w);
  Presentation stage;
  stage.set_box_text(0, 0, "Native slide print");
  auto p = directory.path() + "/stage.pdf";
  result = print_pdf(stage, p);
  assert(result.pages == 1);
  valid(p);
  Workbook sheets;
  sheets.set(0, 1, 1, "Input");
  sheets.set(0, 2, 1, "2");
  sheets.set(0, 1, 2, "Calculated");
  sheets.set(0, 2, 2, "=B1*4");
  sheets.set(0, 7, 45, "Last tile");
  sheets.add_sheet("Second worksheet");
  sheets.set(1, 1, 1, "Preserved");
  auto s = directory.path() + "/sheets.pdf";
  result = print_pdf(sheets, s);
  assert(result.pages == 5);
  valid(s);
  PrintSettings settings;
  settings.sheet = 1;
  result = print_pdf(sheets, s, settings);
  assert(result.pages == 1);
  valid(s);
  settings.width = 1e300;
  rejects([&] { print_pdf(sheets, s, settings); });
  settings.width = 10;
  rejects([&] { print_pdf(sheets, s, settings); });
  valid(s);
  File sentinel(directory.path() + "/.calligra-print-XXXXXX");
  assert(sentinel.open(IO::WriteOnly));
  assert(sentinel.write("preserve") == 8);
  sentinel.close();
  rejects([&] { print_pdf(words, directory.path() + "/wrong.odt"); });
  assert(sentinel.open(IO::ReadOnly));
  assert(sentinel.readAll() == "preserve");
  validate_print_pages("1-3,5", 5);
  rejects([&] { validate_print_pages("3-1", 5); });
  rejects([&] { validate_print_pages("1-6", 5); });
  rejects([&] { validate_print_pages("0", 5); });
  rejects(
      [&] { validate_print_pages("999999999999999999999999999999999999"); });
  rejects([&] { cups_submit(s, "", "title"); });
  rejects([&] { cups_submit(s, "test", "title", 0); });
  rejects([&] { cups_submit(s, "test", "title", 1, "1;touch /tmp/invalid"); });
  std::cout << "PASS native print pagination, Unicode, three document engines, "
               "bounds and output validation\n";
}
