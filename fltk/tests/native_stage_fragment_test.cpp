// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment_fixture.h"
#include "native_stage_fragment.h"
#include "native_image.h"
namespace {
NativeObjectRef object(const SlideScene &scene, const Text &name,
                       int depth = 0) {
  for (size_t i = 0; i < scene.shapes.size(); ++i)
    if (scene.shapes[i].name == name)
      return {int(i), depth};
  std::abort();
}
OdfPackage unpack(const NativeStageFragment &f) {
  OdfPackage p(Kind::Stage);
  p.load_bytes(f.archive);
  return p;
}
void same(const SceneShape &old, const SceneShape &now) {
  assert(old.name == now.name && old.text == now.text &&
         old.width == now.width && old.height == now.height);
  for (auto pair : {std::pair(old.transform.a, now.transform.a),
                    {old.transform.b, now.transform.b},
                    {old.transform.c, now.transform.c},
                    {old.transform.d, now.transform.d},
                    {old.transform.e, now.transform.e},
                    {old.transform.f, now.transform.f}})
    assert(std::abs(pair.first - pair.second) < 1e-9);
  assert(old.paint.fill == now.paint.fill &&
         old.paint.stroke == now.paint.stroke &&
         old.paint.stroke_width == now.paint.stroke_width);
  assert(old.paint.fill_color.red == now.paint.fill_color.red &&
         old.paint.fill_color.green == now.paint.fill_color.green &&
         old.paint.fill_color.blue == now.paint.fill_color.blue &&
         old.paint.fill_color.alpha == now.paint.fill_color.alpha);
  assert(old.paint.stroke_cap == now.paint.stroke_cap &&
         old.paint.stroke_join == now.paint.stroke_join);
  assert(old.text_flow.paragraphs.size() == now.text_flow.paragraphs.size());
  if (old.raster)
    assert(now.raster && *old.raster->data == *now.raster->data);
}
} // namespace
int main(int argc, char **argv) {
  auto p = source();
  auto original = p.xml.toByteArray();
  auto source_styles = p.entry("styles.xml");
  auto before = p;
  auto scenes = read_presentation_scenes(p);
  const auto &scene = scenes.at(0);
  auto x = object(scene, "X");
  auto f = native_stage_fragment(p, scene, {x, x});
  auto copied = unpack(f);
  auto models = read_presentation_scenes(copied);
  assert(f.text == "X Ω" && models.size() == 1 && models[0].shapes.size() == 1);
  const auto &shape = models[0].shapes[0];
  same(scene.shapes[x.shape], shape);
  assert(std::abs(shape.transform.e - 138) < 1e-9 &&
         std::abs(shape.transform.f - 87) < 1e-9);
  assert(shape.paint.fill_color.red == 224 &&
         shape.paint.fill_color.green == 64 &&
         shape.paint.fill_color.blue == 32);
  assert(shape.paint.fill_color.alpha == .5 && shape.paint.stroke_width == 3 &&
         shape.paint.padding_left == 2);
  assert(shape.paint.stroke_cap == ScenePaint::Cap::Round &&
         shape.paint.stroke_join == ScenePaint::Join::Bevel);
  assert(copied.entry_names().size() == 4 &&
         !copied.entry_data("opaque/source") && !copied.entry_data("meta.xml"));
  assert(copied.entry("styles.xml").find("UNSELECTED") == Bytes::npos);
  assert(copied.xml.toByteArray().find("draw:name=\"Y\"") == Bytes::npos);
  assert(copied.xml.elementsByTagNameNS(DRAW, "g").size() == 1);
  assert(copied.xml.elementsByTagNameNS(DRAW, "layer").size() == 1);
  assert(read_document_xml(copied.entry("styles.xml"))
             .elementsByTagNameNS(STYLE, "font-face")
             .size() == 1);
  auto pair = native_stage_fragment(p, scene, {object(scene, "Y"), x});
  auto pair_package = unpack(pair);
  auto pair_scene = read_presentation_scenes(pair_package).at(0);
  assert(pair_scene.shapes.size() == 2 && pair_scene.shapes[0].name == "X" &&
         pair_scene.shapes[1].name == "Y");
  assert(pair_package.xml.elementsByTagNameNS(DRAW, "g").size() == 1);
  assert(pair_package.entry("Pictures/image.svg") ==
         p.entry("Pictures/image.svg"));
  assert(!pair_package.entry_data("Pictures/unselected.svg") &&
         !pair_package.entry_data("opaque/source"));
  auto group = native_stage_fragment(p, scene, {x, object(scene, "X", 1)});
  auto group_package = unpack(group);
  auto group_scenes = read_presentation_scenes(group_package);
  assert(
      group_scenes[0].shapes.size() ==
      2); // Hidden and printer-only members stay in source, with their layers.
  assert(group_package.xml.elementsByTagNameNS(DRAW, "rect").size() == 2);
  auto print_scenes =
      read_presentation_scenes(group_package, {}, SceneTarget::Print);
  assert(print_scenes[0].shapes.size() == 3);
  for (const auto &old : scene.shapes)
    if (old.name == "X" || old.name == "Y")
      same(old,
           group_scenes[0].shapes[object(group_scenes[0], old.name).shape]);
  assert(p.xml.toByteArray() == original &&
         p.entry("styles.xml") == source_styles &&
         p.part_changes_from(before).empty());
  // Extended source is checked separately from the strict ODF 1.2 document
  // corpus. Preserve a QName-valued foreign attribute when the destination's
  // root already binds the same prefix to OpenFormula.
  auto extended = p;
  extended.xml = read_document_xml(original);
  named(extended.xml, "X")
      .setAttributeNS("urn:calligra:clipboard:test", "of:hint", "of:Value");
  auto extended_scenes = read_presentation_scenes(extended);
  auto extended_copy = unpack(native_stage_fragment(
      extended, extended_scenes[0], {object(extended_scenes[0], "X")}));
  auto extended_x = named(extended_copy.xml, "X");
  assert(extended_x.attributeNS("urn:calligra:clipboard:test", "hint") ==
         "of:Value");
  bool qualified = false;
  for (const auto &a : extended_x.attributes())
    if (a.uri == "urn:calligra:clipboard:test")
      qualified = a.name == "of:hint";
  assert(qualified);
  auto legacy = p;
  legacy.xml = read_document_xml(original);
  named(legacy.xml, "X")
      .parentNode()
      .toElement()
      .setAttributeNS(DRAW, "draw:transform", "translate(10pt 5pt)");
  auto legacy_scenes = read_presentation_scenes(legacy);
  auto legacy_copy = unpack(native_stage_fragment(
      legacy, legacy_scenes[0], {object(legacy_scenes[0], "X")}));
  auto legacy_shape = read_presentation_scenes(legacy_copy)[0].shapes[0];
  same(legacy_scenes[0].shapes[object(legacy_scenes[0], "X").shape],
       legacy_shape);
  assert(std::abs(legacy_shape.transform.e - 148) < 1e-9 &&
         std::abs(legacy_shape.transform.f - 92) < 1e-9);
  auto protected_source = p;
  auto protected_styles = read_document_xml(p.entry("styles.xml"));
  for (auto layer : protected_styles.elementsByTagNameNS(DRAW, "layer"))
    if (layer.attributeNS(DRAW, "name") == "Art")
      layer.setAttributeNS(DRAW, "draw:protected", "true");
  protected_source.set_entry("styles.xml", protected_styles.toByteArray(),
                             "text/xml");
  auto protected_scenes = read_presentation_scenes(protected_source);
  assert(protected_scenes[0]
             .shapes[object(protected_scenes[0], "X").shape]
             .layer_protected);
  auto protected_copy =
      unpack(native_stage_fragment(protected_source, protected_scenes[0],
                                   {object(protected_scenes[0], "X")}));
  assert(read_presentation_scenes(protected_copy)[0].shapes[0].layer_protected);
  rejects([&] { native_stage_fragment(p, scene, {}); });
  auto missing = p;
  missing.xml = read_document_xml(original);
  named(missing.xml, "X").setAttributeNS(DRAW, "draw:style-name", "missing");
  auto missing_scenes = read_presentation_scenes(missing);
  rejects([&] {
    native_stage_fragment(missing, missing_scenes[0],
                          {object(missing_scenes[0], "X")});
  });
  auto linked = p;
  linked.xml = read_document_xml(original);
  auto para = child(named(linked.xml, "X"), TEXT, "p");
  auto link = element(linked.xml, TEXT, "text:a", para);
  link.setAttributeNS(XLINK, "xlink:href", "#a");
  link.setAttributeNS(XLINK, "xlink:type", "simple");
  link.appendChild(linked.xml.createTextNode("Link"));
  auto linked_scenes = read_presentation_scenes(linked);
  rejects([&] {
    native_stage_fragment(linked, linked_scenes[0],
                          {object(linked_scenes[0], "X")});
  });
  auto closed = native_stage_fragment(
      linked, linked_scenes[0],
      {object(linked_scenes[0], "X"), object(linked_scenes[0], "A")});
  assert(!closed.archive.empty());
  auto layer_link = p;
  layer_link.xml = read_document_xml(original);
  auto target_bookmark = element(layer_link.xml, TEXT, "text:bookmark",
                                 child(named(layer_link.xml, "A"), TEXT, "p"));
  target_bookmark.setAttributeNS(TEXT, "text:name", "Art");
  auto layer_anchor = element(layer_link.xml, TEXT, "text:a",
                              child(named(layer_link.xml, "X"), TEXT, "p"));
  layer_anchor.setAttributeNS(XLINK, "xlink:href", "#Art");
  layer_anchor.appendChild(layer_link.xml.createTextNode("Target"));
  auto layer_link_scenes = read_presentation_scenes(layer_link);
  rejects([&] {
    native_stage_fragment(layer_link, layer_link_scenes[0],
                          {object(layer_link_scenes[0], "X")});
  });
  assert(!native_stage_fragment(layer_link, layer_link_scenes[0],
                                {object(layer_link_scenes[0], "X"),
                                 object(layer_link_scenes[0], "A")})
              .archive.empty());
  layer_anchor.setAttributeNS(XLINK, "xlink:href", "#outer");
  layer_link_scenes = read_presentation_scenes(layer_link);
  rejects([&] {
    native_stage_fragment(layer_link, layer_link_scenes[0],
                          {object(layer_link_scenes[0], "X")});
  });
  assert(!native_stage_fragment(layer_link, layer_link_scenes[0],
                                {object(layer_link_scenes[0], "X", 1)})
              .archive.empty());
  auto signed_source = p;
  signed_source.set_entry("META-INF/documentsignatures.xml", "signature",
                          "text/xml");
  OdfPackage locked(Kind::Stage);
  locked.load_bytes(signed_source.archive_bytes());
  assert(locked.read_only());
  auto locked_scenes = read_presentation_scenes(locked);
  auto read_only_copy = unpack(native_stage_fragment(
      locked, locked_scenes[0], {object(locked_scenes[0], "X")}));
  assert(!read_only_copy.read_only() &&
         !read_only_copy.entry_data("META-INF/documentsignatures.xml"));
  for (auto version : {Text("1.3"), Text("1.4"), Text("1.10")}) {
    auto newer = p;
    newer.xml = read_document_xml(original);
    newer.xml.documentElement().setAttributeNS(OFFICE, "office:version",
                                               version);
    auto newer_scenes = read_presentation_scenes(newer);
    auto fragment = unpack(native_stage_fragment(
        newer, newer_scenes[0], {object(newer_scenes[0], "X")}));
    assert(fragment.xml.documentElement().attributeNS(OFFICE, "version") ==
           version);
  }
  auto resources = p;
  resources.xml = read_document_xml(original);
  auto y = resources.xml.elementsByTagNameNS(DRAW, "image").front();
  y.setAttributeNS(XLINK, "xlink:href", "Pictures/dependency.svg");
  resources.set_entry(
      "Pictures/dependency.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><image href="nested/leaf.svg"/></svg>)",
      "image/svg+xml");
  resources.set_entry(
      "Pictures/nested/leaf.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg"><rect width="20" height="10" fill="red"/></svg>)",
      "image/svg+xml");
  auto resource_scenes = read_presentation_scenes(resources);
  auto resource_fragment = unpack(native_stage_fragment(
      resources, resource_scenes[0], {object(resource_scenes[0], "Y")}));
  assert(resource_fragment.entry("Pictures/dependency.svg") ==
         resources.entry("Pictures/dependency.svg"));
  assert(resource_fragment.entry("Pictures/nested/leaf.svg") ==
         resources.entry("Pictures/nested/leaf.svg"));
  assert(!resource_fragment.entry_data("Pictures/image.svg"));
  resources.set_entry(
      "Pictures/dependency.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg"><image href="../meta.xml"/></svg>)",
      "image/svg+xml");
  resource_scenes = read_presentation_scenes(resources);
  rejects([&] {
    native_stage_fragment(resources, resource_scenes[0],
                          {object(resource_scenes[0], "Y")});
  });
  auto directory_image = p;
  directory_image.xml = read_document_xml(original);
  directory_image.xml.elementsByTagNameNS(DRAW, "image")
      .front()
      .setAttributeNS(XLINK, "xlink:href", "Pictures/");
  auto directory_scenes = read_presentation_scenes(directory_image);
  rejects([&] {
    native_stage_fragment(directory_image, directory_scenes[0],
                          {object(directory_scenes[0], "Y")});
  });
  auto embedded = p;
  embedded.xml = read_document_xml(original);
  auto frame = embedded.xml.elementsByTagNameNS(DRAW, "image")
                   .front()
                   .parentNode()
                   .toElement();
  auto object_node = embedded.xml.createElementNS(DRAW, "draw:object");
  object_node.setAttributeNS(XLINK, "xlink:href", "Object%201/");
  object_node.setAttributeNS(XLINK, "xlink:type", "simple");
  frame.insertBefore(object_node, frame.firstChild());
  embedded.set_entry("Object 1/", {}, mime(Kind::Words));
  embedded.set_entry(
      "Object 1/content.xml",
      R"(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" office:version="1.2"><office:body><office:text><text:p>Selected embedded object</text:p></office:text></office:body></office:document-content>)",
      "text/xml");
  embedded.set_entry("Object 1/opaque.bin", Bytes("selected\0object", 15),
                     "application/octet-stream");
  auto embedded_scenes = read_presentation_scenes(embedded);
  auto embedded_copy = unpack(native_stage_fragment(
      embedded, embedded_scenes[0], {object(embedded_scenes[0], "Y")}));
  assert(embedded_copy.entry("Object 1/content.xml") ==
         embedded.entry("Object 1/content.xml"));
  assert(embedded_copy.entry("Object 1/opaque.bin") ==
         embedded.entry("Object 1/opaque.bin"));
  assert(embedded_copy.entry_data("Object 1/") &&
         !embedded_copy.entry_data("meta.xml") &&
         !embedded_copy.entry_data("Pictures/unselected.svg"));
  auto image_alias = embedded;
  image_alias.xml = read_document_xml(embedded.xml.toByteArray());
  image_alias.xml.elementsByTagNameNS(DRAW, "image")
      .front()
      .setAttributeNS(XLINK, "xlink:href", "Object%201/");
  auto alias_scenes = read_presentation_scenes(image_alias);
  rejects([&] {
    native_stage_fragment(image_alias, alias_scenes[0],
                          {object(alias_scenes[0], "Y")});
  });
  auto invalid_object = p;
  invalid_object.xml = read_document_xml(original);
  auto invalid_frame = invalid_object.xml.elementsByTagNameNS(DRAW, "image")
                           .front()
                           .parentNode()
                           .toElement();
  auto invalid_node = invalid_object.xml.createElementNS(DRAW, "draw:object");
  invalid_node.setAttributeNS(XLINK, "xlink:href", "Pictures/");
  invalid_frame.insertBefore(invalid_node, invalid_frame.firstChild());
  invalid_object.set_entry("Pictures/", {}, mime(Kind::Words));
  auto invalid_scenes = read_presentation_scenes(invalid_object);
  rejects([&] {
    native_stage_fragment(invalid_object, invalid_scenes[0],
                          {object(invalid_scenes[0], "Y")});
  });
  TempDir model_temp;
  auto model_path = model_temp.path() + "/source.odp";
  p.save(model_path);
  Presentation model;
  model.load(model_path);
  auto revision = model.revision();
  auto exported = model.copy_objects(0, {ref(model, "X")});
  assert(!exported.archive.empty());
  assert(model.revision() == revision && !model.modified() &&
         !model.can_undo() && !model.can_redo());
  model.move_shape(0, find(model, "X"), {5, 0});
  model.undo();
  revision = model.revision();
  assert(model.can_redo());
  model.copy_objects(0, {ref(model, "X")});
  assert(model.revision() == revision && model.can_redo() && !model.modified());
  model.begin_shape_move(0, find(model, "X"), {140, 40});
  rejects([&] { model.copy_objects(0, {ref(model, "X")}); });
  model.finish_handle_drag(true);
  if (argc == 3 && Text(argv[1]) == "--documents") {
    auto dir = Text(argv[2]);
    std::filesystem::create_directories(dir.toStdString());
    p.save(dir + "/source.odp");
    copied.save(dir + "/selected-x.odp");
    pair_package.save(dir + "/selected-pair.odp");
    group_package.save(dir + "/selected-group.odp");
  }
  std::cout << "PASS selected-object ODP dependency closure, ancestor "
               "placement, shared wrappers, styles/fonts/layers/assets, source "
               "preservation, reference guards and read-only copying\n";
}
