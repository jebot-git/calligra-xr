// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_enhanced_geometry.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
void point(ScenePoint p, double x, double y) {
  near(p.x, x);
  near(p.y, y);
}
XmlElement element(XmlElement parent, const Text &ns, const Text &name) {
  auto result = parent.ownerDocument().createElementNS(ns, name);
  parent.appendChild(result);
  return result;
}
XmlElement geometry(OdfPackage &p, const Text &modifiers = "20 30") {
  auto g = element(p.body(), DRAW, "draw:enhanced-geometry");
  g.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
  g.setAttributeNS(DRAW, "draw:enhanced-path", "M0 0 L100 0 100 100 0 100 Z N");
  g.setAttributeNS(DRAW, "draw:modifiers", modifiers);
  return g;
}
XmlElement xy(XmlElement g, const Text &x = "$0", const Text &y = "$1") {
  auto h = element(g, DRAW, "draw:handle");
  h.setAttributeNS(DRAW, "draw:handle-position-x", x);
  h.setAttributeNS(DRAW, "draw:handle-position-y", y);
  return h;
}
void equation(XmlElement g, const Text &name, const Text &formula) {
  auto e = element(g, DRAW, "draw:equation");
  e.setAttributeNS(DRAW, "draw:name", name);
  e.setAttributeNS(DRAW, "draw:formula", formula);
}
void set(XmlElement n, const char *name, const Text &value) {
  n.setAttributeNS(DRAW, Text("draw:") + name, value);
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml.documentElement().setAttributeNS(OFFICE, "office:version", "1.4");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"><office:styles><style:style style:name="Blue" style:family="graphic"><style:graphic-properties draw:fill="solid" draw:fill-color="#4488ee" draw:stroke="solid" svg:stroke-color="#24466e" svg:stroke-width="1pt"/></style:style></office:styles><office:automatic-styles><style:page-layout style:name="Wide"><style:page-layout-properties fo:page-width="400pt" fo:page-height="300pt"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="Wide"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  auto page = element(p.body(), DRAW, "draw:page");
  set(page, "name", "Native adjustment handles");
  set(page, "master-page-name", "Default");
  auto add = [&](XmlElement parent, const Text &name, const Text &x,
                 const Text &y, const Text &w, const Text &height) {
    auto shape = element(parent, DRAW, "draw:custom-shape");
    set(shape, "name", name);
    set(shape, "style-name", "Blue");
    for (auto [key, value] : std::vector<std::pair<Text, Text>>{
             {"x", x}, {"y", y}, {"width", w}, {"height", height}})
      shape.setAttributeNS(SVG, "svg:" + key, value);
    auto g = element(shape, DRAW, "draw:enhanced-geometry");
    g.setAttributeNS(SVG, "svg:viewBox", "0 0 100 100");
    return std::pair{shape, g};
  };
  auto [arrow, g] = add(page, "XY arrow", "20pt", "30pt", "180pt", "90pt");
  set(g, "modifiers", "60 20");
  set(g, "enhanced-path",
      "M0 $1 L$0 $1 $0 0 100 50 $0 100 $0 ?bottom 0 ?bottom Z N");
  equation(g, "bottom", "100-$1");
  auto h = xy(g);
  set(h, "handle-range-x-minimum", "20");
  set(h, "handle-range-x-maximum", "80");
  set(h, "handle-range-y-minimum", "5");
  set(h, "handle-range-y-maximum", "45");
  set_plain_text(element(arrow, TEXT, "text:p"), "Adjustable text");
  auto [polar, pg] =
      add(page, "Mirrored polar", "250pt", "30pt", "100pt", "100pt");
  set(pg, "modifiers", "0 30");
  set(pg, "mirror-horizontal", "true");
  set(pg, "enhanced-path", "M50 50 L?px ?py 50 90 Z N");
  equation(pg, "px", "50+$1*cos($0*pi/180)");
  equation(pg, "py", "50+$1*sin($0*pi/180)");
  h = element(pg, DRAW, "draw:handle");
  set(h, "handle-polar-pole-x", "50");
  set(h, "handle-polar-pole-y", "50");
  set(h, "handle-polar-angle", "$0");
  set(h, "handle-polar-radius", "$1");
  set(h, "handle-radius-range-minimum", "10");
  set(h, "handle-radius-range-maximum", "40");
  auto group = element(page, DRAW, "draw:g");
  set(group, "transform", "rotate(0.3) translate(30pt 180pt)");
  auto [legacy, lg] =
      add(group, "Grouped legacy", "0pt", "0pt", "100pt", "80pt");
  set(lg, "modifiers", "40 50");
  set(lg, "enhanced-path", "M0 0 L$0 0 100 100 0 100 Z N");
  h = element(lg, DRAW, "draw:handle");
  set(h, "handle-position", "$0 50");
  set(h, "handle-range-x-minimum", "10");
  set(h, "handle-range-x-maximum", "90");
  p.set_entry("unknown/retained.bin", "opaque retained bytes",
              "application/octet-stream");
  return p;
}
Bytes content(const Presentation &p) {
  return p.scene(0).shapes[0].source.ownerDocument().toByteArray();
}
ScenePoint handle(const Presentation &p, int shape = 0) {
  const auto &s = p.scene(0).shapes[shape];
  return s.transform.map(s.handles[0].position);
}
void selection(std::optional<PresentationPosition> p, int shape, int box) {
  assert(p && p->slide == 0 && p->shape == shape && p->box == box);
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc >= 3 && Text::fromUtf8(argv[1]) == "--corpus") {
    size_t handles = 0, editable = 0, changes = 0;
    for (int i = 2; i < argc; ++i) {
      auto members = read_document_archive(Text::fromUtf8(argv[i]));
      OdfPackage package(Kind::Stage);
      package.set_entry("meta.xml", members.value("meta.xml"), "text/xml");
      bool radius_first = enhanced_legacy_polar_order(package);
      auto document = read_document_xml(members.value("content.xml"));
      for (auto geometry :
           document.elementsByTagNameNS(DRAW, "enhanced-geometry")) {
        ScenePaint paint;
        paint.fill = paint.stroke = true;
        auto parsed =
            read_enhanced_geometry(geometry, 100, 100, paint, radius_first);
        handles += parsed.handles.size();
        for (size_t j = 0; j < parsed.handles.size(); ++j)
          if (parsed.handles[j].editable) {
            ++editable;
            for (double offset : {-2., 2.}) {
              auto target = parsed.handles[j].position;
              target.x += offset;
              target.y += offset;
              auto values = adjusted_enhanced_modifiers(
                  geometry, 100, 100, paint, j, target, radius_first);
              TextList parts;
              for (double value : values)
                parts << Text::number(value, 'g', 17);
              auto candidate = geometry.cloneNode(true);
              set(candidate, "modifiers", parts.join(' '));
              auto adjusted = read_enhanced_geometry(candidate, 100, 100, paint,
                                                     radius_first);
              path_bounds(*adjusted.path, adjusted.mapping);
              auto before = parsed.handles[j].position,
                   after = adjusted.handles[j].position;
              if (std::hypot(before.x - after.x, before.y - after.y) > 1e-8)
                ++changes;
            }
          }
      }
    }
    assert(handles && editable && changes);
    std::cout << "PASS " << handles << " upstream handles, " << editable
              << " editable handles, " << changes
              << " bounded geometry changes\n";
    return 0;
  }
  TempDir temp;
  auto source = temp.path() + "/handles.odp",
       saved = temp.path() + "/saved.odp";
  auto package = fixture();
  package.save(source);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--invalid-fixture") {
    auto g = package.xml.elementsByTagNameNS(DRAW, "enhanced-geometry")[0];
    set(g, "enhanced-path", "M0 0 L?bad 0 0 100 Z N");
    equation(g, "bad", "1/(80-$0)");
    package.save(Text::fromUtf8(argv[2]));
    return 0;
  }
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture") {
    assert(File::copy(source, Text::fromUtf8(argv[2])));
    return 0;
  }
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation p;
    p.load(Text::fromUtf8(argv[2]));
    const auto before = content(p);
    const auto input = read_document_archive(Text::fromUtf8(argv[2]));
    assert(p.scene(0).shapes[0].handles.size() == 1);
    p.move_handle(0, 0, 0, {90, 45});
    assert(content(p) != before);
    selection(p.undo(), 0, p.scene(0).shapes[0].editor_index);
    assert(content(p) == before);
    p.save(saved);
    auto output = read_document_archive(saved);
    for (auto it = input.cbegin(); it != input.cend(); ++it)
      if (it.key() != "content.xml")
        assert(output.value(it.key()) == it.value());
    assert(output.value("content.xml") == before);
    std::cout << "PASS independent ODF adjustment, exact XML undo and retained "
                 "archive\n";
    return 0;
  }
  OdfPackage p(Kind::Stage);
  auto g = geometry(p);
  auto h = xy(g);
  ScenePaint paint;
  auto read = [&](double w = 200, double ht = 100) {
    return read_enhanced_geometry(g, w, ht, paint);
  };
  auto change = [&](ScenePoint point, double w = 200, double ht = 100) {
    return adjusted_enhanced_modifiers(g, w, ht, paint, 0, point);
  };
  point(read().handles[0].position, 40, 30);
  assert(read().handles[0].editable);
  set(h, "handle-range-x-minimum", "10");
  set(h, "handle-range-x-maximum", "40");
  set(h, "handle-range-y-maximum", "50");
  auto values = change({200, 80});
  near(values[0], 40);
  near(values[1], 50);
  values = change({-100, -10});
  near(values[0], 10);
  near(values[1], -10);
  set(h, "handle-mirror-horizontal", "true");
  point(read().handles[0].position, 40, 70);
  set(h, "handle-mirror-vertical", "true");
  point(read().handles[0].position, 160, 70);
  values = change({180, 80});
  near(values[0], 10);
  near(values[1], 20);
  set(g, "mirror-horizontal", "true");
  point(read().handles[0].position, 40, 70);
  set(h, "handle-mirror-horizontal", "false");
  set(h, "handle-mirror-vertical", "false");
  set(g, "mirror-horizontal", "false");
  set(h, "handle-switched", "true");
  point(read(100, 200).handles[0].position, 30, 40);
  values = change({60, 100}, 100, 200);
  near(values[0], 40);
  near(values[1], 50);
  set(h, "handle-position-x", "?fixed");
  equation(g, "fixed", "15");
  values = change({70, 80});
  near(values[0], 20);
  near(values[1], 50);
  set(h, "handle-position-y", "20");
  assert(!read().handles[0].editable);
  rejects([&] { change({10, 10}); });
  set(h, "handle-position-x", "$0");
  set(h, "handle-position-y", "$0");
  assert(!read().handles[0].editable && !read().unsupported.empty());
  std::cout << "PASS modern XY, one-sided ranges, fixed coordinates, "
               "handle/geometry mirrors and direction switching\n";

  auto pg = geometry(p, "90 20");
  auto ph = element(pg, DRAW, "draw:handle");
  set(ph, "handle-polar-pole-x", "50");
  set(ph, "handle-polar-pole-y", "50");
  set(ph, "handle-polar-angle", "$0");
  set(ph, "handle-polar-radius", "$1");
  set(ph, "handle-radius-range-minimum", "10");
  set(ph, "handle-radius-range-maximum", "30");
  point(read_enhanced_geometry(pg, 100, 100, paint).handles[0].position, 50,
        70);
  values = adjusted_enhanced_modifiers(pg, 100, 100, paint, 0, {0, 50});
  near(values[0], 180);
  near(values[1], 30);
  values = adjusted_enhanced_modifiers(pg, 100, 100, paint, 0, {50, 50});
  near(values[0], 90);
  near(values[1], 10);
  auto legacy = geometry(p, "270 20");
  auto lh = element(legacy, DRAW, "draw:handle");
  set(lh, "handle-position", "$0 $1");
  set(lh, "handle-polar", "50 50");
  point(read_enhanced_geometry(legacy, 100, 100, paint).handles[0].position, 50,
        30);
  values = adjusted_enhanced_modifiers(legacy, 100, 100, paint, 0, {50, 75});
  near(values[0], 90);
  near(values[1], 25);
  auto defaults = geometry(p);
  auto dh = element(defaults, DRAW, "draw:handle");
  set(dh, "handle-polar-pole-x", "20");
  set(dh, "handle-polar-pole-y", "30");
  point(read_enhanced_geometry(defaults, 100, 100, paint).handles[0].position,
        20, 30);
  auto offset = geometry(p);
  offset.setAttributeNS(SVG, "svg:viewBox", "10 20 100 200");
  auto oh = xy(offset, "30", "60");
  set(oh, "handle-mirror-horizontal", "true");
  point(read_enhanced_geometry(offset, 100, 100, paint).handles[0].position, 20,
        80);
  std::cout << "PASS modern/legacy clockwise polar coordinates, defaults, "
               "radius limits and offset view boxes\n";
  auto office_legacy = geometry(p, "20 90");
  auto office_handle = element(office_legacy, DRAW, "draw:handle");
  set(office_handle, "handle-position", "$0 $1");
  set(office_handle, "handle-polar", "50 50");
  point(read_enhanced_geometry(office_legacy, 100, 100, paint, true)
            .handles[0]
            .position,
        50, 70);
  values = adjusted_enhanced_modifiers(office_legacy, 100, 100, paint, 0,
                                       {80, 50}, true);
  near(values[0], 30);
  near(values[1], 0);
  auto malformed = geometry(p);
  auto mh = xy(malformed);
  mh.removeAttributeNS(DRAW, "handle-position-y");
  rejects([&] { read_enhanced_geometry(malformed, 100, 100, paint); });
  set(mh, "handle-position-y", "$1");
  set(mh, "handle-polar-pole-x", "50");
  rejects([&] { read_enhanced_geometry(malformed, 100, 100, paint); });
  assert(!read_enhanced_geometry(office_legacy, 0, 100, paint, true)
              .handles[0]
              .editable);
  rejects([&] {
    adjusted_enhanced_modifiers(office_legacy, 0, 100, paint, 0, {0, 50}, true);
  });

  auto coupled = geometry(p, "30 30");
  auto ch = xy(coupled);
  set(ch, "handle-range-x-maximum", "$1");
  set(ch, "handle-range-y-maximum", "$0");
  values = adjusted_enhanced_modifiers(coupled, 100, 100, paint, 0, {80, 20});
  near(values[0], 20);
  near(values[1], 20);
  equation(coupled, "next", "$0+1");
  set(ch, "handle-range-x-minimum", "?next");
  ch.removeAttributeNS(DRAW, "handle-range-x-maximum");
  rejects([&] {
    adjusted_enhanced_modifiers(coupled, 100, 100, paint, 0, {0, 0});
  });
  for (auto bad : {"$9999", "$-1", "?missing", "0+1", "NaN", "1e999", ""}) {
    set(dh, "handle-polar-angle", bad);
    rejects([&] { read_enhanced_geometry(defaults, 100, 100, paint); });
  }
  set(dh, "handle-polar-angle", "0");
  set(dh, "handle-polar-radius", "-1");
  rejects([&] { read_enhanced_geometry(defaults, 100, 100, paint); });
  set(ph, "handle-radius-range-maximum", "-1");
  rejects([&] { read_enhanced_geometry(pg, 100, 100, paint); });
  set(h, "handle-position-y", "$1");
  set(h, "handle-range-x-minimum", "80");
  rejects([&] { read(); });
  rejects([&] { change({std::numeric_limits<double>::infinity(), 0}); });
  rejects(
      [&] { adjusted_enhanced_modifiers(legacy, 100, 100, paint, 9, {0, 0}); });
  set(lh, "handle-position", "$0 $1 extra");
  rejects([&] { read_enhanced_geometry(legacy, 100, 100, paint); });
  std::cout << "PASS coupled constraints, nonconvergence, invalid parameters, "
               "domains and bounds\n";

  Presentation doc;
  doc.load(source);
  const auto initial = content(doc);
  point(handle(doc), 128, 48);
  doc.begin_handle_drag(0, 0, 0);
  doc.move_handle(0, 0, 0, {140, 55});
  doc.move_handle(0, 0, 0, {160, 60});
  assert(doc.dragging_handle() && doc.modified());
  doc.finish_handle_drag();
  const auto edited = content(doc);
  selection(doc.undo(), 0, 0);
  assert(content(doc) == initial && !doc.modified());
  selection(doc.redo(), 0, 0);
  assert(content(doc) == edited);
  doc.save(saved);
  assert(!doc.modified());
  selection(doc.undo(), 0, 0);
  assert(doc.can_redo());
  doc.begin_handle_drag(0, 1, 0);
  doc.move_handle(0, 1, 0, {300, 120});
  doc.finish_handle_drag(true);
  assert(content(doc) == initial && doc.can_redo());
  selection(doc.redo(), 0, 0);
  assert(content(doc) == edited && !doc.modified());
  doc.begin_handle_drag(0, 1, 0);
  doc.finish_handle_drag();
  assert(!doc.can_redo() && !doc.modified());
  doc.begin_handle_drag(0, 1, 0);
  doc.move_handle(0, 1, 0, {300, 120});
  doc.finish_handle_drag();
  const auto polar_edit = content(doc);
  selection(doc.undo(), 1, -1);
  assert(content(doc) == edited);
  selection(doc.redo(), 1, -1);
  assert(content(doc) == polar_edit);
  const auto &grouped = doc.scene(0).shapes[2];
  auto target = grouped.transform.map({70, 40});
  doc.move_handle(0, 2, 0, target);
  point(handle(doc, 2), target.x, target.y);
  selection(doc.undo(), 2, -1);
  assert(content(doc) == polar_edit);
  doc.save(saved);
  auto saved_entries = read_document_archive(saved);
  assert(saved_entries.value("unknown/retained.bin") ==
         "opaque retained bytes");
  Presentation reopened;
  reopened.load(saved);
  assert(content(reopened) == polar_edit);
  std::cout << "PASS grouped/nontext shape edits, one-command drag undo, "
               "cancellation/redo, selection and save/reload\n";

  auto invalid = fixture();
  auto ig =
      child(children(invalid.body(), DRAW, "page")[0], DRAW, "custom-shape");
  ig = child(ig, DRAW, "enhanced-geometry");
  set(ig, "enhanced-path", "M0 0 L?bad 0 0 100 Z N");
  equation(ig, "bad", "1/(80-$0)");
  invalid.save(source);
  doc.load(source);
  const auto unchanged = content(doc);
  const auto revision = doc.revision();
  rejects([&] {
    doc.move_handle(0, 0, 0, {164, 48});
  }); // x=80 -> division by zero.
  assert(content(doc) == unchanged && doc.revision() == revision &&
         !doc.modified() && !doc.can_undo());
  doc.begin_handle_drag(0, 0, 0);
  rejects([&] { doc.move_handle(0, 1, 0, {300, 80}); });
  doc.finish_handle_drag(true);
  invalid.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                    "text/xml");
  invalid.save(source);
  doc.load(source);
  rejects([&] { doc.begin_handle_drag(0, 0, 0); });
  rejects([&] { doc.move_handle(0, 0, 0, {130, 50}); });
  assert(!doc.modified());
  auto office = fixture();
  auto legacy_geometry =
      office.xml.elementsByTagNameNS(DRAW, "enhanced-geometry")[2];
  set(legacy_geometry, "modifiers", "20 90");
  auto legacy_handle = child(legacy_geometry, DRAW, "handle");
  set(legacy_handle, "handle-position", "$0 $1");
  set(legacy_handle, "handle-polar", "50 50");
  legacy_handle.removeAttributeNS(DRAW, "handle-range-x-minimum");
  legacy_handle.removeAttributeNS(DRAW, "handle-range-x-maximum");
  office.set_entry(
      "meta.xml",
      R"XML(<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:meta="urn:oasis:names:tc:opendocument:xmlns:meta:1.0"><office:meta><meta:generator>LibreOffice/26.8</meta:generator></office:meta></office:document-meta>)XML",
      "text/xml");
  office.save(source);
  doc.load(source);
  const auto &legacy_shape = doc.scene(0).shapes[2];
  auto expected = legacy_shape.transform.map({50, 56});
  point(handle(doc, 2), expected.x, expected.y);
  expected = legacy_shape.transform.map({80, 40});
  doc.move_handle(0, 2, 0, expected);
  point(handle(doc, 2), expected.x, expected.y);
  selection(doc.undo(), 2, -1);
  doc.save(saved);
  assert(read_document_archive(saved).value("meta.xml") ==
         office.entry("meta.xml"));
  std::cout
      << "PASS failed-geometry transaction rollback, gesture target guard, "
         "signed protection and producer-specific legacy polar order\n";
}
