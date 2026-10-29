// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_edit.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
bool paragraph(const XmlElement &e) {
  return e.namespaceURI() == TEXT &&
         (e.localName() == "p" || e.localName() == "h");
}
struct Block {
  XmlElement node;
  Text text;
  size_t start;
};
std::vector<Block> blocks(const XmlElement &parent) {
  std::vector<Block> result;
  size_t offset = 0;
  std::function<void(XmlElement)> walk = [&](XmlElement e) {
    for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        if (paragraph(n)) {
          auto value = plain_text(n);
          result.push_back({n, value, offset});
          offset += value.size() + 1;
        } else
          walk(n);
      }
  };
  walk(parent);
  return result;
}
void supported(const XmlElement &e) {
  for (auto n = e.firstChild(); !n.isNull(); n = n.nextSibling())
    if (n.isElement()) {
      if (n.namespaceURI() != TEXT ||
          !TextList{"span", "a", "s", "tab", "line-break"}.contains(
              n.localName()))
        fail("This paragraph contains an embedded object that cannot be edited "
             "yet");
      supported(n);
    }
}
void strip_ids(XmlElement node) {
  node.removeAttributeNS(XML, "id");
  for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
    if (n.isElement())
      strip_ids(n);
}
bool same_run(XmlElement a, XmlElement b) {
  if (!a.isElement() || !b.isElement() || a.namespaceURI() != TEXT ||
      b.namespaceURI() != TEXT || a.localName() != b.localName() ||
      (a.localName() != "span" && a.localName() != "a"))
    return false;
  auto aid = a.attributeNS(XML, "id"), bid = b.attributeNS(XML, "id");
  if (!aid.empty() && !bid.empty() && aid != bid)
    return false;
  auto attributes = [](XmlElement e) {
    std::map<std::pair<Text, Text>, Text> result;
    for (const auto &a : e.attributes()) {
      auto local = a.name.mid(a.name.indexOf(':') + 1);
      if (a.uri != XML || local != "id")
        result[{a.uri, local}] = a.value;
    }
    return result;
  };
  return attributes(a) == attributes(b);
}
// Ordinary typing must not add a new span for every key. Retain distinct
// identities, but coalesce adjacent equivalent runs and text fragments.
void compact(XmlElement parent) {
  for (auto n = parent.firstChild(); !n.isNull();) {
    auto next = n.nextSibling();
    if (n.isText() && next.isText()) {
      n.setNodeValue(n.nodeValue() + next.nodeValue());
      parent.removeChild(next);
      continue;
    }
    if (same_run(n, next)) {
      if (!n.hasAttributeNS(XML, "id") && next.hasAttributeNS(XML, "id"))
        n.setAttributeNS(XML, "xml:id", next.attributeNS(XML, "id"));
      while (!next.firstChild().isNull())
        n.appendChild(next.firstChild());
      parent.removeChild(next);
      continue;
    }
    if (n.isElement())
      compact(n);
    n = next;
  }
}
// Clone just the selected inline subtree. Whitespace controls remain ODF
// elements, and span/link attributes survive on both sides of a text split.
void slice(XmlElement source, XmlElement target, size_t begin, size_t end,
           bool tail = false) {
  size_t offset = 0;
  for (auto n = source.firstChild(); !n.isNull(); n = n.nextSibling()) {
    Text value;
    bool control = n.isElement() && n.namespaceURI() == TEXT &&
                   (n.localName() == "s" || n.localName() == "tab" ||
                    n.localName() == "line-break");
    if (n.isText() || n.isCDATASection())
      value = n.nodeValue();
    else if (n.isElement()) {
      auto holder = source.ownerDocument().createElement("holder");
      holder.appendChild(n.cloneNode(true));
      value = plain_text(holder);
    } else {
      if (offset >= begin && (offset < end || (tail && offset == end)))
        target.appendChild(n.cloneNode(true));
      continue;
    }
    if (value.empty() && n.isElement()) {
      if (offset >= begin && (offset < end || (tail && offset == end)))
        target.appendChild(n.cloneNode(true));
      continue;
    }
    size_t next = offset + value.size();
    size_t first = std::max(begin, offset), last = std::min(end, next);
    if (first < last) {
      if (n.isText() || n.isCDATASection())
        target.appendChild(source.ownerDocument().createTextNode(
            value.mid(first - offset, last - first)));
      else if (control) {
        auto copy = n.cloneNode(true);
        if (n.localName() == "s")
          copy.setAttributeNS(TEXT, "text:c", last - first);
        target.appendChild(copy);
      } else {
        auto copy = n.cloneNode(false);
        slice(n, copy, first - offset, last - offset, tail && last == end);
        target.appendChild(copy);
      }
    }
    offset = next;
  }
}
std::vector<XmlElement> insertion_path(XmlElement source, size_t position) {
  std::vector<XmlElement> result;
  size_t offset = 0;
  for (auto n = source.firstChild(); !n.isNull(); n = n.nextSibling()) {
    if (!n.isElement() && !n.isText() && !n.isCDATASection())
      continue;
    size_t length;
    if (n.isText() || n.isCDATASection())
      length = n.nodeValue().size();
    else {
      auto holder = source.ownerDocument().createElement("holder");
      holder.appendChild(n.cloneNode(true));
      length = plain_text(holder).size();
    }
    if (position <= offset + length) {
      if (n.isElement() && n.namespaceURI() == TEXT &&
          (n.localName() == "span" || n.localName() == "a")) {
        result.push_back(n);
        auto inner = insertion_path(n, position - offset);
        result.insert(result.end(), inner.begin(), inner.end());
      }
      return result;
    }
    offset += length;
  }
  return result;
}
void insert(XmlElement target, const Text &value,
            const std::vector<XmlElement> &path) {
  if (value.empty())
    return;
  for (const auto &node : path) {
    auto copy = node.cloneNode(false);
    strip_ids(copy);
    target.appendChild(copy);
    target = copy;
  }
  auto holder = target.ownerDocument().createElement("holder");
  set_plain_text(holder, value);
  while (!holder.firstChild().isNull())
    target.appendChild(holder.firstChild());
}
void copy_attributes(
    XmlElement from, XmlElement to,
    const std::function<bool(const XmlNode::Attribute &)> &allow) {
  for (const auto &a : from.attributes())
    if (allow(a)) {
      if (a.uri.empty())
        to.setAttribute(a.name, a.value);
      else
        to.setAttributeNS(a.uri, a.name, a.value);
    }
}
// A split block must not inherit BreakBefore, MasterPageName or list/outline
// overrides. Copy the style chain without its master assignments. Keep each
// inheritance level: flattening changes parent-relative sizes and properties.
Text continuation_style(OdfPackage &package, const XmlElement &source) {
  std::map<Text, XmlElement> styles;
  auto scan = [&](const XmlDocument &xml) {
    for (auto e : xml.documentElement().elementsByTagNameNS(STYLE, "style"))
      if (e.attributeNS(STYLE, "family") == "paragraph")
        styles[e.attributeNS(STYLE, "name")] = e;
  };
  auto bytes = package.entry("styles.xml");
  XmlDocument external;
  if (!bytes.empty()) {
    external = read_document_xml(bytes);
    scan(external);
  }
  scan(package.xml);
  auto name = source.attributeNS(TEXT, "style-name");
  if (name.empty() || !styles.contains(name))
    return name;
  std::vector<XmlElement> chain;
  std::set<Text> seen;
  for (auto current = name; !current.empty() && styles.contains(current);
       current = styles[current].attributeNS(STYLE, "parent-style-name")) {
    if (!seen.insert(current).second || chain.size() >= 64)
      fail("Cyclic paragraph style cannot be split");
    chain.push_back(styles[current]);
  }
  // The nearest declared next style is used; otherwise continue this style.
  for (const auto &style : chain) {
    auto next = style.attributeNS(STYLE, "next-style-name");
    if (!next.empty()) {
      if (next != name && styles.contains(next))
        return next;
      break;
    }
  }
  bool needs_copy = false;
  std::optional<Text> break_before;
  for (auto style : chain) {
    needs_copy =
        needs_copy || !style.attributeNS(STYLE, "master-page-name").empty();
    for (auto p : children(style, STYLE, "paragraph-properties"))
      if (!break_before && p.hasAttributeNS(FO, "break-before"))
        break_before = p.attributeNS(FO, "break-before");
  }
  needs_copy = needs_copy || (break_before && !break_before->empty() &&
                              *break_before != "auto");
  if (!needs_copy)
    return name;
  for (const auto &style : chain)
    for (auto node = style.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement() && (node.namespaceURI() != STYLE ||
                               !node.localName().endsWith("-properties")))
        fail("Splitting this paragraph requires unsupported conditional style "
             "rules");
  auto root = package.xml.documentElement();
  auto automatic = child(root, OFFICE, "automatic-styles");
  if (automatic.isNull()) {
    automatic = package.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(automatic, child(root, OFFICE, "body"));
  }
  size_t number = 1;
  Text generated, previous;
  XmlElement output;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    do {
      generated = "WordsSplit" + Text::number(number++);
    } while (styles.contains(generated));
    output = package.xml.importNode(*it, true);
    strip_ids(output);
    output.setAttributeNS(STYLE, "style:name", generated);
    output.removeAttributeNS(STYLE, "master-page-name");
    output.removeAttributeNS(STYLE, "next-style-name");
    if (!previous.empty())
      output.setAttributeNS(STYLE, "style:parent-style-name", previous);
    automatic.appendChild(output);
    previous = generated;
  }
  auto props = child(output, STYLE, "paragraph-properties");
  if (props.isNull()) {
    props = package.xml.createElementNS(STYLE, "style:paragraph-properties");
    output.appendChild(props);
  }
  props.setAttributeNS(FO, "fo:break-before", "auto");
  return generated;
}
} // namespace
void replace_words_text(OdfPackage &package, size_t begin, size_t count,
                        const Text &inserted, bool paragraph_breaks) {
  auto all = blocks(package.body());
  if (all.empty())
    fail("This document has no editable paragraph");
  size_t size = all.back().start + all.back().text.size();
  if (begin > size || count > size - begin ||
      inserted.size() > 16 * 1024 * 1024 ||
      size - count > 16 * 1024 * 1024 - inserted.size())
    fail("Text edit is outside the supported document range");
  if (!count && inserted.empty())
    return;
  auto locate = [&](size_t position) {
    for (size_t i = 0; i < all.size(); ++i)
      if (position <= all[i].start + all[i].text.size())
        return i;
    return all.size() - 1;
  };
  size_t first = locate(begin), last = locate(begin + count);
  auto parent = all[first].node.parentNode();
  for (size_t i = first; i <= last; ++i) {
    supported(all[i].node);
    if (all[i].node.parentNode() != parent)
      fail("Paragraph joins cannot cross table cells, list items or frames");
  }
  for (auto n = all[first].node.nextSibling();
       first != last && !n.isNull() && n != all[last].node; n = n.nextSibling())
    if (n.isElement() && !paragraph(n))
      fail("Paragraph joins cannot remove intervening document structures");
  bool splitting = paragraph_breaks && inserted.contains('\n');
  if (splitting && parent.namespaceURI() == TEXT &&
      (parent.localName() == "list-item" ||
       parent.localName() == "list-header"))
    fail("Creating a new list item requires the structural list editor");
  TextList parts = splitting ? inserted.split('\n') : TextList{inserted};
  auto path = insertion_path(all[first].node, begin - all[first].start);
  std::vector<XmlElement> replacements;
  Text next_style =
      parts.size() > 1 ? continuation_style(package, all[first].node) : Text();
  for (size_t i = 0; i < parts.size(); ++i) {
    auto p = i == 0 ? all[first].node.cloneNode(false)
                    : package.xml.createElementNS(TEXT, "text:p");
    if (i) {
      copy_attributes(all[first].node, p, [&](const XmlNode::Attribute &a) {
        auto local = a.name.mid(a.name.indexOf(':') + 1);
        return !(a.uri == XML && local == "id") &&
               !(a.uri == TEXT &&
                 (local == "outline-level" || local == "start-value" ||
                  local == "restart-numbering" || local == "style-name"));
      });
      if (!next_style.empty())
        p.setAttributeNS(TEXT, "text:style-name", next_style);
    }
    if (i == 0)
      slice(all[first].node, p, 0, begin - all[first].start);
    insert(p, parts[i], path);
    if (i + 1 == parts.size())
      slice(all[last].node, p, begin + count - all[last].start,
            all[last].text.size(), true);
    replacements.push_back(p);
  }
  std::set<Text> ids;
  std::function<void(XmlElement)> unique_ids = [&](XmlElement node) {
    auto id = node.attributeNS(XML, "id");
    if (!id.empty() && !ids.insert(id).second)
      node.removeAttributeNS(XML, "id");
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement())
        unique_ids(n);
  };
  for (auto p : replacements)
    unique_ids(p);
  for (auto p : replacements)
    compact(p);
  for (auto p : replacements)
    parent.insertBefore(p, all[first].node);
  for (size_t i = first; i <= last; ++i)
    parent.removeChild(all[i].node);
}
} // namespace DomeOffice
