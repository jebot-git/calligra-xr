// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_image_input.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Bytes PNG =
    Bytes::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAEklEQVR4"
                      "nGNQaHig0PCAAUIBACcOBgEpwnGPAAAAAElFTkSuQmCC");
const Bytes SVG_IMAGE =
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>)SVG";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
Bytes content(const Presentation &p) {
  for (const auto &shape : p.scene(0).shapes)
    if (!shape.master && !shape.source.isNull())
      return shape.source.ownerDocument().toByteArray();
  std::abort();
}
int find(const Presentation &p, const Text &name) {
  for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
    if (p.scene(0).shapes[i].name == name)
      return int(i);
  std::abort();
}
Map<Text, Bytes> archive(const Presentation &p, const Text &path) {
  auto copy = p;
  copy.save(path);
  return read_document_archive(path);
}
bool same_archive(const Map<Text, Bytes> &a, const Map<Text, Bytes> &b) {
  if (a.size() != b.size())
    return false;
  for (auto it = a.cbegin(); it != a.cend(); ++it)
    if (!b.contains(it.key()) || b.value(it.key()) != it.value())
      return false;
  return true;
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto master = child(child(styles.documentElement(), OFFICE, "master-styles"),
                      STYLE, "master-page");
  auto bar = styles.createElementNS(DRAW, "draw:rect");
  bar.setAttributeNS(SVG, "svg:width", "400pt");
  bar.setAttributeNS(SVG, "svg:height", "10pt");
  master.appendChild(bar);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.3" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:automatic-styles/><office:body><office:presentation><draw:page draw:name="Images" draw:master-page-name="Default" draw:nav-order="original"><draw:rect draw:name="Rectangle" xml:id="original" svg:width="60pt" svg:height="40pt"/><draw:frame draw:name="Rich" svg:x="10pt" svg:y="60pt" svg:width="150pt" svg:height="50pt"><draw:text-box><text:p>Native <text:span>Český Ω</text:span> source</text:p></draw:text-box></draw:frame><draw:frame draw:name="Missing" svg:x="200pt" svg:y="100pt" svg:width="20pt" svg:height="20pt"><draw:image xlink:href="./Pictures/native-image-1.png"/></draw:frame></draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry("Pictures/native-image-2.png", "existing opaque entry",
              "application/octet-stream");
  p.set_entry("opaque/empty", {}, "application/octet-stream");
  p.set_entry("opaque/retained", "original\0payload",
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 5 && Text(argv[1]) == "--document") {
    Presentation p;
    p.load(Text(argv[2]));
    auto image = load_image_input(Text(argv[3]));
    auto at = p.insert_image(0, image, {40, 80, 160, 100});
    assert(p.scene(0).shapes.at(at.shape).raster &&
           *p.scene(0).shapes.at(at.shape).raster->data == image.data());
    auto after = content(p);
    p.save(Text(argv[4]));
    assert(p.undo() && p.modified());
    assert(p.redo() && content(p) == after && !p.modified());
    p.save(Text(argv[4]));
    std::cout
        << "PASS independent image insertion, native history and ODP export\n";
    return 0;
  }
  auto png = prepare_image_input(PNG), svg = prepare_image_input(SVG_IMAGE);
  assert(png.width() == 2 && png.height() == 2 &&
         png.media_type() == "image/png" && png.extension() == "png");
  assert(svg.width() == 20 && svg.height() == 10 &&
         svg.media_type() == "image/svg+xml");
  for (const auto &bad : std::vector<Bytes>{
           {},
           "bad image",
           "BMbad",
           "GIF89a",
           Bytes(16 * 1024 * 1024 + 1, 'x'),
           R"(<svg width="0" height="10"/>)",
           R"(<svg width="20" height="10" viewBox="0 0 0 10"/>)"})
    rejects([&] { prepare_image_input(bad); });
  TempDir dir;
  auto input = dir.path() + "/input.odp", output = dir.path() + "/output.odp";
  auto package = fixture();
  package.save(input);
  Presentation p;
  p.load(input);
  auto original = archive(p, output);
  auto original_content = content(p);
  auto at = p.insert_image(0, png, {30, 40, 100, 50}, {0, 0, 0});
  auto &shape = p.scene(0).shapes.at(at.shape);
  assert(shape.image == "Pictures/native-image-3.png" && shape.raster &&
         *shape.raster->data == PNG);
  auto first_name = shape.name;
  auto first = archive(p, output);
  auto first_content = content(p);
  assert(first.size() == original.size() + 1 &&
         first.value(shape.image) == PNG);
  for (auto it = original.cbegin(); it != original.cend(); ++it)
    if (it.key() != "content.xml" && it.key() != "META-INF/manifest.xml")
      assert(first.value(it.key()) == it.value());
  auto manifest = read_document_xml(first.value("META-INF/manifest.xml"));
  bool found = false;
  for (auto e : children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (e.attributeNS(MANIFEST, "full-path") == shape.image) {
      assert(e.attributeNS(MANIFEST, "media-type") == "image/png");
      found = true;
    }
  assert(found);
  assert(p.scene(0).shapes.at(find(p, "Missing")).raster == nullptr);
  p.save(output);
  assert(!p.modified());
  auto second_at = p.insert_image(0, svg, {180, 40, 100, 50}, at);
  auto second_name = p.scene(0).shapes.at(second_at.shape).name;
  auto second = archive(p, output);
  p.move_shape(0, find(p, first_name), {10, 20});
  auto moved = archive(p, output);
  p.remove_shape(0, find(p, second_name));
  auto deleted = archive(p, output);
  int rich = p.scene(0).shapes.at(find(p, "Rich")).editor_index;
  p.set_box_text(0, rich, "Native Český Ω source edited", true);
  p.set_box_text(0, rich, "Native Český Ω source edited twice", true);
  assert(p.undo() && same_archive(archive(p, output), deleted));
  assert(p.undo() && same_archive(archive(p, output), moved));
  assert(p.undo() && same_archive(archive(p, output), second));
  assert(p.undo() && same_archive(archive(p, output), first) && !p.modified());
  assert(p.undo() && same_archive(archive(p, output), original) &&
         content(p) == original_content && p.modified());
  assert(p.redo() && same_archive(archive(p, output), first) &&
         content(p) == first_content && !p.modified());
  assert(p.redo() && same_archive(archive(p, output), second));
  assert(p.redo() && same_archive(archive(p, output), moved));
  assert(p.redo() && same_archive(archive(p, output), deleted));
  assert(p.redo() && p.scene(0).shapes.at(find(p, "Rich")).text ==
                         "Native Český Ω source edited twice");
  p.save(output);
  Presentation reopened;
  reopened.load(output);
  assert(reopened.image("Pictures/native-image-3.png") == PNG &&
         !reopened.modified() && !reopened.can_undo());
  // Branching after undo restores exact package parts and discards abandoned
  // assets.
  p.load(input);
  auto one = p.insert_image(0, png, {10, 20, 50, 40});
  assert(p.undo());
  auto revision = p.revision();
  for (auto rect : {SceneRect{0, 0, 0, 40}, SceneRect{1e8, 0, 40, 30}}) {
    rejects([&] { p.insert_image(0, svg, rect); });
    assert(p.revision() == revision && p.can_redo() &&
           same_archive(archive(p, output), original));
  }
  p.insert_image(0, svg, {10, 20, 50, 40});
  assert(!p.can_redo() && p.image("Pictures/native-image-3.png").empty());
  assert(p.undo() && same_archive(archive(p, output), original));
  // Archive quota failures must not commit XML, assets, history or saved state.
  auto crowded = fixture();
  std::vector<ArchivePartChange> changes;
  auto count = read_document_archive(input).size();
  for (size_t i = count; i < 10000; ++i)
    changes.push_back(
        {"extra-" + Text::number(i), false, true, 0, 0, 0, {}, {}});
  crowded.replay_part_changes(changes, true);
  crowded.validate_archive_limits();
  crowded.save(input);
  p.load(input);
  auto before = content(p);
  revision = p.revision();
  rejects([&] { p.insert_image(0, png, {10, 20, 50, 40}); });
  assert(content(p) == before && p.revision() == revision && !p.modified() &&
         !p.can_undo() && p.image("Pictures/native-image-3.png").empty());
  // Encoded asset bytes count against the same bounded history budget.
  package.save(input);
  p.load(input);
  Bytes padded = PNG;
  padded.append(size_t(16 * 1024 * 1024 - PNG.size() - 16), 'x');
  auto large = prepare_image_input(std::move(padded));
  p.insert_image(0, large, {10, 20, 50, 40});
  p.insert_image(0, large, {20, 30, 50, 40});
  assert(p.undo() && !p.can_undo() &&
         !p.image("Pictures/native-image-3.png").empty() &&
         p.image("Pictures/native-image-4.png").empty());
  assert(p.redo() && !p.image("Pictures/native-image-4.png").empty());
  package.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  package.save(input);
  p.load(input);
  rejects([&] { p.insert_image(0, png, {0, 0, 50, 40}); });
  assert(!p.modified() && !p.can_undo());
  rejects([&] { load_image_input(dir.path()); });
  rejects([&] { load_image_input(dir.path() + "/missing.png"); });
  std::cout << "PASS Stage embedded images, source/manifest/asset history, "
               "merge boundaries, savepoints, branch/quota/read-only failures "
               "and byte budget\n";
}
