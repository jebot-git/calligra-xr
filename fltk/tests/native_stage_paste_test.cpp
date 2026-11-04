// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_context_fixture.h"
#include "native_stage_paste.h"
#include "native_stage_clipboard.h"
#include "native_stage_cut.h"
#include "native_stage_materialize.h"
#include "native_table_layout.h"
#include <filesystem>
#include <unistd.h>
namespace {
const SceneShape &chosen_shape(const std::vector<SlideScene> &scenes) {
  for (const auto &s : scenes[0].shapes)
    if (s.name == "X")
      return s;
  std::abort();
}
NativeStageFragment copied(const OdfPackage &p, int parents = 0) {
  auto scenes = read_presentation_scenes(p);
  const auto &s = chosen_shape(scenes);
  return native_stage_fragment(p, scenes[0],
                               {{int(&s - scenes[0].shapes.data()), parents}});
}
OdfPackage destination() {
  auto p = contexts(false);
  auto d = read_document_xml(p.entry("styles.xml"));
  auto common = common_group(d);
  for (auto node : children(common, STYLE, "default-style")) {
    auto t = props(d, node, "text-properties");
    t.removeAttributeNS(STYLE, "font-name");
    t.setAttributeNS(FO, "fo:font-size", "37pt");
    t.setAttributeNS(FO, "fo:font-family", "monospace");
    t.setAttributeNS(FO, "fo:color", "#ff0000");
    if (node.attributeNS(STYLE, "family") == "paragraph") {
      auto q = props(d, node, "paragraph-properties");
      q.setAttributeNS(FO, "fo:margin-left", "70pt");
      q.setAttributeNS(STYLE, "style:tab-stop-distance", "17pt");
    }
  }
  for (auto family : {Text("table"), Text("table-column"), Text("table-row")}) {
    auto def = element(d, STYLE, "style:default-style");
    def.setAttributeNS(STYLE, "style:family", family);
    common.insertBefore(def, common.firstChild());
    auto prop = props(d, def, family + "-properties");
    if (family == "table") {
      prop.setAttributeNS(FO, "fo:margin-left", "30pt");
      prop.setAttributeNS(FO, "fo:background-color", "#40e080");
    }
    if (family == "table-column") {
      prop.setAttributeNS(STYLE, "style:column-width", "50pt");
      prop.setAttributeNS(STYLE, "style:rel-column-width", "3*");
    }
    if (family == "table-row") {
      prop.setAttributeNS(STYLE, "style:row-height", "80pt");
      prop.setAttributeNS(STYLE, "style:min-row-height", "60pt");
    }
  }
  for (auto def : children(common, STYLE, "default-style"))
    if (def.attributeNS(STYLE, "family") == "table-cell") {
      auto prop = props(d, def, "table-cell-properties");
      prop.setAttributeNS(FO, "fo:border", "12pt double #ff0000");
      prop.setAttributeNS(FO, "fo:padding", "20pt");
      prop.setAttributeNS(FO, "fo:background-color", "#ffe000");
      props(d, def, "text-properties")
          .setAttributeNS(FO, "fo:font-size", "300%");
    }
  p.set_entry("styles.xml", d.toByteArray(), "text/xml");
  return p;
}
void layout_equal(const SceneShape &a, const SceneShape &b) {
  assert(a.paint.font == b.paint.font &&
         a.paint.fill_color == b.paint.fill_color &&
         a.paint.padding_left == b.paint.padding_left);
  auto metric = [](const Text &s, const TextFont &f) {
    return TextMetrics{double(s.size()) * f.size / 2, f.size * .8, f.size * .2};
  };
  auto width = a.width - a.paint.padding_left - a.paint.padding_right;
  auto x = layout_text_box(a.text_flow, width, metric),
       y = layout_text_box(b.text_flow, width, metric);
  assert(x.lines.size() == y.lines.size() &&
         x.paints.size() == y.paints.size() &&
         x.cells.size() == y.cells.size());
  near(x.height, y.height);
  for (size_t i = 0; i < x.lines.size(); ++i) {
    const auto &u = x.lines[i], &v = y.lines[i];
    near(u.x, v.x);
    near(u.y, v.y);
    near(u.height, v.height);
    near(u.width, v.width);
    const auto &us =
        u.style ? *u.style : a.text_flow.paragraphs.at(u.paragraph);
    const auto &vs =
        v.style ? *v.style : b.text_flow.paragraphs.at(v.paragraph);
    assert(us.font == vs.font && us.color == vs.color && us.text == vs.text);
    assert(u.tabs.size() == v.tabs.size());
    for (size_t j = 0; j < u.tabs.size(); ++j)
      near(u.tabs[j].position, v.tabs[j].position);
  }
  for (size_t i = 0; i < x.paints.size(); ++i) {
    const auto &u = x.paints[i], &v = y.paints[i];
    assert(u.coverage == v.coverage && u.ellipse == v.ellipse &&
           u.color == v.color);
    near(u.rect.x, v.rect.x);
    near(u.rect.y, v.rect.y);
    near(u.rect.width, v.rect.width);
    near(u.rect.height, v.rect.height);
  }
}
OdfPackage materialize() {
  auto source = contexts();
  auto target = destination();
  auto xml = target.xml.toByteArray(), styles = target.entry("styles.xml");
  auto draft =
      prepare_stage_merge(target, 0, encode_stage_fragment(copied(source)));
  materialize_stage_import_contexts(draft);
  assert(target.xml.toByteArray() == xml &&
         target.entry("styles.xml") == styles);
  auto before = read_presentation_scenes(draft.source),
       after = read_presentation_scenes(draft.package);
  const auto &original = before[0].shapes[0];
  const SceneShape *result = nullptr;
  for (const auto &s : after[0].shapes)
    if (s.source == draft.selected[0])
      result = &s;
  assert(result);
  layout_equal(original, *result);
  assert(original.text_flow.paragraph_sources.size() == 10 &&
         result->text_flow.paragraph_sources.size() == 10);
  auto old = read_presentation_scenes(target);
  layout_equal(chosen_shape(old), chosen_shape(after));
  return std::move(draft.package);
}
void transaction(const Text &dir, const Text &artifacts = {}) {
  auto target = destination();
  auto path = dir + "/target.odp";
  target.save(path);
  auto source = contexts(false);
  auto wire = encode_stage_fragment(copied(source, 1));
  Presentation p;
  p.load(path);
  auto old_image = p.scene(0).shapes.at(find(p, "Y")).image;
  auto old_bytes = p.image(old_image);
  const auto original_revision = p.revision();
  auto original = content(p); // fixture helper expects A
  auto draft = p.prepare_paste_objects(0, wire, {0, 0, find(p, "X")});
  assert(p.revision() == original_revision && !p.modified() && !p.can_undo());
  auto position = p.commit_paste_objects(std::move(draft));
  assert(p.modified() && p.can_undo() && !position.objects.empty());
  auto inserted = content(p);
  assert(p.scene(0).shapes.at(find(p, "Y")).image == old_image &&
         p.image(old_image) == old_bytes);
  auto pasted_fragment = p.copy_objects(0, position.objects);
  OdfPackage pasted(Kind::Stage);
  pasted.load_bytes(pasted_fragment.archive);
  auto pasted_scenes = read_presentation_scenes(pasted);
  for (const auto &shape : pasted_scenes[0].shapes)
    if (!shape.image.empty())
      assert(!pasted.entry(shape.image).empty());
  if (!artifacts.empty()) {
    OdfPackage selected(Kind::Stage);
    selected.load_bytes(copied(source, 1).archive);
    selected.save(artifacts + "/transaction-source-selected.odp");
    pasted.save(artifacts + "/transaction-pasted-selected.odp");
  }
  assert(inserted != original);
  assert(p.undo());
  assert(content(p) == original && !p.modified() && p.can_redo());
  {
    auto cancelled = p.prepare_paste_objects(0, wire);
  }
  assert(content(p) == original && p.can_redo());
  assert(p.redo());
  assert(content(p) == inserted);
  if (!artifacts.empty())
    p.save(artifacts + "/transaction-pasted.odp");
  auto saved = dir + "/pasted.odp";
  p.save(saved);
  Presentation reopened;
  reopened.load(saved);
  assert(reopened.scene(0).shapes.size() == p.scene(0).shapes.size());
  auto stale = p.prepare_paste_objects(0, wire);
  p.move_shape(0, find(p, "A"), {1, 0});
  auto changed = content(p);
  rejects([&] { p.commit_paste_objects(std::move(stale)); });
  assert(content(p) == changed);
  auto foreign = p.prepare_paste_objects(0, wire);
  Presentation other;
  other.load(path);
  rejects([&] { other.commit_paste_objects(std::move(foreign)); });
  assert(!other.modified());
  rejects([&] { p.prepare_paste_objects(-1, wire); });
  rejects([&] { p.prepare_paste_objects(0, "bad"); });
  assert(content(p) == changed);
  auto reloaded = p.prepare_paste_objects(0, wire);
  p.load(path);
  rejects([&] { p.commit_paste_objects(std::move(reloaded)); });
  assert(!p.modified() && !p.can_undo());
  auto moved = p.prepare_paste_objects(0, wire);
  auto ready = std::move(moved);
  rejects([&] { p.commit_paste_objects(std::move(moved)); });
  p.commit_paste_objects(std::move(ready));
}
void representations(const Text &dir) {
  auto formats = stage_clipboard_formats(copied(contexts(false), 1));
  assert(formats.size() == 4 && formats[0].mime == native_stage_fragment_mime);
  Presentation p;
  auto path = dir + "/representation-target.odp";
  destination().save(path);
  p.load(path);
  for (const auto &format : formats) {
    auto before = content(p);
    auto wire = stage_clipboard_fragment(format);
    auto draft = p.prepare_paste_objects(0, wire);
    auto position = p.commit_paste_objects(std::move(draft));
    assert(!position.objects.empty() && content(p) != before);
    p.undo();
    assert(content(p) == before && !p.modified());
  }
  auto bad = formats.front();
  bad.data = {'b', 'a', 'd'};
  rejects([&] { stage_clipboard_fragment(bad); });
  rejects([&] { stage_clipboard_fragment({"text/plain", {0xff}}); });
  rejects([&] { stage_clipboard_fragment({"text/plain", {0}}); });
  assert(stage_clipboard_fragment({"text/plain", {}}).empty());
  for (const auto &text : {std::string(100001, '\n'), std::string(1000001, 'a'),
                           std::string(4000001, 'b')}) {
    std::vector<uint8_t> large(text.begin(), text.end());
    rejects([&] { stage_clipboard_fragment({"text/plain", large}); });
  }
  std::vector<uint8_t> nodes;
  for (int i = 0; i < 500000; ++i) {
    nodes.push_back('a');
    nodes.push_back('\t');
  }
  rejects([&] { stage_clipboard_fragment({"text/plain", nodes}); });

  auto bad_source = contexts(false);
  auto variable = element(bad_source.xml, TEXT, "text:variable-get",
                          child(chosen(bad_source.xml), TEXT, "p"));
  variable.setAttributeNS(TEXT, "text:name", "variable");
  auto before = content(p);
  auto revision = p.revision();
  rejects([&] {
    p.prepare_paste_objects(0, encode_stage_fragment(copied(bad_source)));
  });
  assert(content(p) == before && p.revision() == revision && p.can_redo());
  auto conditional = contexts(false);
  auto styles = read_document_xml(conditional.entry("styles.xml"));
  for (auto node : children(common_group(styles), STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == "Child") {
      auto map = element(styles, STYLE, "style:map", node);
      map.setAttributeNS(STYLE, "style:condition", "outline-level()=1");
      map.setAttributeNS(STYLE, "style:apply-style-name", "Frame");
    }
  conditional.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  rejects([&] {
    p.prepare_paste_objects(0, encode_stage_fragment(copied(conditional)));
  });
  auto navigation = contexts(false);
  children(navigation.body(), DRAW, "page")[0].setAttributeNS(
      DRAW, "draw:nav-order", "y x n1 n2");
  rejects([&] {
    p.prepare_paste_objects(0, encode_stage_fragment(copied(navigation, 1)));
  });
  p.begin_objects_move(0, {ref(p, "A")}, {});
  rejects([&] {
    p.prepare_paste_objects(0, stage_clipboard_fragment(formats.front()));
  });
  p.finish_handle_drag(true);
  auto signed_target = destination();
  signed_target.set_entry("META-INF/documentsignatures.xml", "signature",
                          "text/xml");
  auto signed_path = dir + "/signed.odp";
  signed_target.save(signed_path);
  Presentation locked;
  locked.load(signed_path);
  auto locked_xml = content(locked);
  assert(locked.read_only());
  rejects([&] {
    locked.prepare_paste_objects(0, stage_clipboard_fragment(formats.front()));
  });
  assert(content(locked) == locked_xml && !locked.modified() &&
         !locked.can_undo());
  auto newer = contexts(false);
  newer.xml.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  rejects([&] {
    p.prepare_paste_objects(0, encode_stage_fragment(copied(newer)));
  });
}
} // namespace
int main(int argc, char **argv) {
  auto output = materialize();
  auto pattern = (std::filesystem::temp_directory_path() /
                  "calligra-stage-paste-test-XXXXXX")
                     .string();
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back(0);
  auto temporary = ::mkdtemp(buffer.data());
  assert(temporary);
  std::filesystem::path temp(temporary);
  transaction(Text(temp.string()));
  representations(Text(temp.string()));
  std::filesystem::remove_all(temp);
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    auto dir = Text(argv[2]);
    auto source = contexts(), target = destination();
    source.save(dir + "/source.odp");
    target.save(dir + "/destination-before.odp");
    output.save(dir + "/materialized.odp");
    auto source_fragment = copied(source);
    OdfPackage selected(Kind::Stage);
    selected.load_bytes(source_fragment.archive);
    selected.save(dir + "/source-selected.odp");
    auto draft =
        prepare_stage_merge(target, 0, encode_stage_fragment(source_fragment));
    materialize_stage_import_contexts(draft);
    auto scenes = read_presentation_scenes(draft.package);
    auto subset = native_stage_fragment(
        draft.package, scenes[0],
        native_object_references(scenes[0], draft.selected));
    selected.load_bytes(subset.archive);
    selected.save(dir + "/materialized-selected.odp");
    transaction(dir, dir);
  }
  std::cout << "PASS receiving native styles/table contexts, source layout, "
               "guarded paste/history and save/reopen\n";
}
