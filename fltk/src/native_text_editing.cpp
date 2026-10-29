// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_editing.h"
#include "native_text_shaper.h"
#include <unicode/ubrk.h>

namespace DomeOffice {
namespace {
size_t previous(const std::vector<size_t> &values, size_t at) {
  auto it = std::lower_bound(values.begin(), values.end(), at);
  return it == values.begin() ? 0 : *--it;
}
size_t next(const std::vector<size_t> &values, size_t at, size_t end) {
  auto it = std::upper_bound(values.begin(), values.end(), at);
  return it == values.end() ? end : *it;
}
} // namespace
void TextEditMap::reset(const TextFlow &flow) {
  text_.clear();
  starts_.clear();
  sizes_.clear();
  graphemes_.clear();
  words_.clear();
  word_starts_.clear();
  for (const auto &paragraph : flow.paragraphs) {
    if (!starts_.empty())
      text_ += '\n';
    starts_.push_back(text_.size());
    sizes_.push_back(paragraph.text.size());
    text_ += paragraph.text;
  }
  if (text_.size() > 16'000'000)
    throw std::runtime_error("Native editing text exceeds limit");
  std::vector<UChar> utf16;
  std::vector<size_t> from16;
  for (size_t i = 0; i < text_.size(); ++i) {
    auto c = uint32_t(text_[i]);
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      throw std::runtime_error("Invalid native editing scalar");
    from16.push_back(i);
    if (c > 0xffff) {
      utf16.push_back(0xd800 + ((c - 0x10000) >> 10));
      from16.push_back(i);
      utf16.push_back(0xdc00 + ((c - 0x10000) & 1023));
    } else
      utf16.push_back(c);
  }
  from16.push_back(text_.size());
  static const UChar empty = 0;
  for (auto type : {UBRK_CHARACTER, UBRK_WORD}) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> iter(
        ubrk_open(type, "und", utf16.empty() ? &empty : utf16.data(),
                  utf16.size(), &status),
        ubrk_close);
    if (U_FAILURE(status))
      throw std::runtime_error("Cannot resolve native editing boundaries");
    int32_t begin = ubrk_first(iter.get());
    if (type == UBRK_CHARACTER)
      graphemes_.push_back(0);
    for (int32_t end = ubrk_next(iter.get()); end != UBRK_DONE;
         begin = end, end = ubrk_next(iter.get())) {
      if (type == UBRK_CHARACTER)
        graphemes_.push_back(from16[end]);
      else {
        words_.emplace_back(from16[begin], from16[end]);
        if (ubrk_getRuleStatus(iter.get()) != UBRK_WORD_NONE)
          word_starts_.push_back(from16[begin]);
      }
    }
  }
}
size_t TextEditMap::global(size_t paragraph, size_t offset) const {
  return starts_.at(paragraph) + std::min(offset, sizes_.at(paragraph));
}
size_t TextEditMap::paragraph_size(size_t paragraph) const {
  return sizes_.at(paragraph);
}
size_t TextEditMap::previous_grapheme(size_t at) const {
  return previous(graphemes_, at);
}
size_t TextEditMap::next_grapheme(size_t at) const {
  return next(graphemes_, at, text_.size());
}
size_t TextEditMap::previous_word(size_t at) const {
  return previous(word_starts_, at);
}
size_t TextEditMap::next_word(size_t at) const {
  return next(word_starts_, at, text_.size());
}
std::pair<size_t, size_t> TextEditMap::word_at(size_t at) const {
  auto it = std::lower_bound(
      words_.begin(), words_.end(), at,
      [](const auto &range, size_t at) { return range.second <= at; });
  return it == words_.end() ? std::pair(text_.size(), text_.size()) : *it;
}
std::vector<TextRect> text_selection_rects(const TextLine &line, size_t begin,
                                           size_t end, bool paragraph_break) {
  std::vector<TextRect> result;
  if (!line.shaped || begin >= end)
    return result;
  auto add = [&](double left, double right) {
    TextRect rect{left, line.y, right - left, line.height};
    if (line.clip) {
      double bottom =
          std::min(rect.y + rect.height, line.clip->y + line.clip->height);
      double far = std::min(right, line.clip->x + line.clip->width);
      rect.x = std::max(rect.x, line.clip->x);
      rect.y = std::max(rect.y, line.clip->y);
      rect.width = far - rect.x;
      rect.height = bottom - rect.y;
    }
    if (rect.width > 0 && rect.height > 0)
      result.push_back(rect);
  };
  size_t visible_end = line.begin;
  for (const auto &cluster : line.shaped->clusters) {
    visible_end = std::max(visible_end, cluster.end);
    for (size_t i = 1; i < cluster.carets.size(); ++i) {
      const auto &a = cluster.carets[i - 1], &b = cluster.carets[i];
      if (a.offset < end && b.offset > begin) {
        double x1 = line.x + cluster.x + a.fraction * cluster.metrics.width;
        double x2 = line.x + cluster.x + b.fraction * cluster.metrics.width;
        add(std::min(x1, x2), std::max(x1, x2));
      }
    }
  }
  if ((paragraph_break || line.end > visible_end) && end > visible_end &&
      begin <= visible_end) {
    std::optional<TextCaret> mark;
    for (const auto &caret : text_line_carets(line))
      if (caret.offset == visible_end &&
          (!mark ||
           (caret.rtl == line.shaped->rtl && mark->rtl != line.shaped->rtl)))
        mark = caret;
    if (mark) {
      double width = std::max(2., line.height / 3);
      add(mark->rect.x - (line.shaped->rtl ? width : 0),
          mark->rect.x + (line.shaped->rtl ? 0 : width));
    }
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.x < b.x; });
  std::vector<TextRect> merged;
  for (const auto &r : result) {
    if (!merged.empty() && r.x <= merged.back().x + merged.back().width + 1e-7)
      merged.back().width =
          std::max(merged.back().x + merged.back().width, r.x + r.width) -
          merged.back().x;
    else
      merged.push_back(r);
  }
  return merged;
}
} // namespace DomeOffice
