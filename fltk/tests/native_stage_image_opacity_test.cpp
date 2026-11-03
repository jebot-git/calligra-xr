// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_paint.h"
#include <limits>
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text PRES = "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
XmlElement named(OdfPackage &p, const Text &tag, const Text &name) {
  for (auto node : p.xml.elementsByTagNameNS(DRAW, tag))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  std::abort();
}
Bytes source(const Presentation &p) {
  return p.scene(0).source.ownerDocument().toByteArray();
}
OdfPackage photo() {
  auto p = fixture();
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  if (common.isNull()) {
    common = styles.createElementNS(OFFICE, "office:styles");
    styles.documentElement().insertBefore(
        common, styles.documentElement().firstChild());
  }
  auto base = styles.createElementNS(STYLE, "style:style");
  base.setAttributeNS(STYLE, "style:name", "PhotoBase");
  base.setAttributeNS(STYLE, "style:family", "graphic");
  auto props = styles.createElementNS(STYLE, "style:graphic-properties");
  props.setAttributeNS(DRAW, "draw:fill", "solid");
  props.setAttributeNS(DRAW, "draw:fill-color", "#224466");
  props.setAttributeNS(DRAW, "draw:opacity", "25%");
  props.setAttributeNS(DRAW, "draw:image-opacity", "75%");
  base.appendChild(props);
  common.appendChild(base);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  auto automatic = p.xml.createElementNS(OFFICE, "office:automatic-styles");
  p.xml.documentElement().insertBefore(
      automatic, child(p.xml.documentElement(), OFFICE, "body"));
  auto direct = p.xml.createElementNS(STYLE, "style:style");
  direct.setAttributeNS(STYLE, "style:name", "PhotoAuto");
  direct.setAttributeNS(STYLE, "style:family", "graphic");
  direct.setAttributeNS(STYLE, "style:parent-style-name", "PhotoBase");
  auto override = p.xml.createElementNS(STYLE, "style:graphic-properties");
  override.setAttributeNS(DRAW, "draw:opacity", "50%");
  direct.appendChild(override);
  automatic.appendChild(direct);
  for (auto node : {named(p, "frame", "Y"), named(p, "rect", "X")})
    node.setAttributeNS(DRAW, "draw:style-name", "PhotoAuto");
  auto peer = p.xml.importNode(named(p, "frame", "Y"), true).toElement();
  peer.setAttributeNS(DRAW, "draw:name", "Peer");
  peer.setAttributeNS("http://www.w3.org/XML/1998/namespace", "xml:id", "peer");
  children(p.body(), DRAW, "page")[1].appendChild(peer);
  return p;
}
void independent_and_inherited() {
  TempDir dir;
  auto file = dir.path() + "/photo.odp", out = dir.path() + "/saved.odp";
  auto package = photo();
  package.save(file);
  Presentation p;
  p.load(file);
  const auto initial = source(p), styles = p.image("styles.xml"),
             manifest = p.image("META-INF/manifest.xml"),
             image = p.image("Pictures/image.svg");
  auto paint = p.scene(0).shapes[find(p, "Y")].paint;
  assert(paint.fill_color.alpha == .5 && paint.image_opacity == .75);
  auto info = p.object_paint(0, {ref(p, "X"), ref(p, "Y")});
  assert(info.objects == 2 && info.image_objects == 1 &&
         info.values.fill_opacity == .5 && info.values.image_opacity == .75);
  info = p.object_paint(0, {ref(p, "X")});
  assert(info.image_objects == 0 && !info.values.image_opacity);
  NativePaintFormat same;
  same.image_opacity = .5;
  assert(!p.set_object_paint(0, {ref(p, "X")}, same));
  assert(source(p) == initial && !p.modified());
  p.move_shape(0, find(p, "A"), {1, 0});
  p.undo();
  auto revision = p.revision();
  assert(p.can_redo());
  same.image_opacity = .75;
  assert(!p.set_object_paint(0, {ref(p, "Y")}, same));
  assert(p.can_redo() && p.revision() == revision && source(p) == initial);
  for (double n : {-1., 1.01, std::numeric_limits<double>::infinity(),
                   std::numeric_limits<double>::quiet_NaN()}) {
    NativePaintFormat bad;
    bad.image_opacity = n;
    rejects([&] { p.set_object_paint(0, {ref(p, "Y")}, bad); });
    assert(source(p) == initial && p.revision() == revision && p.can_redo());
  }
  auto group = ref(p, "X", 1);
  same.image_opacity = .5;
  auto at = p.set_object_paint(0, {group}, same);
  assert(at && at->objects.size() == 1);
  assert(p.scene(0).shapes[find(p, "Y")].paint.image_opacity == .5);
  assert(p.scene(0).shapes[find(p, "Y")].paint.fill_color.alpha == .5);
  assert(p.scene(0).shapes[find(p, "X")].source.attributeNS(
             DRAW, "style-name") == "PhotoAuto");
  assert(p.scene(1).shapes.back().paint.image_opacity == .75);
  assert(p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest &&
         p.image("Pictures/image.svg") == image);
  auto formatted = source(p);
  p.save(out);
  Presentation reopened;
  reopened.load(out);
  assert(source(reopened) == formatted &&
         reopened.scene(0).shapes[find(reopened, "Y")].paint.image_opacity ==
             .5);
  auto undo = p.undo();
  assert(undo && source(p) == initial && p.modified());
  auto redo = p.redo();
  assert(redo && source(p) == formatted && !p.modified());
  NativePaintFormat zero;
  zero.image_opacity = 0;
  assert(p.set_object_paint(0, {ref(p, "Y")}, zero));
  assert(p.scene(0).shapes[find(p, "Y")].paint.image_opacity == 0 &&
         p.scene(0).shapes[find(p, "Y")].paint.fill_color.alpha == .5);
}
void default_and_source_mapping() {
  TempDir dir;
  auto file = dir.path() + "/source.odp";
  for (auto pair : std::vector<std::pair<Text, Text>>{
           {"25%", ""}, {"25%", "50%"}, {"100%", "0%"}, {"", "75%"}}) {
    auto package = photo();
    auto styles = read_document_xml(package.entry("styles.xml"));
    auto base = child(child(styles.documentElement(), OFFICE, "styles"), STYLE,
                      "style");
    auto props = child(base, STYLE, "graphic-properties");
    for (auto key : {"opacity", "image-opacity"})
      props.removeAttributeNS(DRAW, key);
    if (!pair.first.empty())
      props.setAttributeNS(DRAW, "draw:opacity", pair.first);
    if (!pair.second.empty())
      props.setAttributeNS(DRAW, "draw:image-opacity", pair.second);
    package.set_entry("styles.xml", styles.toByteArray(), "text/xml");
    auto automatic =
        child(package.xml.documentElement(), OFFICE, "automatic-styles");
    automatic.removeChild(child(automatic, STYLE, "style"));
    named(package, "frame", "Y")
        .setAttributeNS(DRAW, "draw:style-name", "PhotoBase");
    named(package, "rect", "X")
        .setAttributeNS(DRAW, "draw:style-name", "PhotoBase");
    named(package, "frame", "Peer")
        .setAttributeNS(DRAW, "draw:style-name", "PhotoBase");
    package.save(file);
    Presentation p;
    p.load(file);
    auto paint = p.scene(0).shapes[find(p, "Y")].paint;
    const double expected_fill = pair.first == "25%" ? .25 : 1,
                 expected_image = pair.second == "50%"   ? .5
                                  : pair.second == "0%"  ? 0
                                  : pair.second == "75%" ? .75
                                                         : 1;
    assert(paint.fill_color.alpha == expected_fill &&
           paint.image_opacity == expected_image);
  }
  auto package = photo();
  auto styles = read_document_xml(package.entry("styles.xml"));
  auto base =
      child(child(styles.documentElement(), OFFICE, "styles"), STYLE, "style");
  auto props = child(base, STYLE, "graphic-properties");
  props.setAttributeNS(DRAW, "draw:opacity-name", "UnrenderedGradient");
  package.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  package.save(file);
  Presentation p;
  p.load(file);
  assert(std::find(p.scene(0).unsupported.begin(), p.scene(0).unsupported.end(),
                   Text("Opacity gradients")) != p.scene(0).unsupported.end());
  NativePaintFormat image;
  image.image_opacity = .5;
  assert(p.set_object_paint(0, {ref(p, "Y")}, image));
  const auto before = source(p);
  NativePaintFormat fill;
  fill.fill_opacity = .4;
  rejects([&] { p.set_object_paint(0, {ref(p, "Y")}, fill); });
  assert(source(p) == before);
}
void protected_and_readonly() {
  TempDir dir;
  auto file = dir.path() + "/protected.odp";
  auto package = photo();
  auto page = children(package.body(), DRAW, "page")[0];
  auto set = package.xml.createElementNS(DRAW, "draw:layer-set");
  page.insertBefore(set, page.firstChild());
  auto layer = package.xml.createElementNS(DRAW, "draw:layer");
  layer.setAttributeNS(DRAW, "draw:name", "HiddenImage");
  layer.setAttributeNS(DRAW, "draw:display", "none");
  layer.setAttributeNS(DRAW, "draw:protected", "true");
  set.appendChild(layer);
  named(package, "frame", "Y")
      .setAttributeNS(DRAW, "draw:layer", "HiddenImage");
  package.save(file);
  Presentation p;
  p.load(file);
  auto info = p.object_paint(0, {ref(p, "X", 1)});
  assert(!info.editable && info.image_objects == 1 &&
         info.values.image_opacity == .75);
  auto before = source(p);
  NativePaintFormat image;
  image.image_opacity = .5;
  rejects([&] { p.set_object_paint(0, {ref(p, "X", 1)}, image); });
  assert(source(p) == before && !p.modified());
  auto signedpack = photo();
  signedpack.set_entry("META-INF/documentsignatures.xml", "signature",
                       "text/xml");
  signedpack.save(file);
  p.load(file);
  before = source(p);
  assert(p.read_only());
  rejects([&] { p.set_object_paint(0, {ref(p, "Y")}, image); });
  assert(source(p) == before && !p.modified());
}
} // namespace
int main() {
  independent_and_inherited();
  default_and_source_mapping();
  protected_and_readonly();
  std::cout << "PASS independent ODF fill/image opacity, defaults/inheritance, "
               "image-only group projection, exact history, protected and "
               "read-only guards\n";
}
