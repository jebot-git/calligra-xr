// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_page_stories.h"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
XmlElement add(XmlElement parent, const Text &uri, const Text &name) {
  auto node = parent.ownerDocument().createElementNS(uri, name);
  parent.appendChild(node);
  return node;
}
OdfPackage specimen(Kind kind) {
  OdfPackage p(kind);
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
<office:font-face-decls><style:font-face style:name="Face" svg:font-family="fixed"/></office:font-face-decls>
<office:styles>
 <style:default-style style:family="text"><style:text-properties style:font-name="Face" fo:font-size="12pt" fo:color="#123456"/></style:default-style>
 <style:default-style style:family="paragraph"><style:paragraph-properties fo:margin-left="7pt" style:tab-stop-distance="24pt"/></style:default-style>
 <style:default-style style:family="graphic"><style:graphic-properties draw:fill="none" draw:stroke="none" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"/></style:default-style>
 <style:style style:name="Body" style:family="paragraph"><style:text-properties style:font-name="Face"/></style:style>
 <style:style style:name="Graphic" style:family="graphic"><style:text-properties style:font-name="Face" fo:font-size="14pt"/></style:style>
</office:styles>
<office:automatic-styles><style:page-layout style:name="OuterLayout"><style:page-layout-properties fo:page-width="400pt" fo:page-height="300pt" fo:margin="20pt"/><style:header-style><style:header-footer-properties fo:min-height="12pt" fo:margin-bottom="3pt"/></style:header-style></style:page-layout></office:automatic-styles>
<office:master-styles><style:master-page style:name="OuterPage" style:page-layout-name="OuterLayout"><style:header><text:p text:style-name="Body">Header</text:p></style:header></style:master-page></office:master-styles>
</office:document-styles>)XML",
      "text/xml");
  auto root = p.xml.documentElement();
  auto faces = p.xml.createElementNS(OFFICE, "office:font-face-decls");
  root.insertBefore(faces, root.firstChild());
  auto face = add(faces, STYLE, "style:font-face");
  face.setAttributeNS(STYLE, "style:name", "LocalFace");
  face.setAttributeNS(SVG, "svg:font-family", "monospace");
  auto automatic = child(root, OFFICE, "automatic-styles");
  assert(!automatic.isNull());
  auto local = add(automatic, STYLE, "style:style");
  local.setAttributeNS(STYLE, "style:name", "LocalBody");
  local.setAttributeNS(STYLE, "style:family", "paragraph");
  auto props = add(local, STYLE, "style:text-properties");
  props.setAttributeNS(STYLE, "style:font-name", "LocalFace");
  if (kind == Kind::Stage) {
    auto graphic = add(automatic, STYLE, "style:style");
    graphic.setAttributeNS(STYLE, "style:name", "LocalGraphic");
    graphic.setAttributeNS(STYLE, "style:family", "graphic");
    props = add(graphic, STYLE, "style:text-properties");
    props.setAttributeNS(STYLE, "style:font-name", "LocalFace");
    props.setAttributeNS(FO, "fo:font-size", "16pt");
  }
  XmlElement parent = p.body();
  if (kind == Kind::Stage) {
    parent = add(parent, DRAW, "draw:page");
    parent.setAttributeNS(DRAW, "draw:name", "OuterSlide");
    parent.setAttributeNS(DRAW, "draw:master-page-name", "OuterPage");
  }
  for (int i = 0; i < 2; ++i) {
    auto container = parent;
    if (kind == Kind::Stage) {
      container = add(parent, DRAW, "draw:rect");
      container.setAttributeNS(DRAW, "draw:name", i ? "Local" : "Common");
      container.setAttributeNS(DRAW, "draw:style-name",
                               i ? "LocalGraphic" : "Graphic");
      container.setAttributeNS(SVG, "svg:x", i ? "200pt" : "20pt");
      container.setAttributeNS(SVG, "svg:y", "30pt");
      container.setAttributeNS(SVG, "svg:width", "160pt");
      container.setAttributeNS(SVG, "svg:height", "80pt");
    }
    auto paragraph = add(container, TEXT, "text:p");
    paragraph.setAttributeNS(TEXT, "text:style-name", i ? "LocalBody" : "Body");
    set_plain_text(paragraph, i ? "Local text" : "Outer text");
  }
  return p;
}
OdfPackage embedded(const OdfPackage &original, bool in_styles,
                    bool many_labels = false) {
  OdfPackage p(original.kind());
  p.load_bytes(original.archive_bytes());
  XmlDocument doc =
      in_styles ? read_document_xml(p.entry("styles.xml")) : p.xml;
  XmlElement parent;
  if (in_styles)
    parent =
        child(child(child(child(doc.documentElement(), OFFICE, "master-styles"),
                          STYLE, "master-page"),
                    STYLE, "header"),
              TEXT, "p");
  else if (p.kind() == Kind::Words)
    parent = children(p.body(), TEXT, "p").at(0);
  else
    parent = children(p.body(), DRAW, "page").at(0);
  auto frame = add(parent, DRAW, "draw:frame");
  frame.setAttributeNS(DRAW, "draw:name", "Embedded");
  frame.setAttributeNS(SVG, "svg:width", "20pt");
  frame.setAttributeNS(SVG, "svg:height", "20pt");
  if (p.kind() == Kind::Words || in_styles)
    frame.setAttributeNS(TEXT, "text:anchor-type", "as-char");
  auto object = add(frame, DRAW, "draw:object");
  auto nested = read_document_xml(
      R"XML(<office:document office:version="1.2" office:mimetype="application/vnd.oasis.opendocument.text" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0">
<office:font-face-decls><style:font-face style:name="Face" svg:font-family="NestedWrong"/><style:font-face style:name="LocalFace" svg:font-family="NestedWrongLocal"/></office:font-face-decls>
<office:styles><style:default-style style:family="text"><style:text-properties fo:font-size="31pt" fo:color="#cc0000"/></style:default-style><style:default-style style:family="paragraph"><style:paragraph-properties fo:margin-left="77pt" style:tab-stop-distance="90pt"/></style:default-style><text:list-style style:name="NestedList"><text:list-level-style-image text:level="1"><office:binary-data>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==</office:binary-data><style:list-level-properties fo:width="10pt" fo:height="10pt"/></text:list-level-style-image></text:list-style></office:styles>
<office:automatic-styles><style:page-layout style:name="OuterLayout"><style:page-layout-properties fo:page-width="1000pt" fo:page-height="900pt" fo:margin="100pt"/><style:header-style><style:header-footer-properties fo:min-height="100pt" fo:margin-bottom="40pt"/></style:header-style></style:page-layout></office:automatic-styles>
<office:master-styles><style:master-page style:name="OuterPage" style:page-layout-name="OuterLayout"><style:header><text:p>NESTED HEADER</text:p></style:header></style:master-page></office:master-styles>
<office:body><office:text><text:p>NESTED BODY</text:p></office:text></office:body></office:document>)XML");
  if (many_labels) {
    auto group = child(nested.documentElement(), OFFICE, "styles");
    for (int i = 0; i < 4100; ++i) {
      auto list = add(group, TEXT, "text:list-style");
      list.setAttributeNS(STYLE, "style:name", "NestedImage" + Text::number(i));
      auto image = add(list, TEXT, "text:list-level-style-image");
      image.setAttributeNS(TEXT, "text:level", "1");
      set_plain_text(add(image, OFFICE, "office:binary-data"),
                     "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4n"
                     "GP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
    }
  }
  object.appendChild(doc.importNode(nested.documentElement(), true));
  if (in_styles)
    p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  return p;
}
void inline_object(OdfPackage &p) {
  auto page = children(p.body(), DRAW, "page").at(0);
  auto frame = child(page, DRAW, "frame");
  auto paragraph = child(child(page, DRAW, "rect"), TEXT, "p");
  assert(!frame.isNull() && !paragraph.isNull());
  frame.setAttributeNS(TEXT, "text:anchor-type", "as-char");
  paragraph.appendChild(frame);
}
void paragraph(const TextParagraph &a, const TextParagraph &b,
               const Text &mode) {
  if (mode == "fonts") {
    assert(a.font == b.font && a.spans.size() == b.spans.size());
    for (size_t i = 0; i < a.spans.size(); ++i)
      assert(a.spans[i].font == b.spans[i].font);
  } else if (mode == "defaults") {
    assert(a.left == b.left && a.default_tab == b.default_tab &&
           a.color == b.color);
    assert(a.font.size == b.font.size && a.alignment == b.alignment);
  } else if (mode == "opaque")
    assert(a.text == b.text);
}
void check(Kind kind, bool in_styles, const Text &mode) {
  auto original = specimen(kind);
  auto altered = embedded(original, in_styles, mode == "labels");
  auto xml = altered.xml.toByteArray(), styles = altered.entry("styles.xml");
  if (kind == Kind::Words) {
    auto a = read_text_flow(original), b = read_text_flow(altered);
    assert(a.paragraphs.size() == 2 &&
           a.paragraphs.size() == b.paragraphs.size());
    assert(a.paragraphs[0].font.family == "fixed" &&
           a.paragraphs[1].font.family == "monospace");
    assert(a.paragraphs[0].left == 7 && a.paragraphs[0].default_tab == 24);
    assert(a.masters.at("OuterPage").width == 400 &&
           a.masters.at("OuterPage").height == 300);
    for (size_t i = 0; i < a.paragraphs.size(); ++i)
      paragraph(a.paragraphs[i], b.paragraphs[i], mode);
    if (mode == "masters") {
      assert(a.masters.size() == b.masters.size());
      for (auto &[name, x] : a.masters) {
        auto &y = b.masters.at(name);
        assert(x.width == y.width && x.height == y.height && x.left == y.left);
      }
    }
    if (mode == "stories") {
      auto x = a.masters.at("OuterPage").stories->headers[0];
      auto y = b.masters.at("OuterPage").stories->headers[0];
      assert(x->minimum == y->minimum && x->margins == y->margins);
      auto first = x->read({}), second = y->read({});
      assert(first.size() == second.size());
      for (size_t i = 0; i < first.size(); ++i) {
        assert(first[i].flow.paragraphs.size() ==
               second[i].flow.paragraphs.size());
        for (size_t j = 0; j < first[i].flow.paragraphs.size(); ++j)
          assert(first[i].flow.paragraphs[j].text ==
                 second[i].flow.paragraphs[j].text);
      }
    }
  } else {
    for (auto target : {SceneTarget::Screen, SceneTarget::Print}) {
      auto a = read_presentation_scenes(original, {}, target).at(0);
      auto b = read_presentation_scenes(altered, {}, target).at(0);
      assert(a.width == 400 && a.height == 300 && a.width == b.width &&
             a.height == b.height);
      for (const auto &x : a.shapes) {
        auto at = std::find_if(b.shapes.begin(), b.shapes.end(),
                               [&](const auto &y) { return x.name == y.name; });
        assert(at != b.shapes.end());
        assert(x.paint.font.family ==
               (x.name == "Local" ? Text("monospace") : Text("fixed")));
        assert(x.text_flow.paragraphs.at(0).left == 7 &&
               x.text_flow.paragraphs.at(0).default_tab == 24);
        if (mode == "fonts")
          assert(x.paint.font == at->paint.font);
        assert(x.text_flow.paragraphs.size() ==
               at->text_flow.paragraphs.size());
        for (size_t i = 0; i < x.text_flow.paragraphs.size(); ++i)
          paragraph(x.text_flow.paragraphs[i], at->text_flow.paragraphs[i],
                    mode);
      }
      if (!in_styles && mode == "opaque") {
        auto at = std::find_if(
            b.shapes.begin(), b.shapes.end(),
            [](const auto &shape) { return shape.name == "Embedded"; });
        assert(at != b.shapes.end() && at->text.empty() &&
               at->text_flow.paragraphs.empty());
        assert(std::find(b.unsupported.begin(), b.unsupported.end(),
                         Text("Embedded document objects")) !=
               b.unsupported.end());
      }
      if (!in_styles && mode == "inline") {
        auto inline_package = embedded(original, false);
        inline_object(inline_package);
        auto paragraph =
            child(child(children(inline_package.body(), DRAW, "page").at(0),
                        DRAW, "rect"),
                  TEXT, "p");
        auto frame = child(paragraph, DRAW, "frame").cloneNode(true);
        set_plain_text(paragraph, "Scope Č");
        paragraph.appendChild(frame);
        auto spaces = add(paragraph, TEXT, "text:s");
        spaces.setAttributeNS(TEXT, "text:c", "2");
        add(paragraph, TEXT, "text:tab");
        set_plain_text(add(paragraph, TEXT, "text:span"), "A");
        add(paragraph, TEXT, "text:line-break");
        paragraph.appendChild(inline_package.xml.createTextNode("BZ"));
        auto inline_scene =
            read_presentation_scenes(inline_package, {}, target).at(0);
        auto common = std::find_if(
            inline_scene.shapes.begin(), inline_scene.shapes.end(),
            [](const auto &shape) { return shape.name == "Common"; });
        assert(common != inline_scene.shapes.end());
        assert(common->text == "Scope Č  \tA\nBZ");
        assert(common->text_flow.paragraphs.at(0).text == common->text);
      }
    }
  }
  assert(xml == altered.xml.toByteArray() &&
         styles == altered.entry("styles.xml"));
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && Text(argv[1]) == "--documents") {
    std::filesystem::create_directories(argv[2]);
    for (auto kind : {Kind::Words, Kind::Stage}) {
      auto p = specimen(kind);
      Text stem = Text(argv[2]) + (kind == Kind::Words ? "/words" : "/stage");
      Text ext = kind == Kind::Words ? ".odt" : ".odp";
      p.save(stem + "-baseline" + ext);
      auto content = embedded(p, false);
      if (kind == Kind::Stage)
        inline_object(content);
      content.save(stem + "-content" + ext);
      embedded(p, true).save(stem + "-styles" + ext);
    }
    return 0;
  }
  TextList modes{"fonts",  "defaults", "masters", "stories",
                 "opaque", "labels",   "inline"};
  if (argc == 3 && Text(argv[1]) == "--case")
    modes = {Text(argv[2])};
  for (const auto &mode : modes)
    for (auto kind : {Kind::Words, Kind::Stage})
      for (bool in_styles : {false, true}) {
        if (mode == "inline" && (kind != Kind::Stage || in_styles))
          continue;
        check(kind, in_styles, mode);
        std::cout << "PASS " << mode.toStdString() << ' '
                  << (kind == Kind::Words ? "Words" : "Stage")
                  << (in_styles ? " styles" : " content") << std::endl;
      }
}
