// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
int main() {
  auto source = read_document_xml(
      R"(<o:root xmlns:o="urn:office" xmlns:d="urn:drawing" xmlns:s="urn:svg" xmlns:aux="urn:source-value"><d:set><d:layer s:label="metadata" s:kind="aux:Kind"><s:title>Title &amp; text</s:title><s:desc><![CDATA[a<b]]></s:desc></d:layer></d:set></o:root>)");
  const auto source_bytes = source.toByteArray();
  for (int mode = 0; mode < 3; ++mode) {
    auto target = read_document_xml(
        mode == 0
            ? R"(<root xmlns:d="urn:svg" xmlns:s="urn:drawing" xmlns:aux="urn:target-value"/>)"
        : mode == 1
            ? R"(<root xmlns="urn:svg" xmlns:d="urn:other" xmlns:s="urn:office" xmlns:aux="urn:target-value"/>)"
            : R"(<root xmlns:aux="urn:target-value"/>)");
    auto imported = target.importNode(
        source.elementsByTagNameNS("urn:drawing", "set")[0], true);
    target.documentElement().appendChild(imported);
    const auto original_node = imported.identity();
    for (int repeat = 0; repeat < 3; ++repeat) {
      const auto bytes = target.toByteArray();
      assert(imported.identity() == original_node);
      auto parsed = read_document_xml(bytes);
      assert(parsed.elementsByTagNameNS("urn:drawing", "set").size() == 1);
      assert(parsed.elementsByTagNameNS("urn:drawing", "title").empty());
      auto title = parsed.elementsByTagNameNS("urn:svg", "title");
      assert(title.size() == 1 && title[0].text() == "Title & text");
      auto layer = parsed.elementsByTagNameNS("urn:drawing", "layer")[0];
      assert(layer.attributeNS("urn:svg", "label") == "metadata");
      assert(layer.attributeNS("urn:svg", "kind") == "aux:Kind");
      assert(parsed.elementsByTagNameNS("urn:svg", "desc")[0].text() == "a<b");
      auto *doc = xmlReadMemory(bytes.data(), int(bytes.size()), nullptr,
                                nullptr, XML_PARSE_NONET);
      assert(doc);
      auto *root = xmlDocGetRootElement(doc);
      auto *node = root->children;
      while (node && node->type != XML_ELEMENT_NODE)
        node = node->next;
      assert(node);
      auto *binding = xmlSearchNs(doc, node, BAD_CAST "aux");
      assert(binding &&
             xmlStrEqual(binding->href, BAD_CAST "urn:source-value"));
      xmlFreeDoc(doc);
    }
    assert(source.toByteArray() == source_bytes);
  }
  // Same-document layer inheritance uses importNode too. Source and target
  // sibling scopes can bind one prefix to different namespaces.
  auto doc = read_document_xml(
      R"(<root><source xmlns:p="urn:source"><p:object p:kind="p:Type"/></source><target xmlns:p="urn:target"/></root>)");
  auto from = doc.elementsByTagNameNS("", "source")[0],
       to = doc.elementsByTagNameNS("", "target")[0];
  to.appendChild(doc.importNode(from.firstChild(), true));
  auto reopened = read_document_xml(doc.toByteArray());
  assert(reopened.elementsByTagNameNS("urn:source", "object").size() == 2);
  for (auto xml :
       {R"(<root xmlns="urn:a" xmlns:p="urn:a" p:name="old"/>)",
        R"(<root xmlns="urn:a"/>)",
        R"(<root xmlns="urn:a" xmlns:p="urn:other" value="p:Kind"/>)"}) {
    auto d = read_document_xml(xml);
    auto root = d.documentElement();
    root.setAttributeNS("urn:a", "p:name", "new");
    root.setAttributeNS("", "plain", "value");
    auto bytes = d.toByteArray();
    auto out = read_document_xml(bytes);
    assert(out.documentElement().namespaceURI() == "urn:a");
    assert(out.documentElement().attributeNS("urn:a", "name") == "new");
    assert(out.documentElement().attributeNS("", "plain") == "value");
    assert(out.documentElement().hasAttributeNS("", "plain"));
    assert(!out.documentElement().hasAttributeNS("urn:a", "plain"));
    out.documentElement().removeAttributeNS("", "plain");
    auto removed = read_document_xml(out.toByteArray());
    assert(!removed.documentElement().hasAttributeNS("", "plain"));
    assert(removed.documentElement().attributeNS("urn:a", "name") == "new");
    if (root.hasAttribute("value")) {
      auto *raw = xmlReadMemory(bytes.data(), int(bytes.size()), nullptr,
                                nullptr, XML_PARSE_NONET);
      assert(raw);
      auto *binding = xmlSearchNs(raw, xmlDocGetRootElement(raw), BAD_CAST "p");
      assert(binding && xmlStrEqual(binding->href, BAD_CAST "urn:other"));
      xmlFreeDoc(raw);
    }
  }
  std::cout
      << "PASS imported prefix shadows, element/attribute URIs, value QName "
         "bindings, CDATA, repeated serialization and source identity\n";
}
