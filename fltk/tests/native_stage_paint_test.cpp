// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_paint.h"
#include <limits>
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text PRES = "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
Bytes bytes(const Presentation &p) {
  return p.scene(0).source.ownerDocument().toByteArray();
}
XmlElement named(OdfPackage &p, const Text &name) {
  for (auto tag : {"rect", "frame", "line"})
    for (auto n : p.xml.elementsByTagNameNS(DRAW, tag))
      if (n.attributeNS(DRAW, "name") == name)
        return n;
  std::abort();
}
OdfPackage painted() {
  auto p = fixture();
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  if (common.isNull()) {
    common = styles.createElementNS(OFFICE, "office:styles");
    styles.documentElement().insertBefore(
        common, styles.documentElement().firstChild());
  }
  auto base = styles.createElementNS(STYLE, "style:style");
  base.setAttributeNS(STYLE, "style:name", "Shared");
  base.setAttributeNS(STYLE, "style:family", "graphic");
  auto paint = styles.createElementNS(STYLE, "style:graphic-properties");
  paint.setAttributeNS(DRAW, "draw:fill", "solid");
  paint.setAttributeNS(DRAW, "draw:fill-color", "#113355");
  paint.setAttributeNS(DRAW, "draw:opacity", "80%");
  paint.setAttributeNS(DRAW, "draw:stroke", "solid");
  paint.setAttributeNS(SVG, "svg:stroke-color", "#224466");
  paint.setAttributeNS(SVG, "svg:stroke-width", "2pt");
  paint.setAttributeNS(FO, "fo:padding-left", "7pt");
  base.appendChild(paint);
  auto text = styles.createElementNS(STYLE, "style:text-properties");
  text.setAttributeNS(FO, "fo:font-size", "19pt");
  base.appendChild(text);
  common.appendChild(base);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  auto automatic = p.xml.createElementNS(OFFICE, "office:automatic-styles");
  p.xml.documentElement().insertBefore(
      automatic, child(p.xml.documentElement(), OFFICE, "body"));
  auto direct = p.xml.createElementNS(STYLE, "style:style");
  direct.setAttributeNS(STYLE, "style:name", "Direct");
  direct.setAttributeNS(STYLE, "style:family", "graphic");
  direct.setAttributeNS(STYLE, "style:parent-style-name", "Shared");
  auto props = p.xml.createElementNS(STYLE, "style:graphic-properties");
  props.setAttributeNS(DRAW, "draw:fill-color", "#990011");
  props.setAttributeNS(FO, "fo:padding-right", "9pt");
  direct.appendChild(props);
  automatic.appendChild(direct);
  named(p, "A").setAttributeNS(DRAW, "draw:style-name", "Direct");
  named(p, "B").setAttributeNS(DRAW, "draw:style-name", "Shared");
  named(p, "X").setAttributeNS(DRAW, "draw:style-name", "Shared");
  named(p, "C").setAttributeNS(DRAW, "draw:style-name", "Shared");
  children(p.body(), DRAW, "page")[1].firstChild().setAttributeNS(
      DRAW, "draw:style-name", "Shared");
  return p;
}
NativePaintFormat patch() {
  NativePaintFormat p;
  p.fill = "solid";
  p.fill_color = "#12AB34";
  p.fill_opacity = .5;
  p.stroke = "solid";
  p.stroke_color = "#ABCDEF";
  p.stroke_opacity = .25;
  p.stroke_width = 6;
  p.even_odd = true;
  return p;
}
void exact_transactions() {
  TempDir dir;
  auto source = dir.path() + "/source.odp", saved = dir.path() + "/saved.odp";
  auto package = painted();
  package.save(source);
  Presentation p;
  p.load(source);
  auto initial = bytes(p), styles = p.image("styles.xml"),
       manifest = p.image("META-INF/manifest.xml"),
       asset = p.image("Pictures/image.svg"), opaque = p.image("opaque/source");
  auto index = find(p, "A");
  auto info = p.object_paint(0, {ref(p, "A"), ref(p, "B")});
  assert(info.objects == 2 && info.editable && !info.values.fill_color &&
         info.values.fill_opacity == .8 && info.values.stroke_width == 2);
  p.move_shape(0, index, {1, 0});
  p.undo();
  auto revision = p.revision();
  assert(p.can_redo());
  assert(!p.set_object_paint(0, {ref(p, "A")}, {}));
  NativePaintFormat same;
  same.fill_color = "#990011";
  same.stroke_width = 2;
  assert(!p.set_object_paint(0, {ref(p, "A")}, same));
  assert(bytes(p) == initial && p.revision() == revision && p.can_redo());
  auto result = p.set_object_paint(0, {ref(p, "A"), ref(p, "B")}, patch());
  assert(result && result->objects.size() == 2 && p.modified());
  auto formatted = bytes(p);
  assert(formatted != initial && p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest);
  for (auto name : {"A", "B"}) {
    auto &s = p.scene(0).shapes[find(p, name)];
    assert(s.paint.fill && s.paint.stroke && s.paint.even_odd);
    assert(s.paint.fill_color.red == 0x12 && s.paint.fill_color.green == 0xab &&
           s.paint.fill_color.blue == 0x34 && s.paint.fill_color.alpha == .5);
    assert(s.paint.stroke_color.red == 0xab &&
           s.paint.stroke_color.green == 0xcd &&
           s.paint.stroke_color.blue == 0xef &&
           s.paint.stroke_color.alpha == .25 && s.paint.stroke_width == 6);
    assert(s.paint.font.size == 19 && s.paint.padding_left == 7);
  }
  assert(p.scene(0).shapes[find(p, "A")].paint.padding_right == 9);
  assert(p.scene(0).shapes[find(p, "C")].paint.fill_color.red == 0x11);
  assert(p.scene(1).shapes[0].paint.fill_color.red == 0x11);
  assert(p.image("Pictures/image.svg") == asset &&
         p.image("opaque/source") == opaque);
  p.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(bytes(reopened) == formatted);
  auto undo = p.undo();
  assert(undo && undo->objects.size() == 2 && bytes(p) == initial &&
         p.modified());
  auto redo = p.redo();
  assert(redo && redo->objects.size() == 2 && bytes(p) == formatted &&
         !p.modified());
  // Property absence is different from a false enum: explicitly disable both.
  NativePaintFormat none;
  none.fill = "none";
  none.stroke = "none";
  none.even_odd = false;
  assert(p.set_object_paint(0, {ref(p, "A")}, none));
  auto s = p.scene(0).shapes[find(p, "A")];
  assert(!s.paint.fill && !s.paint.stroke && !s.paint.even_odd);
  p.undo();
  assert(bytes(p) == formatted);
  NativePaintFormat ink;
  ink.stroke_color = "#445566";
  assert(p.set_object_paint(0, {ref(p, "A")}, ink, TextSelection{1, 3}));
  auto at = p.undo();
  assert(at && at->selection && at->selection->anchor == 1 &&
         at->selection->cursor == 3);
  at = p.redo();
  assert(at && at->selection && at->selection->anchor == 1 &&
         at->selection->cursor == 3);
}
void group_and_protection() {
  TempDir dir;
  auto file = dir.path() + "/groups.odp";
  auto package = painted();
  auto page = children(package.body(), DRAW, "page")[0];
  auto defs = package.xml.createElementNS(DRAW, "draw:layer-set");
  page.insertBefore(defs, page.firstChild());
  auto layer = package.xml.createElementNS(DRAW, "draw:layer");
  layer.setAttributeNS(DRAW, "draw:name", "Hidden");
  layer.setAttributeNS(DRAW, "draw:display", "none");
  defs.appendChild(layer);
  named(package, "N2").setAttributeNS(DRAW, "draw:layer", "Hidden");
  package.save(file);
  Presentation p;
  p.load(file);
  auto before = bytes(p);
  auto group = ref(p, "X", 1);
  assert(p.object_paint(0, {group}).objects == 4);
  auto result = p.set_object_paint(0, {group}, patch());
  assert(result &&
         result->objects == std::vector<NativeObjectRef>{ref(p, "X", 1)});
  auto xml = p.scene(0).source.ownerDocument();
  auto hidden = xml.elementsByTagNameNS(DRAW, "rect");
  bool found = false;
  for (auto node : hidden)
    if (node.attributeNS(DRAW, "name") == "N2") {
      assert(node.attributeNS(DRAW, "style-name").startsWith("NativePaint"));
      found = true;
    }
  assert(found);
  p.undo();
  assert(bytes(p) == before);
  layer.setAttributeNS(DRAW, "draw:protected", "true");
  package.save(file);
  p.load(file);
  before = bytes(p);
  auto revision = p.revision();
  assert(!p.object_paint(0, {ref(p, "X", 1)}).editable);
  rejects([&] { p.set_object_paint(0, {ref(p, "X", 1)}, patch()); });
  assert(bytes(p) == before && p.revision() == revision && !p.modified());
  assert(p.set_object_paint(0, {ref(p, "X")}, patch()));
}
void failures_and_scope() {
  TempDir dir;
  auto file = dir.path() + "/failures.odp";
  auto package = painted();
  package.save(file);
  Presentation p;
  p.load(file);
  p.move_shape(0, find(p, "C"), {1, 0});
  p.undo();
  auto before = bytes(p);
  auto revision = p.revision();
  for (double n : {-1., 10001., std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity()}) {
    NativePaintFormat f;
    f.stroke_width = n;
    rejects([&] { p.set_object_paint(0, {ref(p, "A")}, f); });
  }
  for (double n : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()}) {
    NativePaintFormat f;
    f.fill_opacity = n;
    rejects([&] { p.set_object_paint(0, {ref(p, "A")}, f); });
  }
  for (auto c : {"red", "#xyzxyz", "#123", "#12345678"}) {
    NativePaintFormat f;
    f.fill_color = c;
    rejects([&] { p.set_object_paint(0, {ref(p, "A")}, f); });
  }
  NativePaintFormat f;
  f.fill = "gradient";
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, f); });
  rejects([&] { p.set_object_paint(0, {}, patch()); });
  assert(bytes(p) == before && p.revision() == revision && p.can_redo() &&
         !p.modified());
  // A presentation style gets a presentation-family overlay. The original
  // common style and unrelated graphic/text properties survive unchanged.
  auto styles = read_document_xml(package.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  auto pres =
      styles.importNode(child(common, STYLE, "style"), true).toElement();
  pres.setAttributeNS(STYLE, "style:name", "Title");
  pres.setAttributeNS(STYLE, "style:family", "presentation");
  common.appendChild(pres);
  package.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  named(package, "B").removeAttributeNS(DRAW, "style-name");
  named(package, "B").setAttributeNS(PRES, "presentation:style-name", "Title");
  package.save(file);
  p.load(file);
  assert(p.set_object_paint(0, {ref(p, "B")}, patch()));
  auto node = p.scene(0).shapes[find(p, "B")].source;
  assert(!node.hasAttributeNS(DRAW, "style-name") &&
         node.attributeNS(PRES, "style-name").startsWith("NativePaint"));
  assert(p.scene(0).shapes[find(p, "B")].paint.fill_color.red == 0x12);
  // Name allocation cannot satisfy a formerly unresolved reference elsewhere.
  auto reserved = package.xml.createElementNS(DRAW, "draw:rect");
  reserved.setAttributeNS(SVG, "svg:width", "10pt");
  reserved.setAttributeNS(SVG, "svg:height", "10pt");
  reserved.setAttributeNS(DRAW, "draw:style-name", "NativePaint1");
  children(package.body(), DRAW, "page")[1].appendChild(reserved);
  package.save(file);
  p.load(file);
  assert(p.set_object_paint(0, {ref(p, "A")}, patch()));
  assert(p.scene(0).shapes[find(p, "A")].source.attributeNS(
             DRAW, "style-name") != "NativePaint1");
  // Missing/automatic parent styles cannot be silently converted into common
  // style inheritance when authoring a new ODF style.
  auto direct =
      child(child(package.xml.documentElement(), OFFICE, "automatic-styles"),
            STYLE, "style");
  direct.setAttributeNS(STYLE, "style:parent-style-name", "Missing");
  package.save(file);
  p.load(file);
  before = bytes(p);
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, patch()); });
  assert(bytes(p) == before);
  auto signedpack = painted();
  signedpack.set_entry("META-INF/documentsignatures.xml", "signature",
                       "text/xml");
  signedpack.save(file);
  p.load(file);
  before = bytes(p);
  assert(p.read_only() && !p.object_paint(0, {ref(p, "A")}).editable);
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, patch()); });
  assert(bytes(p) == before && !p.modified());
}
} // namespace
void producer(const Text &source, const Text &prefix) {
  Presentation p;
  p.load(source);
  const auto original = bytes(p);
  NativePaintFormat format;
  format.fill = "solid";
  format.fill_color = "#336699";
  format.fill_opacity = .5;
  format.stroke = "solid";
  format.stroke_color = "#990066";
  format.stroke_opacity = .75;
  format.stroke_width = 6;
  format.even_odd = true;
  p.set_object_paint(0, {ref(p, "Editable")}, format);
  p.save(prefix + "-paint.odp");
  p.undo();
  assert(bytes(p) == original);
  p.set_object_paint(0, {ref(p, "GroupVisible", 1)}, format);
  p.save(prefix + "-group.odp");
  p.undo();
  assert(bytes(p) == original);
  NativePaintFormat clear;
  clear.fill = "none";
  clear.stroke = "none";
  p.set_object_paint(0, {ref(p, "Editable")}, clear);
  p.save(prefix + "-none.odp");
  p.undo();
  assert(bytes(p) == original);
  int index = -1;
  for (size_t i = 0; i < p.scene(2).shapes.size(); ++i)
    if (p.scene(2).shapes[i].name == "EmptyLocal")
      index = int(i);
  assert(index >= 0);
  p.set_object_paint(2, {{index, 0}}, format);
  p.save(prefix + "-presentation.odp");
  p.undo();
  assert(bytes(p) == original);
  NativePaintFormat image;
  image.fill_opacity = .5;
  p.set_object_paint(0, {ref(p, "Image")}, image);
  p.save(prefix + "-image.odp");
  p.undo();
  assert(bytes(p) == original);
  NativePaintFormat rule;
  rule.even_odd = true;
  p.set_object_paint(0, {ref(p, "Hole")}, rule);
  p.save(prefix + "-rule.odp");
  p.undo();
  assert(bytes(p) == original);
  std::cout << "PASS independent producer paint/group/no-paint/presentation "
               "scopes and exact undo\n";
}
int main(int argc, char **argv) {
  if (argc == 4 && Text(argv[1]) == "--document") {
    producer(Text(argv[2]), Text(argv[3]));
    return 0;
  }

  exact_transactions();
  group_and_protection();
  failures_and_scope();
  std::cout
      << "PASS native drawing paint isolation, inheritance, hidden groups, "
         "protected atomic writes, source/history and archive preservation\n";
}
