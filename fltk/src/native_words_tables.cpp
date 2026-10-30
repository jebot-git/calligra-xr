// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_tables.h"
#include "native_words_edit.h"
#include "native_text_layout.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
struct Paragraph {
  XmlElement node;
  size_t start = 0, size = 0;
};
std::vector<Paragraph> paragraphs(XmlElement body) {
  std::vector<Paragraph> result;
  size_t start = 0;
  std::function<void(XmlElement)> walk = [&](XmlElement node) {
    for (auto e = node.firstChild(); !e.isNull(); e = e.nextSibling())
      if (e.isElement()) {
        if (e.namespaceURI() == TEXT &&
            (e.localName() == "p" || e.localName() == "h")) {
          auto size = plain_text(e).size();
          result.push_back({e, start, size});
          start += size + 1;
        } else
          walk(e);
      }
  };
  walk(body);
  return result;
}
Paragraph locate(const OdfPackage &p, size_t position) {
  auto list = paragraphs(p.body());
  auto size = list.empty() ? 0 : list.back().start + list.back().size;
  if (position > size)
    fail("Table edit is outside the document");
  for (const auto &entry : list)
    if (position <= entry.start + entry.size)
      return entry;
  return {};
}
XmlElement ancestor_table(XmlElement node) {
  for (auto parent = node.parentNode(); parent.isElement();
       parent = parent.parentNode())
    if (parent.namespaceURI() == TABLE && parent.localName() == "table")
      return parent;
  return {};
}
void editable(const OdfPackage &p) {
  if (p.kind() != Kind::Words || p.read_only())
    fail("This document cannot be edited");
}
OdfPackage copy(const OdfPackage &p) {
  auto result = p;
  result.xml = read_document_xml(p.xml.toByteArray());
  return result;
}
Text unused_name(const OdfPackage &p, const Text &ns, const Text &tag,
                 const Text &attribute, const Text &prefix) {
  std::set<Text> names;
  for (auto e : p.xml.elementsByTagNameNS(ns, tag))
    names.insert(e.attributeNS(ns, attribute));
  if (ns == STYLE && !p.entry("styles.xml").empty())
    for (auto e :
         read_document_xml(p.entry("styles.xml")).elementsByTagNameNS(ns, tag))
      names.insert(e.attributeNS(ns, attribute));
  size_t n = 1;
  while (names.contains(prefix + Text::number(n)))
    ++n;
  return prefix + Text::number(n);
}
XmlElement automatic(OdfPackage &p) {
  auto root = p.xml.documentElement();
  auto group = child(root, OFFICE, "automatic-styles");
  if (group.isNull()) {
    group = p.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(group, child(root, OFFICE, "body"));
  }
  return group;
}
Text cell_style(OdfPackage &p) {
  auto group = automatic(p);
  for (auto style : children(group, STYLE, "style")) {
    if (style.attributeNS(STYLE, "family") != "table-cell" ||
        style.hasAttributeNS(STYLE, "parent-style-name"))
      continue;
    auto props = child(style, STYLE, "table-cell-properties");
    if (children(style, STYLE).size() == 1 && props.attributes().size() == 2 &&
        props.attributeNS(FO, "border") == "0.5pt solid #808080" &&
        props.attributeNS(FO, "padding") == "2pt")
      return style.attributeNS(STYLE, "name");
  }
  auto name = unused_name(p, STYLE, "style", "name", "WordsTableCell");
  auto style = p.xml.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", name);
  style.setAttributeNS(STYLE, "style:family", "table-cell");
  auto props = p.xml.createElementNS(STYLE, "style:table-cell-properties");
  props.setAttributeNS(FO, "fo:border", "0.5pt solid #808080");
  props.setAttributeNS(FO, "fo:padding", "2pt");
  style.appendChild(props);
  group.appendChild(style);
  return name;
}
// The original paragraph at an empty prefix becomes the first cell paragraph,
// so IDs and paragraph metadata survive without an extra blank line. Its page
// transition must belong to the table; cell paragraphs do not drive masters.
void table_page_style(OdfPackage &p, XmlElement table,
                      const TextParagraph &source) {
  if (source.master.empty() && !source.break_before)
    return;
  auto name = unused_name(p, STYLE, "style", "name", "WordsTablePage");
  auto style = p.xml.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", name);
  style.setAttributeNS(STYLE, "style:family", "table");
  if (!source.master.empty())
    style.setAttributeNS(STYLE, "style:master-page-name", source.master);
  if (source.break_before) {
    auto props = p.xml.createElementNS(STYLE, "style:table-properties");
    props.setAttributeNS(FO, "fo:break-before", "page");
    style.appendChild(props);
  }
  automatic(p).appendChild(style);
  table.setAttributeNS(TABLE, "table:style-name", name);
}
size_t offset_of(const OdfPackage &p, XmlElement paragraph) {
  for (const auto &entry : paragraphs(p.body()))
    if (entry.node == paragraph)
      return entry.start;
  fail("Table edit lost its caret paragraph");
}
} // namespace
bool words_in_table(const OdfPackage &p, size_t position) {
  if (p.kind() != Kind::Words)
    return false;
  return !ancestor_table(locate(p, position).node).isNull();
}
WordsTableEdit insert_words_table(const OdfPackage &original, size_t position,
                                  const WordsTableInsertion &request) {
  editable(original);
  if (!request.rows || !request.columns || request.rows > 100000 ||
      request.columns > 1024 || request.rows > 10000 / request.columns)
    fail("A new table must have 1–10000 cells, with at most 1024 columns");
  auto p = copy(original);
  auto at = locate(p, position);
  if (at.node.isNull()) {
    auto paragraph = p.xml.createElementNS(TEXT, "text:p");
    p.body().appendChild(paragraph);
    at = locate(p, position);
  }
  auto parent = at.node.parentNode();
  // ODF list-item content admits paragraphs/headings/lists but no direct
  // table. Anchored frames or a list-splitting command are needed for that
  // case.
  if (!((parent.namespaceURI() == OFFICE && parent.localName() == "text") ||
        (parent.namespaceURI() == TEXT && parent.localName() == "section") ||
        (parent.namespaceURI() == TABLE && parent.localName() == "table-cell")))
    fail("Insert tables in the document body, a section, or an existing cell");
  // The shared rich split keeps spans, links, Unicode offsets, IDs and next
  // styles.
  replace_words_text(p, position, 0, "\n", true);
  auto left = locate(p, position).node;
  auto right = locate(p, position + 1).node;
  auto table = p.xml.createElementNS(TABLE, "table:table");
  table.setAttributeNS(TABLE, "table:name",
                       unused_name(p, TABLE, "table", "name", "Table"));
  auto style = cell_style(p);
  auto column = p.xml.createElementNS(TABLE, "table:table-column");
  column.setAttributeNS(TABLE, "table:number-columns-repeated",
                        Text::number(request.columns));
  column.setAttributeNS(TABLE, "table:default-cell-style-name", style);
  table.appendChild(column);
  XmlElement first;
  for (size_t r = 0; r < request.rows; ++r) {
    auto row = p.xml.createElementNS(TABLE, "table:table-row");
    for (size_t c = 0; c < request.columns; ++c) {
      auto cell = p.xml.createElementNS(TABLE, "table:table-cell");
      auto paragraph = p.xml.createElementNS(TEXT, "text:p");
      auto paragraph_style = right.attributeNS(TEXT, "style-name");
      if (!paragraph_style.empty())
        paragraph.setAttributeNS(TEXT, "text:style-name", paragraph_style);
      if (first.isNull())
        first = paragraph;
      cell.appendChild(paragraph);
      row.appendChild(cell);
    }
    if (request.header && r == 0) {
      auto header = p.xml.createElementNS(TABLE, "table:table-header-rows");
      header.appendChild(row);
      table.appendChild(header);
    } else
      table.appendChild(row);
  }
  parent.insertBefore(table, right);
  if (plain_text(left).empty()) {
    auto flow = read_text_flow(p);
    auto list = paragraphs(p.body());
    auto found = std::find_if(list.begin(), list.end(),
                              [&](const auto &e) { return e.node == left; });
    if (found == list.end() ||
        size_t(found - list.begin()) >= flow.paragraphs.size())
      fail("Table page source does not match the native model");
    table_page_style(p, table, flow.paragraphs[size_t(found - list.begin())]);
    first.parentNode().insertBefore(left, first);
    first.parentNode().removeChild(first);
    first = left;
  }
  (void)read_text_flow(p);
  auto cursor = offset_of(p, first);
  return {std::move(p), cursor};
}
WordsTableEdit delete_words_table(const OdfPackage &original, size_t position) {
  editable(original);
  auto p = copy(original);
  auto table = ancestor_table(locate(p, position).node);
  if (table.isNull())
    fail("Place the caret inside a table to delete it");
  size_t beginning = position;
  for (const auto &entry : paragraphs(table)) {
    beginning = offset_of(p, entry.node);
    break;
  }
  auto parent = table.parentNode();
  // A root-flow table can own a page transition. Retain it on a caret paragraph
  // at that location, so deleting content does not silently change page setup.
  if (ancestor_table(table).isNull()) {
    auto flow = read_text_flow(p);
    auto model =
        std::find_if(flow.tables.begin(), flow.tables.end(),
                     [&](const auto &value) { return value.source == table; });
    if (model != flow.tables.end() &&
        (!model->master.empty() || model->break_before)) {
      auto paragraph = p.xml.createElementNS(TEXT, "text:p");
      auto name = unused_name(p, STYLE, "style", "name", "WordsTableAnchor");
      auto style = p.xml.createElementNS(STYLE, "style:style");
      style.setAttributeNS(STYLE, "style:name", name);
      style.setAttributeNS(STYLE, "style:family", "paragraph");
      if (!model->master.empty())
        style.setAttributeNS(STYLE, "style:master-page-name", model->master);
      if (model->break_before) {
        auto props = p.xml.createElementNS(STYLE, "style:paragraph-properties");
        props.setAttributeNS(FO, "fo:break-before", "page");
        style.appendChild(props);
      }
      automatic(p).appendChild(style);
      paragraph.setAttributeNS(TEXT, "text:style-name", name);
      parent.insertBefore(paragraph, table);
    }
  }
  parent.removeChild(table);
  // Cells and list items remain editable after deleting their only nested
  // table. The root document likewise retains a caret paragraph when emptied.
  auto content = paragraphs(parent);
  if (content.empty())
    parent.appendChild(p.xml.createElementNS(TEXT, "text:p"));
  (void)read_text_flow(p);
  auto list = paragraphs(p.body());
  auto length = list.empty() ? 0 : list.back().start + list.back().size;
  return {std::move(p), std::min(beginning, length)};
}
} // namespace DomeOffice
