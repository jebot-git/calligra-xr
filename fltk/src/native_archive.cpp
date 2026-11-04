// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
#include <zip.h>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
struct ZipError {
  zip_error_t value;
  ZipError() { zip_error_init(&value); }
  ~ZipError() { zip_error_fini(&value); }
};
} // namespace
Bytes OdfPackage::archive_bytes() const {
  const auto contents = serialized_entries();
  ZipError error;
  std::unique_ptr<zip_source_t, decltype(&zip_source_free)> source(
      zip_source_buffer_create(nullptr, 0, 0, &error.value), zip_source_free);
  if (!source)
    fail("Cannot create memory document source");
  std::unique_ptr<zip_t, decltype(&zip_discard)> archive(
      zip_open_from_source(source.get(), ZIP_CREATE | ZIP_TRUNCATE,
                           &error.value),
      zip_discard);
  if (!archive)
    fail("Cannot create memory document archive");
  // Both the live archive and the source reader below own a reference.
  zip_source_keep(source.get());
  auto add = [&](const Text &name, const Bytes &data) {
    auto *part = zip_source_buffer(archive.get(), data.data(), data.size(), 0);
    if (!part)
      fail("Cannot prepare memory document part");
    auto index = zip_file_add(archive.get(), name.toUtf8().c_str(), part,
                              ZIP_FL_ENC_UTF_8);
    if (index < 0) {
      zip_source_free(part);
      fail("Cannot add memory document part");
    }
    if (name == "mimetype" &&
        zip_set_file_compression(archive.get(), index, ZIP_CM_STORE, 0))
      fail("Cannot store memory document media type");
  };
  if (!contents.contains("mimetype"))
    fail("Memory document has no media type");
  add("mimetype", *contents.value("mimetype"));
  for (auto it = contents.begin(); it != contents.end(); ++it)
    if (it.key() != "mimetype")
      add(it.key(), *it.value());
  if (zip_close(archive.get()))
    fail("Cannot finish memory document archive");
  archive.release();
  zip_stat_t info;
  zip_stat_init(&info);
  if (zip_source_stat(source.get(), &info) || !(info.valid & ZIP_STAT_SIZE) ||
      info.size > 272 * 1024 * 1024)
    fail("Memory document archive exceeds the serialization limit");
  if (zip_source_open(source.get()))
    fail("Cannot read memory document archive");
  Bytes bytes;
  bytes.resize(size_t(info.size));
  size_t offset = 0;
  while (offset < bytes.size()) {
    auto n = zip_source_read(source.get(), bytes.data() + offset,
                             bytes.size() - offset);
    if (n <= 0) {
      zip_source_close(source.get());
      fail("Cannot read complete memory document archive");
    }
    offset += size_t(n);
  }
  if (zip_source_close(source.get()))
    fail("Cannot close memory document archive source");
  return bytes;
}
} // namespace DomeOffice
