// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_page_canvas.h"
#include "gpu_clip.h"
#include "suite/fltk_appearance.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <cmath>
#include <tuple>
namespace DomeOffice {
TextPageCanvas::TextPageCanvas(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h), glyphs_(std::make_unique<GpuTextCache>()) {}
TextPageCanvas::~TextPageCanvas() { stop_drag(); }
uint64_t TextPageCanvas::rasterizations() const {
  return glyphs_->rasterizations();
}
void TextPageCanvas::set_flow(const TextFlow &flow) {
  if (!shaper_ || shaping_revision_ != dx::font_revision) {
    shaper_ = std::make_unique<NativeTextShaper>(
        Text(dx::font_name.substr(0, dx::font_name.find(" / "))), dx::size);
    shaping_revision_ = dx::font_revision;
  }
  TextEditMap map;
  map.reset(flow);
  auto pages = paginate_text(flow, *shaper_);
  edit_map_ = std::move(map);
  set_pagination(std::move(pages));
}
void TextPageCanvas::set_pagination(TextPagination value) {
  pagination = std::move(value);
  render_error_.clear();
  page_width_ = 1;
  page_offsets_.assign(1, 0);
  for (const auto &page : pagination.pages) {
    page_width_ = std::max(page_width_, page.style.width);
    page_offsets_.push_back(page_offsets_.back() + page.style.height);
  }
  scroll_ = std::clamp(scroll_, 0., std::max(0., extent() - h()));
  lines_.clear();
  for (size_t page = 0; page < pagination.pages.size(); ++page)
    for (size_t index = 0; index < pagination.pages[page].lines.size();
         ++index) {
      const auto &line = pagination.pages[page].lines[index];
      Line record{page, index, {}};
      for (auto caret : text_line_carets(line))
        record.stops.push_back(
            {caret, page, index,
             edit_map_.global(caret.paragraph, caret.offset)});
      lines_.push_back(std::move(record));
    }
  stop_index_.clear();
  for (size_t line = 0; line < lines_.size(); ++line)
    for (size_t stop = 0; stop < lines_[line].stops.size(); ++stop)
      stop_index_.push_back({lines_[line].stops[stop].global, line, stop});
  std::sort(stop_index_.begin(), stop_index_.end());
  redraw();
}
std::optional<TextRect> TextPageCanvas::caret_rect() const {
  if (editing())
    if (auto stop = cursor())
      return screen_rect(*stop);
  return {};
}
std::optional<size_t> TextPageCanvas::caret_page() const {
  if (editing())
    if (auto stop = cursor())
      return stop->page;
  return {};
}
void TextPageCanvas::reset_editing() {
  stop_drag();
  edit_map_ = {};
  set_pagination({});
  preferred_page_ = preferred_line_ = 0;
  affinity_ = TextCaret::Following;
  preferred_x_.reset();
  scroll_ = 0;
  redraw();
}
void TextPageCanvas::selection_changed(bool reveal) {
  if (editing() && reveal)
    reveal_caret();
  redraw();
}
double TextPageCanvas::scale() const {
  return std::clamp((w() - 48) / page_width_, .05, 1.5);
}
double TextPageCanvas::extent() const {
  return 24 + (page_offsets_.empty() ? 0 : page_offsets_.back()) * scale() +
         pagination.pages.size() * 24;
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
  auto *image_host = dynamic_cast<Fl_Gl_Window *>(window());
  if (image_host)
    images.begin_frame(*image_host);
  double top = y() + 12 - scroll_, zoom = scale();
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    const auto &page = pagination.pages[i];
    double left = x() + (w() - page.style.width * zoom) / 2;
    double height = page.style.height * zoom;
    if (top + height >= y() && top <= y() + h()) {
      // Paper and explicit ink colors belong to the document; controls keep
      // the session theme. Composite transparent page colors over white paper.
      const auto color = page.style.background;
      auto channel = [&](unsigned char c) {
        return static_cast<unsigned char>(
            std::lround(c * color.alpha + 255 * (1 - color.alpha)));
      };
      fl_color(fl_rgb_color(channel(color.red), channel(color.green),
                            channel(color.blue)));
      fl_rectf(int(left), int(top), int(page.style.width * zoom), int(height));
      fl_color(FL_INACTIVE_COLOR);
      fl_rect(int(left), int(top), int(page.style.width * zoom), int(height));
      try {
        auto *host = dynamic_cast<Fl_Gl_Window *>(window());
        unsigned char r, g, b;
        Fl::get_color(FL_FOREGROUND_COLOR, r, g, b);
        if (host)
          glyphs_->draw_paints(page.paints, {zoom, 0, 0, zoom, left, top},
                               *host);
        for (const auto &line : page.lines) {
          if (top + (line.y + line.height) * zoom < y() ||
              top + line.y * zoom > y() + h())
            continue;
          if (!line.shaped || !host)
            throw std::runtime_error("Native page glyph positions are missing");
          if (editing() && selection) {
            auto range = selection();
            auto begin = std::min(range.anchor, range.cursor),
                 end = std::max(range.anchor, range.cursor);
            size_t base = edit_map_.global(line.paragraph, 0);
            size_t length = edit_map_.paragraph_size(line.paragraph);
            if (end > base && begin <= base + length) {
              auto rectangles = text_selection_rects(
                  line, begin > base ? begin - base : 0,
                  end > base ? end - base : 0,
                  line.end == length && end > base + length);
              unsigned char sr, sg, sb;
              Fl::get_color(FL_SELECTION_COLOR, sr, sg, sb);
              std::vector<TextPaint> paints;
              for (auto rect : rectangles) {
                if (Fl::compose_state) {
                  rect.y = line.baseline + 1;
                  rect.height = 1;
                }
                paints.push_back({rect,
                                  {sr, sg, sb, Fl::compose_state ? 1. : .4},
                                  TextPaint::Fill,
                                  false,
                                  line.clip});
              }
              glyphs_->draw_paints(paints, {zoom, 0, 0, zoom, left, top},
                                   *host);
            }
          }
          SceneTransform transform{zoom,
                                   0,
                                   0,
                                   zoom,
                                   left + line.x * zoom,
                                   top + line.baseline * zoom};
          glyphs_->draw(*line.shaped, transform, {r, g, b}, *host,
                        relative_text_clip(line.clip, line.x, line.baseline));
          glyphs_->draw_tabs(
              line.tabs, transform, {r, g, b}, *host,
              relative_text_clip(line.clip, line.x, line.baseline));
          if (line.label && line.label->shaped) {
            transform.e = left + line.label->x * zoom;
            glyphs_->draw(
                *line.label->shaped, transform, {r, g, b}, *host,
                relative_text_clip(line.clip, line.label->x, line.baseline));
          }
          if (line.label && line.label->image) {
            transform.e = left + line.label->x * zoom;
            transform.f = top + (line.baseline + line.label->y) * zoom;
            images.draw(line.label->image->asset, transform, line.label->width,
                        line.label->height, *host,
                        relative_text_clip(line.clip, line.label->x,
                                           line.baseline + line.label->y));
          }
        }
      } catch (const std::exception &error) {
        render_error_ = error.what();
        fl_font(dx::font, dx::size);
        fl_color(FL_FOREGROUND_COLOR);
        fl_draw("This page cannot be displayed yet.", x() + 24, y() + 24);
      }
      fl_font(dx::font, dx::size);
      fl_color(FL_FOREGROUND_COLOR);
      auto label = std::to_string(i + 1) + " / " +
                   std::to_string(pagination.pages.size());
      fl_draw(label.c_str(), int(left), int(top + height + 17));
    }
    top += height + 24;
  }
  if (editing() && Fl::focus() == this && selection) {
    auto range = selection();
    if (range.anchor == range.cursor || Fl::compose_state)
      if (auto stop = cursor()) {
        auto rect = screen_rect(*stop);
        if (rect.y + rect.height >= y() && rect.y < y() + h()) {
          fl_color(fl_contrast(FL_FOREGROUND_COLOR, FL_WHITE));
          fl_rectf(int(std::round(rect.x)), int(std::round(rect.y)), 2,
                   std::max(1, int(std::round(rect.height))));
          fl_set_spot(dx::font, std::max(1, int(std::round(rect.height))),
                      int(rect.x), int(rect.y + rect.height), 2,
                      std::max(1, int(rect.height)), window());
        }
      }
  }
  fl_pop_clip();
}
TextRect TextPageCanvas::screen_rect(const Stop &stop) const {
  double zoom = scale(), top = y() + 12 - scroll_ +
                               page_offsets_.at(stop.page) * zoom +
                               stop.page * 24;
  double left =
      x() + (w() - pagination.pages.at(stop.page).style.width * zoom) / 2;
  auto r = stop.caret.rect;
  return {left + r.x * zoom, top + r.y * zoom, 2, r.height * zoom};
}
std::optional<TextPageCanvas::Stop> TextPageCanvas::cursor() const {
  if (!selection)
    return {};
  size_t at = selection().cursor;
  std::optional<Stop> best;
  auto rank = [&](const Stop &s) {
    return std::tuple(std::abs(double(s.page) - preferred_page_),
                      s.caret.affinity != affinity_,
                      std::abs(double(s.line) - preferred_line_));
  };
  auto first =
      std::lower_bound(stop_index_.begin(), stop_index_.end(), at,
                       [](const auto &ref, size_t at) { return ref[0] < at; });
  for (auto it = first; it != stop_index_.end() && (*it)[0] == at; ++it) {
    const auto &stop = lines_[(*it)[1]].stops[(*it)[2]];
    if (!best || rank(stop) < rank(*best))
      best = stop;
  }
  return best;
}
std::optional<TextPageCanvas::Stop> TextPageCanvas::hit(double sx, double sy,
                                                        bool closest) const {
  double zoom = scale(), top = y() + 12 - scroll_;
  for (size_t i = 0; i < pagination.pages.size(); ++i) {
    const auto &page = pagination.pages[i];
    double left = x() + (w() - page.style.width * zoom) / 2;
    double px = (sx - left) / zoom, py = (sy - top) / zoom;
    if (px >= 0 && px < page.style.width && py >= 0 && py < page.style.height)
      if (auto target = text_caret_at(page, px, py))
        return Stop{
            target->caret, i, target->line,
            edit_map_.global(target->caret.paragraph, target->caret.offset)};
    top += page.style.height * zoom + 24;
  }
  if (!closest)
    return {};
  std::optional<Stop> best;
  double distance = INFINITY;
  for (const auto &line : lines_)
    for (const auto &stop : line.stops) {
      auto r = screen_rect(stop);
      double dy = sy < r.y              ? r.y - sy
                  : sy > r.y + r.height ? sy - r.y - r.height
                                        : 0;
      double d = dy * dy + (sx - r.x) * (sx - r.x);
      if (d < distance) {
        distance = d;
        best = stop;
      }
    }
  return best;
}
void TextPageCanvas::choose(Stop stop, bool extend) {
  preferred_page_ = stop.page;
  preferred_line_ = stop.line;
  affinity_ = stop.caret.affinity;
  preferred_x_.reset();
  if (select)
    select(
        {extend && selection ? selection().anchor : stop.global, stop.global});
  selection_changed();
}
void TextPageCanvas::reveal_caret() {
  if (auto stop = cursor()) {
    auto rect = screen_rect(*stop);
    if (rect.y < y() + 12)
      scroll(rect.y - y() - 12);
    else if (rect.y + rect.height > y() + h() - 12)
      scroll(rect.y + rect.height - y() - h() + 12);
  }
}
bool TextPageCanvas::navigate(int key, bool control, bool extend) {
  auto current = cursor();
  if (!current)
    return false;
  auto choose_global = [&](size_t position, bool document_boundary = false) {
    std::optional<Stop> best;
    auto rank = [&](const Stop &s) {
      bool rtl = pagination.pages[s.page].lines[s.line].shaped->rtl;
      double page_distance =
          document_boundary
              ? (key == FL_Home ? double(s.page)
                                : double(pagination.pages.size() - 1 - s.page))
              : std::abs(double(s.page) - current->page);
      return std::tuple(page_distance, s.caret.rtl != rtl,
                        std::abs(double(s.line) - current->line));
    };
    auto first = std::lower_bound(
        stop_index_.begin(), stop_index_.end(), position,
        [](const auto &ref, size_t at) { return ref[0] < at; });
    for (auto it = first; it != stop_index_.end() && (*it)[0] == position;
         ++it) {
      const auto &s = lines_[(*it)[1]].stops[(*it)[2]];
      if (!best || rank(s) < rank(*best))
        best = s;
    }
    if (best)
      choose(*best, extend);
    return bool(best);
  };
  if (control && (key == FL_Home || key == FL_End)) {
    bool moved =
        choose_global(key == FL_Home ? 0 : edit_map_.text().size(), true);
    if (moved && key == FL_Home)
      scroll(-extent());
    return moved;
  }
  if (control && (key == FL_Left || key == FL_Right))
    return choose_global(key == FL_Left
                             ? edit_map_.previous_word(current->global)
                             : edit_map_.next_word(current->global));
  if (control && (key == FL_Up || key == FL_Down)) {
    auto paragraph = current->caret.paragraph;
    if (key == FL_Down)
      return choose_global(paragraph + 1 < edit_map_.paragraph_count()
                               ? edit_map_.global(paragraph + 1, 0)
                               : edit_map_.text().size());
    size_t start = edit_map_.global(paragraph, 0);
    return choose_global(current->global == start && paragraph > 0
                             ? edit_map_.global(paragraph - 1, 0)
                             : start);
  }
  auto found = std::find_if(lines_.begin(), lines_.end(), [&](const Line &l) {
    return l.page == current->page && l.index == current->line;
  });
  if (found == lines_.end() || found->stops.empty())
    return false;
  if (key == FL_Home || key == FL_End) {
    size_t position = found->stops.front().global;
    for (const auto &s : found->stops)
      position = key == FL_Home ? std::min(position, s.global)
                                : std::max(position, s.global);
    std::optional<Stop> best;
    auto desired = key == FL_Home ? TextCaret::Following : TextCaret::Preceding;
    bool base_rtl =
        pagination.pages[found->page].lines[found->index].shaped->rtl;
    auto rank = [&](const Stop &s) {
      return std::pair(s.caret.rtl != base_rtl, s.caret.affinity != desired);
    };
    for (const auto &s : found->stops)
      if (s.global == position && (!best || rank(s) < rank(*best)))
        best = s;
    if (best)
      choose(*best, extend);
    return true;
  }
  if (key == FL_Left || key == FL_Right) {
    if (selection && selection().anchor != selection().cursor && !extend) {
      auto range = selection();
      bool start =
          (key == FL_Left) !=
          pagination.pages[current->page].lines[current->line].shaped->rtl;
      return choose_global(start ? std::min(range.anchor, range.cursor)
                                 : std::max(range.anchor, range.cursor));
    }
    const bool right = key == FL_Right;
    double next_x = right ? INFINITY : -INFINITY;
    for (const auto &s : found->stops)
      if ((right && s.caret.rect.x > current->caret.rect.x + 1e-7 &&
           s.caret.rect.x < next_x) ||
          (!right && s.caret.rect.x < current->caret.rect.x - 1e-7 &&
           s.caret.rect.x > next_x))
        next_x = s.caret.rect.x;
    if (std::isfinite(next_x)) {
      const auto &line = pagination.pages[current->page].lines[current->line];
      double middle = (current->caret.rect.x + next_x) / 2 - line.x;
      bool rtl = line.shaped->rtl;
      for (const auto &c : line.shaped->clusters)
        if (middle >= c.x && middle < c.x + c.metrics.width) {
          rtl = c.rtl;
          break;
        }
      std::optional<Stop> best;
      auto rank = [&](const Stop &s) {
        auto affinity =
            right == rtl ? TextCaret::Following : TextCaret::Preceding;
        return std::pair(s.caret.rtl != rtl, s.caret.affinity != affinity);
      };
      for (const auto &s : found->stops)
        if (std::abs(s.caret.rect.x - next_x) < 1e-7 &&
            (!best || rank(s) < rank(*best)))
          best = s;
      if (best)
        choose(*best, extend);
    } else {
      bool forward =
          right !=
          pagination.pages[found->page].lines[found->index].shaped->rtl;
      auto target = found;
      if (forward) {
        if (++target == lines_.end())
          return true;
      } else {
        if (target == lines_.begin())
          return true;
        --target;
      }
      if (!target->stops.empty()) {
        size_t position = target->stops.front().global;
        for (const auto &s : target->stops)
          position = forward ? std::min(position, s.global)
                             : std::max(position, s.global);
        bool base_rtl =
            pagination.pages[target->page].lines[target->index].shaped->rtl;
        std::optional<Stop> best;
        for (const auto &s : target->stops)
          if (s.global == position && (!best || (s.caret.rtl == base_rtl &&
                                                 best->caret.rtl != base_rtl)))
            best = s;
        if (best)
          choose(*best, extend);
      }
    }
    return true;
  }
  bool down = key == FL_Down || key == FL_Page_Down;
  auto rect = screen_rect(*current);
  double desired = preferred_x_.value_or(rect.x);
  const double zoom = scale();
  auto page_top = [&](size_t page) {
    return y() + 12 - scroll_ + page_offsets_[page] * zoom + page * 24;
  };
  const auto &current_line =
      pagination.pages[current->page].lines[current->line];
  double line_top = page_top(current->page) + current_line.y * zoom;
  double center = line_top + current_line.height * zoom / 2;
  double target_y = center + ((key == FL_Page_Down || key == FL_Page_Up)
                                  ? (down ? 1 : -1) * h() * .85
                                  : 0);
  auto distance =
      std::tuple<double, double, bool, bool>{INFINITY, INFINITY, true, true};
  std::optional<Stop> best;
  for (const auto &l : lines_) {
    if (l.page == current->page && l.index == current->line)
      continue;
    const auto &candidate_line = pagination.pages[l.page].lines[l.index];
    double candidate_top = page_top(l.page) + candidate_line.y * zoom;
    double cy = candidate_top + candidate_line.height * zoom / 2;
    if ((down && candidate_top <= line_top + 1e-7) ||
        (!down && candidate_top >= line_top - 1e-7))
      continue;
    // Find the adjacent visual row before choosing its horizontal stop.
    // A large indent must not make an arrow skip an intervening line. Compute
    // row positions once so different glyph ascents cannot perturb ties.
    double vertical = key == FL_Page_Down || key == FL_Page_Up
                          ? std::abs(cy - target_y)
                          : std::abs(candidate_top - line_top);
    for (const auto &s : l.stops) {
      auto r = screen_rect(s);
      auto d = std::tuple(vertical, std::abs(r.x - desired),
                          s.caret.rtl != candidate_line.shaped->rtl,
                          s.caret.affinity != affinity_);
      if (d < distance) {
        distance = d;
        best = s;
      }
    }
  }
  if (best) {
    choose(*best, extend);
    preferred_x_ = desired;
  }
  return true;
}
void TextPageCanvas::stop_drag() {
  dragging_ = false;
  Fl::remove_timeout(drag_scroll, this);
}
double TextPageCanvas::drag_scroll_amount() const {
  if (drag_y_ < y() + 12)
    return -std::clamp((y() + 12 - drag_y_) * .5, 12., 64.);
  if (drag_y_ > y() + h() - 12)
    return std::clamp((drag_y_ - y() - h() + 12) * .5, 12., 64.);
  return 0;
}
void TextPageCanvas::drag_scroll(void *data) {
  auto &canvas = *static_cast<TextPageCanvas *>(data);
  if (!canvas.dragging_ || !canvas.visible_r() || !canvas.active_r())
    return;
  double amount = canvas.drag_scroll_amount(), before = canvas.scroll_;
  if (!amount)
    return;
  canvas.scroll(amount);
  if (canvas.scroll_ != before) {
    if (auto target = canvas.hit(canvas.drag_x_, canvas.drag_y_, true))
      canvas.choose(*target, true);
    if (canvas.dragging_)
      Fl::repeat_timeout(.05, drag_scroll, data);
  }
}
int TextPageCanvas::handle(int event) {
  if (event == FL_FOCUS || event == FL_UNFOCUS) {
    if (event == FL_UNFOCUS) {
      stop_drag();
      if (cancel_composition)
        cancel_composition();
      fl_reset_spot();
    }
    redraw();
    return 1;
  }
  if (event == FL_MOUSEWHEEL) {
    scroll(Fl::event_dy() * 48);
    return 1;
  }
  if (event == FL_PUSH) {
    stop_drag();
    if (cancel_composition)
      cancel_composition();
    take_focus();
    if (Fl::event_button() == FL_RIGHT_MOUSE) {
      if (input)
        input(event);
      take_focus();
      redraw();
      return 1;
    }
    if (auto target = hit(Fl::event_x(), Fl::event_y())) {
      choose(*target, Fl::event_state() & FL_SHIFT);
      dragging_ = Fl::event_button() == FL_LEFT_MOUSE;
      if (Fl::event_clicks() && Fl::event_button() == FL_LEFT_MOUSE && select) {
        if (Fl::event_clicks() >= 2) {
          size_t begin = edit_map_.global(target->caret.paragraph, 0);
          select({begin, std::min(edit_map_.text().size(),
                                  begin +
                                      edit_map_.paragraph_size(
                                          target->caret.paragraph) +
                                      1)});
        } else {
          size_t sample = target->global;
          if ((Fl::event_x() < screen_rect(*target).x) != target->caret.rtl)
            sample = edit_map_.previous_grapheme(sample);
          auto word = edit_map_.word_at(sample);
          select({word.first, word.second});
        }
        selection_changed();
      }
      if (Fl::event_button() == FL_MIDDLE_MOUSE) {
        Fl::paste(*this, 0);
        dragging_ = false;
      }
    }
    return 1;
  }
  if (event == FL_DRAG && dragging_) {
    drag_x_ = Fl::event_x();
    drag_y_ = Fl::event_y();
    Fl::remove_timeout(drag_scroll, this);
    scroll(drag_scroll_amount());
    if (auto target = hit(Fl::event_x(), Fl::event_y(), true))
      choose(*target, true);
    if (drag_scroll_amount())
      Fl::add_timeout(.05, drag_scroll, this);
    return 1;
  }
  if (event == FL_RELEASE) {
    stop_drag();
    return 1;
  }
  if (event == FL_KEYDOWN) {
    auto key = Fl::event_key();
    bool control = Fl::event_state() & FL_CTRL,
         extend = Fl::event_state() & FL_SHIFT;
    if (key == FL_Escape) {
      if (cancel_composition)
        cancel_composition();
      stop_drag();
      if (selection && select) {
        auto at = selection().cursor;
        select({at, at});
      }
      redraw();
      return 1;
    }
    bool movement = key == FL_Left || key == FL_Right || key == FL_Up ||
                    key == FL_Down || key == FL_Home || key == FL_End ||
                    key == FL_Page_Up || key == FL_Page_Down;
    if (movement) {
      if (cancel_composition)
        cancel_composition();
      navigate(key, control, extend);
      return 1;
    }
    if (key == FL_BackSpace || (key == FL_Delete && !extend)) {
      if (cancel_composition)
        cancel_composition();
      if (selection && erase) {
        auto range = selection();
        auto begin = std::min(range.anchor, range.cursor),
             end = std::max(range.anchor, range.cursor);
        if (begin == end) {
          if (key == FL_BackSpace)
            begin = control ? edit_map_.previous_word(begin)
                            : edit_map_.previous_grapheme(begin);
          else
            end = control ? edit_map_.next_word(end)
                          : edit_map_.next_grapheme(end);
        }
        erase(begin, end);
        selection_changed();
        return 1;
      }
    }
    if (input) {
      auto handled = input(event);
      selection_changed();
      return handled;
    }
  }
  if (event == FL_PASTE && input) {
    auto handled = input(event);
    selection_changed();
    return handled;
  }
  return Fl_Widget::handle(event);
}
} // namespace DomeOffice
