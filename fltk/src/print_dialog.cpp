// SPDX-License-Identifier: GPL-2.0-or-later
#include "print_dialog.h"
#include "suite/fltk_appearance.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_ask.H>
namespace DomeOffice {
namespace {
class PrintPanel : public Fl_Group {
public:
  bool done = false, accepted = false;
  using Fl_Group::Fl_Group;
  int handle(int event) override {
    if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
      done = true;
      return 1;
    }
    // Keep input inside this modal panel, including clicks outside its
    // controls.
    if (Fl_Group::handle(event))
      return 1;
    return event == FL_PUSH || event == FL_DRAG || event == FL_RELEASE ||
           event == FL_MOUSEWHEEL || event == FL_KEYDOWN ||
           event == FL_SHORTCUT;
  }
};
} // namespace
std::optional<PrintRequest> print_dialog(Fl_Window &window, bool workbook,
                                         int current_sheet) {
  auto destinations = cups_destinations();
  if (destinations.empty()) {
    fl_message("No CUPS printers are configured. Add a printer to CUPS, then "
               "try Print again.");
    return {};
  }
  int width = 510, height = workbook ? 320 : 270;
  int x = std::max(0, (window.w() - width) / 2),
      y = std::max(0, (window.h() - height) / 2);
  auto previous = Fl_Group::current();
  auto previous_focus = Fl::focus();
  window.begin();
  PrintPanel panel(0, 0, window.w(), window.h());
  Fl_Box backdrop(0, 0, window.w(), window.h());
  backdrop.box(FL_FLAT_BOX);
  backdrop.color(FL_BACKGROUND_COLOR);
  Fl_Box border(x, y, width, height);
  border.box(FL_BORDER_BOX);
  border.color(FL_BACKGROUND_COLOR);
  Fl_Choice printer(x + 135, y + 20, 345, 30, "Printer:");
  int preferred = 0;
  std::vector<std::string> names;
  names.reserve(destinations.size());
  std::vector<Fl_Menu_Item> items(destinations.size() + 1);
  for (size_t i = 0; i < destinations.size(); ++i) {
    names.push_back(destinations[i].name.toStdString());
    if (destinations[i].is_default)
      preferred = int(i);
  }
  for (size_t i = 0; i < names.size(); ++i)
    items[i].text = names[i].c_str();
  printer.copy(items.data());
  printer.value(preferred);
  Fl_Int_Input copies(x + 135, y + 65, 80, 30, "Copies:");
  copies.value("1");
  Fl_Input pages(x + 135, y + 110, 345, 30, "Pages:");
  pages.tooltip("Leave empty for all pages, or enter a range such as 1-3,5");
  Fl_Box hint(x + 135, y + 142, 345, 25, "All pages, or a range such as 1-3,5");
  hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  Fl_Choice scope(x + 135, y + 180, 345, 30, "Worksheets:");
  scope.add("All worksheets|Current worksheet");
  scope.value(0);
  if (!workbook)
    scope.hide();
  Fl_Button cancel(x + 285, y + height - 50, 90, 30, "Cancel");
  Fl_Return_Button submit(x + 390, y + height - 50, 90, 30, "Print");
  cancel.callback(
      [](Fl_Widget *, void *p) { static_cast<PrintPanel *>(p)->done = true; },
      &panel);
  submit.callback(
      [](Fl_Widget *, void *p) {
        auto *s = static_cast<PrintPanel *>(p);
        s->accepted = true;
        s->done = true;
      },
      &panel);
  panel.end();
  window.end();
  Fl_Group::current(previous);
  for (int i = 0; i < panel.children(); ++i) {
    panel.child(i)->labelfont(dx::font);
    panel.child(i)->labelsize(dx::size);
  }
  printer.textfont(dx::font);
  printer.textsize(dx::size);
  copies.textfont(dx::font);
  copies.textsize(dx::size);
  pages.textfont(dx::font);
  pages.textsize(dx::size);
  scope.textfont(dx::font);
  scope.textsize(dx::size);
  panel.show();
  copies.take_focus();
  window.redraw();
  while (!panel.done && window.shown())
    Fl::wait();
  panel.hide();
  window.redraw();
  if (previous_focus)
    previous_focus->take_focus();
  if (!panel.accepted || !window.shown())
    return {};
  bool valid = false;
  int count = Text(copies.value()).toInt(&valid);
  if (!valid || count < 1 || count > 999)
    throw std::runtime_error("Enter a copy count between 1 and 999");
  PrintRequest request;
  request.destination = destinations.at(printer.value()).name;
  request.copies = count;
  request.pages = Text(pages.value()).trimmed();
  if (workbook && scope.value() == 1)
    request.settings.sheet = current_sheet;
  return request;
}
} // namespace DomeOffice
