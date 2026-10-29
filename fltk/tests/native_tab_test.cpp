// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_tabs.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
void near(double a, double b) {
  if (std::abs(a - b) > 1e-6)
    std::cerr << a << " != " << b << '\n';
  assert(std::abs(a - b) < 1e-6);
}
TextMetrics measure(const Text &text, const TextFont &font) {
  return {text.size() * font.size / 2, font.size * .8, font.size * .2};
}
TextLine line(TextParagraph p, NativeTextShaper *shaper = nullptr,
              double width = 400) {
  TextFlow flow;
  flow.paragraphs = {std::move(p)};
  auto result = shaper ? layout_text_box(flow, width, *shaper)
                       : layout_text_box(flow, width, measure);
  assert(result.lines.size() == 1);
  return result.lines[0];
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
double fragment(const TextLine &line, const Text &text) {
  for (const auto &f : line.fragments)
    if (f.text == text)
      return line.x + f.x;
  throw std::runtime_error("Missing fragment");
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  if (argc == 3 && (Text::fromUtf8(argv[1]) == "--external" ||
                    Text::fromUtf8(argv[1]) == "--stage-external")) {
    Text path = Text::fromUtf8(argv[2]);
    NativeTextShaper shaper("DejaVu Sans Mono", 16);
    TextFlow flow;
    if (Text::fromUtf8(argv[1]) == "--external") {
      TextDocument document;
      document.load(path);
      flow = document.flow();
      auto before = document.text();
      document.set_text("Edited " + before);
      document.save(path + ".native.odt");
      TextDocument reopened;
      reopened.load(path + ".native.odt");
      assert(reopened.text() == "Edited " + before);
    } else {
      Presentation document;
      document.load(path);
      flow = document.scene(0).shapes[0].text_flow;
      auto before = document.slide(0).boxes[0].text;
      document.set_box_text(0, 0, "Edited " + before);
      document.save(path + ".native.odp");
      document.undo();
      assert(document.slide(0).boxes[0].text == before);
      document.redo();
      assert(document.slide(0).boxes[0].text == "Edited " + before);
    }
    assert(flow.paragraphs.size() == 14 && flow.unsupported.empty());
    assert(flow.paragraphs[0].tabs.size() == 1 &&
           flow.paragraphs[0].relative_tabs);
    assert(flow.paragraphs[10].tabs.empty());
    near(flow.paragraphs[10].default_tab, 64);
    assert(flow.paragraphs[8].tabs[0].leader_font.color->blue == 204);
    assert(flow.paragraphs[13].tabs[0].leader_font.color->red == 204);
    auto layout = layout_text_box(flow, 572, shaper);
    size_t tabs = 0, lines = 0, texts = 0;
    for (const auto &row : layout.lines)
      for (const auto &tab : row.tabs) {
        ++tabs;
        lines += !tab.strokes.empty();
        texts += bool(tab.leader);
      }
    assert(tabs == 14 && lines >= 8 && texts == 2);
    std::cout << "PASS independent ODF tab inheritance, defaults, scoped "
                 "leader styles, native layout and preserved edit/save/undo\n";
    return 0;
  }
  TextParagraph p;
  p.text = "A\tBC";
  p.font.size = 12;
  TextTabStop stop;
  stop.position = 100;
  p.tabs = {stop};
  auto l = line(p);
  assert(l.tabs.size() == 1);
  near(l.width, 112);
  near(fragment(l, "B"), 100);
  near(l.tabs[0].x1, 6);
  near(l.tabs[0].x2, 100);
  p.tabs[0].type = TextTabStop::Right;
  l = line(p);
  near(l.width, 100);
  near(fragment(l, "B"), 88);
  p.tabs[0].type = TextTabStop::Center;
  l = line(p);
  near(l.width, 106);
  near(fragment(l, "B"), 94);
  p.tabs[0].type = TextTabStop::Character;
  p.text = "A\t12.50";
  l = line(p);
  near(l.width, 115);
  near(fragment(l, ".") + 3, 100);
  p.text = "A\t123";
  l = line(p);
  near(l.width, 100);
  near(fragment(l, "1"), 82);
  p.tabs[0].type = TextTabStop::Left;
  p.text = "A\tBC";
  p.left = 20;
  p.indent = 10;
  l = line(p);
  near(fragment(l, "B"), 120);
  p.relative_tabs = false;
  l = line(p);
  near(fragment(l, "B"), 100);
  p.tabs.clear();
  p.default_tab = 40;
  p.relative_tabs = true;
  l = line(p);
  near(fragment(l, "B"), 60);
  p.indent = -30;
  l = line(p);
  near(fragment(l, "B"), 20);
  p.relative_tabs = false;
  l = line(p);
  near(fragment(l, "B"), 0);
  p.relative_tabs = true;
  p.indent = 10;
  p.text = "A\t\tB";
  l = line(p);
  assert(l.tabs.size() == 2);
  near(fragment(l, "B"), 100);
  p.left = p.indent = 0;
  p.text = "A\tBC";
  p.tabs = {stop};
  p.direction = TextParagraph::RTL;
  l = line(p);
  near(fragment(l, "C"), 400 - 100); // Physical left edge at stop.
  p.tabs[0].type = TextTabStop::Right;
  l = line(p);
  near(fragment(l, "B"), 400 - 100 - 6);
  std::cout << "PASS physical left/right/center/delimiter tabs, defaults, "
               "indentation, absolute origin and RTL\n";
  NativeTextShaper shaper("DejaVu Sans Mono", 16);
  p.direction = TextParagraph::LTR;
  p.text = "A\t12.50";
  p.tabs[0].type = TextTabStop::Character;
  l = line(p, &shaper);
  assert(l.shaped && l.tabs.size() == 1);
  double pen = 0;
  bool decimal = false;
  for (const auto &glyph : l.shaped->glyphs) {
    assert(glyph.begin != 1); // A tab is not four rendered spaces.
    if (glyph.begin == 4) {
      near(l.x + glyph.x + glyph.advance / 2, 100);
      decimal = true;
    }
    pen += glyph.advance;
  }
  assert(decimal && l.shaped->clusters.size() == p.text.size());
  p.text = "שלום\t12.50";
  p.direction = TextParagraph::RTL;
  l = line(p, &shaper);
  for (const auto &glyph : l.shaped->glyphs)
    if (glyph.begin == 7)
      near(l.x + glyph.x + glyph.advance / 2, 300);
  p.direction = TextParagraph::LTR;
  p.text = "A\t١٢٫٥";
  p.tabs[0].delimiter = U'٫';
  l = line(p, &shaper);
  for (const auto &glyph : l.shaped->glyphs)
    if (glyph.begin == 4)
      near(l.x + glyph.x + glyph.advance / 2, 100);
  p.tabs[0].delimiter = U'.';
  std::cout << "PASS shaped source coverage and visual decimal anchoring in "
               "mixed RTL text\n";
  p.direction = TextParagraph::LTR;
  p.text = "A\tBC";
  p.tabs[0] = stop;
  p.tabs[0].line_style = TextTabStop::Solid;
  for (auto style : {TextTabStop::Solid, TextTabStop::Dotted, TextTabStop::Dash,
                     TextTabStop::LongDash, TextTabStop::DotDash,
                     TextTabStop::DotDotDash, TextTabStop::Wave}) {
    p.tabs[0].line_style = style;
    p.tabs[0].double_line = true;
    p.tabs[0].width = 1.5;
    l = line(p, &shaper);
    assert(!l.tabs[0].strokes.empty());
    for (auto stroke : l.tabs[0].strokes) {
      assert(stroke.x1 >= l.tabs[0].x1 && stroke.x2 <= l.tabs[0].x2);
      near(stroke.width, 1.5);
    }
  }
  p.spans = {{0, 1, p.font, {}}, {1, 2, p.font, {}}, {2, 4, p.font, {}}};
  p.spans[1].font.size = 18;
  p.tabs[0].leader = "·";
  p.tabs[0].leader_font.scale = 2;
  p.tabs[0].leader_font.color_set = true;
  p.tabs[0].leader_font.color = TextColor{200, 20, 40};
  l = line(p, &shaper);
  assert(l.shaped->metrics.ascent >=
         shaper.shape(" ", p.spans[1].font).metrics.ascent);
  assert(l.tabs[0].strokes.empty() && l.tabs[0].leader &&
         !l.tabs[0].leader_positions.empty());
  for (const auto &glyph : l.tabs[0].leader->glyphs) {
    near(glyph.size, 36);
    assert(glyph.begin == 1 && glyph.end == 2 && glyph.color->red == 200);
  }
  std::cout << "PASS bounded GPU-ready line patterns, double/wave leaders and "
               "styled text leaders using the tab font\n";
  p.spans.clear();
  p.tabs[0] = stop;
  p.text = "A\tBC DE FG HI JK LM NO PQ RS TU VW XY Z";
  TextFlow flow;
  flow.paragraphs = {p};
  auto wrap = layout_text_box(flow, 130, shaper);
  assert(wrap.lines.size() > 1);
  size_t cursor = 0, tabs = 0;
  for (const auto &row : wrap.lines) {
    assert(row.begin == cursor && row.end > row.begin);
    cursor = row.end;
    tabs += row.tabs.size();
    assert(row.width <= 130 + 1e-6);
  }
  assert(cursor == p.text.size() && tabs == 1);
  p.text = "A\tB\nC\tD";
  flow.paragraphs = {p};
  wrap = layout_text_box(flow, 200, shaper);
  assert(wrap.lines.size() == 2 && wrap.lines[0].tabs.size() == 1 &&
         wrap.lines[1].tabs.size() == 1);
  for (bool rtl : {false, true})
    for (auto kind : {TextTabStop::Left, TextTabStop::Right,
                      TextTabStop::Center, TextTabStop::Character}) {
      p.text =
          "A B\t1 2.50 three four five six seven eight nine ten eleven twelve";
      p.direction = rtl ? TextParagraph::RTL : TextParagraph::LTR;
      p.alignment = TextParagraph::Justify;
      p.tabs = {stop};
      p.tabs[0].position = 80;
      p.tabs[0].type = kind;
      flow.paragraphs = {p};
      auto rows = layout_text_box(flow, 140, shaper);
      assert(rows.lines.size() > 1 && rows.lines[0].tabs.size() == 1);
      const auto &row = rows.lines[0];
      double left = 1e9, right = -1e9;
      bool has_decimal = false;
      size_t dot = p.text.indexOf('.');
      for (const auto &glyph : row.shaped->glyphs) {
        if (glyph.begin <= 3)
          continue;
        left = std::min(left, row.x + glyph.x);
        right = std::max(right, row.x + glyph.x + glyph.advance);
        if (glyph.begin == dot) {
          has_decimal = true;
          if (row.tabs[0].aligned && kind == TextTabStop::Character)
            near(row.x + glyph.x + glyph.advance / 2, rtl ? 60 : 80);
        }
      }
      assert(has_decimal);
      if (!row.tabs[0].aligned) {
        near(row.tabs[0].x2 - row.tabs[0].x1, 0);
        continue;
      }
      if (kind == TextTabStop::Center)
        near((left + right) / 2, rtl ? 60 : 80);
      if (kind == TextTabStop::Right && !rtl)
        near(right, 80);
      if (kind == TextTabStop::Left && rtl)
        near(left, 60);
    }
  p.alignment = TextParagraph::Start;
  p.direction = TextParagraph::LTR;
  p.tabs.clear();
  p.text.clear();
  for (int i = 0; i < 2000; ++i)
    p.text += "a\tb ";
  flow.paragraphs = {p};
  size_t measured = 0;
  auto many =
      layout_text_box(flow, 180, [&](const Text &value, const TextFont &font) {
        ++measured;
        return measure(value, font);
      });
  assert(many.lines.size() > 100 && measured <= p.text.size() * 2);
  size_t controls = 0;
  for (const auto &row : many.lines)
    controls += row.tabs.size();
  assert(controls == 2000);
  std::cout << "PASS justification preserves tab anchors and large tabbed "
               "paragraphs use bounded measurement work\n";
  p.tabs = {stop};
  p.default_tab = 0;
  rejects([&] { line(p); });
  p.default_tab = 40;
  p.tabs[0].position = std::numeric_limits<double>::infinity();
  rejects([&] { line(p); });
  p.tabs = {stop, stop};
  rejects([&] { line(p); });
  std::cout << "PASS wrapping/hard breaks without repeated controls, source "
               "ranges and invalid tab rejection\n";
}
