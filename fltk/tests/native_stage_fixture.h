#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_xml_source.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
int find(const Presentation &p, const Text &name) {
  for (size_t i = 0; i < p.scene(0).shapes.size(); ++i)
    if (p.scene(0).shapes[i].name == name)
      return int(i);
  std::abort();
}
NativeObjectRef ref(const Presentation &p, const Text &name, int parents = 0) {
  return {find(p, name), parents};
}
Bytes content(const Presentation &p) {
  return p.scene(0)
      .shapes.at(find(p, "A"))
      .source.ownerDocument()
      .toByteArray();
}
void order(const Presentation &p, std::initializer_list<Text> expected) {
  std::vector<Text> actual;
  for (const auto &shape : p.scene(0).shapes)
    if (!shape.master)
      actual.push_back(shape.name);
  assert(actual == std::vector<Text>(expected));
}
void same_geometry(const Presentation &p,
                   const std::vector<SceneShape> &before) {
  for (const auto &old : before) {
    const auto &now = p.scene(0).shapes.at(find(p, old.name));
    const auto a = old.transform, b = now.transform;
    for (auto pair : {std::pair(a.a, b.a),
                      {a.b, b.b},
                      {a.c, b.c},
                      {a.d, b.d},
                      {a.e, b.e},
                      {a.f, b.f}})
      assert(std::abs(pair.first - pair.second) < 1e-8);
    assert(old.text == now.text && old.image == now.image);
    assert(old.width == now.width && old.height == now.height);
    assert(old.paint.font.size == now.paint.font.size);
    assert(old.paint.stroke_width == now.paint.stroke_width);
  }
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml = read_document_xml(
      R"XML(<office:document-content office:version="1.2" xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:xlink="http://www.w3.org/1999/xlink"><office:body><office:presentation><draw:page draw:name="Grouping" draw:master-page-name="Default" draw:nav-order="a b x y n1 n2 c">
 <draw:rect draw:name="A" xml:id="a" draw:z-index="0" svg:x="10pt" svg:y="20pt" svg:width="100pt" svg:height="60pt"><text:p>A<text:span>Český</text:span></text:p></draw:rect>
 <draw:rect draw:name="B" xml:id="b" draw:z-index="1" svg:x="40pt" svg:y="60pt" svg:width="80pt" svg:height="40pt"><text:p>B</text:p></draw:rect>
 <draw:g draw:name="Outer" xml:id="outer"><draw:rect draw:name="X" xml:id="x" svg:x="140pt" svg:y="40pt" svg:width="50pt" svg:height="80pt"><text:p>X</text:p></draw:rect><draw:frame draw:name="Y" xml:id="y" svg:x="180pt" svg:y="90pt" svg:width="60pt" svg:height="40pt"><draw:image xlink:type="simple" xlink:href="Pictures/image.svg"/></draw:frame><draw:g draw:name="Inner" xml:id="inner"><draw:line draw:name="N1" xml:id="n1" svg:x1="10pt" svg:y1="180pt" svg:x2="100pt" svg:y2="160pt"/><draw:rect draw:name="N2" xml:id="n2" svg:x="100pt" svg:y="150pt" svg:width="80pt" svg:height="40pt"/></draw:g></draw:g>
 <draw:rect draw:name="C" xml:id="c" svg:x="250pt" svg:y="150pt" svg:width="80pt" svg:height="40pt"><text:p>C</text:p></draw:rect>
</draw:page><draw:page draw:name="Unchanged" draw:master-page-name="Default"><draw:rect svg:width="20pt" svg:height="20pt"/></draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry(
      "Pictures/image.svg",
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect width="20" height="10" fill="red"/></svg>)",
      "image/svg+xml");
  p.set_entry("opaque/source", Bytes("preserve\0data", 13),
              "application/octet-stream");
  return p;
}
} // namespace
