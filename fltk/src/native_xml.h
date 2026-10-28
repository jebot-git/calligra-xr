// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
#include <functional>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>
struct XmlOwner {
  xmlDocPtr doc = nullptr;
  std::set<xmlNodePtr> detached;
  ~XmlOwner() {
    for (auto *n : detached)
      xmlFreeNode(n);
    if (doc)
      xmlFreeDoc(doc);
  }
};
class XmlDocument;
class XmlNode {
  friend class XmlDocument;

protected:
  std::shared_ptr<XmlOwner> owner_;
  xmlNodePtr node_ = nullptr;
  XmlNode(std::shared_ptr<XmlOwner> o, xmlNodePtr n)
      : owner_(std::move(o)), node_(n) {}
  xmlNsPtr ns(const Text &uri, const Text &qname) const {
    auto u = uri.toUtf8();
    auto at = qname.indexOf(':');
    auto prefix = at >= 0 ? qname.left(at).toUtf8() : Bytes();
    auto found = xmlSearchNsByHref(owner_->doc, node_, BAD_CAST u.c_str());
    if (!found)
      found = xmlNewNs(node_, BAD_CAST u.c_str(),
                       prefix.empty() ? nullptr : BAD_CAST prefix.c_str());
    return found;
  }
  void attach(XmlNode child) const {
    if (!node_ || !child.node_)
      throw std::runtime_error("Cannot attach null XML node");
    if (owner_ != child.owner_)
      throw std::runtime_error("Cross-document nodes must be imported");
    xmlUnlinkNode(child.node_);
    owner_->detached.erase(child.node_);
  }

public:
  XmlNode() = default;
  bool isNull() const { return !node_; }
  bool isElement() const { return node_ && node_->type == XML_ELEMENT_NODE; }
  bool isText() const { return node_ && node_->type == XML_TEXT_NODE; }
  bool isCDATASection() const {
    return node_ && node_->type == XML_CDATA_SECTION_NODE;
  }
  XmlNode toElement() const { return isElement() ? *this : XmlNode(); }
  XmlNode firstChild() const {
    return {owner_, node_ ? node_->children : nullptr};
  }
  XmlNode lastChild() const { return {owner_, node_ ? node_->last : nullptr}; }
  XmlNode nextSibling() const {
    return {owner_, node_ ? node_->next : nullptr};
  }
  XmlNode previousSibling() const {
    return {owner_, node_ ? node_->prev : nullptr};
  }
  XmlNode parentNode() const {
    return {owner_, node_ ? node_->parent : nullptr};
  }
  Text namespaceURI() const {
    return node_ && node_->ns
               ? Text(reinterpret_cast<const char *>(node_->ns->href))
               : Text();
  }
  Text localName() const {
    return node_ ? Text(reinterpret_cast<const char *>(node_->name)) : Text();
  }
  Text tagName() const {
    if (!isElement())
      return {};
    auto name = localName();
    return node_->ns && node_->ns->prefix
               ? Text(reinterpret_cast<const char *>(node_->ns->prefix)) + ':' +
                     name
               : name;
  }
  Text nodeValue() const {
    return node_ && node_->content
               ? Text(reinterpret_cast<const char *>(node_->content))
               : Text();
  }
  void setNodeValue(const Text &value) {
    if (node_)
      xmlNodeSetContent(node_, BAD_CAST value.toUtf8().c_str());
  }
  Text text() const {
    if (!node_)
      return {};
    auto *p = xmlNodeGetContent(node_);
    Text result(reinterpret_cast<const char *>(p));
    xmlFree(p);
    return result;
  }
  Text attributeNS(const Text &uri, const Text &name,
                   const Text &fallback = {}) const {
    if (!node_)
      return fallback;
    auto *p = xmlGetNsProp(node_, BAD_CAST name.toUtf8().c_str(),
                           BAD_CAST uri.toUtf8().c_str());
    if (!p)
      return fallback;
    Text out(reinterpret_cast<const char *>(p));
    xmlFree(p);
    return out;
  }
  Text attribute(const Text &name, const Text &fallback = {}) const {
    if (!node_)
      return fallback;
    auto *p = xmlGetProp(node_, BAD_CAST name.toUtf8().c_str());
    if (!p)
      return fallback;
    Text out(reinterpret_cast<const char *>(p));
    xmlFree(p);
    return out;
  }
  bool hasAttribute(const Text &name) const {
    return node_ && xmlHasProp(node_, BAD_CAST name.toUtf8().c_str());
  }
  bool hasAttributeNS(const Text &uri, const Text &name) const {
    return node_ && xmlHasNsProp(node_, BAD_CAST name.toUtf8().c_str(),
                                 BAD_CAST uri.toUtf8().c_str());
  }
  void setAttributeNS(const Text &uri, const Text &qname, const Text &value) {
    if (!node_)
      throw std::runtime_error("Attribute on null node");
    auto local = qname.mid(qname.indexOf(':') + 1).toUtf8();
    auto v = value.toUtf8();
    xmlSetNsProp(node_, ns(uri, qname), BAD_CAST local.c_str(),
                 BAD_CAST v.c_str());
  }
  template <class T>
    requires std::is_arithmetic_v<T>
  void setAttributeNS(const Text &uri, const Text &name, T value) {
    setAttributeNS(uri, name, Text::number(value));
  }
  void setAttribute(const Text &name, const Text &value) {
    if (node_)
      xmlSetProp(node_, BAD_CAST name.toUtf8().c_str(),
                 BAD_CAST value.toUtf8().c_str());
  }
  void removeAttributeNS(const Text &uri, const Text &name) {
    if (node_) {
      auto *n =
          xmlSearchNsByHref(owner_->doc, node_, BAD_CAST uri.toUtf8().c_str());
      if (n)
        xmlUnsetNsProp(node_, n, BAD_CAST name.toUtf8().c_str());
    }
  }
  // libxml's xmlAddChild merges/frees adjacent text nodes. Document editing
  // retains handles to individual runs, so link nodes without coalescing them.
  XmlNode insertBefore(XmlNode child, XmlNode before) {
    if (child.node_ == before.node_)
      return child;
    if (before.node_ && before.node_->parent != node_)
      throw std::runtime_error("Invalid XML insertion point");
    for (auto *p = node_; p; p = p->parent)
      if (p == child.node_)
        throw std::runtime_error("Cyclic XML insertion");
    attach(child);
    auto *n = child.node_;
    auto *next = before.node_;
    auto *prev = next ? next->prev : node_->last;
    n->parent = node_;
    n->prev = prev;
    n->next = next;
    if (prev)
      prev->next = n;
    else
      node_->children = n;
    if (next)
      next->prev = n;
    else
      node_->last = n;
    return child;
  }
  XmlNode appendChild(XmlNode child) { return insertBefore(child, {}); }
  XmlNode insertAfter(XmlNode child, XmlNode after) {
    if (child.node_ == after.node_)
      return child;
    return insertBefore(child,
                        after.node_ ? after.nextSibling() : firstChild());
  }
  XmlNode removeChild(XmlNode child) {
    if (!child.node_ || child.node_->parent != node_)
      throw std::runtime_error("Invalid XML child removal");
    xmlUnlinkNode(child.node_);
    owner_->detached.insert(child.node_);
    return child;
  }
  XmlNode cloneNode(bool deep) const {
    if (!node_)
      return {};
    auto *n = xmlDocCopyNode(node_, owner_->doc, deep ? 1 : 2);
    owner_->detached.insert(n);
    return {owner_, n};
  }
  List<XmlNode> elementsByTagNameNS(const Text &uri, const Text &name) const {
    List<XmlNode> out;
    std::function<void(xmlNodePtr)> walk = [&](xmlNodePtr n) {
      for (; n; n = n->next) {
        XmlNode v(owner_, n);
        if (v.isElement() && v.namespaceURI() == uri && v.localName() == name)
          out << v;
        if (n->children)
          walk(n->children);
      }
    };
    if (node_)
      walk(node_->children);
    return out;
  }
  XmlDocument ownerDocument() const;
  bool operator==(const XmlNode &o) const { return node_ == o.node_; }
  bool operator!=(const XmlNode &o) const { return !(*this == o); }
};
using XmlElement = XmlNode;
class XmlDocument {
  friend class XmlNode;
  std::shared_ptr<XmlOwner> owner_;
  explicit XmlDocument(std::shared_ptr<XmlOwner> o) : owner_(std::move(o)) {}

public:
  enum class ParseOption { UseNamespaceProcessing };
  XmlDocument() : owner_(std::make_shared<XmlOwner>()) {
    owner_->doc = xmlNewDoc(BAD_CAST "1.0");
  }
  bool setContent(const Bytes &data,
                  ParseOption = ParseOption::UseNamespaceProcessing) {
    auto *reader = xmlReaderForMemory(
        data.data(), int(data.size()), nullptr, nullptr,
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!reader)
      return false;
    int status = 0;
    while ((status = xmlTextReaderRead(reader)) == 1) {
      auto type = xmlTextReaderNodeType(reader);
      if (type == XML_READER_TYPE_DOCUMENT_TYPE ||
          type == XML_READER_TYPE_ENTITY_REFERENCE ||
          type == XML_READER_TYPE_ENTITY || xmlTextReaderDepth(reader) > 256) {
        xmlFreeTextReader(reader);
        return false;
      }
    }
    xmlFreeTextReader(reader);
    if (status < 0)
      return false;
    auto *parsed = xmlReadMemory(
        data.data(), int(data.size()), nullptr, nullptr,
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!parsed || parsed->intSubset || parsed->extSubset) {
      if (parsed)
        xmlFreeDoc(parsed);
      return false;
    }
    auto next = std::make_shared<XmlOwner>();
    next->doc = parsed;
    owner_ = std::move(next);
    return true;
  }
  XmlElement documentElement() const {
    return {owner_, xmlDocGetRootElement(owner_->doc)};
  }
  XmlElement createElement(const Text &name) {
    auto *n = xmlNewDocNode(owner_->doc, nullptr,
                            BAD_CAST name.toUtf8().c_str(), nullptr);
    owner_->detached.insert(n);
    return {owner_, n};
  }
  XmlElement createElementNS(const Text &uri, const Text &qname) {
    auto local = qname.mid(qname.indexOf(':') + 1).toUtf8();
    auto *n =
        xmlNewDocNode(owner_->doc, nullptr, BAD_CAST local.c_str(), nullptr);
    owner_->detached.insert(n);
    XmlNode value(owner_, n);
    n->ns = value.ns(uri, qname);
    return value;
  }
  XmlNode createTextNode(const Text &value) {
    auto b = value.toUtf8();
    auto *n = xmlNewDocTextLen(owner_->doc, BAD_CAST b.data(), b.size());
    owner_->detached.insert(n);
    return {owner_, n};
  }
  XmlNode importNode(const XmlNode &node, bool deep) {
    if (!node.node_)
      return {};
    auto *n = xmlDocCopyNode(node.node_, owner_->doc, deep ? 1 : 2);
    owner_->detached.insert(n);
    return {owner_, n};
  }
  List<XmlNode> elementsByTagNameNS(const Text &uri, const Text &name) const {
    return documentElement().elementsByTagNameNS(uri, name);
  }
  Text toString() const { return Text::fromUtf8(toByteArray()); }
  Bytes toByteArray(int = -1) const {
    xmlReconciliateNs(owner_->doc, xmlDocGetRootElement(owner_->doc));
    xmlChar *p = nullptr;
    int n = 0;
    xmlDocDumpMemoryEnc(owner_->doc, &p, &n, "UTF-8");
    if (!p)
      throw std::runtime_error("Cannot serialize XML");
    Bytes out(reinterpret_cast<char *>(p), n);
    xmlFree(p);
    return out;
  }
};
inline XmlDocument XmlNode::ownerDocument() const {
  return XmlDocument(owner_);
}
