// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_image.h"
#include "native_image_decode.h"
#include "gpu_clip.h"
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
    if (!image_is_vector(*asset->data))
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
    auto image = decode_image(*asset->data, w, h);
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
            Fl_Gl_Window &host, std::optional<TextRect> clip) {
    if (width <= 0 || height <= 0 ||
        (clip && (clip->width <= 0 || clip->height <= 0)))
      return true;
    double left = 0, top = 0, right = width, bottom = height;
    if (clip) {
      left = std::max(left, clip->x);
      top = std::max(top, clip->y);
      right = std::min(right, clip->x + clip->width);
      bottom = std::min(bottom, clip->y + clip->height);
    }
    if (right <= left || bottom <= top)
      return true;
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
      glTexCoord2d(left / width, top / height);
      vertex(left, top);
      glTexCoord2d(right / width, top / height);
      vertex(right, top);
      glTexCoord2d(right / width, bottom / height);
      vertex(right, bottom);
      glTexCoord2d(left / width, bottom / height);
      vertex(left, bottom);
      glEnd();
      return true;
    }
    glDisable(GL_TEXTURE_2D);
    unsigned char r, g, b;
    Fl::get_color(FL_INACTIVE_COLOR, r, g, b);
    glColor4ub(r, g, b, 255);
    glLineWidth(1);
    if (!clip) {
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
    } else {
      auto line = [&](ClipPoint a, ClipPoint b) {
        if (clip_segment(a, b, clip)) {
          vertex(a.x, a.y);
          vertex(b.x, b.y);
        }
      };
      glBegin(GL_LINES);
      line({0, 0}, {width, 0});
      line({width, 0}, {width, height});
      line({width, height}, {0, height});
      line({0, height}, {0, 0});
      line({0, 0}, {width, height});
      line({0, height}, {width, 0});
      glEnd();
    }
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
                         Fl_Gl_Window &host, std::optional<TextRect> clip) {
  return impl_->draw(asset, t, w, h, host, clip);
}
void GpuImageCache::clear() { impl_->clear(); }
uint64_t GpuImageCache::decodes() const { return impl_->decodes; }
uint64_t GpuImageCache::uploads() const { return impl_->uploads; }
size_t GpuImageCache::texture_bytes() const { return impl_->bytes; }
const std::vector<std::string> &GpuImageCache::errors() const {
  return impl_->errors;
}
} // namespace DomeOffice
