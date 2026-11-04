// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_paint.h"
#include "native_stage_fragment.h"
#include "native_stage_paste.h"
#include "native_stroke.h"
#include <filesystem>
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
OdfPackage patterned() {
  auto p = fixture();
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto common = child(doc.documentElement(), OFFICE, "styles");
  if (common.isNull()) {
    common = doc.createElementNS(OFFICE, "office:styles");
    doc.documentElement().insertBefore(common,
                                       doc.documentElement().firstChild());
  }
  for (bool dots : {false, true}) {
    auto dash = doc.createElementNS(DRAW, "draw:stroke-dash");
    dash.setAttributeNS(DRAW, "draw:name", dots ? "Dots" : "Mixed");
    dash.setAttributeNS(DRAW, "draw:style", dots ? "round" : "rect");
    dash.setAttributeNS(DRAW, "draw:dots1", dots ? "1" : "2");
    dash.setAttributeNS(DRAW, "draw:dots1-length", dots ? "0pt" : "200%");
    dash.setAttributeNS(DRAW, "draw:dots2", dots ? "0" : "1");
    dash.setAttributeNS(DRAW, "draw:dots2-length", "3pt");
    dash.setAttributeNS(DRAW, "draw:distance", dots ? "6pt" : "50%");
    common.appendChild(dash);
  }
  auto style = doc.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", "SharedDashes");
  style.setAttributeNS(STYLE, "style:family", "graphic");
  auto props = doc.createElementNS(STYLE, "style:graphic-properties");
  props.setAttributeNS(DRAW, "draw:fill", "none");
  props.setAttributeNS(DRAW, "draw:stroke", "dash");
  props.setAttributeNS(DRAW, "draw:stroke-dash", "Mixed");
  props.setAttributeNS(DRAW, "draw:stroke-dash-names", "Dots Mixed Dots");
  props.setAttributeNS(SVG, "svg:stroke-width", "2pt");
  props.setAttributeNS(SVG, "svg:stroke-opacity", "50%");
  style.appendChild(props);
  common.appendChild(style);
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  for (auto node : p.xml.elementsByTagNameNS(DRAW, "rect"))
    if (TextList{"A", "B"}.contains(node.attributeNS(DRAW, "name")))
      node.setAttributeNS(DRAW, "draw:style-name", "SharedDashes");
  return p;
}
const ScenePaint &paint(const Presentation &p, const Text &name = "A") {
  return p.scene(0).shapes.at(find(p, name)).paint;
}
void consumption() {
  auto p = patterned();
  auto scenes = read_presentation_scenes(p);
  const auto &s = scenes[0].shapes[0];
  const auto &a = s.paint;
  assert(a.dashed && a.stroke && a.dash_name == "Mixed");
  assert(a.dash_names == TextList({"Dots", "Mixed", "Dots"}));
  assert(a.stroke_patterns.size() == 4 && a.stroke_patterns[1].round);
  assert(a.stroke_patterns[0].lengths(2) ==
         std::vector<double>({4, 1, 4, 1, 3, 1}));
  auto passes = stroke_pass_outlines(s.contours, a);
  assert(passes.size() == 4 && !passes[0].empty() && !passes[1].empty());
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto prop = doc.elementsByTagNameNS(STYLE, "graphic-properties").back();
  prop.setAttributeNS(DRAW, "draw:stroke-dash-names", "");
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  assert(
      read_presentation_scenes(p)[0].shapes[0].paint.stroke_patterns.size() ==
      1);
  prop.setAttributeNS(DRAW, "draw:stroke-dash", "Missing");
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  scenes = read_presentation_scenes(p);
  assert(!scenes[0].unsupported.empty() && scenes[0].shapes[0].paint.dashed);
  assert(scenes[0].shapes[0].paint.stroke_patterns[0].lengths(2).empty());
  std::cout << "PASS named dash consumption, mixed percent/absolute units, "
               "repeated overlays and missing-name diagnostics\n";
}
void authoring(const Text &output) {
  TempDir dir;
  auto input = dir.path() + "/source.odp";
  auto source = patterned();
  source.save(input);
  Presentation p;
  p.load(input);
  auto original = content(p), styles = p.image("styles.xml"),
       manifest = p.image("META-INF/manifest.xml");
  const auto other = paint(p, "B");
  NativePaintFormat width;
  width.stroke_width = 4;
  assert(p.set_object_paint(0, {ref(p, "A")}, width));
  assert(paint(p).stroke_patterns[0].lengths(4) ==
         std::vector<double>({8, 2, 8, 2, 3, 2}));
  assert(paint(p, "B").stroke_width == other.stroke_width);
  assert(p.image("styles.xml") == styles);
  assert(p.undo() && content(p) == original && p.image("styles.xml") == styles);
  assert(p.image("META-INF/manifest.xml") == manifest);
  auto revision = p.revision();
  assert(!p.set_object_paint(0, {ref(p, "A")},
                             p.object_paint(0, {ref(p, "A")}).values));
  assert(p.revision() == revision && p.can_redo());
  NativePaintFormat change;
  change.dash_pattern = SceneDashPattern{};
  change.dash_pattern->first = 1;
  change.dash_pattern->first_length = SceneDashLength{1.5, true};
  change.dash_pattern->second = 2;
  change.dash_pattern->second_length = SceneDashLength{2, false};
  change.dash_pattern->distance = SceneDashLength{3, false};
  change.dash_pattern->round = true;
  change.dash_names = TextList{};
  assert(p.set_object_paint(0, {ref(p, "A")}, change));
  auto changed = content(p), changed_styles = p.image("styles.xml"),
       changed_manifest = p.image("META-INF/manifest.xml");
  assert(changed_styles != styles && paint(p).stroke_patterns.size() == 1);
  assert(paint(p).stroke_patterns[0] == *change.dash_pattern);
  assert(paint(p, "B").stroke_patterns == other.stroke_patterns);
  assert(p.image("opaque/source") == source.entry("opaque/source"));
  assert(p.undo() && content(p) == original &&
         p.image("styles.xml") == styles &&
         p.image("META-INF/manifest.xml") == manifest);
  auto invalid = change;
  invalid.dash_pattern->distance->value = -1;
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, invalid); });
  invalid = change;
  invalid.dash_names = TextList{"Missing"};
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, invalid); });
  invalid = change;
  invalid.dash_pattern->first_length = SceneDashLength{1e-100, false};
  invalid.dash_pattern->distance = SceneDashLength{1e-100, false};
  invalid.dash_pattern->second = 0;
  rejects([&] { p.set_object_paint(0, {ref(p, "A")}, invalid); });
  assert(content(p) == original && p.image("styles.xml") == styles &&
         p.can_redo());
  assert(p.redo() && content(p) == changed &&
         p.image("styles.xml") == changed_styles &&
         p.image("META-INF/manifest.xml") == changed_manifest);
  auto saved = dir.path() + "/saved.odp";
  p.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(paint(reopened).stroke_patterns == paint(p).stroke_patterns);
  if (!output.empty()) {
    source.save(output + "/dash-source.odp");
    p.save(output + "/dash-authored.odp");
  }
  std::cout << "PASS scoped dash authoring, final-width resolution, unrelated "
               "source, exact archive history, no-ops, limits and reopen\n";
}
void first_styles() {
  TempDir dir;
  auto source = fixture();
  const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
  auto old_manifest = source.entry("META-INF/manifest.xml");
  auto manifest = read_document_xml(old_manifest);
  for (auto n : children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (n.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest.documentElement().removeChild(n);
  auto bytes = source.entry("styles.xml"),
       next_manifest = manifest.toByteArray();
  source.replay_part_changes(
      {{"styles.xml", true, false, 0, bytes.size(), 0, bytes, {}},
       {"META-INF/manifest.xml", true, true, 0, old_manifest.size(),
        next_manifest.size(), old_manifest, next_manifest}},
      true);
  auto path = dir.path() + "/first.odp";
  source.save(path);
  Presentation p;
  p.load(path);
  auto original = content(p),
       original_manifest = p.image("META-INF/manifest.xml");
  assert(p.image("styles.xml").empty());
  NativePaintFormat patch;
  patch.dash_pattern = SceneDashPattern{};
  patch.dash_pattern->first = 1;
  patch.dash_pattern->first_length = SceneDashLength{3, true};
  patch.dash_pattern->distance = SceneDashLength{2, true};
  patch.stroke_width = 2;
  assert(p.set_object_paint(0, {ref(p, "A"), ref(p, "B")}, patch));
  auto created = p.image("styles.xml"), changed = content(p);
  assert(paint(p).stroke_patterns == paint(p, "B").stroke_patterns);
  auto document = read_document_xml(created);
  assert(document.elementsByTagNameNS(DRAW, "stroke-dash").size() == 1);
  assert(p.undo() && content(p) == original && p.image("styles.xml").empty() &&
         p.image("META-INF/manifest.xml") == original_manifest);
  assert(p.redo() && content(p) == changed && p.image("styles.xml") == created);
  std::cout
      << "PASS first styles.xml and automatic-styles creation, multi-object "
         "mapping, one shared fresh pattern and exact archive removal/replay\n";
}
void clipboard(const Text &output) {
  TempDir dir;
  auto source = patterned();
  auto scene = read_presentation_scenes(source)[0];
  auto fragment = native_stage_fragment(source, scene, {{0, 0}});
  OdfPackage selected(Kind::Stage);
  selected.load_bytes(fragment.archive);
  auto before = read_presentation_scenes(selected)[0].shapes[0].paint;
  auto destination = patterned();
  // Change all receiving patterns with the same names: import must rename the
  // whole dependency list, including duplicate references, rather than bind it.
  auto doc = read_document_xml(destination.entry("styles.xml"));
  for (auto node : doc.elementsByTagNameNS(DRAW, "stroke-dash"))
    node.setAttributeNS(DRAW, "draw:distance", "70pt");
  destination.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  auto path = dir.path() + "/receiving.odp";
  destination.save(path);
  Presentation p;
  p.load(path);
  auto old = content(p), old_styles = p.image("styles.xml");
  auto old_paint = paint(p);
  auto draft = p.prepare_paste_objects(0, encode_stage_fragment(fragment));
  assert(content(p) == old && p.image("styles.xml") == old_styles);
  auto position = p.commit_paste_objects(std::move(draft));
  assert(position.objects.size() == 1);
  auto after = p.scene(0).shapes.at(position.objects[0].shape).paint;
  assert(after.stroke_patterns == before.stroke_patterns);
  assert(after.dash_names.size() == 3 &&
         after.dash_names[0] == after.dash_names[2]);
  assert(after.dash_names[0] != "Dots" && after.dash_names[1] != "Mixed");
  assert(paint(p).stroke_patterns == old_paint.stroke_patterns);
  auto changed = content(p), changed_styles = p.image("styles.xml");
  assert(p.undo() && content(p) == old && p.image("styles.xml") == old_styles);
  assert(p.redo() && content(p) == changed &&
         p.image("styles.xml") == changed_styles);
  auto saved = dir.path() + "/pasted.odp";
  p.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(reopened.scene(0).shapes.back().paint.stroke_patterns ==
         before.stroke_patterns);
  if (!output.empty()) {
    selected.save(output + "/dash-selected.odp");
    p.save(output + "/dash-pasted.odp");
  }
  std::cout
      << "PASS clipboard dash dependency closure, ordered duplicate remapping, "
         "destination retention, actual paste/history and reopen\n";
}
} // namespace
int main(int argc, char **argv) {
  Text output = argc > 1 ? Text(argv[1]) : Text();
  if (!output.empty())
    std::filesystem::create_directories(output.toStdString());
  consumption();
  authoring(output);
  first_styles();
  clipboard(output);
}
