// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_print.h"
#include "native_image_decode.h"
#include "native_path.h"
#include "native_table_layout.h"
#include "native_text_shaper.h"
#include <cairo-ft.h>
#include <cairo-pdf.h>
#include <gio/gio.h>
#include <numbers>
namespace DomeOffice {
namespace {
void check(cairo_status_t status) {
  if (status != CAIRO_STATUS_SUCCESS)
    throw std::runtime_error(std::string("Cannot prepare print pages: ") +
                             cairo_status_to_string(status));
}
struct Save {
  cairo_t *cr;
  explicit Save(cairo_t *c) : cr(c) { cairo_save(cr); }
  ~Save() { cairo_restore(cr); }
};
void color(cairo_t *cr, TextColor c) {
  cairo_set_source_rgba(cr, c.red / 255., c.green / 255., c.blue / 255.,
                        c.alpha);
}
void clip(cairo_t *cr, const TextRect &r) {
  cairo_rectangle(cr, r.x, r.y, std::max(0., r.width), std::max(0., r.height));
  cairo_clip(cr);
}
void transform(cairo_t *cr, const SceneTransform &m) {
  cairo_matrix_t matrix{m.a, m.b, m.c, m.d, m.e, m.f};
  // Cairo permanently errors on a singular CTM; an invisible collapsed shape
  // is handled before entering its transform instead.
  cairo_transform(cr, &matrix);
}
class Pdf {
  TempFile temporary_;
  Text target_;
  cairo_surface_t *surface_ = nullptr;
  cairo_t *cr_ = nullptr;
  std::map<uint64_t, cairo_font_face_t *> fonts_;
  static inline cairo_user_data_key_t font_owner_;

public:
  NativeTextShaper shaper;
  PrintResult result;
  explicit Pdf(const Text &path)
      : temporary_(FileInfo(path).absolutePath() + "/.calligra-print-XXXXXX"),
        target_(path) {
    if (FileInfo(path).suffix().compare("pdf", CaseInsensitive) != 0)
      throw std::runtime_error("Print output must use a .pdf filename");
    if (!temporary_.open())
      throw std::runtime_error(
          "Cannot create print document in the selected directory");
    temporary_.close();
    surface_ = cairo_pdf_surface_create(temporary_.fileName().toUtf8().c_str(),
                                        595, 842);
    try {
      check(cairo_surface_status(surface_));
      cairo_pdf_surface_set_metadata(surface_, CAIRO_PDF_METADATA_CREATOR,
                                     "Calligra FLTK native print engine");
      cr_ = cairo_create(surface_);
      check(cairo_status(cr_));
      cairo_font_options_t *options = cairo_font_options_create();
      cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
      cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
      cairo_set_font_options(cr_, options);
      cairo_font_options_destroy(options);
    } catch (...) {
      if (cr_)
        cairo_destroy(cr_);
      cairo_surface_destroy(surface_);
      throw;
    }
  }
  ~Pdf() {
    if (cr_)
      cairo_destroy(cr_);
    if (surface_)
      cairo_surface_destroy(surface_);
    for (auto &[id, font] : fonts_)
      cairo_font_face_destroy(font);
  }
  cairo_t *cr() { return cr_; }
  void page(double width, double height,
            TextColor background = {255, 255, 255}) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 ||
        height < 1 || width > 14400 || height > 14400 || result.pages >= 10000)
      throw std::runtime_error(
          "Print page size or page count exceeds the supported limit");
    cairo_pdf_surface_set_size(surface_, width, height);
    cairo_reset_clip(cr_);
    cairo_identity_matrix(cr_);
    color(cr_, {255, 255, 255});
    cairo_paint(cr_);
    color(cr_, background);
    cairo_paint(cr_);
    clip(cr_, {0, 0, width, height});
    ++result.pages;
  }
  void end_page() {
    check(cairo_status(cr_));
    cairo_show_page(cr_);
  }
  void finish() {
    check(cairo_status(cr_));
    cairo_surface_finish(surface_);
    check(cairo_surface_status(surface_));
    File sync(temporary_.fileName());
    if (!sync.open(IO::ReadOnly) || fsync(sync.handle()))
      throw std::runtime_error("Cannot synchronize print document");
    sync.close();
    if (::rename(temporary_.fileName().toUtf8().c_str(),
                 target_.toUtf8().c_str()))
      throw std::runtime_error("Cannot replace print document");
  }
  void image(const std::shared_ptr<const ImageAsset> &asset, double width,
             double height) {
    if (!asset || width <= 0 || height <= 0) {
      color(cr_, {180, 40, 40});
      cairo_rectangle(cr_, 0, 0, std::max(0., width), std::max(0., height));
      cairo_stroke(cr_);
      result.unsupported.push_back("Missing print image");
      return;
    }
    int w = int(std::clamp(std::ceil(width * 300 / 72.), 1., 4000.));
    int h = int(std::clamp(std::ceil(height * 300 / 72.), 1., 4000.));
    auto decoded = decode_image(*asset->data, w, h);
    if (!decoded || decoded->fail() || !decoded->data() || !decoded->data()[0])
      throw std::runtime_error("Cannot decode document print image");
    w = decoded->w();
    h = decoded->h();
    int depth = decoded->d();
    if (w < 1 || h < 1 || depth < 1 || depth > 4 || uint64_t(w) * h > 16000000)
      throw std::runtime_error("Print image exceeds native raster limits");
    int stride = decoded->ld() ? decoded->ld() : w * depth;
    if (stride < w * depth)
      throw std::runtime_error("Invalid print image stride");
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    check(cairo_surface_status(surface));
    auto data =
        reinterpret_cast<uint32_t *>(cairo_image_surface_get_data(surface));
    auto source = reinterpret_cast<const unsigned char *>(decoded->data()[0]);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        auto p = source + y * stride + x * depth;
        unsigned a = depth == 2 ? p[1] : depth == 4 ? p[3] : 255;
        unsigned r = p[0], g = depth < 3 ? r : p[1], b = depth < 3 ? r : p[2];
        data[size_t(y) * w + x] = (a << 24) | ((r * a + 127) / 255 << 16) |
                                  ((g * a + 127) / 255 << 8) |
                                  ((b * a + 127) / 255);
      }
    cairo_surface_mark_dirty(surface);
    Save save(cr_);
    cairo_scale(cr_, width / w, height / h);
    cairo_set_source_surface(cr_, surface, 0, 0);
    cairo_paint(cr_);
    check(cairo_surface_status(surface));
    cairo_surface_destroy(surface);
  }
  void glyphs(const ShapedText &text, TextColor fallback = {}) {
    result.missing_glyphs += text.missing_glyphs;
    for (const auto &g : text.glyphs) {
      if (!g.face)
        throw std::runtime_error("Missing native print typeface");
      color(cr_, g.color.value_or(fallback));
      if (g.face->scalable) {
        auto [it, inserted] = fonts_.emplace(g.face->id, nullptr);
        if (inserted) {
          auto font = cairo_ft_font_face_create_for_ft_face(
              NativeTextShaper::print_face(*g.face), FT_LOAD_NO_HINTING);
          check(cairo_font_face_status(font));
          auto holder =
              new std::shared_ptr<NativeTypeface::Impl>(g.face->implementation);
          auto status = cairo_font_face_set_user_data(
              font, &font_owner_, holder, [](void *p) {
                delete static_cast<std::shared_ptr<NativeTypeface::Impl> *>(p);
              });
          if (status != CAIRO_STATUS_SUCCESS) {
            delete holder;
            cairo_font_face_destroy(font);
            check(status);
          }
          it->second = font;
        }
        cairo_set_font_face(cr_, it->second);
        cairo_set_font_size(cr_, g.size);
        cairo_glyph_t glyph{g.glyph, g.x, g.y};
        cairo_show_glyphs(cr_, &glyph, 1);
      } else {
        auto bitmap = NativeTextShaper::rasterize(
            *g.face, g.glyph,
            unsigned(std::clamp(std::ceil(g.size * 300 / 72), 1., 4096.)));
        if (!bitmap.width || !bitmap.height)
          continue;
        auto mask = cairo_image_surface_create(CAIRO_FORMAT_A8, bitmap.width,
                                               bitmap.height);
        check(cairo_surface_status(mask));
        int stride = cairo_image_surface_get_stride(mask);
        auto data = cairo_image_surface_get_data(mask);
        for (int y = 0; y < bitmap.height; ++y)
          std::copy_n(bitmap.alpha.data() + size_t(y) * bitmap.width,
                      bitmap.width, data + size_t(y) * stride);
        cairo_surface_mark_dirty(mask);
        Save save(cr_);
        double unit = g.size / bitmap.pixels_per_em;
        cairo_translate(cr_, g.x + bitmap.left * unit, g.y + bitmap.top * unit);
        cairo_scale(cr_, unit, unit);
        cairo_mask_surface(cr_, mask, 0, 0);
        cairo_surface_destroy(mask);
      }
    }
  }
  void paints(const std::vector<TextPaint> &paints) {
    for (const auto &p : paints) {
      Save save(cr_);
      if (p.clip)
        clip(cr_, *p.clip);
      color(cr_, p.color);
      if (p.ellipse) {
        cairo_translate(cr_, p.rect.x + p.rect.width / 2,
                        p.rect.y + p.rect.height / 2);
        cairo_scale(cr_, std::max(.0001, p.rect.width / 2),
                    std::max(.0001, p.rect.height / 2));
        cairo_arc(cr_, 0, 0, 1, 0, 2 * std::numbers::pi);
      } else
        cairo_rectangle(cr_, p.rect.x, p.rect.y, p.rect.width, p.rect.height);
      cairo_fill(cr_);
    }
  }
  void lines(const std::vector<TextLine> &lines) {
    for (const auto &line : lines) {
      Save saved(cr_);
      if (line.clip)
        clip(cr_, *line.clip);
      if (!line.shaped)
        throw std::runtime_error("Missing native print glyph layout");
      {
        Save origin(cr_);
        cairo_translate(cr_, line.x, line.baseline);
        glyphs(*line.shaped);
        for (const auto &tab : line.tabs) {
          color(cr_, tab.color.value_or(TextColor{}));
          for (const auto &s : tab.strokes) {
            cairo_set_line_width(cr_, s.width);
            cairo_move_to(cr_, s.x1, s.y1);
            cairo_line_to(cr_, s.x2, s.y2);
            cairo_stroke(cr_);
          }
          if (tab.leader)
            glyphs(*tab.leader, tab.color.value_or(TextColor{}));
        }
      }
      if (line.label) {
        Save origin(cr_);
        cairo_translate(cr_, line.label->x, line.baseline);
        if (line.label->shaped)
          glyphs(*line.label->shaped, line.label->color.value_or(TextColor{}));
        if (line.label->image) {
          cairo_translate(cr_, 0, line.label->y);
          image(line.label->image->asset, line.label->width,
                line.label->height);
        }
      }
    }
  }
};
Text run(const std::vector<Text> &arguments) {
  std::vector<std::string> bytes;
  for (const auto &a : arguments)
    bytes.push_back(a.toStdString());
  std::vector<char *> argv;
  for (auto &a : bytes)
    argv.push_back(a.data());
  argv.push_back(nullptr);
  GError *error = nullptr;
  char *out = nullptr, *err = nullptr;
  int status = 0;
  auto environment = g_environ_setenv(g_get_environ(), "LC_ALL", "C", true);
  bool ok = g_spawn_sync(nullptr, argv.data(), environment, G_SPAWN_SEARCH_PATH,
                         nullptr, nullptr, &out, &err, &status, &error);
  g_strfreev(environment);
  Text output(out ? out : "");
  Text message(err ? err : "");
  g_free(out);
  g_free(err);
  if (!ok) {
    std::string why = error ? error->message : "Cannot run CUPS client";
    if (error)
      g_error_free(error);
    throw std::runtime_error(why);
  }
  if (!g_spawn_check_wait_status(status, &error)) {
    std::string why = message.empty()
                          ? (error ? error->message : "CUPS request failed")
                          : message.toStdString();
    if (error)
      g_error_free(error);
    throw std::runtime_error(why);
  }
  return output.trimmed();
}
} // namespace
PrintResult print_pdf(const TextDocument &document, const Text &path,
                      const PrintSettings &) {
  Pdf pdf(path);
  auto pagination = paginate_text(document.flow(), pdf.shaper);
  pdf.result.unsupported = pagination.unsupported;
  for (const auto &page : pagination.pages) {
    pdf.page(page.style.width, page.style.height, page.style.background);
    pdf.paints(page.paints);
    pdf.lines(page.lines);
    pdf.end_page();
  }
  pdf.finish();
  return pdf.result;
}
PrintResult print_pdf(const Presentation &document, const Text &path,
                      const PrintSettings &) {
  Pdf pdf(path);
  for (int i = 0; i < document.count(); ++i) {
    const auto &scene = document.scene(i);
    pdf.page(scene.width, scene.height, scene.background);
    pdf.result.unsupported.insert(pdf.result.unsupported.end(),
                                  scene.unsupported.begin(),
                                  scene.unsupported.end());
    for (const auto &shape : scene.shapes) {
      if (std::abs(shape.transform.a * shape.transform.d -
                   shape.transform.b * shape.transform.c) < 1e-15)
        continue;
      Save saved(pdf.cr());
      transform(pdf.cr(), shape.transform);
      auto contours =
          shape.path ? flatten_path(*shape.path, shape.path_map,
                                    SceneTransform{300 / 72., 0, 0, 300 / 72.} *
                                        shape.transform,
                                    .1)
                     : shape.contours;
      std::map<size_t, std::vector<SceneContour>> groups;
      for (auto &c : contours)
        groups[c.paint_group].push_back(std::move(c));
      for (const auto &[id, items] : groups) {
        auto draw_path = [&](bool filling) {
          cairo_new_path(pdf.cr());
          for (const auto &c : items)
            if ((filling ? c.fill : c.stroke) && !c.points.empty()) {
              cairo_move_to(pdf.cr(), c.points[0].x, c.points[0].y);
              for (size_t j = 1; j < c.points.size(); ++j)
                cairo_line_to(pdf.cr(), c.points[j].x, c.points[j].y);
              if (c.closed)
                cairo_close_path(pdf.cr());
            }
        };
        if (shape.paint.fill) {
          draw_path(true);
          color(pdf.cr(), shape.paint.fill_color);
          cairo_set_fill_rule(pdf.cr(), (shape.path && shape.path->even_odd
                                             ? *shape.path->even_odd
                                             : shape.paint.even_odd)
                                            ? CAIRO_FILL_RULE_EVEN_ODD
                                            : CAIRO_FILL_RULE_WINDING);
          cairo_fill(pdf.cr());
        }
        if (shape.paint.stroke && shape.paint.stroke_width > 0) {
          draw_path(false);
          color(pdf.cr(), shape.paint.stroke_color);
          cairo_set_line_width(pdf.cr(), shape.paint.stroke_width);
          cairo_stroke(pdf.cr());
        }
      }
      if (!shape.image.empty())
        pdf.image(shape.raster, shape.width, shape.height);
      auto area =
          shape.text_area.value_or(SceneRect{0, 0, shape.width, shape.height});
      double width =
          area.width - shape.paint.padding_left - shape.paint.padding_right;
      double height =
          area.height - shape.paint.padding_top - shape.paint.padding_bottom;
      if (width > 0 && height > 0 &&
          (!shape.text.empty() || !shape.text_flow.tables.empty())) {
        Save frame(pdf.cr());
        clip(pdf.cr(), {area.x, area.y, area.width, area.height});
        auto layout = layout_text_box(shape.text_flow, width, pdf.shaper);
        double extra = std::max(0., height - layout.height);
        double y =
            area.y + shape.paint.padding_top +
            (shape.paint.vertical_alignment == ScenePaint::Middle   ? extra / 2
             : shape.paint.vertical_alignment == ScenePaint::Bottom ? extra
                                                                    : 0);
        offset_text_layout(layout, area.x + shape.paint.padding_left, y);
        pdf.result.unsupported.insert(pdf.result.unsupported.end(),
                                      layout.unsupported.begin(),
                                      layout.unsupported.end());
        pdf.paints(layout.paints);
        pdf.lines(layout.lines);
      }
    }
    pdf.end_page();
  }
  pdf.finish();
  return pdf.result;
}
PrintResult print_pdf(const Workbook &book, const Text &path,
                      const PrintSettings &settings) {
  if (!std::isfinite(settings.width) || !std::isfinite(settings.height) ||
      !std::isfinite(settings.margin) || settings.margin < 0 ||
      settings.width > 14400 || settings.height > 14400 ||
      settings.width < 2 * settings.margin + 120 ||
      settings.height < 2 * settings.margin + 100 || settings.sheet < -1 ||
      settings.sheet >= book.sheet_count())
    throw std::runtime_error("Invalid worksheet print settings");
  Pdf pdf(path);
  constexpr double column = 100, row = 20, head = 24;
  int across =
      int(std::floor((settings.width - 2 * settings.margin - 32) / column));
  int down =
      int(std::floor((settings.height - 2 * settings.margin - 2 * head) / row));
  if (across < 1 || down < 1)
    throw std::runtime_error("Print paper is too small for the worksheet");
  TextFont font;
  font.family = "monospace";
  font.size = 10;
  auto label = [&](const Text &text, double x, double y) {
    Save save(pdf.cr());
    cairo_translate(pdf.cr(), x, y);
    pdf.glyphs(pdf.shaper.shape(text, font));
  };
  for (int sheet = 0; sheet < book.sheet_count(); ++sheet) {
    if (settings.sheet >= 0 && settings.sheet != sheet)
      continue;
    int rows = std::max(1, book.rows(sheet)),
        cols = std::max(1, book.columns(sheet));
    if (uint64_t((rows + down - 1) / down) * ((cols + across - 1) / across) >
        10000 - pdf.result.pages)
      throw std::runtime_error(
          "Worksheet printout exceeds 10000 pages; choose a smaller worksheet");
    for (int r = 1; r <= rows; r += down)
      for (int c = 1; c <= cols; c += across) {
        pdf.page(settings.width, settings.height);
        label(book.sheet_name(sheet) + " - rows " + Text::number(r) + "-" +
                  Text::number(std::min(rows, r + down - 1)) + ", columns " +
                  Text::number(c) + "-" +
                  Text::number(std::min(cols, c + across - 1)),
              settings.margin, settings.margin + 12);
        for (int y = r; y <= std::min(rows, r + down - 1); ++y) {
          double top = settings.margin + head + (y - r) * row;
          label(Text::number(y), settings.margin, top + 14);
          for (int x = c; x <= std::min(cols, c + across - 1); ++x) {
            double left = settings.margin + 32 + (x - c) * column;
            color(pdf.cr(), {190, 190, 190});
            cairo_set_line_width(pdf.cr(), .4);
            cairo_rectangle(pdf.cr(), left, top, column, row);
            cairo_stroke(pdf.cr());
            Save cell(pdf.cr());
            clip(pdf.cr(), {left + 3, top + 1, column - 6, row - 2});
            label(book.display(sheet, x, y), left + 3, top + 14);
          }
        }
        pdf.end_page();
      }
  }
  pdf.finish();
  return pdf.result;
}
std::vector<CupsDestination> cups_destinations() {
  auto names = run({"lpstat", "-e"});
  std::vector<CupsDestination> destinations;
  Text preferred;
  try {
    auto status = run({"lpstat", "-d"});
    auto at = status.indexOf(": ");
    if (at >= 0)
      preferred = status.mid(at + 2).trimmed();
  } catch (const std::exception &) {
  }
  for (const auto &name : names.split('\n'))
    if (!name.trimmed().empty())
      destinations.push_back({name.trimmed(), name.trimmed() == preferred});
  return destinations;
}
void validate_print_pages(const Text &pages, size_t page_count) {
  if (pages.empty())
    return;
  if (pages.size() > 1024 ||
      !std::regex_match(pages.toStdString(),
                        std::regex("[1-9][0-9]*(?:-[1-9][0-9]*)?(?:,[1-9][0-9]*"
                                   "(?:-[1-9][0-9]*)?)*")))
    throw std::runtime_error("Use print pages such as 1-3,5");
  for (const auto &range : pages.split(',')) {
    auto ends = range.split('-');
    bool valid = false;
    auto first = ends[0].toLongLong(&valid);
    if (!valid || first < 1)
      throw std::runtime_error("Invalid print page number");
    auto last = ends.size() == 1 ? first : ends[1].toLongLong(&valid);
    if (!valid || last < first || (page_count && uint64_t(last) > page_count))
      throw std::runtime_error("Print page range is outside this document");
  }
}
Text cups_submit(const Text &pdf, const Text &destination, const Text &title,
                 int copies, const Text &pages, size_t page_count) {
  if (destination.empty() || destination.size() > 1024 || copies < 1 ||
      copies > 999)
    throw std::runtime_error("Invalid printer or copy count");
  validate_print_pages(pages, page_count);
  auto absolute = Text(std::filesystem::absolute(pdf.toStdString()).string());
  std::vector<Text> arguments = {"lp",         "-d", destination,          "-t",
                                 title,        "-n", Text::number(copies), "-o",
                                 "fit-to-page"};
  if (!pages.empty()) {
    arguments.push_back("-P");
    arguments.push_back(pages);
  }
  arguments.push_back(absolute);
  return run(arguments);
}
} // namespace DomeOffice
