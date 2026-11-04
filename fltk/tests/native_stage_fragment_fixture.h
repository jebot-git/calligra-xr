#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_image.h"
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
XmlElement element(XmlDocument &doc, const Text &uri, const Text &name,
                   XmlElement parent = {}) {
  auto node = doc.createElementNS(uri, name);
  if (!parent.isNull())
    parent.appendChild(node);
  return node;
}
XmlElement style(XmlDocument &doc, XmlElement parent, const Text &name,
                 const Text &family) {
  auto node = element(doc, STYLE, "style:style", parent);
  node.setAttributeNS(STYLE, "style:name", name);
  node.setAttributeNS(STYLE, "style:family", family);
  return node;
}
XmlElement named(XmlDocument &doc, const Text &name) {
  for (auto node : doc.elementsByTagNameNS(DRAW, "rect"))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  std::abort();
}
OdfPackage source() {
  auto p = fixture();
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto root = doc.documentElement();
  auto common = child(root, OFFICE, "styles");
  auto faces = element(doc, OFFICE, "office:font-face-decls");
  root.insertBefore(faces, common);
  for (auto name : {Text("DocBitmap"), Text("SecretFont")}) {
    auto font = element(doc, STYLE, "style:font-face", faces);
    font.setAttributeNS(STYLE, "style:name", name);
    font.setAttributeNS(SVG, "svg:font-family",
                        name == "DocBitmap" ? Text("fixed")
                                            : Text("UNSELECTED_FONT"));
  }
  auto base = style(doc, common, "CommonGraphic", "graphic");
  auto paint = element(doc, STYLE, "style:graphic-properties", base);
  paint.setAttributeNS(DRAW, "draw:fill", "solid");
  paint.setAttributeNS(DRAW, "draw:fill-color", "#e04020");
  paint.setAttributeNS(DRAW, "draw:stroke", "solid");
  paint.setAttributeNS(SVG, "svg:stroke-color", "#102030");
  paint.setAttributeNS(SVG, "svg:stroke-width", "3pt");
  paint.setAttributeNS(SVG, "svg:stroke-linecap", "round");
  paint.setAttributeNS(DRAW, "draw:stroke-linejoin", "bevel");
  auto default_graphic = element(doc, STYLE, "style:default-style", common);
  default_graphic.setAttributeNS(STYLE, "style:family", "graphic");
  auto props = element(doc, STYLE, "style:graphic-properties", default_graphic);
  props.setAttributeNS(FO, "fo:padding", "2pt");
  auto default_p = element(doc, STYLE, "style:default-style", common);
  default_p.setAttributeNS(STYLE, "style:family", "paragraph");
  props = element(doc, STYLE, "style:text-properties", default_p);
  props.setAttributeNS(STYLE, "style:font-name", "DocBitmap");
  props.setAttributeNS(FO, "fo:font-size", "11pt");
  props.setAttributeNS(FO, "fo:color", "#123456");
  auto paragraph = style(doc, common, "CommonParagraph", "paragraph");
  paragraph.setAttributeNS(STYLE, "style:next-style-name", "AfterParagraph");
  style(doc, common, "AfterParagraph", "paragraph");
  auto inline_style = style(doc, common, "Emphasis", "text");
  props = element(doc, STYLE, "style:text-properties", inline_style);
  props.setAttributeNS(FO, "fo:color", "#abcdef");
  style(doc, common, "UNSELECTED_STYLE", "graphic");
  auto layers = element(doc, DRAW, "draw:layer-set",
                        child(root, OFFICE, "master-styles"));
  for (auto name :
       {Text("Art"), Text("Hidden"), Text("Printer"), Text("UnusedLayer")}) {
    auto layer = element(doc, DRAW, "draw:layer", layers);
    layer.setAttributeNS(DRAW, "draw:name", name);
    if (name == "Hidden")
      layer.setAttributeNS(DRAW, "draw:display", "none");
    if (name == "Printer")
      layer.setAttributeNS(DRAW, "draw:display", "printer");
    auto title = element(doc, SVG, "svg:title", layer);
    title.appendChild(doc.createTextNode(
        name == "UnusedLayer" ? Text("UNSELECTED_LAYER") : name));
  }
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  auto automatic = element(p.xml, OFFICE, "office:automatic-styles");
  p.xml.documentElement().insertBefore(
      automatic, child(p.xml.documentElement(), OFFICE, "body"));
  auto graphic = style(p.xml, automatic, "paint", "graphic");
  graphic.setAttributeNS(STYLE, "style:parent-style-name", "CommonGraphic");
  props = element(p.xml, STYLE, "style:graphic-properties", graphic);
  props.setAttributeNS(DRAW, "draw:opacity", "50%");
  auto text = style(p.xml, automatic, "P", "paragraph");
  text.setAttributeNS(STYLE, "style:parent-style-name", "CommonParagraph");
  props = element(p.xml, STYLE, "style:text-properties", text);
  props.setAttributeNS(FO, "fo:font-size", "14pt");
  auto x = named(p.xml, "X");
  x.setAttributeNS(DRAW, "draw:style-name", "paint");
  x.setAttributeNS(DRAW, "draw:layer", "Art");
  auto paragraph_x = child(x, TEXT, "p");
  paragraph_x.setAttributeNS(TEXT, "text:style-name", "P");
  auto span = element(p.xml, TEXT, "text:span", paragraph_x);
  span.setAttributeNS(TEXT, "text:style-name", "Emphasis");
  span.appendChild(p.xml.createTextNode(" Ω"));
  // ODF1.2 puts transforms on the shapes, not draw:g. The legacy group-
  // transform compatibility case is exercised separately below.
  const Text affine = "matrix(.8 .2 -.1 1.1 0 0) translate(30pt 15pt)";
  x.setAttributeNS(DRAW, "draw:transform", affine);
  auto hidden = named(p.xml, "N2");
  hidden.setAttributeNS(DRAW, "draw:layer", "Hidden");
  hidden.setAttributeNS(DRAW, "draw:transform", affine);
  for (auto image : p.xml.elementsByTagNameNS(DRAW, "image"))
    image.parentNode().toElement().setAttributeNS(DRAW, "draw:transform",
                                                  affine);
  for (auto line : p.xml.elementsByTagNameNS(DRAW, "line")) {
    line.setAttributeNS(DRAW, "draw:layer", "Printer");
    line.setAttributeNS(DRAW, "draw:style-name", "paint");
    line.setAttributeNS(DRAW, "draw:transform", affine);
  }
  p.set_entry(
      "Pictures/unselected.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg"><text>UNSELECTED_ASSET</text></svg>)",
      "image/svg+xml");
  p.set_entry(
      "meta.xml",
      R"(<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:dc="http://purl.org/dc/elements/1.1/" office:version="1.2"><office:meta><dc:description>UNSELECTED_METADATA</dc:description></office:meta></office:document-meta>)",
      "text/xml");
  return p;
}
} // namespace
