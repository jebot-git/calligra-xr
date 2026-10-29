// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_text_shaper.h"
#include "native_font_family.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <atomic>
#include <cmath>
#include <hb-ft.h>
#include <hb.h>
#include <set>
#include <tuple>
#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
struct Library {
  FT_Library ft = nullptr;
  Library() {
    if (FT_Init_FreeType(&ft))
      fail("Cannot initialize native text fonts");
  }
  ~Library() { FT_Done_FreeType(ft); }
};
bool ignorable(uint32_t c) {
  return u_hasBinaryProperty(c, UCHAR_DEFAULT_IGNORABLE_CODE_POINT) ||
         c == '\n' || c == '\r' || c == '\t' || c == 0x2028 || c == 0x2029;
}
bool hard(uint32_t c) {
  return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}
int weight(const TextFont &font) {
  return font.bold && font.weight < 600 ? 700 : font.weight;
}
bool valid_font(const TextFont &font) {
  return std::isfinite(font.size) && font.size > 0 && font.size <= 1000 &&
         font.weight >= 1 && font.weight <= 1000 &&
         font.family.size() <= 1024 && font.language.size() <= 128;
}
bool valid_color(const std::optional<TextColor> &color) {
  return !color || (std::isfinite(color->alpha) && color->alpha >= 0 &&
                    color->alpha <= 1);
}
void size_face(FT_Face face, unsigned pixels) {
  if (FT_IS_SCALABLE(face)) {
    if (FT_Set_Pixel_Sizes(face, 0, pixels))
      fail("Cannot size native document font");
  } else {
    if (!face->num_fixed_sizes)
      fail("Document font has no usable size");
    int best = 0;
    for (int i = 1; i < face->num_fixed_sizes; ++i)
      if (std::abs(face->available_sizes[i].y_ppem / 64. - pixels) <
          std::abs(face->available_sizes[best].y_ppem / 64. - pixels))
        best = i;
    if (FT_Select_Size(face, best))
      fail("Cannot select document bitmap font size");
  }
}
void variations(FT_Library library, FT_Face face, const TextFont &font) {
  if (!FT_HAS_MULTIPLE_MASTERS(face))
    return;
  FT_MM_Var *axes = nullptr;
  if (FT_Get_MM_Var(face, &axes))
    return;
  std::vector<FT_Fixed> coords(axes->num_axis);
  if (FT_Get_Var_Design_Coordinates(face, axes->num_axis, coords.data()))
    for (FT_UInt i = 0; i < axes->num_axis; ++i)
      coords[i] = axes->axis[i].def;
  for (FT_UInt i = 0; i < axes->num_axis; ++i) {
    const auto &axis = axes->axis[i];
    if (axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
      coords[i] = weight(font) * 65536L;
    if (axis.tag == FT_MAKE_TAG('i', 't', 'a', 'l'))
      coords[i] = font.italic ? 65536L : 0;
    if (axis.tag == FT_MAKE_TAG('s', 'l', 'n', 't'))
      coords[i] = font.italic ? -12 * 65536L : 0;
    coords[i] = std::clamp(coords[i], axis.minimum, axis.maximum);
  }
  auto error =
      FT_Set_Var_Design_Coordinates(face, axes->num_axis, coords.data());
  FT_Done_MM_Var(library, axes);
  if (error)
    fail("Cannot configure variable document font");
}
struct BidiDelete {
  void operator()(UBiDi *p) const { ubidi_close(p); }
};
using Bidi = std::unique_ptr<UBiDi, BidiDelete>;
struct BufferDelete {
  void operator()(hb_buffer_t *p) const { hb_buffer_destroy(p); }
};
using Buffer = std::unique_ptr<hb_buffer_t, BufferDelete>;
} // namespace
struct NativeTypeface::Impl {
  std::shared_ptr<Library> library;
  FT_Face shaping = nullptr, raster = nullptr;
  hb_font_t *hb = nullptr;
  double units = 1;
  TextMetrics metrics;
  ~Impl() {
    if (hb)
      hb_font_destroy(hb);
    if (shaping)
      FT_Done_Face(shaping);
    if (raster)
      FT_Done_Face(raster);
  }
};
struct NativeTextShaper::Impl {
  std::shared_ptr<Library> library = std::make_shared<Library>();
  FcConfig *config = nullptr;
  Text family;
  int bitmap_pixels;
  using Request = std::tuple<Text, int, bool, Text>;
  std::map<Request, std::shared_ptr<NativeTypeface>> primary;
  using FaceKey = std::tuple<std::string, int, int, bool>;
  std::map<FaceKey, std::weak_ptr<NativeTypeface>> faces;
  std::map<std::pair<Request, Text>, std::shared_ptr<NativeTypeface>> fallback;
  Impl(const Text &preferred, int pixels)
      : family(preferred), bitmap_pixels(pixels) {
    if (pixels < 1 || pixels > 256 || preferred.size() > 1024)
      fail("Invalid document font defaults");
    config = FcConfigCreate();
    // Explicitly load the system font config into a private object. The FLTK
    // session's FONTCONFIG_FILE and Pango adapters remain untouched.
    if (!config ||
        !FcConfigParseAndLoad(
            config, reinterpret_cast<const FcChar8 *>("/etc/fonts/fonts.conf"),
            false) ||
        !FcConfigBuildFonts(config)) {
      if (config)
        FcConfigDestroy(config);
      config = nullptr;
      fail("Cannot load native document font configuration");
    }
  }
  ~Impl() {
    if (config)
      FcConfigDestroy(config);
  }
  std::shared_ptr<NativeTypeface> match(const TextFont &font,
                                        const Text &characters = {},
                                        bool scalable = false) {
    Request request{font.family.empty() ? family : font.family, weight(font),
                    font.italic, font.language};
    if (characters.empty()) {
      if (auto found = primary.find(request); found != primary.end())
        return found->second;
    } else if (auto found = fallback.find({request, characters});
               found != fallback.end())
      return found->second;
    using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;
    Pattern pattern(FcPatternCreate(), FcPatternDestroy);
    for (const auto &family : document_font_families(std::get<0>(request))) {
      auto name = family.toUtf8();
      FcPatternAddString(pattern.get(), FC_FAMILY,
                         reinterpret_cast<const FcChar8 *>(name.c_str()));
    }
    FcPatternAddString(pattern.get(), FC_FAMILY,
                       reinterpret_cast<const FcChar8 *>("monospace"));
    FcPatternAddDouble(pattern.get(), FC_WEIGHT,
                       FcWeightFromOpenTypeDouble(weight(font)));
    FcPatternAddInteger(pattern.get(), FC_SLANT,
                        font.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddDouble(pattern.get(), FC_PIXEL_SIZE, bitmap_pixels);
    if (!font.language.empty() && font.language != "none") {
      FcLangSet *languages = FcLangSetCreate();
      FcLangSetAdd(languages, reinterpret_cast<const FcChar8 *>(
                                  font.language.toUtf8().c_str()));
      FcPatternAddLangSet(pattern.get(), FC_LANG, languages);
      FcLangSetDestroy(languages);
    }
    if (scalable)
      FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
    if (!characters.empty()) {
      FcCharSet *charset = FcCharSetCreate();
      for (auto c : characters)
        if (!ignorable(c))
          FcCharSetAddChar(charset, c);
      FcPatternAddCharSet(pattern.get(), FC_CHARSET, charset);
      FcCharSetDestroy(charset);
    }
    FcConfigSubstitute(config, pattern.get(), FcMatchPattern);
    FcConfigSetDefaultSubstitute(config, pattern.get());
    FcResult result;
    Pattern matched(FcFontMatch(config, pattern.get(), &result),
                    FcPatternDestroy);
    FcChar8 *file = nullptr, *name = nullptr, *style = nullptr;
    int index = 0;
    if (!matched ||
        FcPatternGetString(matched.get(), FC_FILE, 0, &file) != FcResultMatch)
      fail("No usable native document font");
    FcPatternGetInteger(matched.get(), FC_INDEX, 0, &index);
    FcPatternGetString(matched.get(), FC_FAMILY, 0, &name);
    FcPatternGetString(matched.get(), FC_STYLE, 0, &style);
    FaceKey key{reinterpret_cast<const char *>(file), index, weight(font),
                font.italic};
    std::shared_ptr<NativeTypeface> face;
    if (auto found = faces.find(key); found != faces.end())
      face = found->second.lock();
    if (!face) {
      if (faces.size() >= 512) {
        std::erase_if(faces,
                      [](const auto &item) { return item.second.expired(); });
        if (faces.size() >= 512)
          fail("Native document font limit exceeded");
      }
      face = std::make_shared<NativeTypeface>();
      static std::atomic<uint64_t> next_id{1};
      face->file = reinterpret_cast<const char *>(file);
      face->index = index;
      face->id = next_id.fetch_add(1, std::memory_order_relaxed);
      face->family = name ? reinterpret_cast<const char *>(name) : "";
      face->style = style ? reinterpret_cast<const char *>(style) : "";
      face->implementation = std::make_shared<NativeTypeface::Impl>();
      auto &native = *face->implementation;
      native.library = library;
      if (FT_New_Face(library->ft, face->file.c_str(), index,
                      &native.shaping) ||
          FT_New_Face(library->ft, face->file.c_str(), index, &native.raster))
        fail("Cannot open document font");
      face->scalable = FT_IS_SCALABLE(native.shaping);
      variations(library->ft, native.shaping, font);
      variations(library->ft, native.raster, font);
      size_face(native.shaping, face->scalable ? 64 : bitmap_pixels);
      native.hb = hb_ft_font_create_referenced(native.shaping);
      hb_ft_font_set_load_flags(
          native.hb, face->scalable ? FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP
                                    : FT_LOAD_DEFAULT);
      native.units = native.shaping->size->metrics.y_ppem * 64.;
      native.metrics = {
          0,
          std::max(0., native.shaping->size->metrics.ascender / native.units),
          std::max(0.,
                   -native.shaping->size->metrics.descender / native.units)};
      if (native.units <= 0 ||
          native.metrics.ascent + native.metrics.descent <= 0)
        fail("Invalid document font metrics");
      faces[key] = face;
    }
    if (characters.empty()) {
      if (primary.size() >= 512)
        fail("Native font request limit exceeded");
      primary[request] = face;
    } else {
      // Fallback entries are an optimization, never an unbounded text cache.
      if (fallback.size() >= 4096)
        fallback.clear();
      if (characters.size() <= 64)
        fallback[{request, characters}] = face;
    }
    return face;
  }
  static bool covers(const NativeTypeface &face, const Text &text) {
    for (auto c : text)
      if (!ignorable(c) && !FT_Get_Char_Index(face.implementation->shaping, c))
        return false;
    return true;
  }
};
struct ShapingParagraph::Impl {
  std::shared_ptr<NativeTextShaper::Impl> engine;
  TextParagraph paragraph;
  std::vector<uint32_t> scalars;
  std::vector<UChar> utf16;
  std::vector<UChar> direction_text;
  std::vector<int32_t> to16;
  std::vector<size_t> from16;
  Bidi bidi;
  struct Segment {
    size_t begin, end;
    TextFont font;
    UScriptCode script;
    std::shared_ptr<NativeTypeface> face;
    std::optional<TextColor> color;
  };
  std::vector<Segment> segments;
  ShapedText whole;
  Impl(std::shared_ptr<NativeTextShaper::Impl> e, const TextParagraph &p)
      : engine(std::move(e)), paragraph(p) {
    if (p.text.size() > 1'000'000 || !valid_font(p.font) ||
        !valid_color(p.color))
      fail("Invalid native shaping paragraph");
    size_t end = 0;
    for (const auto &s : p.spans) {
      if (s.begin != end || s.end <= s.begin || s.end > p.text.size() ||
          !valid_font(s.font) || !valid_color(s.color))
        fail("Invalid native shaping span");
      end = s.end;
    }
    if (!p.spans.empty() && end != p.text.size())
      fail("Incomplete native shaping spans");
    for (size_t i = 0; i < p.text.size(); ++i) {
      uint32_t c = p.text[i];
      if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        fail("Invalid native shaping scalar");
      scalars.push_back(c);
      to16.push_back(utf16.size());
      from16.push_back(i);
      if (c > 0xffff) {
        utf16.push_back(0xd800 + ((c - 0x10000) >> 10));
        from16.push_back(i);
        utf16.push_back(0xdc00 + ((c - 0x10000) & 1023));
      } else
        utf16.push_back(c);
    }
    to16.push_back(utf16.size());
    from16.push_back(p.text.size());
    UErrorCode status = U_ZERO_ERROR;
    bidi.reset(ubidi_openSized(utf16.size(), 0, &status));
    UBiDiLevel level = p.direction == TextParagraph::RTL   ? 1
                       : p.direction == TextParagraph::LTR ? 0
                                                           : UBIDI_DEFAULT_LTR;
    static const UChar empty = 0;
    const UChar *text_data = utf16.empty() ? &empty : utf16.data();
    // A soft line break retains the ODF paragraph's base direction.
    direction_text = utf16;
    for (size_t i = 0; i < scalars.size(); ++i)
      if (hard(scalars[i]))
        direction_text[to16[i]] = 0x2028;
    ubidi_setPara(bidi.get(),
                  direction_text.empty() ? &empty : direction_text.data(),
                  direction_text.size(), level, nullptr, &status);
    if (U_FAILURE(status))
      fail("Cannot resolve document text direction");
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> iter(
        ubrk_open(UBRK_CHARACTER, "und", text_data, utf16.size(), &status),
        ubrk_close);
    if (U_FAILURE(status))
      fail("Cannot resolve shaping graphemes");
    struct Atom {
      size_t begin, end;
      TextFont font;
      UScriptCode script;
      std::optional<TextColor> color;
    };
    std::vector<Atom> atoms;
    int32_t start = ubrk_first(iter.get());
    size_t span = 0;
    for (int32_t end = ubrk_next(iter.get()); end != UBRK_DONE;
         start = end, end = ubrk_next(iter.get())) {
      size_t a = from16[start], b = from16[end];
      while (span + 1 < p.spans.size() && p.spans[span].end <= a)
        ++span;
      TextFont font = p.spans.empty() ? p.font : p.spans[span].font;
      auto color = p.spans.empty() ? p.color : p.spans[span].color;
      UScriptCode script = USCRIPT_COMMON;
      for (size_t i = a; i < b; ++i) {
        auto candidate = uscript_getScript(scalars[i], &status);
        if (candidate != USCRIPT_COMMON && candidate != USCRIPT_INHERITED) {
          script = candidate;
          break;
        }
      }
      atoms.push_back({a, b, font, script, color});
    }
    UScriptCode previous = USCRIPT_COMMON;
    for (auto &a : atoms) {
      if (hard(scalars[a.begin])) {
        previous = USCRIPT_COMMON;
        continue;
      }
      if (a.script == USCRIPT_COMMON)
        a.script = previous;
      else
        previous = a.script;
    }
    previous = USCRIPT_LATIN;
    for (auto it = atoms.rbegin(); it != atoms.rend(); ++it) {
      if (hard(scalars[it->begin])) {
        previous = USCRIPT_LATIN;
        continue;
      }
      if (it->script == USCRIPT_COMMON)
        it->script = previous;
      else
        previous = it->script;
    }
    for (size_t first = 0; first < atoms.size();) {
      size_t last = first + 1;
      while (last < atoms.size() && atoms[last].font == atoms[first].font &&
             atoms[last].script == atoms[first].script &&
             !hard(scalars[atoms[last - 1].begin]) &&
             !hard(scalars[atoms[last].begin]))
        ++last;
      auto primary = engine->match(atoms[first].font);
      auto text = p.text.mid(atoms[first].begin,
                             atoms[last - 1].end - atoms[first].begin);
      std::shared_ptr<NativeTypeface> group = primary;
      if (!NativeTextShaper::Impl::covers(*primary, text)) {
        auto candidate = engine->match(atoms[first].font, text, true);
        if (NativeTextShaper::Impl::covers(*candidate, text))
          group = candidate;
      }
      for (size_t i = first; i < last; ++i) {
        auto face = group;
        auto text = p.text.mid(atoms[i].begin, atoms[i].end - atoms[i].begin);
        if (!NativeTextShaper::Impl::covers(*face, text))
          face = engine->match(atoms[i].font, text, true);
        if (!segments.empty() && segments.back().end == atoms[i].begin &&
            segments.back().face == face &&
            segments.back().font == atoms[i].font &&
            segments.back().color == atoms[i].color &&
            segments.back().script == atoms[i].script &&
            !hard(scalars[atoms[i].begin]) &&
            !hard(scalars[segments.back().begin]) &&
            scalars[atoms[i].begin] != '\t' &&
            scalars[segments.back().begin] != '\t')
          segments.back().end = atoms[i].end;
        else
          segments.push_back({atoms[i].begin, atoms[i].end, atoms[i].font,
                              atoms[i].script, face, atoms[i].color});
      }
      first = last;
    }
    // Shape separately at explicit line/paragraph boundaries.
    size_t begin = 0;
    for (size_t i = 0; i <= scalars.size(); ++i)
      if (i == scalars.size() || hard(scalars[i])) {
        auto part = shape_line(begin, i);
        for (auto g : part->glyphs) {
          g.x += whole.metrics.width;
          whole.glyphs.push_back(std::move(g));
        }
        whole.clusters.insert(whole.clusters.end(), part->clusters.begin(),
                              part->clusters.end());
        whole.metrics.width += part->metrics.width;
        whole.metrics.ascent =
            std::max(whole.metrics.ascent, part->metrics.ascent);
        whole.metrics.descent =
            std::max(whole.metrics.descent, part->metrics.descent);
        whole.missing_glyphs += part->missing_glyphs;
        if (i < scalars.size())
          whole.clusters.push_back(
              {i, i + 1, {0, part->metrics.ascent, part->metrics.descent}});
        begin = i + 1;
      }
    whole.rtl = ubidi_getParaLevel(bidi.get()) & 1;
  }
  std::shared_ptr<ShapedText> shape_line(size_t begin, size_t end) const {
    if (begin > end || end > scalars.size())
      fail("Invalid native shaping line extent");
    auto output = std::make_shared<ShapedText>();
    output->rtl = ubidi_getParaLevel(bidi.get()) & 1;
    if (begin == end) {
      const TextFont *font = &paragraph.font;
      if (begin < scalars.size()) {
        // A leading/interior empty line still has a source character (usually
        // an explicit break). Its run supplies the font. At the paragraph end
        // there is no source run; retain the paragraph-mark/default font.
        auto span = std::lower_bound(
            paragraph.spans.begin(), paragraph.spans.end(), begin,
            [](const TextSpan &span, size_t at) { return span.end <= at; });
        if (span != paragraph.spans.end())
          font = &span->font;
      }
      auto face = engine->match(*font);
      output->metrics = {0, face->implementation->metrics.ascent * font->size,
                         face->implementation->metrics.descent * font->size};
      return output;
    }
    UErrorCode status = U_ZERO_ERROR;
    Bidi line(ubidi_openSized(to16[end] - to16[begin], 0, &status));
    ubidi_setLine(bidi.get(), to16[begin], to16[end], line.get(), &status);
    int count = ubidi_countRuns(line.get(), &status);
    if (U_FAILURE(status))
      fail("Cannot reorder document text line");
    std::map<size_t, ShapedCluster> logical;
    for (int run = 0; run < count; ++run) {
      int32_t start16, length16;
      bool rtl =
          ubidi_getVisualRun(line.get(), run, &start16, &length16) == UBIDI_RTL;
      size_t a = from16[to16[begin] + start16],
             b = from16[to16[begin] + start16 + length16];
      std::vector<const Segment *> pieces;
      auto first = std::lower_bound(
          segments.begin(), segments.end(), a,
          [](const Segment &s, size_t pos) { return s.end <= pos; });
      for (auto it = first; it != segments.end() && it->begin < b; ++it)
        pieces.push_back(&*it);
      if (rtl)
        std::reverse(pieces.begin(), pieces.end());
      for (const auto *segment : pieces) {
        size_t lo = std::max(a, segment->begin), hi = std::min(b, segment->end);
        auto &native = *segment->face->implementation;
        double unit = segment->font.size / native.units;
        double ascent = native.metrics.ascent * segment->font.size,
               descent = native.metrics.descent * segment->font.size;
        output->metrics.ascent = std::max(output->metrics.ascent, ascent);
        output->metrics.descent = std::max(output->metrics.descent, descent);
        Buffer buffer(hb_buffer_create());
        hb_buffer_set_cluster_level(buffer.get(),
                                    HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_set_direction(buffer.get(),
                                rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(
            buffer.get(),
            hb_script_from_string(uscript_getShortName(segment->script), -1));
        if (!segment->font.language.empty())
          hb_buffer_set_language(
              buffer.get(), hb_language_from_string(
                                segment->font.language.toUtf8().c_str(), -1));
        if (scalars[lo] == '\t') {
          logical[lo] = {lo, hi, {0, ascent, descent}};
          continue; // Position-dependent tab expansion belongs to native
                    // layout.
        }
        hb_buffer_add_utf32(buffer.get(), scalars.data() + begin, end - begin,
                            lo - begin, hi - lo);
        hb_shape(native.hb, buffer.get(), nullptr, 0);
        unsigned length;
        auto *info = hb_buffer_get_glyph_infos(buffer.get(), &length);
        auto *positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
        if (length > 4'000'000)
          fail("Native shaping glyph limit exceeded");
        std::set<size_t> boundaries{lo, hi};
        for (unsigned i = 0; i < length; ++i)
          boundaries.insert(begin + info[i].cluster);
        for (auto it = boundaries.begin(); std::next(it) != boundaries.end();
             ++it)
          logical[*it] = {*it, *std::next(it), {0, ascent, descent}};
        for (unsigned i = 0; i < length; ++i) {
          size_t source = begin + info[i].cluster;
          auto next = boundaries.upper_bound(source);
          if (source < lo || source >= hi || next == boundaries.end())
            fail("Invalid native shaping cluster");
          double advance = positions[i].x_advance * unit;
          if (advance < 0)
            fail("Unsupported negative horizontal font advance");
          output->glyphs.push_back(
              {segment->face, info[i].codepoint, source, *next,
               output->metrics.width + positions[i].x_offset * unit,
               -positions[i].y_offset * unit, advance, segment->font.size,
               segment->color});
          output->metrics.width += advance;
          if (!info[i].codepoint)
            ++output->missing_glyphs;
          if (output->glyphs.size() > 4'000'000)
            fail("Native shaping glyph limit exceeded");
          auto &cluster = logical[source];
          cluster.begin = source;
          cluster.end = *next;
          cluster.metrics.width += advance;
          cluster.metrics.ascent = std::max(cluster.metrics.ascent, ascent);
          cluster.metrics.descent = std::max(cluster.metrics.descent, descent);
        }
      }
    }
    for (auto &[key, cluster] : logical)
      output->clusters.push_back(cluster);
    return output;
  }
};
NativeTextShaper::NativeTextShaper(const Text &family, int pixels)
    : impl_(std::make_shared<Impl>(family, pixels)) {}
ShapingParagraph NativeTextShaper::prepare(const TextParagraph &p) const {
  return ShapingParagraph(std::make_shared<ShapingParagraph::Impl>(impl_, p));
}
ShapedText NativeTextShaper::shape(const Text &text, const TextFont &font,
                                   bool rtl) const {
  TextParagraph p;
  p.text = text;
  p.font = font;
  p.direction = rtl ? TextParagraph::RTL : TextParagraph::Auto;
  return prepare(p).whole();
}
ShapingParagraph::ShapingParagraph(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
const ShapedText &ShapingParagraph::whole() const { return impl_->whole; }
std::shared_ptr<ShapedText> ShapingParagraph::line(size_t begin,
                                                   size_t end) const {
  return impl_->shape_line(begin, end);
}
FT_Face NativeTextShaper::print_face(const NativeTypeface &font) {
  if (!font.implementation)
    fail("Missing native print font");
  return font.implementation->raster;
}
GlyphBitmap NativeTextShaper::rasterize(const NativeTypeface &font,
                                        uint32_t glyph, unsigned pixels) {
  if (!font.implementation || pixels < 1 || pixels > 4096)
    fail("Invalid native glyph raster request");
  auto face = font.implementation->raster;
  size_face(face, pixels);
  if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT | FT_LOAD_COLOR) ||
      FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
    fail("Cannot rasterize native document glyph");
  const auto &bitmap = face->glyph->bitmap;
  GlyphBitmap result;
  result.width = bitmap.width;
  result.height = bitmap.rows;
  result.left = face->glyph->bitmap_left;
  result.top = -face->glyph->bitmap_top;
  result.pixels_per_em = face->size->metrics.y_ppem;
  if (result.width > 16384 || result.height > 4096 ||
      size_t(result.width) * result.height > 16'000'000 ||
      result.pixels_per_em <= 0)
    fail("Native glyph raster limit exceeded");
  result.alpha.resize(size_t(result.width) * result.height);
  for (int y = 0; y < result.height; ++y) {
    auto row = bitmap.buffer + (bitmap.pitch >= 0 ? y : result.height - 1 - y) *
                                   std::abs(bitmap.pitch);
    for (int x = 0; x < result.width; ++x) {
      unsigned value;
      if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
        value = row[x] * 255 / std::max(1, int(bitmap.num_grays) - 1);
      else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY2)
        value = ((row[x / 4] >> (6 - 2 * (x % 4))) & 3) * 85;
      else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY4)
        value = ((row[x / 2] >> (4 - 4 * (x % 2))) & 15) * 17;
      else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
        value = (row[x / 8] & (0x80 >> (x % 8))) ? 255 : 0;
      else if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
        value = row[x * 4 + 3];
      else
        fail("Unsupported native glyph bitmap format");
      result.alpha[size_t(y) * result.width + x] = value;
    }
  }
  return result;
}
} // namespace DomeOffice
