// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment_fixture.h"
#include "native_stage_import.h"
#include "native_stage_styles.h"
#include "native_stage_timing.h"
#include <nlohmann/json.hpp>
namespace {
OdfPackage collision_source() {
  auto p = source();
  auto common = read_document_xml(p.entry("styles.xml"));
  auto styles = child(common.documentElement(), OFFICE, "styles");
  auto automatic = child(common.documentElement(), OFFICE, "automatic-styles");
  auto decoy = style(common, automatic, "Emphasis", "text");
  auto props = element(common, STYLE, "style:text-properties", decoy);
  props.setAttributeNS(FO, "fo:color", "#ff0000");
  for (auto node : children(styles, STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == "CommonParagraph") {
      auto paragraph =
          element(common, STYLE, "style:paragraph-properties", node);
      auto tabs = element(common, STYLE, "style:tab-stops", paragraph);
      auto tab = element(common, STYLE, "style:tab-stop", tabs);
      tab.setAttributeNS(STYLE, "style:position", "20pt");
      tab.setAttributeNS(STYLE, "style:leader-text", ".");
      tab.setAttributeNS(STYLE, "style:leader-text-style", "Emphasis");
    }
  p.set_entry("styles.xml", common.toByteArray(), "text/xml");
  auto root = p.xml.documentElement();
  auto faces = element(p.xml, OFFICE, "office:font-face-decls");
  root.insertBefore(faces, root.firstChild());
  auto font = element(p.xml, STYLE, "style:font-face", faces);
  font.setAttributeNS(STYLE, "style:name", "DocBitmap");
  font.setAttributeNS(SVG, "svg:font-family", "monospace");
  automatic = child(root, OFFICE, "automatic-styles");
  auto emphasis = style(p.xml, automatic, "Emphasis", "text");
  props = element(p.xml, STYLE, "style:text-properties", emphasis);
  props.setAttributeNS(FO, "fo:color", "#2468ac");
  for (auto node : children(automatic, STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == "P") {
      node.setAttributeNS(STYLE, "style:name",
                          "paint"); // Same lexical name, another family.
      child(node, STYLE, "text-properties")
          .setAttributeNS(STYLE, "style:font-name", "DocBitmap");
    }
  auto paragraph = child(named(p.xml, "X"), TEXT, "p");
  paragraph.setAttributeNS(TEXT, "text:style-name", "paint");
  paragraph.firstChild().setNodeValue("X\t");
  return p;
}
OdfPackage destination() {
  auto p = fixture();
  p.xml.documentElement().setAttribute("reserved", "NativePaste1");
  p.xml.documentElement().setAttribute("unresolved", "#%4EativePaste2");
  p.set_entry("nested/resource.xml",
              "<x missing='Objects/NativePa&#115;te3/missing.svg'/>",
              "text/xml");
  p.set_entry("opaque/reserved", Bytes("binary\0NativePaste4\xff", 20),
              "application/octet-stream");
  p.set_entry("Pictures/image.svg",
              "<svg xmlns='http://www.w3.org/2000/svg' width='20' "
              "height='10'><rect width='20' height='10' fill='blue'/></svg>",
              "image/svg+xml");
  return p;
}
void paint_equal(const ScenePaint &a, const ScenePaint &b) {
  assert(a.fill == b.fill && a.stroke == b.stroke && a.even_odd == b.even_odd &&
         a.fill_color == b.fill_color && a.stroke_color == b.stroke_color &&
         a.text_color == b.text_color &&
         a.use_window_text_color == b.use_window_text_color &&
         a.stroke_width == b.stroke_width && a.padding == b.padding &&
         a.padding_left == b.padding_left &&
         a.padding_right == b.padding_right && a.padding_top == b.padding_top &&
         a.padding_bottom == b.padding_bottom &&
         a.vertical_alignment == b.vertical_alignment && a.font == b.font &&
         a.image_opacity == b.image_opacity && a.stroke_cap == b.stroke_cap &&
         a.stroke_join == b.stroke_join);
}
void scene_equal(const OdfPackage &a, const OdfPackage &b, SceneTarget target) {
  auto before = read_presentation_scenes(a, {}, target).at(0);
  auto after = read_presentation_scenes(b, {}, target).at(0);
  assert(before.shapes.size() == after.shapes.size() &&
         before.width == after.width && before.height == after.height &&
         before.unsupported == after.unsupported);
  for (size_t i = 0; i < before.shapes.size(); ++i) {
    const auto &x = before.shapes[i], &y = after.shapes[i];
    assert(x.kind == y.kind && x.text == y.text && x.width == y.width &&
           x.height == y.height);
    for (auto pair : {std::pair{x.transform.a, y.transform.a},
                      {x.transform.b, y.transform.b},
                      {x.transform.c, y.transform.c},
                      {x.transform.d, y.transform.d},
                      {x.transform.e, y.transform.e},
                      {x.transform.f, y.transform.f}})
      assert(pair.first == pair.second);
    assert(x.contours.size() == y.contours.size());
    for (size_t c = 0; c < x.contours.size(); ++c) {
      const auto &a = x.contours[c], &b = y.contours[c];
      assert(a.closed == b.closed && a.fill == b.fill && a.stroke == b.stroke &&
             a.paint_group == b.paint_group &&
             a.points.size() == b.points.size());
      for (size_t k = 0; k < a.points.size(); ++k)
        assert(a.points[k].x == b.points[k].x &&
               a.points[k].y == b.points[k].y);
    }
    assert(x.handles.size() == y.handles.size());
    for (size_t k = 0; k < x.handles.size(); ++k)
      assert(x.handles[k].position.x == y.handles[k].position.x &&
             x.handles[k].position.y == y.handles[k].position.y &&
             x.handles[k].editable == y.handles[k].editable);
    paint_equal(x.paint, y.paint);
    assert(x.layer_protected == y.layer_protected);
    assert(x.text_flow.paragraphs.size() == y.text_flow.paragraphs.size());
    for (size_t k = 0; k < x.text_flow.paragraphs.size(); ++k) {
      const auto &p = x.text_flow.paragraphs[k], &q = y.text_flow.paragraphs[k];
      assert(p.text == q.text && p.font == q.font && p.color == q.color &&
             p.background == q.background && p.decorations == q.decorations &&
             p.left == q.left && p.right == q.right && p.before == q.before &&
             p.after == q.after && p.indent == q.indent &&
             p.tabs.size() == q.tabs.size() && p.default_tab == q.default_tab &&
             p.alignment == q.alignment);
      for (size_t j = 0; j < p.spans.size(); ++j)
        assert(p.spans[j].begin == q.spans.at(j).begin &&
               p.spans[j].end == q.spans.at(j).end &&
               p.spans[j].font == q.spans.at(j).font &&
               p.spans[j].color == q.spans.at(j).color);
      for (size_t j = 0; j < p.tabs.size(); ++j)
        assert(p.tabs[j].position == q.tabs[j].position &&
               p.tabs[j].leader == q.tabs[j].leader &&
               p.tabs[j].leader_font.family == q.tabs[j].leader_font.family &&
               p.tabs[j].leader_font.color == q.tabs[j].leader_font.color);
    }
    if (!x.image.empty()) {
      assert(y.image.startsWith("Objects/NativePaste5/"));
      assert(a.entry(x.image) == b.entry(y.image));
    }
  }
}
OdfPackage unpack(const NativeStageFragment &fragment) {
  OdfPackage p(Kind::Stage);
  p.load_bytes(fragment.archive);
  return p;
}
NativeObjectRef outer(const SlideScene &scene) {
  for (size_t i = 0; i < scene.shapes.size(); ++i)
    if (scene.shapes[i].name == "X")
      return {int(i), 1};
  std::abort();
}
void save(const Text &path, const Bytes &bytes) {
  std::ofstream output(path.toStdString(), std::ios::binary);
  output.write(bytes.data(), bytes.size());
  assert(output.good());
}
std::pair<NativeStageFragment, NativeStageFragment> local_geometry() {
  auto original = collision_source();
  auto shape = named(original.xml, "X");
  shape.renameLocal("custom-shape");
  auto glue = element(original.xml, DRAW, "draw:glue-point");
  glue.setAttributeNS(DRAW, "draw:id", 4);
  glue.setAttributeNS(DRAW, "draw:escape-direction", "auto");
  glue.setAttributeNS(SVG, "svg:x", "20pt");
  glue.setAttributeNS(SVG, "svg:y", "20pt");
  shape.insertBefore(glue, shape.firstChild());
  auto geometry = element(original.xml, DRAW, "draw:enhanced-geometry", shape);
  geometry.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
  geometry.setAttributeNS(DRAW, "draw:modifiers", "65");
  geometry.setAttributeNS(DRAW, "draw:enhanced-path",
                          "M 0 0 L ?edge 0 ?edge 100 0 100 Z N");
  geometry.setAttributeNS(DRAW, "draw:text-areas", "0 0 ?edge 100");
  auto equation = element(original.xml, DRAW, "draw:equation", geometry);
  equation.setAttributeNS(DRAW, "draw:name", "edge");
  equation.setAttributeNS(DRAW, "draw:formula", "$0");
  auto handle = element(original.xml, DRAW, "draw:handle", geometry);
  handle.setAttributeNS(DRAW, "draw:handle-position", "$0 50");
  auto model = read_presentation_scenes(original).at(0);
  auto selected = native_stage_fragment(original, model, {outer(model)});
  auto wire = encode_stage_fragment(selected);
  auto before = decode_stage_fragment(wire);
  auto after = remap_stage_fragment_for_destination(destination(), wire);
  auto actual = unpack(after), source_package = unpack(before);
  assert(actual.xml.elementsByTagNameNS(DRAW, "equation")[0].attributeNS(
             DRAW, "name") == "edge");
  assert(actual.xml.elementsByTagNameNS(DRAW, "glue-point")[0].attributeNS(
             DRAW, "id") == "4");
  assert(
      actual.xml.elementsByTagNameNS(DRAW, "enhanced-geometry")[0].attributeNS(
          DRAW, "enhanced-path") == "M 0 0 L ?edge 0 ?edge 100 0 100 Z N");
  scene_equal(source_package, actual, SceneTarget::Screen);
  scene_equal(source_package, actual, SceneTarget::Print);
  return {before, after};
}
void local_parameter_and_fields() {
  auto original = fixture();
  auto shape = named(original.xml, "X");
  auto container = element(original.xml, "urn:clipboard-reference-test",
                           "probe:local", shape);
  auto parameter = element(original.xml, DRAW, "draw:param", container);
  parameter.setAttributeNS(DRAW, "draw:name", "quality");
  parameter.setAttributeNS(DRAW, "draw:value", "4");
  auto variable =
      element(original.xml, TEXT, "text:variable-set", child(shape, TEXT, "p"));
  variable.setAttributeNS(TEXT, "text:name", "amount");
  variable.setAttributeNS(TEXT, "text:formula", "of:=amount+1");
  set_plain_text(variable, "1");
  auto section = element(original.xml, TEXT, "text:section", shape);
  section.setAttributeNS(TEXT, "text:name", "SourceSection");
  set_plain_text(element(original.xml, TEXT, "text:p", section), "Section");
  auto linked = element(original.xml, TEXT, "text:section", shape);
  linked.setAttributeNS(TEXT, "text:name", "LinkedSection");
  auto section_source =
      element(original.xml, TEXT, "text:section-source", linked);
  section_source.setAttributeNS(TEXT, "text:section-name", "SourceSection");
  set_plain_text(element(original.xml, TEXT, "text:p", linked),
                 "Cached section");
  auto model = read_presentation_scenes(original).at(0);
  auto selected = native_stage_fragment(original, model, {outer(model)});
  auto actual = unpack(remap_stage_fragment_for_destination(
      destination(), encode_stage_fragment(selected)));
  assert(actual.xml.elementsByTagNameNS(DRAW, "param")[0].attributeNS(
             DRAW, "name") == "quality");
  auto field = actual.xml.elementsByTagNameNS(TEXT, "variable-set")[0];
  assert(field.attributeNS(TEXT, "name") == "amount" &&
         field.attributeNS(TEXT, "formula") == "of:=amount+1");
  auto actual_sections = actual.xml.elementsByTagNameNS(TEXT, "section");
  assert(actual_sections.size() == 2);
  assert(
      actual_sections[0].attributeNS(TEXT, "name").startsWith("NativePaste"));
  assert(actual.xml.elementsByTagNameNS(TEXT, "section-source")[0].attributeNS(
             TEXT, "section-name") ==
         actual_sections[0].attributeNS(TEXT, "name"));
}
void timing_references() {
  std::set<Text> ids{"x", "x.part", "člověk", "a.b", "člověk.end-0"};
  auto parse = [&](const Text &value) {
    return native_stage_timing_references(
        value, [&](const Text &id) { return ids.contains(id); });
  };
  for (const auto &literal :
       {Text("0.25s"), Text("01:02.5"), Text("click+0.5s"), Text("click-0.5s"),
        Text("indefinite"), Text("wallclock(2026-11-04T00:00:00.5Z)"),
        Text("accessKey(a)+1s")})
    assert(parse(literal).empty());
  auto refs = parse("  x.part.click+1.5s ; člověk.end; a\\.b.begin-2s");
  assert(refs.size() == 3 && refs[0].id == "x.part" && refs[0].begin == 2 &&
         refs[0].size == 6 && refs[1].id == "člověk" && refs[2].id == "a.b" &&
         refs[2].size == 4);
  assert(parse("člověk.end-0.5s").at(0).id == "člověk");
  rejects([&] { parse("člověk2.end"); });
  rejects([&] { parse("unknown.click+0.5s"); });
  Text malicious = "x";
  for (int i = 0; i < 2000; ++i)
    malicious += ".part";
  malicious += ".begin";
  rejects([&] { parse(malicious); });
  NativeStageTimingBudget aggregate;
  auto selected = [](const Text &id) { return id == "x"; };
  for (int i = 0; i < 100; ++i)
    native_stage_timing_references("x.end", selected, &aggregate);
  assert(aggregate.examined == 100 && aggregate.terms == 100);
  aggregate.examined = 1000000;
  rejects(
      [&] { native_stage_timing_references("x.end", selected, &aggregate); });
  aggregate = {0, 100000};
  rejects([&] { native_stage_timing_references("1s", selected, &aggregate); });
}
void identities_and_links() {
  auto p = fixture(), destination_package = destination();
  const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
  auto x = named(p.xml, "X");
  x.setAttributeNS(XML, "xml:id", "x.part");
  x.setAttributeNS(DRAW, "draw:id", "legacy-x");
  auto page = children(p.body(), DRAW, "page")[0];
  page.setAttributeNS(DRAW, "draw:nav-order", "a b x.part y n1 n2 c");
  auto paragraph = child(x, TEXT, "p");
  auto begin = element(p.xml, TEXT, "text:bookmark-start", paragraph);
  begin.setAttributeNS(TEXT, "text:name", "anchor");
  auto link = element(p.xml, TEXT, "text:a", paragraph);
  link.setAttributeNS(XLINK, "xlink:href", "#%61nchor");
  set_plain_text(link, "Linked");
  auto end = element(p.xml, TEXT, "text:bookmark-end", paragraph);
  end.setAttributeNS(TEXT, "text:name", "anchor");
  auto ref = element(p.xml, TEXT, "text:bookmark-ref", paragraph);
  ref.setAttributeNS(TEXT, "text:ref-name", "anchor");
  set_plain_text(ref, "Cached");
  // Foreign reference-container placement is a model-only compatibility case;
  // it does not count as an ODF schema or animation-rendering specimen.
  auto references =
      element(p.xml, "urn:clipboard-reference-test", "probe:refs", x);
  references.setAttributeNS(SMIL, "smil:targetElement", "x.part");
  references.setAttributeNS(
      SMIL, "smil:begin",
      "x.part.click+1s; y.end;wallclock(2026-11-04T00:00:00.5Z)");
  references.setAttributeNS(DRAW, "draw:start-shape", "x.part");
  references.setAttributeNS(DRAW, "draw:end-shape", "y");
  references.setAttributeNS(DRAW, "draw:copy-of", "Y");
  references.setAttributeNS(DRAW, "draw:chain-next-name", "Y");
  references.setAttributeNS("urn:clipboard-reference-test", "probe:literal",
                            "x.part");
  auto model = read_presentation_scenes(p).at(0);
  auto input = native_stage_fragment(p, model, {outer(model)});
  auto actual = unpack(remap_stage_fragment_for_destination(
      destination_package, encode_stage_fragment(input)));
  auto imported_page = children(actual.body(), DRAW, "page")[0];
  auto group = children(imported_page, DRAW, "g")[0];
  auto new_x = children(group, DRAW, "rect")[0];
  auto new_y = children(group, DRAW, "frame")[0];
  auto new_id = new_x.attributeNS(XML, "id"),
       y_id = new_y.attributeNS(XML, "id");
  assert(new_id.startsWith("NativePaste") &&
         new_id != new_x.attributeNS(DRAW, "id"));
  auto probe =
      actual.xml.elementsByTagNameNS("urn:clipboard-reference-test", "refs")[0];
  assert(probe.attributeNS(SMIL, "targetElement") == new_id);
  assert(probe.attributeNS(DRAW, "start-shape") == new_id &&
         probe.attributeNS(DRAW, "end-shape") == y_id);
  assert(probe.attributeNS(DRAW, "copy-of") == new_y.attributeNS(DRAW, "name"));
  assert(probe.attributeNS(DRAW, "chain-next-name") ==
         new_y.attributeNS(DRAW, "name"));
  assert(probe.attributeNS("urn:clipboard-reference-test", "literal") ==
         "x.part");
  assert(probe.attributeNS(SMIL, "begin") ==
         new_id + ".click+1s; " + y_id +
             ".end;wallclock(2026-11-04T00:00:00.5Z)");
  auto new_anchor =
      actual.xml.elementsByTagNameNS(TEXT, "bookmark-start")[0].attributeNS(
          TEXT, "name");
  assert(actual.xml.elementsByTagNameNS(TEXT, "bookmark-end")[0].attributeNS(
             TEXT, "name") == new_anchor);
  assert(actual.xml.elementsByTagNameNS(TEXT, "bookmark-ref")[0].attributeNS(
             TEXT, "ref-name") == new_anchor);
  assert(actual.xml.elementsByTagNameNS(TEXT, "a")[0].attributeNS(
             XLINK, "href") == "#" + new_anchor);
  assert(imported_page.attributeNS(DRAW, "nav-order")
             .startsWith(new_id + " " + y_id + " "));
  auto duplicate = fixture();
  named(duplicate.xml, "X").setAttributeNS(XML, "xml:id", "y");
  rejects([&] {
    auto scene = read_presentation_scenes(duplicate).at(0);
    auto copy = native_stage_fragment(duplicate, scene, {outer(scene)});
    remap_stage_fragment_for_destination(destination_package,
                                         encode_stage_fragment(copy));
  });
  auto ambiguous = fixture();
  auto original_y = ambiguous.xml.elementsByTagNameNS(DRAW, "frame")[0];
  original_y.setAttributeNS(DRAW, "draw:name", "X");
  named(ambiguous.xml, "X").setAttributeNS(DRAW, "draw:chain-next-name", "X");
  rejects([&] {
    auto scene = read_presentation_scenes(ambiguous).at(0);
    auto copy = native_stage_fragment(ambiguous, scene, {outer(scene)});
    remap_stage_fragment_for_destination(destination_package,
                                         encode_stage_fragment(copy));
  });
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 5 && Text(argv[1]) == "--remap") {
    try {
      OdfPackage receiving(Kind::Stage);
      receiving.load(argv[2]);
      if (std::filesystem::file_size(argv[3]) > 64 * 1024 * 1024)
        throw std::runtime_error("Oversized clipboard test input");
      std::ifstream input(argv[3], std::ios::binary);
      if (!input)
        throw std::runtime_error("Cannot open clipboard test input");
      Bytes wire{std::string(std::istreambuf_iterator<char>(input), {})};
      auto result = remap_stage_fragment_for_destination(receiving, wire);
      auto encoded = encode_stage_fragment(result);
      save(argv[4], encoded);
      std::cout << nlohmann::json{{"text", result.text.toStdString()},
                                  {"selection", result.selection}}
                       .dump()
                << '\n';
      return 0;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  auto original = collision_source(), target = destination();
  auto input =
      native_stage_fragment(original, read_presentation_scenes(original).at(0),
                            {outer(read_presentation_scenes(original).at(0))});
  auto wire = encode_stage_fragment(input);
  auto safe = decode_stage_fragment(wire);
  auto public_source = unpack(safe);
  auto target_xml = target.xml.toByteArray();
  std::map<Text, std::shared_ptr<const Bytes>> target_parts;
  for (const auto &name : target.entry_names())
    target_parts[name] = target.entry_data(name);
  auto renamed = remap_stage_fragment_for_destination(target, wire);
  auto imported = unpack(renamed);
  assert(target.xml.toByteArray() == target_xml);
  auto geometry_cases = local_geometry();
  local_parameter_and_fields();
  timing_references();
  identities_and_links();
  for (const auto &[name, data] : target_parts)
    assert(target.entry_data(name) == data);
  assert(renamed.text == safe.text);
  assert(renamed.selection == safe.selection);
  auto nodes = stage_fragment_nodes(imported, renamed.selection);
  assert(nodes.size() == 1 && nodes[0].localName() == "g" &&
         nodes[0].attributeNS(DRAW, "name").startsWith("NativePaste"));
  scene_equal(public_source, imported, SceneTarget::Screen);
  scene_equal(public_source, imported, SceneTarget::Print);
  NativeStageStyleCatalog styles(imported);
  std::set<Text> unique;
  for (const auto &d : styles.definitions)
    if (std::get<0>(d.key) != "default-style") {
      auto name = std::get<2>(d.key);
      assert(name.startsWith("NativePaste") && unique.insert(name).second);
    }
  assert(!imported.entry_data("Pictures/image.svg") &&
         !imported.entry_data("opaque/source"));
  for (const auto &name : imported.entry_names())
    if (name != "mimetype" && name != "content.xml" && name != "styles.xml" &&
        name != "META-INF/manifest.xml")
      assert(name.startsWith("Objects/NativePaste5/"));
  // The context-sensitive common leader resolves the common style, not the
  // styles.xml automatic decoy of the same name.
  auto clean_styles = read_document_xml(public_source.entry("styles.xml"));
  for (auto style : children(
           child(clean_styles.documentElement(), OFFICE, "automatic-styles"),
           STYLE, "style"))
    assert(style.attributeNS(STYLE, "name") != "Emphasis");
  auto roundtrip =
      unpack(decode_stage_fragment(encode_stage_fragment(renamed)));
  scene_equal(imported, roundtrip, SceneTarget::Screen);
  rejects(
      [&] { remap_stage_fragment_for_destination(target, "broken envelope"); });
  OdfPackage wrong(Kind::Words);
  rejects([&] { remap_stage_fragment_for_destination(wrong, wire); });
  assert(target.xml.toByteArray() == target_xml);
  if (argc == 3 && Text(argv[1]) == "--documents") {
    Text directory = argv[2];
    std::filesystem::create_directories(directory.toStdString());
    save(directory + "/source.odp", safe.archive);
    save(directory + "/remapped.odp", renamed.archive);
    save(directory + "/geometry-source.odp", geometry_cases.first.archive);
    save(directory + "/geometry-remapped.odp", geometry_cases.second.archive);
    save(directory + "/remapped.cstage", encode_stage_fragment(renamed));
  }
  std::cout << "PASS isolated import source: family/part/common/font scope, "
               "unresolved target reservation, IDs/names/layers/assets, "
               "screen/print equivalence and immutable destination\n";
}
