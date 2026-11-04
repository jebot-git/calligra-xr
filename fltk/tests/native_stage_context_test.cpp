// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_context_fixture.h"
#include "native_stage_context.h"
#include "native_stage_fragment.h"
#include "native_stage_source.h"
#include "native_text_shaper.h"
#include "native_tabs.h"
#include <filesystem>
namespace {
const SceneShape &shape(const std::vector<SlideScene> &scenes) {
  for (const auto &s : scenes.at(0).shapes)
    if (s.name == "X")
      return s;
  std::abort();
}
TextParagraph frame_base(const SceneShape &shape) {
  TextParagraph p;
  p.font = shape.paint.font;
  if (!shape.paint.use_window_text_color)
    p.color = shape.paint.text_color;
  return p;
}
void capture() {
  auto p = contexts();
  auto xml = p.xml.toByteArray(), styles = p.entry("styles.xml");
  OdfTextReader reader(p);
  auto scene = read_presentation_scenes(p);
  const auto &x = shape(scene);
  assert(x.paint.font.language == "fr-CA");
  const auto &flow = x.text_flow;
  assert(flow.paragraphs.size() == 4 && flow.paragraph_sources.size() == 4);
  auto base = frame_base(x);
  auto a = reader.paragraph_context(flow.paragraph_sources[0], base, "Frame");
  auto b = reader.paragraph_context(flow.paragraph_sources[1], base, "Frame");
  near(a.font.size, 11);
  near(b.font.size, 13.2);
  near(a.left, 9);
  near(b.left, 15);
  near(flow.paragraphs[0].font.size, 16.5);
  near(flow.paragraphs[1].font.size, 16.5);
  auto table = flow.blocks.back().table;
  assert(table && table->cells.size() == 6);
  for (const auto &cell : table->cells) {
    auto names = std::vector<Text>{cell.style_name};
    if (cell.column == 2) {
      auto context = reader.paragraph_context({}, base, "Frame", names);
      assert(cell.empty_format);
      near(context.font.size, 19.8);
      near(context.font.size, cell.empty_format->font.size);
      continue;
    }
    auto &block = cell.blocks.at(0);
    assert(block.formatted);
    assert(flow.paragraph_sources.at(block.index).localName() == "p");
    auto context = reader.paragraph_context(flow.paragraph_sources[block.index],
                                            base, "Frame", names);
    near(context.font.size, cell.column == 0 ? 12.1 : 16.5);
    near(block.formatted->font.size, cell.column == 0 ? 18.15 : 24.75);
    auto inner = cell.blocks.at(1).table;
    assert(inner && inner->cells.size() == 1);
    const auto &nested = inner->cells[0];
    names.push_back(nested.style_name);
    auto index = nested.blocks.at(0).index;
    auto nested_context = reader.paragraph_context(
        flow.paragraph_sources[index], base, "Frame", names);
    near(nested_context.font.size, cell.column == 0 ? 15.125 : 20.625);
    near(nested.blocks.at(0).formatted->font.size,
         cell.column == 0 ? 22.6875 : 30.9375);
    assert(nested_context.color ==
           std::optional<TextColor>(TextColor{0x65, 0x43, 0x21}));
  }
  rejects([&] { reader.paragraph_context(x.source, base); });
  rejects(
      [&] { reader.paragraph_context({}, base, {}, std::vector<Text>(17)); });
  auto after = reader.frame(x.source, base, "Frame");
  assert(after.paragraph_sources == flow.paragraph_sources);
  assert(after.blocks.back().table->cells.size() == 6);
  assert(p.xml.toByteArray() == xml && p.entry("styles.xml") == styles);
}
void paragraph_equal(const TextParagraph &a, const TextParagraph &b,
                     bool exact_tabs = true) {
  assert(a.font == b.font && a.color == b.color &&
         a.background == b.background && a.decorations == b.decorations);
  for (auto [x, y] : {std::pair{a.left, b.left},
                      {a.right, b.right},
                      {a.before, b.before},
                      {a.after, b.after},
                      {a.indent, b.indent},
                      {a.line_factor, b.line_factor}})
    near(x, y);
  assert(a.alignment == b.alignment && a.direction == b.direction);
  assert(a.keep_next == b.keep_next && a.keep_together == b.keep_together &&
         a.break_before == b.break_before && a.break_after == b.break_after);
  assert(a.spans.size() == b.spans.size());
  for (size_t i = 0; i < a.spans.size(); ++i)
    assert(a.spans[i].font == b.spans[i].font &&
           a.spans[i].color == b.spans[i].color &&
           a.spans[i].decorations == b.spans[i].decorations);
  if (!exact_tabs)
    return;
  assert(a.tabs.size() == b.tabs.size());
  for (size_t i = 0; i < a.tabs.size(); ++i) {
    const auto &x = a.tabs[i], &y = b.tabs[i];
    near(x.position, y.position);
    near(x.width_scale, y.width_scale);
    assert(x.width == y.width && x.color == y.color && x.type == y.type &&
           x.delimiter == y.delimiter && x.leader == y.leader &&
           x.line_style == y.line_style && x.double_line == y.double_line);
    assert(x.leader_font.family == y.leader_font.family &&
           x.leader_font.size == y.leader_font.size &&
           x.leader_font.weight == y.leader_font.weight &&
           x.leader_font.italic == y.leader_font.italic &&
           x.leader_font.language == y.leader_font.language &&
           x.leader_font.color_set == y.leader_font.color_set &&
           x.leader_font.color == y.leader_font.color);
    near(x.leader_font.scale, y.leader_font.scale);
  }
}
void paint_equal(const ScenePaint &a, const ScenePaint &b) {
  assert(a.font == b.font && a.fill == b.fill && a.stroke == b.stroke &&
         a.fill_color == b.fill_color && a.stroke_color == b.stroke_color &&
         a.text_color == b.text_color &&
         a.use_window_text_color == b.use_window_text_color &&
         a.even_odd == b.even_odd && a.stroke_cap == b.stroke_cap &&
         a.stroke_join == b.stroke_join &&
         a.vertical_alignment == b.vertical_alignment);
  for (auto [x, y] : {std::pair{a.stroke_width, b.stroke_width},
                      {a.padding, b.padding},
                      {a.padding_left, b.padding_left},
                      {a.padding_right, b.padding_right},
                      {a.padding_top, b.padding_top},
                      {a.padding_bottom, b.padding_bottom},
                      {a.image_opacity, b.image_opacity}})
    near(x, y);
}
OdfPackage materialize(OdfPackage source,
                       OdfPackage *receiving_before = nullptr,
                       bool absolute14 = false) {
  auto scenes = read_presentation_scenes(source);
  const auto &original = shape(scenes);
  OdfPackage target(Kind::Stage);
  target.load_bytes(source.archive_bytes());
  auto external = read_document_xml(target.entry("styles.xml"));
  if (absolute14) {
    target.xml.documentElement().setAttributeNS(OFFICE, "office:version",
                                                "1.4");
    external.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  }
  auto common = common_group(external);
  for (auto node : children(common, STYLE, "default-style")) {
    auto t = props(external, node, "text-properties");
    t.removeAttributeNS(STYLE, "font-name");
    t.setAttributeNS(FO, "fo:font-size", "37pt");
    t.setAttributeNS(FO, "fo:font-family", "monospace");
    t.setAttributeNS(FO, "fo:language", "de");
    t.setAttributeNS(FO, "fo:color", "#ff0000");
    if (node.attributeNS(STYLE, "family") == "paragraph") {
      auto q = props(external, node, "paragraph-properties");
      q.setAttributeNS(FO, "fo:margin-left", "70pt");
      q.setAttributeNS(STYLE, "style:tab-stop-distance", "17pt");
      if (absolute14)
        q.setAttributeNS(TEXT, "text:relative-tab-stop-position", "false");
    }
    if (node.attributeNS(STYLE, "family") == "graphic") {
      auto g = props(external, node, "graphic-properties");
      g.setAttributeNS(FO, "fo:padding", "30pt");
      g.setAttributeNS(DRAW, "draw:fill-color", "#00ff00");
    }
  }
  target.set_entry("styles.xml", external.toByteArray(), "text/xml");
  if (receiving_before)
    receiving_before->load_bytes(target.archive_bytes());
  auto before = read_presentation_scenes(target);
  auto receiving_defaults = OdfTextReader(target).paragraph_context({}, {});
  near(receiving_defaults.default_tab, 17);
  assert(receiving_defaults.relative_tabs == !absolute14);
  auto x = chosen(target.xml);
  auto automatic =
      child(target.xml.documentElement(), OFFICE, "automatic-styles");
  size_t next = 0;
  NativeStageContextStyles encoder(
      external, [&] { return "Context" + Text::number(++next); });
  auto paint = original.paint;
  bool presentation = x.hasAttributeNS(PRESENTATION, "style-name");
  auto graphic = encoder.graphic(paint, presentation ? Text("presentation")
                                                     : Text("graphic"));
  x.setAttributeNS(presentation ? PRESENTATION : DRAW,
                   presentation ? Text("presentation:style-name")
                                : Text("draw:style-name"),
                   graphic.attributeNS(STYLE, "name"));
  automatic.appendChild(target.xml.importNode(graphic, true));
  for (size_t i = 0; i < original.text_flow.paragraphs.size(); ++i) {
    auto value = original.text_flow.paragraphs[i];
    value.tabs = native_stage_portable_tabs(value, receiving_defaults,
                                            original.width, false);
    auto style = encoder.paragraph(value);
    auto paragraph = stage_candidate_source(
        original.text_flow.paragraph_sources[i], target.xml);
    paragraph.setAttributeNS(TEXT, "text:style-name",
                             style.attributeNS(STYLE, "name"));
    automatic.appendChild(target.xml.importNode(style, true));
  }
  for (auto style : encoder.supporting_styles())
    common.appendChild(style);
  auto faces = child(external.documentElement(), OFFICE, "font-face-decls");
  for (auto face : encoder.font_declarations())
    faces.appendChild(face);
  target.set_entry("styles.xml", external.toByteArray(), "text/xml");
  auto result = read_presentation_scenes(target);
  const auto &after = shape(result);
  paint_equal(after.paint, original.paint);
  for (size_t i = 0; i < original.text_flow.paragraphs.size(); ++i) {
    auto expected = original.text_flow.paragraphs[i];
    paragraph_equal(expected, after.text_flow.paragraphs[i], false);
    near(after.text_flow.paragraphs[i].default_tab, 17);
    assert(after.text_flow.paragraphs[i].relative_tabs == !absolute14);
    auto metric = [](const Text &s, const TextFont &f) {
      return TextMetrics{double(s.size()) * f.size / 2, f.size * .8,
                         f.size * .2};
    };
    auto old_box = layout_text_box(TextFlow{{expected}}, 400, metric);
    auto new_box =
        layout_text_box(TextFlow{{after.text_flow.paragraphs[i]}}, 400, metric);
    assert(old_box.lines.size() == new_box.lines.size() &&
           old_box.lines[0].tabs.size() == 2 &&
           new_box.lines[0].tabs.size() == 2);
    near(old_box.lines[0].tabs[1].position, 165);
    near(new_box.lines[0].tabs[1].position, 165);
    auto wrong = expected;
    wrong.default_tab = 17;
    auto wrong_box = layout_text_box(TextFlow{{wrong}}, 400, metric);
    near(wrong_box.lines[0].tabs[1].position, 168);
  }
  // Only the chosen object's styles change. Receiving defaults and all other
  // objects retain their pre-materialization source/model values.
  for (const auto &old : before[0].shapes)
    if (old.name != "X") {
      auto found =
          std::find_if(result[0].shapes.begin(), result[0].shapes.end(),
                       [&](const auto &s) { return s.name == old.name; });
      assert(found != result[0].shapes.end());
      paint_equal(found->paint, old.paint);
      assert(found->text_flow.paragraphs.size() ==
             old.text_flow.paragraphs.size());
      for (size_t i = 0; i < old.text_flow.paragraphs.size(); ++i)
        paragraph_equal(found->text_flow.paragraphs[i],
                        old.text_flow.paragraphs[i]);
    }
  return target;
}
void portable_tabs() {
  size_t cases = 0;
  for (bool source_relative : {false, true})
    for (bool target_relative : {false, true})
      for (bool rtl : {false, true})
        for (double margin : {-15., 0., 23.})
          for (double interval : {11., 30., 57.})
            for (bool explicit_stops : {false, true}) {
              TextParagraph p, q;
              p.left = margin;
              p.default_tab = interval;
              p.relative_tabs = source_relative;
              q = p;
              q.default_tab = 17;
              q.relative_tabs = target_relative;
              if (explicit_stops) {
                TextTabStop a;
                a.position = 35;
                a.type = TextTabStop::Center;
                TextTabStop b;
                b.position = 90;
                b.type = TextTabStop::Character;
                b.delimiter = U',';
                p.tabs = {a, b};
              }
              q.tabs = native_stage_portable_tabs(p, q, 300, rtl);
              for (double current : {-25., 0., 14., 35., 64., 100., 170., 230.})
                for (double width : {0., 19.}) {
                  auto a = next_tab(p, current, source_relative ? margin : 0,
                                    width, width / 2, rtl);
                  auto b = next_tab(q, current, target_relative ? margin : 0,
                                    width, width / 2, rtl);
                  near(a.position, b.position);
                  near(a.advance, b.advance);
                  near(a.offset, b.offset);
                  ++cases;
                }
            }
  assert(cases == 2304);
  TextParagraph p, q;
  q.default_tab = 17;
  p.list = TextListLabel{};
  p.list->visible = true;
  p.list->alignment_mode = true;
  p.list->follow = TextListLabel::Tab;
  rejects([&] { native_stage_portable_tabs(p, q, 200, false); });
  q.default_tab = p.default_tab;
  assert(native_stage_portable_tabs(p, q, 200, false).empty());
  p.list.reset();
  p.default_tab = 1e-14;
  p.tabs = {TextTabStop{}};
  p.tabs[0].position = 100000;
  rejects([&] { native_stage_portable_tabs(p, q, 100000, false); });
  p.tabs.clear();
  p.default_tab = .001;
  rejects([&] { native_stage_portable_tabs(p, q, 100, false); });
  p.default_tab = 30;
  rejects([&] { native_stage_portable_tabs(p, q, NAN, false); });
  std::cout << "PASS " << cases
            << " portable tab advances/origins/directions and bounded "
               "unsupported cases\n";
}
OdfPackage fallback_source() {
  auto p = contexts(false);
  auto styles = read_document_xml(p.entry("styles.xml"));
  for (auto t : styles.elementsByTagNameNS(STYLE, "text-properties")) {
    t.removeAttributeNS(STYLE, "font-name");
    t.removeAttributeNS(FO, "font-family");
  }
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  return p;
}
OdfPackage presentation_source() {
  auto p = contexts(false);
  auto automatic = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  auto s = style(p.xml, automatic, "PresentationOverlay", "presentation");
  auto g = props(p.xml, s, "graphic-properties");
  g.setAttributeNS(DRAW, "draw:opacity", "85%");
  g.setAttributeNS(DRAW, "draw:fill", "solid");
  g.setAttributeNS(DRAW, "draw:fill-color", "#e04020");
  g.setAttributeNS(DRAW, "draw:stroke", "solid");
  g.setAttributeNS(SVG, "svg:stroke-color", "#102030");
  g.setAttributeNS(SVG, "svg:stroke-width", "3pt");
  auto t = props(p.xml, s, "text-properties");
  t.setAttributeNS(FO, "fo:font-size", "110%");
  t.setAttributeNS(FO, "fo:language", "none");
  t.setAttributeNS(FO, "fo:country", "CA");
  auto frame = as_frame(p, chosen(p.xml));
  frame.removeAttributeNS(DRAW, "style-name");
  frame.setAttributeNS(PRESENTATION, "presentation:style-name",
                       "PresentationOverlay");
  return p;
}
void selected(const OdfPackage &p, const Text &path) {
  auto scenes = read_presentation_scenes(p);
  const auto &x = shape(scenes);
  auto index = size_t(&x - scenes[0].shapes.data());
  auto fragment = native_stage_fragment(p, scenes[0], {{int(index), 0}});
  OdfPackage result(Kind::Stage);
  result.load_bytes(fragment.archive);
  result.save(path);
}
void invalid_inputs() {
  auto p = contexts(false);
  auto xml = p.xml.toByteArray();
  size_t next = 0;
  NativeStageContextStyles styles(
      p.xml, [&] { return "Detached" + Text::number(++next); });
  ScenePaint paint;
  paint.font.family = "fixed";
  paint.font.size = 14;
  auto s = styles.graphic(paint);
  assert(s.parentNode().isNull());
  TextParagraph paragraph;
  paragraph.font.family = "fixed";
  paragraph.font.language = "fr-CA";
  auto t = styles.paragraph(paragraph);
  assert(t.parentNode().isNull());
  assert(p.xml.toByteArray() == xml);
  paint.image_opacity = 2;
  rejects([&] { styles.graphic(paint); });
  paint.image_opacity = 1;
  rejects([&] { styles.graphic(paint, "table-cell"); });
  paragraph.font.size = NAN;
  rejects([&] { styles.paragraph(paragraph); });
  paragraph.font.size = 12;
  paragraph.font.language = "sr-Latn-RS";
  rejects([&] { styles.paragraph(paragraph); });
  NativeStageContextStyles duplicates(p.xml, [] { return Text("Same"); });
  duplicates.graphic(paint);
  rejects([&] { duplicates.graphic(paint); });
  assert(p.xml.toByteArray() == xml);
}
} // namespace
int main(int argc, char **argv) {
  capture();
  portable_tabs();
  invalid_inputs();
  auto original = contexts(false);
  OdfPackage before(Kind::Stage), fallback_before(Kind::Stage);
  auto isolated = materialize(original, &before);
  auto fallback_original = fallback_source();
  auto fallback = materialize(fallback_original, &fallback_before);
  auto presentation_original = presentation_source();
  auto presentation = materialize(presentation_original);
  OdfPackage optional14_before(Kind::Stage);
  auto optional14 = materialize(original, &optional14_before, true);
  assert(shape(read_presentation_scenes(fallback_original))
             .paint.font.family.empty());
  assert(shape(read_presentation_scenes(fallback)).paint.font.family.empty());
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    auto stem = Text(argv[2]);
    original.save(stem + "/context-source.odp");
    isolated.save(stem + "/context-materialized.odp");
    before.save(stem + "/context-destination-before.odp");
    fallback_original.save(stem + "/context-fallback-source.odp");
    fallback_before.save(stem + "/context-fallback-before.odp");
    fallback.save(stem + "/context-fallback.odp");
    contexts().save(stem + "/nested-contexts.odp");
    presentation_original.save(stem + "/context-presentation-source.odp");
    presentation.save(stem + "/context-presentation-materialized.odp");
    selected(original, stem + "/selected-source.odp");
    selected(isolated, stem + "/selected-materialized.odp");
    selected(fallback_original, stem + "/selected-fallback-source.odp");
    selected(fallback, stem + "/selected-fallback.odp");
    selected(contexts(), stem + "/selected-nested-contexts.odp");
    selected(presentation_original, stem + "/selected-presentation-source.odp");
    selected(presentation, stem + "/selected-presentation-materialized.odp");
    optional14_before.save(stem + "/optional14-destination-before.odp");
    optional14.save(stem + "/optional14-materialized.odp");
    selected(optional14, stem + "/selected-optional14-materialized.odp");
  }
  std::cout << "PASS source paragraph contexts, repeated/nested cell cascades "
               "and native scoped style materialization\n";
}
