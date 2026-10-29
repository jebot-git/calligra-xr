// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "text_documents.h"
#include "workbook.h"
namespace DomeOffice {
struct PrintSettings {
  double width = 595.275590551, height = 841.88976378;
  double margin = 36;
  int sheet = -1; // All worksheets, or one zero-based worksheet.
};
struct PrintResult {
  size_t pages = 0, missing_glyphs = 0;
  std::vector<Text> unsupported;
};
PrintResult print_pdf(const TextDocument &, const Text &,
                      const PrintSettings & = {});
PrintResult print_pdf(const Presentation &, const Text &,
                      const PrintSettings & = {});
PrintResult print_pdf(const Workbook &, const Text &,
                      const PrintSettings & = {});
struct CupsDestination {
  Text name;
  bool is_default = false;
};
std::vector<CupsDestination> cups_destinations();
void validate_print_pages(const Text &, size_t page_count = 0);
Text cups_submit(const Text &pdf, const Text &destination, const Text &title,
                 int copies = 1, const Text &pages = {}, size_t page_count = 0);
} // namespace DomeOffice
