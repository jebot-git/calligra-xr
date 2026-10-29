// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_print.h"
#include <FL/Fl_Window.H>
namespace DomeOffice {
struct PrintRequest {
  Text destination, pages;
  int copies = 1;
  PrintSettings settings;
};
std::optional<PrintRequest> print_dialog(Fl_Window &, bool workbook,
                                         int current_sheet);
} // namespace DomeOffice
