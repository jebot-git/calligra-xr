// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_fragment.h"
#include <numeric>
#include <source_location>
#include <nlohmann/json.hpp>
namespace {
template <class F>
void must_reject(F f,
                 std::source_location at = std::source_location::current()) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  std::cerr << "Expected rejection at " << at.file_name() << ':' << at.line()
            << '\n';
  std::abort();
}
NativeObjectRef object(const SlideScene &scene, const Text &name,
                       int ancestors = 0) {
  for (size_t i = 0; i < scene.shapes.size(); ++i)
    if (scene.shapes[i].name == name)
      return {int(i), ancestors};
  std::abort();
}
std::vector<Text> names(const NativeStageFragment &fragment) {
  OdfPackage package(Kind::Stage);
  package.load_bytes(fragment.archive);
  std::vector<Text> result;
  for (auto node : stage_fragment_nodes(package, fragment.selection))
    result.push_back(node.attributeNS(DRAW, "name"));
  return result;
}
void number(Bytes &out, uint64_t value, size_t size) {
  for (size_t i = size; i; --i)
    out.push_back(char(value >> ((i - 1) * 8)));
}
// Independent protocol constructor, not the implementation's encoder.
Bytes envelope(const Bytes &archive,
               const std::vector<NativeStageObjectPath> &paths) {
  Bytes out("CStage\0\1", 8);
  number(out, archive.size(), 8);
  number(out, paths.size(), 4);
  for (const auto &path : paths) {
    number(out, path.size(), 2);
    for (auto rank : path)
      number(out, rank, 4);
  }
  out += archive;
  return out;
}
void set(Bytes &out, size_t at, uint64_t value, size_t size) {
  for (size_t i = 0; i < size; ++i)
    out[at + size - 1 - i] = char(value >> (i * 8));
}
OdfPackage simple() {
  OdfPackage p(Kind::Stage);
  auto page = p.xml.createElementNS(DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:name", "Original");
  page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
  p.body().appendChild(page);
  for (auto name : {Text("Chosen"), Text("NotChosen")}) {
    page.appendChild(p.xml.createTextNode("\n "));
    auto r = p.xml.createElementNS(DRAW, "draw:rect");
    r.setAttributeNS(DRAW, "draw:name", name);
    r.setAttributeNS(SVG, "svg:width", "60pt");
    r.setAttributeNS(SVG, "svg:height", "40pt");
    auto text = p.xml.createElementNS(TEXT, "text:p");
    set_plain_text(text, name == "Chosen" ? Text("Český λ 😀")
                                          : Text("UNSELECTED_TEXT"));
    r.appendChild(text);
    page.appendChild(r);
  }
  p.set_entry("opaque/unselected", Bytes("UNSELECTED_BINARY\0", 18),
              "application/octet-stream");
  p.set_entry(
      "meta.xml",
      R"(<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" office:version="1.2"><office:meta/></office:document-meta>)",
      "text/xml");
  return p;
}
void save(const Text &path, const Bytes &bytes) {
  std::ofstream stream(path.toStdString(), std::ios::binary);
  stream.write(bytes.data(), bytes.size());
  assert(stream.good());
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 4 && Text(argv[1]) == "--canonicalize") {
    try {
      if (std::filesystem::file_size(argv[2]) > 64 * 1024 * 1024)
        throw std::runtime_error("Oversized test clipboard input");
      std::ifstream input(argv[2], std::ios::binary);
      if (!input)
        throw std::runtime_error("Cannot open test clipboard input");
      Bytes wire{std::string(std::istreambuf_iterator<char>(input), {})};
      auto decoded = decode_stage_fragment(wire);
      auto encoded = encode_stage_fragment(decoded);
      auto object_names = names(decoded);
      std::vector<std::string> named;
      for (const auto &name : object_names)
        named.push_back(name.toStdString());
      save(argv[3], encoded);
      std::cout << nlohmann::json{{"selection", decoded.selection},
                                  {"names", named},
                                  {"text", decoded.text.toStdString()}}
                       .dump()
                << '\n';
      return 0;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  auto p = fixture();
  auto model = read_presentation_scenes(p).at(0);
  auto children =
      native_stage_fragment(p, model, {object(model, "Y"), object(model, "X")});
  auto group = native_stage_fragment(p, model, {object(model, "X", 1)});
  assert(names(children) == std::vector<Text>({"Y", "X"}));
  assert(names(group) == std::vector<Text>({"Outer"}));
  auto decoded = decode_stage_fragment(encode_stage_fragment(children));
  assert(names(decoded) == std::vector<Text>({"Y", "X"}) &&
         decoded.text == children.text);
  auto decoded_group = decode_stage_fragment(encode_stage_fragment(group));
  assert(names(decoded_group) == std::vector<Text>({"Outer"}));
  OdfPackage unpacked(Kind::Stage);
  unpacked.load_bytes(decoded.archive);
  assert(unpacked.entry("Pictures/image.svg") == p.entry("Pictures/image.svg"));
  assert(!unpacked.entry_data("opaque/source"));

  auto independent = simple();
  auto archive = independent.archive_bytes();
  auto wire = envelope(archive, {{0}});
  auto sanitized = decode_stage_fragment(wire);
  assert(names(sanitized) == std::vector<Text>({"Chosen"}) &&
         sanitized.text == "Český λ 😀");
  OdfPackage clean(Kind::Stage);
  clean.load_bytes(sanitized.archive);
  assert(clean.entry_names().size() == 4 && !clean.entry_data("meta.xml") &&
         !clean.entry_data("opaque/unselected"));
  assert(clean.xml.toByteArray().find("UNSELECTED") == Bytes::npos);
  NativeStageFragment forged{archive, "Untrusted text fallback", {{0}}};
  auto encoded = encode_stage_fragment(forged);
  auto encoded_checked = decode_stage_fragment(encoded);
  assert(encoded_checked.text == "Český λ 😀" &&
         names(encoded_checked) == std::vector<Text>({"Chosen"}));
  // Inspect the encoder's ZIP bytes directly: sanitizing only on decode would
  // still publish unrelated source content to another clipboard consumer.
  size_t offset = 20;
  auto count = (uint32_t(uint8_t(encoded[16])) << 24) |
               (uint32_t(uint8_t(encoded[17])) << 16) |
               (uint32_t(uint8_t(encoded[18])) << 8) | uint8_t(encoded[19]);
  assert(count == 1);
  auto length =
      (uint16_t(uint8_t(encoded[offset])) << 8) | uint8_t(encoded[offset + 1]);
  offset += 2 + 4 * length;
  OdfPackage advertised(Kind::Stage);
  advertised.load_bytes(encoded.substr(offset));
  assert(advertised.entry_names().size() == 4 &&
         !advertised.entry_data("opaque/unselected"));

  for (size_t i = 0; i < wire.size(); ++i)
    must_reject([&] { decode_stage_fragment(Bytes(wire.substr(0, i))); });
  for (size_t at : {size_t(0), size_t(6), size_t(7)}) {
    auto bad = wire;
    bad[at] ^= 1;
    must_reject([&] { decode_stage_fragment(bad); });
  }
  auto trailing = wire + Bytes("x");
  must_reject([&] { decode_stage_fragment(trailing); });
  for (uint64_t n : {uint64_t(0), uint64_t(64 * 1024 * 1024 + 1), UINT64_MAX}) {
    auto bad = wire;
    set(bad, 8, n, 8);
    must_reject([&] { decode_stage_fragment(bad); });
  }
  for (uint64_t n : {uint64_t(0), uint64_t(100001), uint64_t(UINT32_MAX)}) {
    auto bad = wire;
    set(bad, 16, n, 4);
    must_reject([&] { decode_stage_fragment(bad); });
  }
  for (uint64_t n : {uint64_t(0), uint64_t(257)}) {
    auto bad = wire;
    set(bad, 20, n, 2);
    must_reject([&] { decode_stage_fragment(bad); });
  }
  auto bad_rank = wire;
  set(bad_rank, 22, 1000001, 4);
  must_reject([&] { decode_stage_fragment(bad_rank); });
  for (auto paths : {std::vector<NativeStageObjectPath>{{0}, {0}},
                     {{0}, {0, 0}},
                     {{9}},
                     {{0, 0}}})
    must_reject([&] { decode_stage_fragment(envelope(archive, paths)); });
  must_reject([&] { encode_stage_fragment({archive, {}, {}}); });
  auto page = independent.xml.createElementNS(DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
  independent.body().appendChild(page);
  must_reject([&] {
    decode_stage_fragment(envelope(independent.archive_bytes(), {{0}}));
  });
  OdfPackage words(Kind::Words);
  must_reject(
      [&] { decode_stage_fragment(envelope(words.archive_bytes(), {{0}})); });
  // ZIP readers can legally ignore a changed local-header prefix; use an
  // independently framed non-ZIP payload for this archive-rejection check.
  must_reject(
      [&] { decode_stage_fragment(envelope("not an ODP ZIP", {{0}})); });

  OdfPackage wide(Kind::Stage);
  auto wide_page = wide.xml.createElementNS(DRAW, "draw:page");
  wide.body().appendChild(wide_page);
  std::vector<NativeStageObjectPath> reverse;
  for (uint32_t i = 0; i < 10000; ++i) {
    auto node = wide.xml.createElementNS(DRAW, "draw:rect");
    node.setAttributeNS(DRAW, "draw:name", Text::number(i));
    wide_page.appendChild(node);
    reverse.push_back({9999 - i});
  }
  auto resolved = stage_fragment_nodes(wide, reverse);
  assert(resolved.size() == 10000);
  for (size_t i = 0; i < resolved.size(); ++i)
    assert(resolved[i].attributeNS(DRAW, "name") == Text::number(9999 - i));
  must_reject([&] {
    stage_fragment_nodes(wide, std::vector<NativeStageObjectPath>(
                                   3907, NativeStageObjectPath(256, 0)));
  });
  Bytes too_large(64 * 1024 * 1024 + 1, 'x');
  must_reject([&] { decode_stage_fragment(too_large); });

  if (argc == 3 && Text(argv[1]) == "--documents") {
    Text directory = argv[2];
    std::filesystem::create_directories(directory.toStdString());
    save(directory + "/children.odp", children.archive);
    save(directory + "/children.cstage", encode_stage_fragment(children));
    save(directory + "/group.odp", group.archive);
    save(directory + "/group.cstage", encode_stage_fragment(group));
    save(directory + "/chosen.odp", sanitized.archive);
    save(directory + "/chosen.cstage", encode_stage_fragment(sanitized));
    save(directory + "/children.txt", children.text.toUtf8());
    save(directory + "/group.txt", group.text.toUtf8());
    save(directory + "/chosen.txt", sanitized.text.toUtf8());
  }
  std::cout << "PASS native clipboard envelope version/quotas, selection "
               "order/group identity, source canonicalization, corrupt input "
               "and wide linear traversal\n";
}
