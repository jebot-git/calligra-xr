// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_page_canvas.h"
#include "suite/fltk_appearance.h"
#include <FL/fl_draw.H>
#include <algorithm>
namespace DomeOffice {
TextPageCanvas::TextPageCanvas(int x,int y,int w,int h):Fl_Widget(x,y,w,h),glyphs_(std::make_unique<GpuTextCache>()){}
TextPageCanvas::~TextPageCanvas()=default;
uint64_t TextPageCanvas::rasterizations()const{return glyphs_->rasterizations();}
void TextPageCanvas::set_flow(const TextFlow &flow) {
  if (!shaper_ || shaping_revision_ != dx::font_revision) {
    shaper_ = std::make_unique<NativeTextShaper>(
        Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
    shaping_revision_ = dx::font_revision;
  }
  set_pagination(paginate_text(flow, *shaper_));
}
void TextPageCanvas::set_pagination(TextPagination value) {
  pagination = std::move(value);
  render_error_.clear();
  scroll_ = 0;
  redraw();
}
double TextPageCanvas::scale() const {
  double width = 1;
  for (const auto &page : pagination.pages)
    width = std::max(width, page.style.width);
  return std::clamp((w() - 48) / width, .05, 1.5);
}
double TextPageCanvas::extent() const {
  double height = 24, zoom = scale();
  for (const auto &page : pagination.pages)
    height += page.style.height * zoom + 24;
  return height;
}
void TextPageCanvas::scroll(double amount) {
  scroll_ = std::clamp(scroll_ + amount, 0., std::max(0., extent() - h()));
  redraw();
}
size_t TextPageCanvas::current_page() const {
  double top = 12 - scroll_, zoom = scale();
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    top += pagination.pages[i].style.height * zoom + 24;
    if (top > h() / 2.)
      return i;
  }
  return pagination.pages.empty() ? 0 : pagination.pages.size() - 1;
}
void TextPageCanvas::draw() {
  fl_push_clip(x(), y(), w(), h());
  fl_color(FL_BACKGROUND_COLOR);
  fl_rectf(x(), y(), w(), h());
  double top = y() + 12 - scroll_, zoom = scale();
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    const auto &page = pagination.pages[i];
    double left = x() + (w() - page.style.width * zoom) / 2;
    double height = page.style.height * zoom;
    if (top + height >= y() && top <= y() + h()) {
      // Session colors apply to page, text and border as well as controls.
      fl_color(FL_BACKGROUND2_COLOR);
      fl_rectf(int(left), int(top), int(page.style.width * zoom), int(height));
      fl_color(FL_INACTIVE_COLOR);
      fl_rect(int(left), int(top), int(page.style.width * zoom), int(height));
      try {
        auto *host=dynamic_cast<Fl_Gl_Window *>(window());
        unsigned char r,g,b;Fl::get_color(FL_FOREGROUND_COLOR,r,g,b);
        for(const auto &line:page.lines) {
          if(top+(line.y+line.height)*zoom<y()||top+line.y*zoom>y()+h())continue;
          if(!line.shaped||!host)throw std::runtime_error("Native page glyph positions are missing");
          SceneTransform transform{zoom,0,0,zoom,left+line.x*zoom,top+line.baseline*zoom};
          glyphs_->draw(*line.shaped,transform,{r,g,b},*host);
        }
      } catch (const std::exception &error) {
        render_error_ = error.what();
        fl_font(dx::font, dx::size);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw("This page cannot be displayed yet.", x() + 24, y() + 24);
      }
      fl_font(dx::font, dx::size);
      fl_color(FL_FOREGROUND_COLOR);
      auto label = std::to_string(i + 1) + " / " + std::to_string(pagination.pages.size());
      fl_draw(label.c_str(), int(left), int(top + height + 17));
    }
    top += height + 24;
  }
  fl_pop_clip();
}
int TextPageCanvas::handle(int event) {
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_MOUSEWHEEL) {
    scroll(Fl::event_dy() * 48);
    return 1;
  }
  if (event == FL_KEYDOWN) {
    auto key = Fl::event_key();
    if (key == FL_Page_Down || key == ' ') {
      scroll(h() * .85);
      return 1;
    }
    if (key == FL_Page_Up) {
      scroll(-h() * .85);
      return 1;
    }
    if (key == FL_Down) {
      scroll(48);
      return 1;
    }
    if (key == FL_Up) {
      scroll(-48);
      return 1;
    }
    if (key == FL_Home) {
      scroll(-extent());
      return 1;
    }
    if (key == FL_End) {
      scroll(extent());
      return 1;
    }
  }
  if (event == FL_PUSH) {
    take_focus();
    if (Fl::event_clicks() && edit_paragraph) {
      double top = y() + 12 - scroll_, zoom = scale();
      for (const auto &page : pagination.pages) {
        for (const auto &line : page.lines)
          if (Fl::event_y() >= top + line.y * zoom &&
              Fl::event_y() <= top + (line.y + line.height) * zoom) {
            edit_paragraph(line.paragraph);
            return 1;
          }
        top += page.style.height * zoom + 24;
      }
    }
    return 1;
  }
  return Fl_Widget::handle(event);
}
} // namespace DomeOffice
