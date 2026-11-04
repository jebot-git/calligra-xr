// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
#include "native_image.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F action) {
  bool caught = false;
  try {
    action();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
std::map<Text, Bytes> parts(const OdfPackage &p) {
  std::map<Text, Bytes> out;
  for (auto name : p.entry_names())
    out[name] = p.entry(name);
  return out;
}
void write(const Text &path, const Bytes &bytes) {
  std::ofstream stream(path.toStdString(), std::ios::binary);
  stream.write(bytes.data(), bytes.size());
  assert(stream.good());
}
Bytes read(const Text &path) {
  std::ifstream stream(path.toStdString(), std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), {}};
}
Bytes bad_crc(Bytes data, const Bytes &part) {
  // Independent ZIP32 central-directory mutation, including the zero-byte
  // entry which requires an explicit EOF read to verify its checksum.
  size_t at = 0;
  while ((at = data.find(Bytes("PK\1\2", 4), at)) != Bytes::npos) {
    assert(at + 46 <= data.size());
    size_t length =
        uint8_t(data[at + 28]) + (size_t(uint8_t(data[at + 29])) << 8);
    if (data.substr(at + 46, length) == part) {
      data[at + 16] ^= 1;
      return data;
    }
    at += 46;
  }
  std::abort();
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 5 &&
      (Text(argv[1]) == "--roundtrip" || Text(argv[1]) == "--file-roundtrip")) {
    try {
      OdfPackage p(Text(argv[2]) == "words"    ? Kind::Words
                   : Text(argv[2]) == "sheets" ? Kind::Sheets
                                               : Kind::Stage);
      if (Text(argv[1]) == "--roundtrip")
        p.load_bytes(read(argv[3]));
      else
        p.load(argv[3]);
      write(argv[4], p.archive_bytes());
      return 0;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  TempDir temp;
  const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
  for (Kind kind : {Kind::Words, Kind::Sheets, Kind::Stage}) {
    OdfPackage p(kind);
    auto node =
        p.xml.createElementNS(kind == Kind::Stage ? DRAW : TEXT,
                              kind == Kind::Stage ? "draw:page" : "text:p");
    if (kind != Kind::Sheets)
      p.body().appendChild(node);
    p.set_entry("opaque/binary", Bytes("hello\0world\xff", 12),
                "application/octet-stream");
    p.set_entry("empty.bin", {}, "application/octet-stream");
    p.set_entry(
        "Pictures/a.svg",
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="2" height="1"/>)",
        "image/svg+xml");
    auto image = p.image_asset("Pictures/a.svg");
    assert(image);
    auto xml = p.xml.toByteArray();
    auto before = parts(p);
    OdfPackage checkpoint = p;
    auto bytes = p.archive_bytes();
    assert(p.xml.toByteArray() == xml && parts(p) == before);
    assert(p.part_changes_from(checkpoint).empty());
    assert(p.image_asset("Pictures/a.svg") == image);
    OdfPackage reopened(kind);
    reopened.load_bytes(bytes);
    assert(reopened.xml.toByteArray() == xml);
    assert(reopened.entry("opaque/binary") == before.at("opaque/binary"));
    assert(reopened.entry_data("empty.bin") &&
           reopened.entry("empty.bin").empty());
    auto saved = temp.path() + "/file." + suffix(kind);
    p.save(saved);
    auto from_file = read_document_archive(saved),
         from_memory = read_document_archive_bytes(bytes);
    assert(from_file.keys() == from_memory.keys());
    for (auto name : from_file.keys())
      assert(from_file.value(name) == from_memory.value(name));
    auto stable_xml = reopened.xml.toByteArray();
    auto stable_parts = parts(reopened);
    for (const auto &part : {Bytes("opaque/binary"), Bytes("empty.bin")}) {
      auto damaged = bad_crc(bytes, part);
      rejects([&] { reopened.load_bytes(damaged); });
      assert(reopened.xml.toByteArray() == stable_xml &&
             parts(reopened) == stable_parts);
    }
    for (const auto &bad :
         {Bytes(), Bytes("not a zip"), Bytes(bytes.substr(0, 30))}) {
      rejects([&] { reopened.load_bytes(bad); });
      assert(reopened.xml.toByteArray() == stable_xml &&
             parts(reopened) == stable_parts);
    }
    OdfPackage other(kind == Kind::Stage ? Kind::Words : Kind::Stage);
    rejects([&] { reopened.load_bytes(other.archive_bytes()); });
    assert(reopened.xml.toByteArray() == stable_xml &&
           parts(reopened) == stable_parts);
    auto manifest = read_document_xml(p.entry("META-INF/manifest.xml"));
    manifest.documentElement().removeAttributeNS(MANIFEST, "version");
    auto raw = manifest.toByteArray();
    auto prior = p.entry("META-INF/manifest.xml");
    p.replay_part_changes({{"META-INF/manifest.xml", true, true, 0,
                            prior.size(), raw.size(), prior, raw}},
                          true);
    auto repaired = read_document_archive_bytes(p.archive_bytes());
    assert(p.entry("META-INF/manifest.xml") == raw);
    assert(read_document_xml(repaired.value("META-INF/manifest.xml"))
               .documentElement()
               .attributeNS(MANIFEST, "version") == "1.2");
  }
  OdfPackage signed_source(Kind::Stage);
  signed_source.set_entry("META-INF/documentsignatures.xml", "opaque signature",
                          "text/xml");
  OdfPackage signed_loaded(Kind::Stage);
  signed_loaded.load_bytes(signed_source.archive_bytes());
  assert(signed_loaded.read_only());
  auto signed_parts = parts(signed_loaded);
  rejects([&] { signed_loaded.archive_bytes(); });
  assert(parts(signed_loaded) == signed_parts);
  std::cout
      << "PASS memory ODT/ODS/ODP round trips, detached exports, binary/empty "
         "parts, atomic failed loads, manifest repair and signed protection\n";
}
