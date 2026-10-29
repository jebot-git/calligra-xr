// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_image.h"
#include <FL/Fl.H>
#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/gl.h>
#include <charconv>
#include <list>
#include <sstream>
namespace DomeOffice {
namespace {
struct State {
  GLint mode;
  State(Fl_Gl_Window &window) {
    glGetIntegerv(GL_MATRIX_MODE, &mode);
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_TEXTURE_BIT |
                 GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, window.w(), window.h(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  }
  ~State() {
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(mode);
    glPopAttrib();
  }
};
struct DecodeLimit {
  size_t before = Fl_RGB_Image::max_size();
  DecodeLimit() { Fl_RGB_Image::max_size(std::min(before, size_t(64000000))); }
  ~DecodeLimit() { Fl_RGB_Image::max_size(before); }
};
bool gif_size(const Bytes &data) {
  auto word = [&](size_t p) {
    return unsigned(static_cast<unsigned char>(data[p])) |
           unsigned(static_cast<unsigned char>(data[p + 1])) << 8;
  };
  auto valid = [](unsigned w, unsigned h) {
    return w && h && w <= 4000 && h <= 4000 && uint64_t(w) * h <= 16000000;
  };
  if (data.size() < 13 || !valid(word(6), word(8)))
    return false;
  unsigned flags = static_cast<unsigned char>(data[10]);
  size_t p = 13 + ((flags & 128) ? 3u * (1u << ((flags & 7) + 1)) : 0);
  while (p < data.size()) {
    unsigned marker = static_cast<unsigned char>(data[p++]);
    if (marker == 0x2c)
      return p + 9 <= data.size() && valid(word(p + 4), word(p + 6));
    if (marker != 0x21 || p == data.size())
      return false;
    ++p;
    while (p < data.size()) {
      unsigned size = static_cast<unsigned char>(data[p++]);
      if (!size)
        break;
      if (size > data.size() - p)
        return false;
      p += size;
    }
  }
  return false;
}
std::optional<Bytes> svg_data(const Bytes &bytes) {
  XmlDocument xml;
  if (!xml.setContent(bytes))
    return {};
  auto root = xml.documentElement();
  if (root.localName() != "svg")
    return {};
  double view_w = 300, view_h = 150;
  if (root.hasAttribute("viewBox")) {
    auto box = root.attribute("viewBox").toStdString();
    std::replace(box.begin(), box.end(), ',', ' ');
    std::istringstream input(box);
    double x, y;
    std::string extra;
    if (!(input >> x >> y >> view_w >> view_h) || (input >> extra) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(view_w) ||
        !std::isfinite(view_h) || std::abs(x) > 1e9 || std::abs(y) > 1e9 ||
        view_w <= 0 || view_h <= 0 || view_w > 1e9 || view_h > 1e9)
      return {};
  }
  for (auto [name, extent] :
       std::initializer_list<std::pair<const char *, double>>{
           {"width", view_w}, {"height", view_h}}) {
    if (!root.hasAttribute(name))
      root.setAttribute(name, Text::number(extent, 'g', 17));
    auto value = root.attribute(name).trimmed().toUtf8();
    double n;
    auto result = std::from_chars(value.data(), value.data() + value.size(), n);
    if (result.ec != std::errc() || !std::isfinite(n) || n <= 0 || n > 1e9)
      return {};
    std::string_view unit(result.ptr,
                          size_t(value.data() + value.size() - result.ptr));
    double scale = unit == "%"    ? extent / 100
                   : unit == "in" ? 96
                   : unit == "cm" ? 96 / 2.54
                   : unit == "mm" ? 96 / 25.4
                   : unit == "pt" ? 96 / 72.
                   : unit == "pc" ? 16
                   : unit == "em" ? 16
                                  : 1;
    if (n * scale > 1e9)
      return {};
  }
  return xml.toByteArray();
}
std::unique_ptr<Fl_RGB_Image> decode(const Bytes &bytes, int w, int h) {
  DecodeLimit limit;
  auto data = reinterpret_cast<const unsigned char *>(bytes.data());
  if (bytes.startsWith("\x89PNG"))
    return std::make_unique<Fl_PNG_Image>(nullptr, data, bytes.size());
  if (bytes.startsWith("\xff\xd8"))
    return std::make_unique<Fl_JPEG_Image>(nullptr, data, bytes.size());
  if (bytes.startsWith("BM"))
    return std::make_unique<Fl_BMP_Image>(nullptr, data, bytes.size());
  if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")) {
    if (!gif_size(bytes))
      return {};
    Fl_GIF_Image gif(nullptr, data, bytes.size());
    if (gif.fail())
      return {};
    return std::make_unique<Fl_RGB_Image>(&gif);
  }
  auto svg = svg_data(bytes);
  if (!svg)
    return {};
  auto image = std::make_unique<Fl_SVG_Image>(
      nullptr, reinterpret_cast<const unsigned char *>(svg->data()),
      svg->size());
  if (image->fail())
    return {};
  image->proportional = false;
  image->resize(w, h);
  image->normalize();
  return image;
}
bool vector_data(const Bytes &data) {
  return !(data.startsWith("\x89PNG") || data.startsWith("\xff\xd8") ||
           data.startsWith("BM") || data.startsWith("GIF87a") ||
           data.startsWith("GIF89a"));
}
int bucket(double value, int max) {
  int result = 8;
  while (result < value && result < 4000)
    result *= 2;
  return std::max(1, std::min({result, max, 4000}));
}
} // namespace
struct GpuImageCache::Impl {
  using Key = std::tuple<std::string, int, int>;
  struct Entry {
    GLuint texture = 0;
    size_t bytes = 0;
    std::string error;
    std::list<Key>::iterator position;
  };
  std::map<Key, Entry> entries;
  std::list<Key> order;
  std::unique_ptr<Fl_Widget_Tracker> tracker;
  Fl_Gl_Window *window = nullptr;
  GLContext context = nullptr;
  size_t bytes = 0;
  uint64_t decodes = 0, uploads = 0;
  std::vector<std::string> errors;
  void clear() {
    if (window && tracker && !tracker->deleted() && context &&
        window->context() == context) {
      window->make_current();
      for (auto &[key, e] : entries)
        if (e.texture)
          glDeleteTextures(1, &e.texture);
    }
    entries.clear();
    order.clear();
    bytes = 0;
    errors.clear();
  }
  ~Impl() { clear(); }
  void prepare(Fl_Gl_Window &host) {
    auto live = std::find_if(entries.begin(), entries.end(), [](const auto &e) {
      return e.second.texture != 0;
    });
    if (!tracker || tracker->deleted() || window != &host ||
        context != host.context() ||
        (live != entries.end() && !glIsTexture(live->second.texture))) {
      clear();
      host.make_current();
      window = &host;
      tracker = std::make_unique<Fl_Widget_Tracker>(&host);
      context = host.context();
    }
  }
  void error(const std::string &message) {
    if (std::find(errors.begin(), errors.end(), message) == errors.end() &&
        errors.size() < 4096)
      errors.push_back(message);
  }
  void erase_old() {
    auto it = entries.find(order.front());
    if (it->second.texture)
      glDeleteTextures(1, &it->second.texture);
    bytes -= it->second.bytes;
    order.pop_front();
    entries.erase(it);
  }
  const Entry &get(const std::shared_ptr<const ImageAsset> &asset, int w,
                   int h) {
    if (!vector_data(*asset->data))
      w = h = 0;
    Key key{asset->fingerprint, w, h};
    if (auto it = entries.find(key); it != entries.end()) {
      order.splice(order.end(), order, it->second.position);
      return it->second;
    }
    while (entries.size() >= 4096)
      erase_old();
    order.push_back(key);
    auto position = entries.emplace(key, Entry{}).first;
    auto &entry = position->second;
    entry.position = std::prev(order.end());
    entry.error = "Cannot decode image";
    ++decodes;
    auto image = decode(*asset->data, w, h);
    if (!image || image->fail() || !image->data() || !image->data()[0])
      entry.error = "Cannot decode image";
    else {
      entry.error.clear();
      int width = image->w(), height = image->h(), depth = image->d();
      GLint max = 0;
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
      if (width < 1 || height < 1 || width > max || height > max || depth < 1 ||
          depth > 4 || uint64_t(width) * height > 16000000)
        entry.error = "Image exceeds native raster limits";
      else {
        size_t size = size_t(width) * height * 4;
        while (!entries.empty() && bytes + size > 128 * 1024 * 1024)
          erase_old();
        const auto *src =
            reinterpret_cast<const unsigned char *>(image->data()[0]);
        int stride = image->ld() ? image->ld() : width * depth;
        if (stride < width * depth)
          entry.error = "Invalid decoded image stride";
        else {
          std::vector<unsigned char> rgba(size);
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
              auto *out = rgba.data() + (size_t(y) * width + x) * 4;
              const auto *in = src + size_t(y) * stride + x * depth;
              out[0] = in[0];
              out[1] = depth < 3 ? in[0] : in[1];
              out[2] = depth < 3 ? in[0] : in[2];
              out[3] = depth == 2 ? in[1] : depth == 4 ? in[3] : 255;
            }
          glGenTextures(1, &entry.texture);
          glBindTexture(GL_TEXTURE_2D, entry.texture);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
          glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
          glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, rgba.data());
          glPopClientAttrib();
          GLint uploaded_width = 0;
          glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
                                   &uploaded_width);
          if (uploaded_width != width) {
            glDeleteTextures(1, &entry.texture);
            entry.texture = 0;
            entry.error = "Cannot upload image texture";
          } else {
            entry.bytes = size;
            bytes += size;
            ++uploads;
          }
        }
      }
    }
    return entry;
  }
  bool draw(const std::shared_ptr<const ImageAsset> &asset,
            const SceneTransform &transform, double width, double height,
            Fl_Gl_Window &host) {
    prepare(host);
    State state(host);
    GLint max = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    double dpi = std::max(double(host.pixel_w()) / host.w(),
                          double(host.pixel_h()) / host.h());
    double scale =
        std::sqrt(transform.a * transform.a + transform.b * transform.b +
                  transform.c * transform.c + transform.d * transform.d);
    GLuint texture = 0;
    if (asset && asset->data && !asset->data->empty()) {
      const auto &entry =
          get(asset, bucket(std::clamp(width * scale * dpi, 1., 4000.), max),
              bucket(std::clamp(height * scale * dpi, 1., 4000.), max));
      texture = entry.texture;
      if (!entry.error.empty())
        error(entry.error + ": " + asset->source.toStdString());
    } else
      error("Image data is unavailable");
    auto vertex = [&](double x, double y) {
      auto p = transform.map({x, y});
      glVertex2d(p.x, p.y);
    };
    if (texture) {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texture);
      glColor4d(1, 1, 1, 1);
      glBegin(GL_QUADS);
      glTexCoord2f(0, 0);
      vertex(0, 0);
      glTexCoord2f(1, 0);
      vertex(width, 0);
      glTexCoord2f(1, 1);
      vertex(width, height);
      glTexCoord2f(0, 1);
      vertex(0, height);
      glEnd();
      return true;
    }
    glDisable(GL_TEXTURE_2D);
    unsigned char r, g, b;
    Fl::get_color(FL_INACTIVE_COLOR, r, g, b);
    glColor4ub(r, g, b, 255);
    glLineWidth(1);
    glBegin(GL_LINE_LOOP);
    vertex(0, 0);
    vertex(width, 0);
    vertex(width, height);
    vertex(0, height);
    glEnd();
    glBegin(GL_LINES);
    vertex(0, 0);
    vertex(width, height);
    vertex(0, height);
    vertex(width, 0);
    glEnd();
    return false;
  }
};
GpuImageCache::GpuImageCache() : impl_(std::make_unique<Impl>()) {}
GpuImageCache::~GpuImageCache() = default;
void GpuImageCache::begin_frame(Fl_Gl_Window &host) {
  impl_->prepare(host);
  impl_->errors.clear();
}
bool GpuImageCache::draw(const std::shared_ptr<const ImageAsset> &asset,
                         const SceneTransform &t, double w, double h,
                         Fl_Gl_Window &host) {
  return impl_->draw(asset, t, w, h, host);
}
void GpuImageCache::clear() { impl_->clear(); }
uint64_t GpuImageCache::decodes() const { return impl_->decodes; }
uint64_t GpuImageCache::uploads() const { return impl_->uploads; }
size_t GpuImageCache::texture_bytes() const { return impl_->bytes; }
const std::vector<std::string> &GpuImageCache::errors() const {
  return impl_->errors;
}
} // namespace DomeOffice
