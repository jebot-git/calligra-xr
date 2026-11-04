// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_fragment_fixture.h"
namespace {

const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
void near(double a, double b) { assert(std::abs(a - b) < 1e-10); }
XmlElement common_group(XmlDocument doc) {
  return child(doc.documentElement(), OFFICE, "styles");
}
XmlElement props(XmlDocument doc, XmlElement style, const Text &kind) {
  auto p = child(style, STYLE, kind);
  if (!p.isNull())
    return p;
  p = doc.createElementNS(STYLE, "style:" + kind);
  auto rank = [](const Text &name) {
    return name == "text-properties"        ? 2
           : name == "paragraph-properties" ? 1
           : name.endsWith("-properties")   ? 0
                                            : 3;
  };
  XmlElement before;
  for (auto n : children(style, STYLE))
    if (rank(n.localName()) > rank(kind)) {
      before = n;
      break;
    }
  style.insertBefore(p, before);
  return p;
}
XmlElement chosen(const XmlDocument &doc) {
  for (auto tag : {Text("rect"), Text("frame")})
    for (auto node : doc.elementsByTagNameNS(DRAW, tag))
      if (node.attributeNS(DRAW, "name") == "X")
        return node;
  std::abort();
}
XmlElement as_frame(OdfPackage &p, XmlElement source) {
  auto frame = p.xml.createElementNS(DRAW, "draw:frame");
  for (const auto &a : source.attributes())
    frame.setAttributeNS(a.uri, a.name, a.value);
  auto box = element(p.xml, DRAW, "draw:text-box", frame);
  for (auto node = source.firstChild(); !node.isNull();) {
    auto next = node.nextSibling();
    box.appendChild(node);
    node = next;
  }
  auto parent = source.parentNode();
  parent.insertBefore(frame, source);
  parent.removeChild(source);
  return frame;
}
OdfPackage contexts(bool tables = true) {
  auto p = source();
  auto doc = read_document_xml(p.entry("styles.xml"));
  auto common = common_group(doc);
  for (auto node : children(common, STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == "CommonGraphic") {
      auto t = props(doc, node, "text-properties");
      t.setAttributeNS(FO, "fo:language", "fr");
      t.setAttributeNS(FO, "fo:country", "CA");
    }
  for (auto node : children(common, STYLE, "default-style"))
    if (node.attributeNS(STYLE, "family") == "paragraph") {
      auto q = props(doc, node, "paragraph-properties");
      q.setAttributeNS(FO, "fo:margin-left", "9pt");
      q.setAttributeNS(STYLE, "style:tab-stop-distance", "30pt");
    }
  auto frame = style(doc, common, "Frame", "paragraph");
  props(doc, frame, "text-properties")
      .setAttributeNS(FO, "fo:font-size", "120%");
  props(doc, frame, "paragraph-properties")
      .setAttributeNS(FO, "fo:margin-left", "15pt");
  auto tabs = element(doc, STYLE, "style:tab-stops",
                      props(doc, frame, "paragraph-properties"));
  auto tab = element(doc, STYLE, "style:tab-stop", tabs);
  tab.setAttributeNS(STYLE, "style:position", "140pt");
  tab.setAttributeNS(STYLE, "style:type", "char");
  tab.setAttributeNS(STYLE, "style:char", ".");
  tab.setAttributeNS(STYLE, "style:leader-style", "wave");
  tab.setAttributeNS(STYLE, "style:leader-type", "double");
  tab.setAttributeNS(STYLE, "style:leader-width", "12%");
  tab.setAttributeNS(STYLE, "style:leader-text", ".");
  tab.setAttributeNS(STYLE, "style:leader-text-style", "Emphasis");
  auto descendant = style(doc, common, "Child", "paragraph");
  descendant.setAttributeNS(STYLE, "style:parent-style-name", "Frame");
  auto t = props(doc, descendant, "text-properties");
  t.setAttributeNS(FO, "fo:font-size", "125%");
  t.setAttributeNS(STYLE, "style:text-underline-style", "wave");
  t.setAttributeNS(STYLE, "style:text-underline-width", "20%");
  t.setAttributeNS(STYLE, "style:text-underline-color", "#345678");
  auto relative = style(doc, common, "Relative", "paragraph");
  props(doc, relative, "text-properties")
      .setAttributeNS(FO, "fo:font-size", "125%");
  auto cell_default = element(doc, STYLE, "style:default-style", common);
  cell_default.setAttributeNS(STYLE, "style:family", "table-cell");
  t = props(doc, cell_default, "text-properties");
  t.setAttributeNS(FO, "fo:font-size", "100%");
  t.setAttributeNS(FO, "fo:color", "#314159");
  for (auto [name, size] : {std::pair{Text("CellA"), Text("110%")},
                            std::pair{Text("CellB"), Text("150%")},
                            std::pair{Text("Nested"), Text("125%")}}) {
    auto s = style(doc, common, name, "table-cell");
    t = props(doc, s, "text-properties");
    t.setAttributeNS(FO, "fo:font-size", size);
    if (name == "Nested")
      t.setAttributeNS(FO, "fo:color", "#654321");
  }
  auto first = common.firstChild();
  for (auto node : children(common, STYLE, "default-style"))
    common.insertBefore(node, first);
  p.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  auto x = named(p.xml, "X");
  x.removeAttributeNS(DRAW, "transform");
  x.setAttributeNS(SVG, "svg:x", "20pt");
  x.setAttributeNS(SVG, "svg:y", "20pt");
  x.setAttributeNS(SVG, "svg:width", "400pt");
  x.setAttributeNS(SVG, "svg:height", "260pt");
  x.setAttributeNS(DRAW, "draw:text-style-name", "Frame");
  set_plain_text(x, {});
  auto container = x;
  if (tables) {
    auto frame = p.xml.createElementNS(DRAW, "draw:frame");
    for (const auto &a : x.attributes())
      frame.setAttributeNS(a.uri, a.name, a.value);
    x.parentNode().insertBefore(frame, x);
    x.parentNode().removeChild(x);
    x = frame;
    container = element(p.xml, DRAW, "draw:text-box", x);
  }
  for (auto name : {Text("Child"), Text("Relative")}) {
    auto q = element(p.xml, TEXT, "text:p", container);
    q.setAttributeNS(TEXT, "text:style-name", name);
    set_plain_text(q, name + "\tČeský\tEND");
  }
  if (tables) {
    auto table = element(p.xml, TABLE, "table:table", container);
    table.setAttributeNS(TABLE, "table:name", "OuterTable");
    for (auto name : {Text("CellA"), Text("CellB"), Text("CellB")}) {
      auto column = element(p.xml, TABLE, "table:table-column", table);
      column.setAttributeNS(TABLE, "table:default-cell-style-name", name);
    }
    auto row = element(p.xml, TABLE, "table:table-row", table);
    row.setAttributeNS(TABLE, "table:number-rows-repeated", "2");
    auto cell = element(p.xml, TABLE, "table:table-cell", row);
    cell.setAttributeNS(TABLE, "table:number-columns-repeated", "2");
    auto q = element(p.xml, TEXT, "text:p", cell);
    q.setAttributeNS(TEXT, "text:style-name", "Child");
    set_plain_text(q, "Cell");
    auto inner = element(p.xml, TABLE, "table:table", cell);
    inner.setAttributeNS(TABLE, "table:name", "InnerTable");
    element(p.xml, TABLE, "table:table-column", inner);
    auto r = element(p.xml, TABLE, "table:table-row", inner);
    auto c = element(p.xml, TABLE, "table:table-cell", r);
    c.setAttributeNS(TABLE, "table:style-name", "Nested");
    q = element(p.xml, TEXT, "text:p", c);
    q.setAttributeNS(TEXT, "text:style-name", "Child");
    set_plain_text(q, "Nested");
    element(p.xml, TABLE, "table:table-cell", row); // Empty third column.
  }
  return p;
}

} // namespace
