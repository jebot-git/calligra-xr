// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_edit.h"
#include "native_text_layout.h"
#include "native_words_format.h"
#include "native_words_tables.h"
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
      if (n.namespaceURI() == TEXT &&
          TextList{"page-number", "page-count"}.contains(n.localName()))
        continue;
      if (n.namespaceURI() != TEXT ||
          !TextList{"span", "a", "s", "tab", "line-break", "meta"}.contains(
              n.localName()))
        fail("This paragraph contains an embedded object that cannot be edited "
             "yet");
      if (n.localName() == "meta" && !plain_text(n).empty())
        fail("Editing text-bearing metadata requires the rich metadata editor");
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
        if (n.namespaceURI() == TEXT &&
            TextList{"page-number", "page-count"}.contains(n.localName())) {
          if (first != offset || last != next)
            fail(
                "Page fields are atomic; select the whole field to replace it");
          target.appendChild(n.cloneNode(true));
          offset = next;
          continue;
        }
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
            const std::vector<XmlElement> &path, bool retain_empty = false) {
  if (value.empty() && !retain_empty)
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
  std::map<Text, XmlElement> styles, common;
  auto scan = [&](const XmlDocument &xml) {
    for (auto e : xml.documentElement().elementsByTagNameNS(STYLE, "style"))
      if (e.attributeNS(STYLE, "family") == "paragraph") {
        styles[e.attributeNS(STYLE, "name")] = e;
        if (e.parentNode().namespaceURI() == OFFICE &&
            e.parentNode().localName() == "styles")
          common[e.attributeNS(STYLE, "name")] = e;
      }
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
  for (auto current = styles[name]; !current.isNull();) {
    if (std::find(chain.begin(), chain.end(), current) != chain.end() ||
        chain.size() >= 64)
      fail("Cyclic paragraph style cannot be split");
    chain.push_back(current);
    auto parent_name = current.attributeNS(STYLE, "parent-style-name");
    current = common.contains(parent_name)   ? common.at(parent_name)
              : styles.contains(parent_name) ? styles.at(parent_name)
                                             : XmlElement{};
  }
  // The nearest declared next style is used; otherwise continue this style.
  for (const auto &style : chain) {
    auto next = style.attributeNS(STYLE, "next-style-name");
    if (!next.empty()) {
      if (next != name && common.contains(next))
        return words_paragraph_style_reference(package, next);
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
bool text_element(XmlElement node, const Text &name) {
  return node.isElement() && node.namespaceURI() == TEXT &&
         node.localName() == name;
}
Text default_list_style(OdfPackage &package) {
  auto root = package.xml.documentElement();
  std::set<Text> names;
  for (auto style : root.elementsByTagNameNS(TEXT, "list-style"))
    names.insert(style.attributeNS(STYLE, "name"));
  auto bytes = package.entry("styles.xml");
  if (!bytes.empty()) {
    auto styles = read_document_xml(bytes);
    for (auto style : styles.elementsByTagNameNS(TEXT, "list-style"))
      names.insert(style.attributeNS(STYLE, "name"));
  }
  size_t number = 1;
  Text name;
  do {
    name = "WordsListDefault" + Text::number(number++);
  } while (names.contains(name));
  auto automatic = child(root, OFFICE, "automatic-styles");
  if (automatic.isNull()) {
    automatic = package.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(automatic, child(root, OFFICE, "body"));
  }
  auto style = package.xml.createElementNS(TEXT, "text:list-style");
  style.setAttributeNS(STYLE, "style:name", name);
  auto level =
      package.xml.createElementNS(TEXT, "text:list-level-style-number");
  level.setAttributeNS(TEXT, "text:level", "1");
  level.setAttributeNS(STYLE, "style:num-format", "1");
  level.setAttributeNS(STYLE, "style:num-suffix", ".");
  auto props =
      package.xml.createElementNS(STYLE, "style:list-level-properties");
  props.setAttributeNS(TEXT, "text:space-before", "0pt");
  props.setAttributeNS(TEXT, "text:min-label-width", "18pt");
  level.appendChild(props);
  style.appendChild(level);
  automatic.appendChild(style);
  return name;
}
// Joining adjacent list items retains the first item's identity/numbering and
// the last item's trailing blocks. Do not flatten a nested list or a header
// hidden between the selected source paragraphs.
bool join_items(OdfPackage &package, const std::vector<Block> &all,
                size_t first, size_t last) {
  auto parent = all[first].node.parentNode();
  auto end_parent = all[last].node.parentNode();
  if (parent == end_parent)
    return false;
  auto list = parent.parentNode();
  if (!text_element(parent, "list-item") || !text_element(list, "list") ||
      !text_element(end_parent, "list-item") || end_parent.parentNode() != list)
    fail("Paragraph joins cannot cross table cells, lists or frames");
  std::set<const void *> affected;
  for (size_t i = first; i <= last; ++i) {
    auto item = all[i].node.parentNode();
    if (!text_element(item, "list-item") || item.parentNode() != list)
      fail("Paragraph joins cannot remove nested lists or other structures");
    affected.insert(item.identity());
  }
  std::vector<XmlElement> items;
  bool found_end = false;
  for (auto node = parent; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement())
      continue;
    if (!text_element(node, "list-item") || !affected.contains(node.identity()))
      fail("Paragraph joins cannot remove a list header or an empty structure");
    items.push_back(node);
    if (node == end_parent) {
      found_end = true;
      break;
    }
  }
  if (!found_end)
    fail("List items are not in document order");
  bool selected = false, finished = false;
  for (auto item : items)
    for (auto node = item.firstChild(); !node.isNull();
         node = node.nextSibling()) {
      if (node == all[first].node)
        selected = true;
      if (selected && !finished && node.isElement() && !paragraph(node) &&
          !text_element(node, "number"))
        fail("Paragraph joins cannot remove nested lists or other structures");
      if (node == all[last].node)
        finished = true;
    }
  auto source_style = read_source_list_style(package, end_parent);
  Text native_default;
  if (source_style != read_source_list_style(package, parent))
    for (auto node = all[last].node.nextSibling(); !node.isNull();
         node = node.nextSibling())
      if (text_element(node, "list") &&
          node.attributeNS(TEXT, "style-name").empty()) {
        // Retain the old item's inherited sub-list style after its blocks move.
        // An unnamed native default needs an explicit style in its new scope.
        auto style = read_source_list_style(package, node);
        if (style.empty()) {
          if (native_default.empty())
            native_default = default_list_style(package);
          style = native_default;
        }
        node.setAttributeNS(TEXT, "text:style-name", style);
      }
  for (size_t i = 1; i < items.size(); ++i) {
    auto item = items[i];
    while (!item.firstChild().isNull()) {
      auto node = item.firstChild();
      // text:number is a producer's cached label, not editable document text.
      if (text_element(node, "number"))
        item.removeChild(node);
      else
        parent.appendChild(node);
    }
    list.removeChild(item);
  }
  return true;
}
// Each inserted paragraph break starts a sibling item. Unselected following
// paragraphs/sub-lists travel with the final suffix, preserving source order.
void split_items(XmlElement item, const std::vector<XmlElement> &paragraphs) {
  if (paragraphs.size() < 2)
    return;
  auto list = item.parentNode();
  if (!text_element(list, "list"))
    fail("List item has no containing list");
  auto tail = paragraphs.back().nextSibling();
  auto previous = item;
  for (size_t i = 1; i < paragraphs.size(); ++i) {
    auto next = item.ownerDocument().createElementNS(TEXT, "text:list-item");
    copy_attributes(item, next, [](const XmlNode::Attribute &a) {
      auto local = a.name.mid(a.name.indexOf(':') + 1);
      return !(a.uri == XML && local == "id") &&
             !(a.uri == TEXT && local == "start-value");
    });
    next.appendChild(paragraphs[i]);
    list.insertAfter(next, previous);
    previous = next;
  }
  while (!tail.isNull()) {
    auto next = tail.nextSibling();
    previous.appendChild(tail);
    tail = next;
  }
}
XmlElement previous_element(XmlElement node) {
  do {
    node = node.previousSibling();
  } while (!node.isNull() && !node.isElement());
  return node;
}
XmlElement next_element(XmlElement node) {
  do {
    node = node.nextSibling();
  } while (!node.isNull() && !node.isElement());
  return node;
}
Text fresh_list_id(OdfPackage &package) {
  std::set<Text> used;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    used.insert(node.attributeNS(XML, "id"));
    for (auto child = node.firstChild(); !child.isNull();
         child = child.nextSibling())
      if (child.isElement())
        walk(child);
  };
  walk(package.xml.documentElement());
  size_t number = 1;
  Text id;
  do {
    id = "WordsList" + Text::number(number++);
  } while (used.contains(id));
  return id;
}
Text pin_list_style(OdfPackage &package, XmlElement list) {
  auto style = read_source_list_style(package, list);
  if (style.empty())
    style = default_list_style(package);
  list.setAttributeNS(TEXT, "text:style-name", style);
  return style;
}
// A tail is a continuation of its prefix, while later references to the
// original whole list must now continue its tail. References inside the
// prefix/between fragments still resolve against the original source anchor.
XmlElement list_tail(OdfPackage &package, XmlElement list, XmlElement after) {
  if (next_element(after).isNull())
    return {};
  pin_list_style(package, list);
  auto tail = list.cloneNode(false);
  tail.removeAttributeNS(XML, "id");
  auto id = list.attributeNS(XML, "id");
  if (id.empty()) {
    id = fresh_list_id(package);
    list.setAttributeNS(XML, "xml:id", id);
  }
  auto tail_id = fresh_list_id(package);
  tail.setAttributeNS(XML, "xml:id", tail_id);
  tail.setAttributeNS(TEXT, "text:continue-list", id);
  tail.removeAttributeNS(TEXT, "continue-numbering");
  auto node = after.nextSibling();
  while (!node.isNull()) {
    auto next = node.nextSibling();
    tail.appendChild(node);
    node = next;
  }
  return tail;
}
void continue_after_tail(OdfPackage &package, XmlElement list,
                         XmlElement tail) {
  if (tail.isNull())
    return;
  auto id = list.attributeNS(XML, "id"), tail_id = tail.attributeNS(XML, "id");
  bool after = false;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    if (after && text_element(node, "list") &&
        node.attributeNS(TEXT, "continue-list") == id)
      node.setAttributeNS(TEXT, "text:continue-list", tail_id);
    if (node == tail)
      after = true;
    for (auto child = node.firstChild(); !child.isNull();
         child = child.nextSibling())
      if (child.isElement())
        walk(child);
  };
  walk(package.xml.documentElement());
}
void indent_items(OdfPackage &package, const std::vector<XmlElement> &items) {
  auto list = items.front().parentNode();
  auto style = pin_list_style(package, list);
  auto parent = previous_element(items.front());
  if (parent.isNull()) {
    // A header provides an unnumbered structural parent when the first item
    // is indented. ODF permits a header containing only a nested list.
    parent = package.xml.createElementNS(TEXT, "text:list-header");
    list.insertBefore(parent, items.front());
  }
  if (!text_element(parent, "list-item") &&
      !text_element(parent, "list-header"))
    fail("List indentation cannot cross an unknown structure");
  auto nested = parent.lastChild();
  while (!nested.isNull() && !nested.isElement())
    nested = nested.previousSibling();
  if (!text_element(nested, "list") ||
      read_source_list_style(package, nested) != style) {
    nested = package.xml.createElementNS(TEXT, "text:list");
    nested.setAttributeNS(TEXT, "text:style-name", style);
    parent.appendChild(nested);
  }
  for (auto item : items)
    nested.appendChild(item);
}
void outdent_items(OdfPackage &package, const std::vector<XmlElement> &items) {
  auto list = items.front().parentNode();
  auto parent = list.parentNode();
  auto outer = parent.parentNode();
  if (!text_element(parent, "list-item") &&
      !text_element(parent, "list-header"))
    return;
  if (!text_element(outer, "list"))
    fail("Nested list has no containing outer list");
  if (!next_element(list).isNull())
    fail("Outdent is not yet supported when the parent item has text after "
         "this sub-list");
  auto style = pin_list_style(package, list);
  auto outer_style = pin_list_style(package, outer);
  auto tail = list_tail(package, list, items.back());
  auto previous = parent;
  for (auto item : items) {
    if (!item.hasAttributeNS(TEXT, "style-override") && style != outer_style)
      item.setAttributeNS(TEXT, "text:style-override", style);
    outer.insertAfter(item, previous);
    previous = item;
  }
  if (!tail.isNull()) {
    auto last = items.back();
    last.appendChild(tail);
    continue_after_tail(package, list, tail);
  }
}
void unlist_empty_item(OdfPackage &package, XmlElement item) {
  auto list = item.parentNode(), parent = list.parentNode();
  pin_list_style(package, list);
  auto tail = list_tail(package, list, item);
  if (!tail.isNull())
    parent.insertAfter(tail, list);
  auto paragraph = child(item, TEXT, "p");
  if (paragraph.isNull())
    paragraph = child(item, TEXT, "h");
  auto id = item.attributeNS(XML, "id");
  if (!id.empty()) {
    if (!paragraph.hasAttributeNS(XML, "id"))
      paragraph.setAttributeNS(XML, "xml:id", id);
    else if (paragraph.attributeNS(XML, "id") != id) {
      auto anchor = package.xml.createElementNS(TEXT, "text:meta");
      anchor.setAttributeNS(XML, "xml:id", id);
      paragraph.insertBefore(anchor, paragraph.firstChild());
    }
  }
  auto previous = list;
  while (!item.firstChild().isNull()) {
    auto node = item.firstChild();
    if (text_element(node, "number"))
      item.removeChild(node);
    else {
      parent.insertAfter(node, previous);
      previous = node;
    }
  }
  list.removeChild(item);
  continue_after_tail(package, list, tail);
}
} // namespace
bool change_words_list(OdfPackage &package, size_t begin, size_t end,
                       WordsListAction action, bool at_start_only) {
  auto all = blocks(package.body());
  if (all.empty())
    return false;
  auto size = all.back().start + all.back().text.size();
  if (begin > end || end > size)
    fail("List selection is outside the document");
  auto locate = [&](size_t position) {
    for (size_t i = 0; i < all.size(); ++i)
      if (position <= all[i].start + all[i].text.size())
        return i;
    return all.size() - 1;
  };
  auto first = locate(begin), last = locate(end);
  auto source = all[first].node;
  auto item = source.parentNode();
  if (!text_element(item, "list-item") ||
      !text_element(item.parentNode(), "list"))
    return false;
  if (action == WordsListAction::ExitEmpty) {
    if (begin != end || !all[first].text.empty())
      return false;
    supported(source);
    for (auto node = item.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement() && node != source && !text_element(node, "number") &&
          !text_element(node, "soft-page-break"))
        fail("Leaving this list is not yet supported for a continuation "
             "paragraph or an item containing a sub-list");
    // Empty-item exit removes numbering entirely. Until paragraph-level list
    // continuations are available, refuse to reparent unselected nested tails.
    for (auto current = item;
         text_element(current.parentNode().parentNode(), "list-item") ||
         text_element(current.parentNode().parentNode(), "list-header");
         current = current.parentNode().parentNode())
      if (!next_element(current).isNull() ||
          !next_element(current.parentNode()).isNull())
        fail("Leaving a nested list is not yet supported while following "
             "nested items or parent text remain");
    while (text_element(item.parentNode().parentNode(), "list-item") ||
           text_element(item.parentNode().parentNode(), "list-header"))
      outdent_items(package, {item});
    unlist_empty_item(package, item);
    return true;
  }
  if (at_start_only && !((begin == end && begin == all[first].start) ||
                         (begin != end && first != last)))
    return false;
  if (begin != end && last > first && end == all[last].start)
    --last;
  std::set<const void *> selected;
  std::vector<XmlElement> candidates;
  for (size_t i = first; i <= last; ++i) {
    auto node = all[i].node.parentNode();
    if (!text_element(node, "list-item") ||
        !text_element(node.parentNode(), "list"))
      fail("List level selection includes paragraphs outside list items");
    if (selected.insert(node.identity()).second)
      candidates.push_back(node);
  }
  std::vector<XmlElement> top;
  for (auto node : candidates) {
    bool contained = false;
    for (auto parent = node.parentNode(); parent.isElement();
         parent = parent.parentNode())
      contained = contained || selected.contains(parent.identity());
    if (!contained)
      top.push_back(node);
  }
  auto list = top.front().parentNode();
  for (auto node : top)
    if (node.parentNode() != list)
      fail("List level selection crosses unrelated lists or mixed levels");
  std::set<const void *> picked;
  for (auto node : top)
    picked.insert(node.identity());
  std::vector<XmlElement> items;
  bool active = false;
  for (auto node = list.firstChild(); !node.isNull();
       node = node.nextSibling()) {
    if (picked.contains(node.identity()))
      active = true;
    if (active && node.isElement()) {
      if (!text_element(node, "list-item"))
        fail("List level selection contains an unknown structure");
      items.push_back(node);
      if (node == top.back())
        break;
    }
  }
  if (action == WordsListAction::Indent)
    indent_items(package, items);
  else
    outdent_items(package, items);
  return true;
}
bool change_words_lists(OdfPackage &package,
                        const std::vector<std::pair<size_t, size_t>> &ranges,
                        WordsListAction action) {
  if (action == WordsListAction::ExitEmpty)
    fail("Empty-item exit requires one collapsed caret");
  auto all = blocks(package.body());
  if (all.empty() || ranges.empty())
    return false;
  const auto size = all.back().start + all.back().text.size();
  std::set<size_t> selected_paragraphs;
  for (const auto &[begin, end] : ranges) {
    if (begin > end || end > size)
      fail("List selection is outside the document");
    auto first = std::lower_bound(
        all.begin(), all.end(), begin,
        [](const auto &b, size_t at) { return b.start + b.text.size() < at; });
    for (auto it = first; it != all.end() && it->start <= end; ++it) {
      if (it != first && it->start == end)
        break;
      selected_paragraphs.insert(size_t(it - all.begin()));
    }
  }
  std::set<const void *> selected;
  std::vector<XmlElement> candidates;
  bool outside = false;
  for (auto index : selected_paragraphs) {
    auto item = all[index].node.parentNode();
    if (!text_element(item, "list-item") ||
        !text_element(item.parentNode(), "list")) {
      outside = true;
      continue;
    }
    if (selected.insert(item.identity()).second)
      candidates.push_back(item);
  }
  if (candidates.empty())
    return false;
  if (outside)
    fail("List level selection includes paragraphs outside list items");
  // Partition actual sibling runs, rather than taking the scalar envelope of
  // ranges (which can include covered metadata or other unselected content).
  std::vector<std::vector<XmlElement>> groups;
  for (auto item : candidates) {
    bool contained = false;
    for (auto parent = item.parentNode(); parent.isElement();
         parent = parent.parentNode())
      contained = contained || selected.contains(parent.identity());
    if (contained)
      continue;
    if (groups.empty() ||
        groups.back().back().parentNode() != item.parentNode() ||
        previous_element(item) != groups.back().back())
      groups.emplace_back();
    groups.back().push_back(item);
  }
  if (groups.empty())
    return false;
  // Later groups first: outdenting can split a nested tail, and must not move
  // an earlier group's nodes before their turn.
  for (auto it = groups.rbegin(); it != groups.rend(); ++it)
    if (action == WordsListAction::Indent)
      indent_items(package, *it);
    else
      outdent_items(package, *it);
  auto after = blocks(package.body());
  if (after.size() != all.size())
    fail("List level editing changed source paragraphs");
  for (size_t i = 0; i < all.size(); ++i)
    if (all[i].node != after[i].node || all[i].text != after[i].text)
      fail("List level editing changed source text or paragraph order");
  (void)read_text_flow(package);
  return true;
}
void replace_words_text(OdfPackage &package, size_t begin, size_t count,
                        const Text &inserted, bool paragraph_breaks,
                        bool retain_empty_format) {
  replace_scoped_text(package, package.body(), begin, count, inserted,
                      paragraph_breaks, retain_empty_format);
}
void replace_scoped_text(OdfPackage &package, XmlElement scope, size_t begin,
                         size_t count, const Text &inserted,
                         bool paragraph_breaks, bool retain_empty_format) {
  // Document APIs enforce write permission before publishing a candidate.
  // Format inspection also uses this low-level operation on a private clone,
  // including when the original package is signed/read-only.
  if (scope.isNull() ||
      scope.ownerDocument().documentElement() !=
          package.xml.documentElement())
    fail("This text container cannot be edited");
  auto all = blocks(scope);
  size_t size = all.empty() ? 0 : all.back().start + all.back().text.size();
  if (begin > size || count > size - begin ||
      inserted.size() > 16 * 1024 * 1024 ||
      size - count > 16 * 1024 * 1024 - inserted.size())
    fail("Text edit is outside the supported document range");
  if (!count && inserted.empty())
    return;
  if (all.empty()) {
    if (scope.isNull())
      fail("This document has no text body");
    auto paragraph = package.xml.createElementNS(TEXT, "text:p");
    scope.appendChild(paragraph);
    all = blocks(scope);
  }
  auto locate = [&](size_t position) {
    for (size_t i = 0; i < all.size(); ++i)
      if (position <= all[i].start + all[i].text.size())
        return i;
    return all.size() - 1;
  };
  size_t first = locate(begin), last = locate(begin + count);
  for (size_t i = first; i <= last; ++i)
    supported(all[i].node);
  std::vector<XmlElement> string_caches;
  std::set<const void *> cache_ids;
  for (size_t i = first; i <= last; ++i)
    for (auto parent = all[i].node.parentNode(); parent.isElement();
         parent = parent.parentNode())
      if (parent.namespaceURI() == TABLE &&
          parent.localName() == "table-cell" &&
          cache_ids.insert(parent.identity()).second)
        string_caches.push_back(parent);
  bool changed_list = join_items(package, all, first, last);
  auto parent = all[first].node.parentNode();
  for (size_t i = first; i <= last; ++i) {
    if (all[i].node.parentNode() != parent)
      fail("Paragraph joins cannot cross table cells, list items or frames");
  }
  for (auto n = all[first].node.nextSibling();
       first != last && !n.isNull() && n != all[last].node; n = n.nextSibling())
    if (n.isElement() && !paragraph(n))
      fail("Paragraph joins cannot remove intervening document structures");
  bool splitting = paragraph_breaks && inserted.contains('\n');
  TextList parts = splitting ? inserted.split('\n') : TextList{inserted};
  auto path = insertion_path(all[first].node, begin - all[first].start);
  std::vector<XmlElement> replacements;
  Text next_style =
      parts.size() > 1 ? continuation_style(package, all[first].node) : Text();
  auto next_outline = next_style.empty()
                          ? std::optional<int>{}
                          : words_paragraph_style_outline(package, next_style);
  for (size_t i = 0; i < parts.size(); ++i) {
    auto p =
        i == 0 ? all[first].node.cloneNode(false)
               : package.xml.createElementNS(
                     TEXT, next_outline && *next_outline ? "text:h" : "text:p");
    if (i) {
      copy_attributes(all[first].node, p, [&](const XmlNode::Attribute &a) {
        auto local = a.name.mid(a.name.indexOf(':') + 1);
        return !(a.uri == XML && local == "id") &&
               !(a.uri == TEXT &&
                 (local == "outline-level" || local == "start-value" ||
                  local == "restart-numbering" || local == "is-list-header" ||
                  local == "style-name"));
      });
      if (!next_style.empty())
        p.setAttributeNS(TEXT, "text:style-name", next_style);
      if (next_outline && *next_outline)
        p.setAttributeNS(TEXT, "text:outline-level", *next_outline);
    }
    if (i == 0)
      slice(all[first].node, p, 0, begin - all[first].start);
    insert(p, parts[i], path, retain_empty_format && i == 0);
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
  for (auto cell : string_caches)
    cell.removeAttributeNS(OFFICE, "string-value");
  if (text_element(parent, "list-item") ||
      text_element(parent, "list-header")) {
    split_items(parent, replacements);
    // A new item can overflow a continued/consecutive counter elsewhere in
    // the document. Validate the candidate before TextDocument commits it.
    if (changed_list || replacements.size() > 1) {
      if (package.kind() == Kind::Words)
        (void)read_text_flow(package);
      else
        (void)OdfTextReader(package).frame(scope, {});
    }
  }
}
void insert_words_page_field(OdfPackage &package, size_t begin, size_t count,
                             bool page_count) {
  if (package.kind() != Kind::Words || package.read_only())
    fail("This document cannot be edited");
  const Text marker(1, char32_t(0xfffc));
  replace_words_text(package, begin, count, marker, true);
  auto all = blocks(package.body());
  for (const auto &block : all)
    if (begin >= block.start && begin < block.start + block.text.size()) {
      size_t position = begin - block.start;
      std::function<bool(XmlElement, size_t)> replace = [&](XmlElement parent,
                                                            size_t offset) {
        for (auto n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
          auto holder = package.xml.createElement("holder");
          holder.appendChild(n.cloneNode(true));
          auto value = plain_text(holder);
          if (offset >= value.size()) {
            offset -= value.size();
            continue;
          }
          if (n.isText() || n.isCDATASection()) {
            if (value[offset] != marker[0])
              fail("Page field insertion lost its source marker");
            if (offset)
              parent.insertBefore(
                  package.xml.createTextNode(value.left(offset)), n);
            auto field = package.xml.createElementNS(
                TEXT, page_count ? "text:page-count" : "text:page-number");
            if (!page_count)
              field.setAttributeNS(TEXT, "text:select-page", "current");
            set_plain_text(field, "1");
            parent.insertBefore(field, n);
            if (offset + 1 < value.size())
              parent.insertBefore(
                  package.xml.createTextNode(value.mid(offset + 1)), n);
            parent.removeChild(n);
            return true;
          }
          return replace(n, offset);
        }
        return false;
      };
      if (replace(block.node, position))
        return;
    }
  fail("Page field insertion has no editable source");
}
WordsRangeClear clear_words_selection(const OdfPackage &original,
                                      const WordsTextPoint &anchor,
                                      const WordsTextPoint &cursor) {
  auto prepared = materialize_words_selection(original, anchor, cursor);
  auto baseline = prepared.document.xml.toByteArray();
  auto all = blocks(prepared.document.body());
  auto cell = [](XmlElement node) {
    for (auto parent = node.parentNode(); parent.isElement();
         parent = parent.parentNode())
      if (parent.namespaceURI() == TABLE && parent.localName() == "table-cell")
        return parent;
    return XmlElement{};
  };
  auto element_after = [](XmlElement node) {
    do {
      node = node.nextSibling();
    } while (!node.isNull() && !node.isElement());
    return node;
  };
  auto joinable = [&](const Block &a, const Block &b) {
    if (cell(a.node) != cell(b.node))
      return false;
    auto pa = a.node.parentNode(), pb = b.node.parentNode();
    if (pa == pb)
      return element_after(a.node) == b.node;
    if (!text_element(pa, "list-item") || !text_element(pb, "list-item") ||
        pa.parentNode() != pb.parentNode() || element_after(pa) != pb)
      return false;
    // Do not cross a nested list, frame, section or unknown tail/header.
    for (auto node = a.node.nextSibling(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement() && !text_element(node, "number"))
        return false;
    for (auto node = pb.firstChild(); node != b.node; node = node.nextSibling())
      if (node.isNull() || (node.isElement() && !text_element(node, "number")))
        return false;
    return true;
  };
  struct Group {
    size_t begin, end, last;
  };
  std::vector<Group> groups;
  std::set<const void *> checked;
  for (const auto &[begin, end] : prepared.ranges) {
    auto at = std::find_if(all.begin(), all.end(), [&](const auto &b) {
      return begin >= b.start && end <= b.start + b.text.size();
    });
    if (at == all.end())
      fail("The displayed edit range is not a source paragraph");
    for (auto parent = at->node.parentNode(); parent.isElement();
         parent = parent.parentNode())
      if (parent.namespaceURI() == TABLE &&
          parent.localName() == "table-cell" &&
          checked.insert(parent.identity()).second) {
        for (auto attribute : parent.attributes()) {
          auto name = attribute.name.mid(attribute.name.indexOf(':') + 1);
          if ((attribute.uri == TABLE &&
               TextList{"formula", "number-matrix-rows-spanned",
                        "number-matrix-columns-spanned"}
                   .contains(name)) ||
              (attribute.uri == OFFICE && name != "string-value" &&
               (name != "value-type" || attribute.value != "string")))
            fail("These cells require value/formula editing before text "
                 "replacement");
        }
      }
    auto index = size_t(at - all.begin());
    if (!groups.empty() && index == groups.back().last + 1 &&
        groups.back().end == all[groups.back().last].start +
                                 all[groups.back().last].text.size() &&
        begin == at->start && joinable(all[groups.back().last], *at)) {
      groups.back().end = end;
      groups.back().last = index;
    } else
      groups.push_back({begin, end, index});
  }
  if (groups.empty())
    fail("The displayed edit range has no editable paragraphs");
  // Reverse source order keeps every earlier source offset valid.
  for (size_t i = groups.size(); i-- > 0;) {
    const auto &group = groups[i];
    replace_words_text(prepared.document, group.begin, group.end - group.begin,
                       {}, true, i == 0);
  }
  (void)read_text_flow(prepared.document);
  auto position = groups.front().begin;
  bool changed = prepared.document.xml.toByteArray() != baseline;
  auto path = words_table_cells_at(prepared.document, position);
  return {std::move(prepared.document), {position, std::move(path)}, changed};
}

} // namespace DomeOffice
