// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_gradient.h"
#include "native_stage_fragment.h"
#include "native_stage_paste.h"
#include "native_stage_paint.h"
#include <limits>
#include <numbers>
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const SceneRect area{10, 20, 200, 100};
XmlElement definition(XmlDocument &doc, const Text &style = "linear") {
  doc = read_document_xml(
      ("<draw:gradient xmlns:draw=\"" + DRAW +
       "\" draw:name=\"SourceGradient\" draw:style=\"" + style +
       "\" draw:start-color=\"#c86432\" draw:end-color=\"#1464dc\"/>")
          .toUtf8());
  return doc.documentElement();
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-10); }
void parser_and_fields() {
  XmlDocument doc;
  auto node = definition(doc);
  auto g = read_scene_gradient(node);
  near(scene_gradient_parameter(g, area, {110, 20}), 0);
  near(scene_gradient_parameter(g, area, {110, 70}), .5);
  near(scene_gradient_parameter(g, area, {110, 120}), 1);
  assert(scene_gradient_color(g, area, {110, 70}) == SceneColor(110, 100, 135));
  for (const auto &angle :
       {"90", "90deg", "100grad", "1.5707963267948966rad"}) {
    node.setAttributeNS(DRAW, "draw:angle", angle);
    auto h = read_scene_gradient(node);
    near(h.angle, std::numbers::pi / 2);
    near(scene_gradient_parameter(h, area, {10, 70}), 0);
    near(scene_gradient_parameter(h, area, {210, 70}), 1);
  }
  node.setAttributeNS(DRAW, "draw:angle", "45deg");
  g = read_scene_gradient(node);
  near(scene_gradient_parameter(g, area, {10, 20}), 0);
  near(scene_gradient_parameter(g, area, {210, 120}), 1);
  near(scene_gradient_parameter(g, area, {110, 20}), 1. / 3);
  node.setAttributeNS(DRAW, "draw:angle", "0");
  node.setAttributeNS(DRAW, "draw:border", "25%");
  node.setAttributeNS(DRAW, "draw:start-intensity", "50%");
  node.setAttributeNS(DRAW, "draw:end-intensity", "25%");
  g = read_scene_gradient(node);
  assert(scene_gradient_color(g, area, {110, 30}) == SceneColor(100, 50, 25));
  near(scene_gradient_parameter(g, area, {110, 82.5}), .5);
  assert(scene_gradient_color(g, area, {110, 120}) == SceneColor(5, 25, 55));
  node = definition(doc, "axial");
  g = read_scene_gradient(node);
  near(scene_gradient_parameter(g, area, {110, 70}), 0);
  near(scene_gradient_parameter(g, area, {110, 20}), 1);
  near(scene_gradient_parameter(g, area, {110, 95}), .5);
  g.border = .5;
  near(scene_gradient_parameter(g, area, {110, 95}), 1);
  g.border = 1;
  near(scene_gradient_parameter(g, area, {110, 70}), 1);
  for (auto type :
       {SceneGradientDefinition::Radial, SceneGradientDefinition::Ellipsoid,
        SceneGradientDefinition::Square,
        SceneGradientDefinition::Rectangular}) {
    g = {};
    g.type = type;
    near(scene_gradient_parameter(g, area, {110, 70}), 1);
    near(scene_gradient_parameter(g, area, {10000, 10000}), 0);
    g.border = 1;
    near(scene_gradient_parameter(g, area, {110, 70}), 0);
  }
  g = {};
  g.type = SceneGradientDefinition::Radial;
  near(scene_gradient_parameter(g, area, {110 + std::sqrt(12500.) / 2, 70}),
       .5);
  g.angle = .7123;
  near(scene_gradient_parameter(g, area, {110 + std::sqrt(12500.) / 2, 70}),
       .5);
  g.type = SceneGradientDefinition::Ellipsoid;
  g.angle = 0;
  near(scene_gradient_parameter(g, area, {110 + 100 / std::sqrt(2.), 70}), .5);
  near(scene_gradient_parameter(g, area, {110, 70 + 50 / std::sqrt(2.)}), .5);
  g.type = SceneGradientDefinition::Square;
  near(scene_gradient_parameter(g, area, {110, 20}), .5);
  g.type = SceneGradientDefinition::Rectangular;
  near(scene_gradient_parameter(g, area, {160, 70}), 1);
  near(scene_gradient_parameter(g, area, {185, 70}), .5);
  near(scene_gradient_parameter(g, area, {110, 45}), .5);
  g.type = SceneGradientDefinition::Linear;
  g.start = {0, 0, 0};
  g.end = {255, 255, 255};
  assert(scene_gradient_color(g, area, {110, 21}, 3).red == 0);
  assert(scene_gradient_color(g, area, {110, 70}, 3).red == 128);
  assert(scene_gradient_color(g, area, {110, 119}, 3).red == 255);
  assert(read_scene_gradient_steps("0") == 0);
  assert(read_scene_gradient_steps("+3") == 3);
  g.type = SceneGradientDefinition::Radial;
  g.border = 1 - std::numeric_limits<double>::epsilon();
  const double tiny = std::numeric_limits<double>::denorm_min();
  near(scene_gradient_parameter(g, {0, 0, tiny, tiny}, {0, 0}), 0);
  assert(read_scene_gradient_steps("18446744073709551615") == UINT64_MAX);
  for (const auto &n : {"1", "2", "-1", "3pt", "3.5", "18446744073709551616"})
    rejects([&] { (void)read_scene_gradient_steps(n); });
  for (const auto &n : {"NaN", "inf", "90degjunk", "45 rad"}) {
    node = definition(doc);
    node.setAttributeNS(DRAW, "draw:angle", n);
    rejects([&] { (void)read_scene_gradient(node); });
  }
  for (const auto &n : {"-1%", "101%", "50", "inf%"}) {
    node = definition(doc);
    node.setAttributeNS(DRAW, "draw:border", n);
    rejects([&] { (void)read_scene_gradient(node); });
  }
  g.type = SceneGradientDefinition::Type(99);
  rejects([&] { (void)scene_gradient_color(g, area, {0, 0}); });
  std::cout << "PASS six gradient fields, angle units, intensities, borders, "
               "offsets, discrete colors and bounded malformed inputs\n";
}
OdfPackage source_gradient() {
  auto p = fixture();
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto common = child(styles.documentElement(), OFFICE, "styles");
  XmlDocument gradient_doc;
  auto gradient = definition(gradient_doc);
  common.appendChild(styles.importNode(gradient, true));
  auto style = styles.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:family", "graphic");
  style.setAttributeNS(STYLE, "style:name", "GradientPaint");
  auto properties = styles.createElementNS(STYLE, "style:graphic-properties");
  properties.setAttributeNS(DRAW, "draw:fill", "gradient");
  properties.setAttributeNS(DRAW, "draw:fill-gradient-name", "SourceGradient");
  properties.setAttributeNS(DRAW, "draw:gradient-step-count", "5");
  properties.setAttributeNS(DRAW, "draw:opacity", "50%");
  style.appendChild(properties);
  common.appendChild(style);
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "rect"))
    if (node.attributeNS(DRAW, "name") == "A")
      node.setAttributeNS(DRAW, "draw:style-name", "GradientPaint");
  return p;
}
void model_and_clipboard(bool inherited_default = false) {
  auto source = source_gradient();
  if (inherited_default) {
    auto doc = read_document_xml(source.entry("styles.xml"));
    auto common = child(doc.documentElement(), OFFICE, "styles");
    for (auto node : children(common, STYLE, "default-style"))
      if (node.attributeNS(STYLE, "family") == "graphic")
        common.removeChild(node);
    for (auto node : children(common, STYLE, "style"))
      if (node.attributeNS(STYLE, "name") == "GradientPaint") {
        node.renameLocal("default-style");
        node.removeAttributeNS(STYLE, "name");
      }
    source.set_entry("styles.xml", doc.toByteArray(), "text/xml");
    for (auto node : source.xml.elementsByTagNameNS(DRAW, "rect"))
      if (node.attributeNS(DRAW, "name") == "A")
        node.removeAttributeNS(DRAW, "style-name");
  }
  auto original = source.xml.toByteArray(), styles = source.entry("styles.xml");
  auto scene = read_presentation_scenes(source)[0];
  const auto &paint = scene.shapes[0].paint;
  assert(paint.fill && paint.gradient && paint.gradient_definition);
  near(paint.fill_color.alpha, .5);
  assert(paint.gradient_steps == 5 && paint.gradient_name == "SourceGradient");
  auto fragment = native_stage_fragment(source, scene, {{0, 0}});
  OdfPackage detached(Kind::Stage);
  detached.load_bytes(fragment.archive);
  auto selected = read_presentation_scenes(detached)[0];
  assert(selected.shapes.size() == 1 && selected.shapes[0].paint.gradient);
  assert(*selected.shapes[0].paint.gradient_definition ==
         *paint.gradient_definition);
  auto destination = source_gradient();
  auto changed = read_document_xml(destination.entry("styles.xml"));
  auto def = changed.elementsByTagNameNS(DRAW, "gradient").front();
  def.setAttributeNS(DRAW, "draw:end-color", "#00ff00");
  destination.set_entry("styles.xml", changed.toByteArray(), "text/xml");
  TempDir directory;
  auto path = directory.path() + "/destination.odp";
  destination.save(path);
  Presentation p;
  p.load(path);
  auto before = content(p), before_styles = p.image("styles.xml");
  auto result = p.commit_paste_objects(
      p.prepare_paste_objects(0, encode_stage_fragment(fragment)));
  auto pasted = p.scene(0).shapes.at(result.objects[0].shape).paint;
  assert(pasted.gradient && pasted.gradient_name != paint.gradient_name);
  assert(*pasted.gradient_definition == *paint.gradient_definition);
  assert(pasted.gradient_steps == 5 && pasted.fill_color.alpha == .5);
  assert(p.undo() && content(p) == before &&
         p.image("styles.xml") == before_styles);
  assert(p.redo());
  assert(source.xml.toByteArray() == original &&
         source.entry("styles.xml") == styles);
  std::cout << (inherited_default ? "PASS default gradient context: "
                                  : "PASS explicit gradient context: ")
            << "resource/context clipboard "
               "isolation and archive/source history\n";
}
void raster() {
  SceneGradientDefinition g;
  auto asset = scene_gradient_image(g, {0, 0, 10, 10}, {10, 0, 0, 10});
  assert(asset && asset->data->substr(0, 2) == "BM");
  auto n = [&](size_t offset) {
    uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i)
      result |= uint32_t(static_cast<unsigned char>((*asset->data)[offset + i]))
                << (8 * i);
    return result;
  };
  assert(n(18) == 100 && n(22) == 100);
  assert(static_cast<unsigned char>((*asset->data)[54]) == 254);
  assert(static_cast<unsigned char>((*asset->data)[54 + 99 * 300]) == 1);
  asset = scene_gradient_image(g, area, {1000, 0, 0, 1000}, 0, 128);
  assert(n(18) * n(22) <= 128);
  for (size_t limit : {size_t(1), size_t(128)}) {
    for (auto b : {SceneRect{0, 0, 10000, 1}, SceneRect{0, 0, 1, 10000}}) {
      asset = scene_gradient_image(g, b, {}, 0, limit);
      assert(n(18) * n(22) <= limit);
    }
  }
  assert(!scene_gradient_image(g, {0, 0, 0, 10}, {}));
  rejects([&] { (void)scene_gradient_image(g, area, {}, 0, 0); });
  rejects([&] { (void)scene_gradient_image(g, area, {NAN, 0, 0, 1}); });
  std::cout
      << "PASS shared bounded BMP colors, orientation and pixel budgets\n";
}
void authoring() {
  TempDir directory;
  auto path = directory.path() + "/source.odp";
  source_gradient().save(path);
  Presentation p;
  p.load(path);
  auto original = content(p), styles = p.image("styles.xml"),
       manifest = p.image("META-INF/manifest.xml");
  const auto current = p.object_paint(0, {ref(p, "A")}).values;
  assert(!p.set_object_paint(0, {ref(p, "A")}, current));
  assert(!p.modified());
  NativePaintFormat patch;
  patch.gradient_definition = SceneGradientDefinition{};
  patch.gradient_definition->type = SceneGradientDefinition::Ellipsoid;
  patch.gradient_definition->angle = .7;
  patch.gradient_definition->cx = .25;
  patch.gradient_definition->end = {10, 30, 200};
  patch.gradient_steps = 7;
  patch.fill_opacity = .6;
  assert(p.set_object_paint(0, {ref(p, "A"), ref(p, "B")}, patch));
  auto a = p.scene(0).shapes.at(find(p, "A")).paint,
       b = p.scene(0).shapes.at(find(p, "B")).paint;
  assert(a.gradient && b.gradient && a.gradient_name == b.gradient_name);
  assert(a.gradient_name != "SourceGradient");
  assert(*a.gradient_definition == *patch.gradient_definition);
  assert(a.fill_color.alpha == .6 && b.fill_color.alpha == .6);
  assert(a.gradient_steps == 7 && b.gradient_steps == 7);
  assert(!p.scene(0).shapes.at(find(p, "C")).paint.gradient);
  auto saved_content = content(p), saved_styles = p.image("styles.xml");
  auto definitions =
      read_document_xml(saved_styles).elementsByTagNameNS(DRAW, "gradient");
  assert(definitions.size() == 2);
  assert(definitions.front().attributeNS(DRAW, "name") == "SourceGradient");
  assert(!p.set_object_paint(0, {ref(p, "A"), ref(p, "B")}, patch));
  assert(content(p) == saved_content && p.image("styles.xml") == saved_styles);
  assert(p.undo() && content(p) == original && p.image("styles.xml") == styles);
  assert(p.image("META-INF/manifest.xml") == manifest && !p.modified());
  assert(p.redo() && content(p) == saved_content &&
         p.image("styles.xml") == saved_styles);
  NativePaintFormat invalid;
  invalid.gradient_name = "MissingGradient";
  rejects([&] { (void)p.set_object_paint(0, {ref(p, "A")}, invalid); });
  invalid = patch;
  invalid.gradient_definition->border = 2;
  rejects([&] { (void)p.set_object_paint(0, {ref(p, "A")}, invalid); });
  assert(content(p) == saved_content && p.image("styles.xml") == saved_styles);
  assert(p.undo());
  NativePaintFormat existing;
  existing.gradient_name = "SourceGradient";
  assert(p.set_object_paint(0, {ref(p, "B")}, existing));
  assert(p.scene(0).shapes.at(find(p, "B")).paint.gradient);
  assert(p.image("styles.xml") == styles);
  assert(p.undo() && content(p) == original);
  std::cout
      << "PASS scoped gradient resources, mixed selection, literal radian "
         "roundtrip, no-op, invalid rollback and exact source/archive "
         "history\n";
}
} // namespace
int main() {
  parser_and_fields();
  raster();
  model_and_clipboard();
  model_and_clipboard(true);
  authoring();
}
