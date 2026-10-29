// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
#include <ft2build.h>
#include FT_FREETYPE_H

namespace DomeOffice {
struct NativeTypeface {
  std::string file, family, style;
  int index = 0;
  uint64_t id = 0;
  bool scalable = false;
  struct Impl;
  std::shared_ptr<Impl> implementation;
};
struct ShapedGlyph {
  std::shared_ptr<const NativeTypeface> face;
  uint32_t glyph = 0;
  size_t begin = 0, end = 0;
  double x = 0, y = 0, advance = 0, size = 12;
  std::optional<TextColor> color;
};
struct ShapedCluster {
  size_t begin = 0, end = 0;
  TextMetrics metrics;
  double x = 0; // Visual pen origin, independent of glyph ink offsets.
  bool rtl = false;
  struct Caret {
    size_t offset = 0;
    double fraction = 0; // Position across this cluster's visual advance.
  };
  std::vector<Caret> carets; // Unicode grapheme boundaries in logical order.
};
struct ShapedText {
  TextMetrics metrics;
  bool rtl = false;
  size_t missing_glyphs = 0;
  std::vector<ShapedGlyph> glyphs;
  std::vector<ShapedCluster> clusters; // Logical order, full source coverage.
  size_t empty_offset = 0;
  // Original scalar source shared by all wrapped lines, also used for PDF
  // text/cluster mapping. Decorative repeated leaders intentionally omit it.
  std::shared_ptr<const Text> source;
};
struct GlyphBitmap {
  int width = 0, height = 0, left = 0, top = 0;
  double pixels_per_em = 1;
  std::vector<unsigned char> alpha;
};
class ShapingParagraph {
public:
  struct Impl;
  explicit ShapingParagraph(std::shared_ptr<Impl>);
  const ShapedText &whole() const;
  std::shared_ptr<ShapedText> line(size_t begin, size_t end) const;

private:
  std::shared_ptr<Impl> impl_;
};
// A single-threaded native shaping/raster context. Typeface handles retain
// their FreeType library, so display lists may outlive the context that shaped
// them.
class NativeTextShaper {
public:
  explicit NativeTextShaper(const Text &default_family = "monospace",
                            int bitmap_pixels = 16);
  ShapingParagraph prepare(const TextParagraph &) const;
  ShapedText shape(const Text &, const TextFont &, bool rtl = false) const;
  static GlyphBitmap rasterize(const NativeTypeface &, uint32_t glyph,
                               unsigned pixels);
  // Borrowed font for native print surfaces. Retain font.implementation for
  // the complete consumer lifetime; this context is single-threaded.
  static FT_Face print_face(const NativeTypeface &);
  struct Impl;

private:
  std::shared_ptr<Impl> impl_;
};
} // namespace DomeOffice
