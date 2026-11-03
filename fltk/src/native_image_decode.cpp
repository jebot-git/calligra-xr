// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_image_decode.h"
#include "native_image_input.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "odf_package.h"
#include <FL/Fl_BMP_Image.H>
#include <FL/Fl_GIF_Image.H>
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <charconv>
#include <sstream>
namespace DomeOffice {
namespace {
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
} // namespace
std::unique_ptr<Fl_RGB_Image> decode_image(const Bytes &bytes, int w, int h) {
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
bool image_is_vector(const Bytes &data) {
  return !(data.startsWith("\x89PNG") || data.startsWith("\xff\xd8") ||
           data.startsWith("BM") || data.startsWith("GIF87a") ||
           data.startsWith("GIF89a"));
}
NativeImageInput prepare_image_input(Bytes bytes) {
  if (bytes.empty() || bytes.size() > 16 * 1024 * 1024)
    throw std::runtime_error("Choose an image no larger than 16 MiB");
  DecodeLimit limit;
  Text media, extension;
  std::unique_ptr<Fl_RGB_Image> image;
  if (bytes.startsWith("\x89PNG")) {
    media = "image/png";
    extension = "png";
  } else if (bytes.startsWith("\xff\xd8")) {
    media = "image/jpeg";
    extension = "jpg";
  } else if (bytes.startsWith("BM")) {
    media = "image/bmp";
    extension = "bmp";
  } else if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")) {
    media = "image/gif";
    extension = "gif";
  }
  int width = 0, height = 0;
  if (!media.empty()) {
    image = decode_image(bytes, 1, 1);
    if (image) {
      width = image->w();
      height = image->h();
    }
    if (!image || image->fail() || width <= 0 || height <= 0 || width > 4000 ||
        height > 4000 || uint64_t(width) * height > 16000000 || !image->array)
      throw std::runtime_error(
          "The image is damaged or exceeds the native pixel limit");
  } else {
    auto svg = svg_data(bytes);
    if (!svg)
      throw std::runtime_error(
          "Choose a valid PNG, JPEG, SVG, GIF or BMP image");
    auto vector = std::make_unique<Fl_SVG_Image>(
        nullptr, reinterpret_cast<const unsigned char *>(svg->data()),
        svg->size());
    width = vector->w();
    height = vector->h();
    if (vector->fail() || width <= 0 || height <= 0)
      throw std::runtime_error("The SVG image has no usable dimensions");
    // Validate rendering at a bounded resolution, retaining original vector
    // bytes.
    vector->proportional = false;
    vector->resize(64, 64);
    vector->normalize();
    if (vector->fail() || !vector->array)
      throw std::runtime_error("The SVG image cannot be rendered");
    media = "image/svg+xml";
    extension = "svg";
  }
  return NativeImageInput(std::move(bytes), media, extension, width, height);
}
NativeImageInput load_image_input(const Text &path) {
  int fd = ::open(path.toUtf8().c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    throw std::runtime_error("Cannot open the selected image");
  struct Close {
    int fd;
    ~Close() { ::close(fd); }
  } close{fd};
  struct stat info;
  if (::fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      info.st_size > 16 * 1024 * 1024)
    throw std::runtime_error(
        "Choose a regular image file no larger than 16 MiB");
  Bytes bytes;
  char buffer[8192];
  for (;;) {
    auto n = ::read(fd, buffer, sizeof buffer);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("Cannot read the selected image");
    }
    if (!n)
      break;
    if (bytes.size() + size_t(n) > 16 * 1024 * 1024)
      throw std::runtime_error("The image exceeds 16 MiB");
    bytes.append(buffer, size_t(n));
  }
  return prepare_image_input(std::move(bytes));
}
} // namespace DomeOffice
