// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_image.h"
#include "native_text_shaper.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
           FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Bytes IMAGE =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>)";
void near(double a, double b) { assert(std::abs(a - b) < 1e-6); }
TextMetrics measure(const Text &text, const TextFont &font) {
  return {text.size() * font.size / 2, font.size * .8, font.size * .2};
}
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  auto styles = read_document_xml(p.entry("styles.xml"));
  auto holder = child(styles.documentElement(), OFFICE, "styles");
  auto doc = read_document_xml(
      R"(<root xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0">
 <text:list-style style:name="Picture"><text:list-level-style-image text:level="1" xlink:href="./Pictures/red%20bullet.svg"><style:list-level-properties fo:width="20pt" fo:height="10pt" text:space-before="4pt" text:min-label-width="24pt" text:min-label-distance="3pt" style:vertical-pos="middle" style:vertical-rel="text"/></text:list-level-style-image></text:list-style>
 <text:list-style style:name="Inline"><text:list-level-style-image text:level="1"><office:binary-data> U1ZH
</office:binary-data></text:list-level-style-image></text:list-style></root>)");
  for (auto e : children(doc.documentElement(), TEXT))
    holder.appendChild(styles.importNode(e, true));
  p.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  auto body = read_document_xml(
      R"(<root xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"><text:list text:style-name="Picture"><text:list-header><text:p>Header</text:p></text:list-header><text:list-item><text:p>First item with enough words for wrapping to a second and third line</text:p><text:p>Continuation paragraph</text:p></text:list-item><text:list-item><text:p>Second item</text:p></text:list-item></text:list><text:list text:style-name="Inline"><text:list-item><text:p>Inline item</text:p></text:list-item></text:list></root>)");
  for (auto e : children(body.documentElement(), TEXT))
    p.body().appendChild(p.xml.importNode(e, true));
  p.set_entry("Pictures/red bullet.svg", IMAGE, "image/svg+xml");
  return p;
}
} // namespace
int main() {
  std::cout << std::unitbuf;
  assert(package_image_path("./Pictures/a%20b/../red.svg") ==
         Text("Pictures/red.svg"));
  for (const auto &path : {"", "../escape", "/absolute", "file:///tmp/image",
                           "https://example.com/x", "Pictures/a?x",
                           "a#fragment", "a\\b", "%00", "%2foutside", "bad%xx"})
    assert(!package_image_path(path));
  assert(inline_image_data(" U1 ZH\n\t") == Bytes("SVG"));
  assert(inline_image_data("YQ==") == Bytes("a"));
  assert(inline_image_data("YWI=") == Bytes("ab"));
  for (const auto &data :
       {"", "YQ=", "=YQ=", "Y===", "YQ==AAAA", "YQ$=", "éAA="})
    assert(!inline_image_data(data));
  std::cout << "PASS package-only image paths and bounded folded base64\n";
  auto p = fixture();
  auto first = p.image_asset("Pictures/red%20bullet.svg");
  assert(first && *first->data == IMAGE);
  assert(p.image_asset("./Pictures/red%20bullet.svg") == first);
  auto copy = p;
  assert(copy.entry_data("Pictures/red bullet.svg") ==
         p.entry_data("Pictures/red bullet.svg"));
  p.set_entry("Pictures/red bullet.svg", "replacement", "image/svg+xml");
  auto changed = p.image_asset("Pictures/red%20bullet.svg");
  assert(changed && changed->id != first->id &&
         *changed->data == "replacement" && *first->data == IMAGE);
  assert(copy.image_asset("Pictures/red%20bullet.svg") == first);
  std::shared_ptr<const ImageAsset> retained;
  {
    auto ephemeral = fixture();
    retained = ephemeral.image_asset("Pictures/red%20bullet.svg");
  }
  assert(retained && *retained->data == IMAGE);
  std::cout << "PASS immutable shared archive bytes, cache identity, "
               "replacement and retained lifetime\n";
  assert(!make_image_asset("empty", Bytes()));
  assert(!make_image_asset("oversize", Bytes(16 * 1024 * 1024 + 1, 'x')));
  std::vector<SlideScene> scenes;
  {
    OdfPackage stage(Kind::Stage);
    stage.set_entry("Pictures/red bullet.svg", IMAGE, "image/svg+xml");
    auto frame = read_document_xml(
        R"(<draw:page xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" draw:name="Retained"><draw:frame svg:x="10pt" svg:y="10pt" svg:width="50pt" svg:height="30pt"><draw:image xlink:href="./Pictures/red%20bullet.svg"/></draw:frame></draw:page>)");
    stage.body().appendChild(
        stage.xml.importNode(frame.documentElement(), true));
    scenes = read_presentation_scenes(stage);
    assert(scenes.size() == 1 && scenes[0].shapes.size() == 1 &&
           scenes[0].shapes[0].raster);
  }
  assert(*scenes[0].shapes[0].raster->data == IMAGE);
  std::cout << "PASS native scene image ownership beyond its archive lifetime "
               "and encoded size bound\n";
  auto flow = read_text_flow(copy);
  assert(flow.paragraphs.size() == 5 && flow.unsupported.empty());
  assert(!flow.paragraphs[0].list->visible &&
         !flow.paragraphs[2].list->visible);
  auto &marker = *flow.paragraphs[1].list;
  assert(marker.text.empty() && marker.image && marker.image->asset == first);
  assert(flow.paragraphs[3].list->image->asset == first);
  assert(*flow.paragraphs[4].list->image->asset->data == "SVG");
  near(marker.image->width, 20);
  near(marker.image->height, 10);
  auto layout = layout_text_box(flow, 150, measure);
  int labels = 0;
  for (auto &line : layout.lines)
    if (line.label) {
      ++labels;
      assert(line.label->image && !line.label->shaped &&
             line.label->text.empty());
    }
  assert(labels == 3 && layout.lines.size() > 5);
  auto para = flow.paragraphs[1];
  para.text = "body";
  para.spans.clear();
  para.font.size = 10;
  para.line_factor = 1;
  TextFlow single;
  single.paragraphs = {para};
  auto get = [&] {
    auto box = layout_text_box(single, 150, measure);
    assert(box.lines.size() == 1 && box.lines[0].label);
    return box.lines[0];
  };
  auto &image = *single.paragraphs[0].list->image;
  image.height = 20;
  image.reference = ListImage::TextBox;
  image.position = ListImage::Top;
  auto line = get();
  near(line.label->y, -8);
  near(line.height, 20);
  near(line.label->width, 20);
  image.reference = ListImage::Baseline;
  image.position = ListImage::Bottom;
  line = get();
  near(line.label->y, -20);
  near(line.height, 22);
  image.reference = ListImage::Line;
  image.position = ListImage::Top;
  line = get();
  near(line.baseline + line.label->y, 0);
  image.position = ListImage::Bottom;
  line = get();
  near(line.baseline + line.label->y + 20, line.height);
  image.position = ListImage::Middle;
  line = get();
  near(line.baseline + line.label->y, (line.height - 20) / 2);
  image.position = ListImage::FromTop;
  image.offset = 3;
  line = get();
  near(line.baseline + line.label->y, 3);
  single.paragraphs[0].direction = TextParagraph::RTL;
  line = get();
  assert(line.label->x > line.x);
  single.paragraphs[0].direction = TextParagraph::LTR;
  auto &label = *single.paragraphs[0].list;
  label.alignment_mode = true;
  label.indent = -25;
  single.paragraphs[0].left = 30;
  line = get();
  near(line.label->width, 20);
  assert(line.label->x < line.x);
  image.width = 0;
  bool rejected = false;
  try {
    get();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "PASS image label wrapping, shared assets, both spacing modes, "
               "RTL and vertical metrics\n";
  TempDir temp;
  auto path = temp.path() + "/image.odt";
  copy.save(path);
  TextDocument document;
  document.load(path);
  auto text = document.text();
  document.set_text("Edited " + text);
  document.save(path);
  OdfPackage reopened(Kind::Words);
  reopened.load(path);
  assert(reopened.entry("styles.xml") == copy.entry("styles.xml") &&
         reopened.entry("Pictures/red bullet.svg") == IMAGE);
  auto after = read_text_flow(reopened);
  assert(after.paragraphs[1].list->image->asset &&
         after.paragraphs[4].list->image->asset);
  assert(reopened.image_asset("Pictures/red%20bullet.svg")->id != first->id);
  auto styles = read_document_xml(reopened.entry("styles.xml"));
  auto level = styles.elementsByTagNameNS(TEXT, "list-level-style-image")[0];
  level.setAttributeNS(XLINK, "xlink:href", "file:///tmp/does-not-exist");
  reopened.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  auto missing = read_text_flow(reopened);
  assert(!missing.unsupported.empty() &&
         !missing.paragraphs[1].list->image->asset);
  NativeTextShaper shaper("Noto Sans", 12);
  auto shaped = layout_text_box(after, 150, shaper);
  assert(!shaped.lines.empty() && shaped.lines[1].shaped);
  std::cout << "PASS image/inline/style preservation through native text edit, "
               "save/reload and missing-source diagnostics\n";
}
