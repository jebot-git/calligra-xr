// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_tables.h"
#include "native_words_selection.h"
#include "native_words_tabs.h"
#include "native_words_geometry.h"
#include "native_table_model.h"
#include <functional>
#include <limits>
#include <numeric>
#include <cmath>
namespace DomeOffice {
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
constexpr size_t missing = std::numeric_limits<size_t>::max();
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
size_t count(XmlElement node, const Text &attribute) {
  bool ok = false;
  auto value = node.attributeNS(TABLE, attribute, "1").toLongLong(&ok);
  if (!ok || value < 1 || value > 1000000)
    fail("Invalid table repetition or span");
  return size_t(value);
}
// ODF lengths do not accept scientific notation. Expand the round-trippable
// decimal rather than rounding tiny intervals created beside existing edges.
Text point_length(double value) {
  if (!std::isfinite(value) || value < 0)
    fail("Invalid physical measure");
  auto raw = Text::number(value, 'g', 17).toUtf8().toStdString();
  auto exponent = raw.find_first_of("eE");
  if (exponent != std::string::npos) {
    int shift = std::stoi(raw.substr(exponent + 1));
    auto digits = raw.substr(0, exponent);
    auto dot = digits.find('.');
    int decimal = dot == std::string::npos ? int(digits.size()) : int(dot);
    if (dot != std::string::npos)
      digits.erase(dot, 1);
    decimal += shift;
    if (decimal <= 0)
      raw = "0." + std::string(size_t(-decimal), '0') + digits;
    else if (size_t(decimal) >= digits.size())
      raw = digits + std::string(size_t(decimal) - digits.size(), '0');
    else
      raw = digits.substr(0, size_t(decimal)) + "." +
            digits.substr(size_t(decimal));
  }
  return Text(raw) + "pt";
}
void set_count(XmlElement node, const Text &attribute, size_t value) {
  if (!value)
    fail("Invalid empty table run");
  if (value == 1)
    node.removeAttributeNS(TABLE, attribute);
  else
    node.setAttributeNS(TABLE, "table:" + attribute, Text::number(value));
}
void walk(XmlElement node, const std::function<void(XmlElement)> &visit) {
  visit(node);
  for (auto e = node.firstChild(); !e.isNull(); e = e.nextSibling())
    if (e.isElement())
      walk(e, visit);
}
bool is_paragraph(XmlElement node) {
  return node.namespaceURI() == TEXT &&
         (node.localName() == "p" || node.localName() == "h");
}
std::vector<XmlElement> paragraphs(XmlElement root) {
  std::vector<XmlElement> result;
  // Match TextDocument's source stream. Embedded note/annotation paragraphs
  // belong to their containing paragraph, not a second editable source range.
  std::function<void(XmlElement)> collect = [&](XmlElement n) {
    if (is_paragraph(n)) {
      result.push_back(n);
      return;
    }
    for (auto child = n.firstChild(); !child.isNull(); child = child.nextSibling())
      if (child.isElement())
        collect(child);
  };
  collect(root);
  return result;
}
size_t offset_of(const OdfPackage &p, XmlElement node) {
  size_t offset = 0;
  for (auto e : paragraphs(p.body())) {
    if (e == node)
      return offset;
    offset += plain_text(e).size() + 1;
  }
  fail("Table caret source is missing");
}
XmlElement ancestor(XmlElement node, const Text &kind) {
  for (auto e = node.parentNode(); e.isElement(); e = e.parentNode())
    if (e.namespaceURI() == TABLE && e.localName() == kind)
      return e;
  return {};
}
bool definition(XmlElement e, const XmlNode::Attribute &a) {
  auto name = a.name.mid(a.name.indexOf(':') + 1), tag = e.localName();
  if (a.uri == XML && name == "id")
    return true;
  if (a.uri == TABLE && name == "name" && tag == "table")
    return true;
  if (a.uri == DRAW && (name == "id" || name == "name"))
    return true;
  if (a.uri == TEXT) {
    if (name == "name" &&
        TextList{"bookmark", "bookmark-start", "bookmark-end", "reference-mark",
                 "reference-mark-start", "reference-mark-end", "section"}
            .contains(tag))
      return true;
    if (name == "id" && tag == "note")
      return true;
    if (name == "ref-name" && tag == "sequence")
      return true;
  }
  return false;
}
class Names {
  std::set<Text> used;
  size_t next = 1;

public:
  explicit Names(const OdfPackage &p) { reserve(p.xml.documentElement()); }
  void reserve(XmlElement root) {
    walk(root, [&](XmlElement e) {
      for (auto a : e.attributes())
        if (definition(e, a))
          used.insert(a.value);
    });
  }
  XmlElement clone(XmlElement source) {
    auto result = source.cloneNode(true);
    std::map<Text, Text> renamed;
    walk(result, [&](XmlElement e) {
      if (e.hasAttributeNS(TABLE, "formula"))
        fail("Repeated formula cells need reference adjustment before this "
             "edit");
      for (auto a : e.attributes())
        if (definition(e, a)) {
          if (!renamed.contains(a.value)) {
            Text name;
            do {
              name = "WordsGridCopy" + Text::number(next++);
            } while (used.contains(name));
            used.insert(name);
            renamed[a.value] = name;
          }
          e.setAttributeNS(a.uri, a.name, renamed.at(a.value));
        }
    });
    walk(result, [&](XmlElement e) {
      for (auto a : e.attributes()) {
        auto name = a.name.mid(a.name.indexOf(':') + 1);
        Text key = a.value;
        bool fragment = a.uri == XLINK && name == "href" && key.startsWith('#');
        bool ref =
            fragment ||
            (a.uri == TEXT &&
             (name == "continue-list" || name == "ref-name")) ||
            (a.uri == DRAW && (name == "start-shape" || name == "end-shape"));
        if (fragment)
          key = key.mid(1);
        if (ref && renamed.contains(key))
          e.setAttributeNS(a.uri, a.name,
                           (fragment ? Text("#") : Text()) + renamed.at(key));
      }
    });
    return result;
  }
};
// Isolate one logical instance. Only the changed run is materialized; large
// untouched repetitions remain compact and retain their original source IDs.
XmlElement isolate(XmlElement source, const Text &attribute, size_t instance,
                   Names &names) {
  auto n = count(source, attribute);
  if (instance >= n)
    fail("Stale repeated table source");
  if (n == 1)
    return source;
  auto parent = source.parentNode();
  if (instance == 0) {
    auto tail = names.clone(source);
    set_count(tail, attribute, n - 1);
    parent.insertAfter(tail, source);
    set_count(source, attribute, 1);
    return source;
  }
  auto target = names.clone(source);
  set_count(target, attribute, 1);
  parent.insertAfter(target, source);
  if (instance + 1 < n) {
    auto tail = names.clone(source);
    set_count(tail, attribute, n - instance - 1);
    parent.insertAfter(tail, target);
  }
  set_count(source, attribute, instance);
  return target;
}
bool cell_node(XmlElement e) {
  return e.namespaceURI() == TABLE && (e.localName() == "table-cell" ||
                                       e.localName() == "covered-table-cell");
}
struct Slot {
  XmlElement node;
  size_t instance = 0;
};
Slot slot(XmlElement row, size_t column) {
  size_t start = 0;
  for (auto e : children(row, TABLE))
    if (cell_node(e)) {
      auto n = count(e, "number-columns-repeated");
      if (column >= start && column - start < n)
        return {e, column - start};
      start += n;
    }
  return {};
}
size_t row_slots(XmlElement row) {
  size_t result = 0;
  for (auto e : children(row, TABLE))
    if (cell_node(e))
      result += count(e, "number-columns-repeated");
  return result;
}
XmlElement blank(OdfPackage &p, const TextTableCell *source = nullptr,
                 bool covered = false) {
  auto node = p.xml.createElementNS(TABLE, covered ? "table:covered-table-cell"
                                                   : "table:table-cell");
  if (!covered) {
    if (source && !source->style_name.empty())
      node.setAttributeNS(TABLE, "table:style-name", source->style_name);
    auto paragraph = p.xml.createElementNS(TEXT, "text:p");
    if (source) {
      auto ps = paragraphs(source->source);
      if (!ps.empty()) {
        auto style = ps[0].attributeNS(TEXT, "style-name");
        if (!style.empty())
          paragraph.setAttributeNS(TEXT, "text:style-name", style);
      }
    }
    node.appendChild(paragraph);
  }
  return node;
}
void insert_slot(OdfPackage &p, XmlElement row, size_t at, XmlElement node,
                 Names &names) {
  auto found = slot(row, at);
  if (!found.node.isNull()) {
    if (found.instance == 0)
      row.insertBefore(node, found.node);
    else {
      auto n = count(found.node, "number-columns-repeated");
      auto tail = names.clone(found.node);
      set_count(found.node, "number-columns-repeated", found.instance);
      set_count(tail, "number-columns-repeated", n - found.instance);
      row.insertAfter(tail, found.node);
      row.insertBefore(node, tail);
    }
  } else {
    auto n = row_slots(row);
    if (n > at)
      fail("Invalid table insertion slot");
    if (n < at) {
      auto filler = blank(p);
      set_count(filler, "number-columns-repeated", at - n);
      row.appendChild(filler);
    }
    row.appendChild(node);
  }
}
void remove_slot(XmlElement row, size_t at) {
  auto found = slot(row, at);
  if (found.node.isNull())
    return;
  auto n = count(found.node, "number-columns-repeated");
  if (n == 1)
    row.removeChild(found.node);
  else
    set_count(found.node, "number-columns-repeated", n - 1);
}
struct Grid {
  const TextTable &model;
  size_t rows, columns;
  std::vector<size_t> owner;
  explicit Grid(const TextTable &m)
      : model(m), rows(m.rows.size()), columns(m.columns.size()),
        owner(rows * columns, missing) {
    for (size_t i = 0; i < m.cells.size(); ++i) {
      const auto &c = m.cells[i];
      for (size_t r = c.row; r < c.row + c.rows; ++r)
        for (size_t col = c.column; col < c.column + c.columns; ++col)
          owner.at(r * columns + col) = i;
    }
  }
  size_t id(size_t row, size_t col) const {
    return row < rows && col < columns ? owner[row * columns + col] : missing;
  }
  const TextTableCell *cell(size_t row, size_t col) const {
    auto i = id(row, col);
    return i == missing ? nullptr : &model.cells[i];
  }
};
std::vector<XmlElement> columns(XmlElement table) {
  std::vector<XmlElement> result;
  std::function<void(XmlElement)> collect = [&](XmlElement n) {
    for (auto e : children(n, TABLE)) {
      if (e.localName() == "table-column")
        result.push_back(e);
      else if (TextList{"table-columns", "table-column-group",
                        "table-header-columns"}
                   .contains(e.localName()))
        collect(e);
    }
  };
  collect(table);
  return result;
}
void complete_columns(OdfPackage &p, XmlElement table, size_t total) {
  size_t explicit_count = 0;
  for (auto e : columns(table))
    explicit_count += count(e, "number-columns-repeated");
  if (explicit_count < total) {
    auto e = p.xml.createElementNS(TABLE, "table:table-column");
    set_count(e, "number-columns-repeated", total - explicit_count);
    XmlElement next;
    for (auto c : children(table, TABLE))
      if (TextList{"table-row", "table-rows", "table-row-group",
                   "table-header-rows"}
              .contains(c.localName())) {
        next = c;
        break;
      }
    table.insertBefore(e, next);
  }
}
Slot column_slot(XmlElement table, size_t column) {
  size_t start = 0;
  for (auto e : columns(table)) {
    auto n = count(e, "number-columns-repeated");
    if (column >= start && column - start < n)
      return {e, column - start};
    start += n;
  }
  return {};
}
void column_definition(OdfPackage &p, const Grid &grid, size_t target,
                       bool insert, bool after, Names &names) {
  auto table = grid.model.source;
  complete_columns(p, table, grid.columns);
  auto ref = column_slot(table, target);
  if (ref.node.isNull())
    fail("Missing table column definition");
  if (!insert) {
    auto n = count(ref.node, "number-columns-repeated");
    if (n > 1)
      set_count(ref.node, "number-columns-repeated", n - 1);
    else
      ref.node.parentNode().removeChild(ref.node);
    return;
  }
  auto old = isolate(ref.node, "number-columns-repeated", ref.instance, names);
  auto added = p.xml.createElementNS(TABLE, "table:table-column");
  for (const auto &a : old.attributes())
    if (a.uri == TABLE &&
        TextList{"style-name", "default-cell-style-name", "visibility"}
            .contains(a.name.mid(a.name.indexOf(':') + 1)))
      added.setAttributeNS(a.uri, a.name, a.value);
  if (after)
    old.parentNode().insertAfter(added, old);
  else
    old.parentNode().insertBefore(added, old);
}
Text neutral_cell_style(OdfPackage &p) {
  auto root = p.xml.documentElement();
  auto group = child(root, OFFICE, "automatic-styles");
  if (group.isNull()) {
    group = p.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(group, child(root, OFFICE, "body"));
  }
  std::set<Text> used;
  for (auto style : p.xml.elementsByTagNameNS(STYLE, "style")) {
    used.insert(style.attributeNS(STYLE, "name"));
    if (style.parentNode() == group &&
        style.attributeNS(STYLE, "family") == "table-cell" &&
        style.attributes().size() == 2 && style.firstChild().isNull())
      return style.attributeNS(STYLE, "name");
  }
  if (!p.entry("styles.xml").empty())
    for (auto style : read_document_xml(p.entry("styles.xml"))
                          .elementsByTagNameNS(STYLE, "style"))
      used.insert(style.attributeNS(STYLE, "name"));
  size_t n = 1;
  Text name;
  do {
    name = "WordsGridNeutral" + Text::number(n++);
  } while (used.contains(name));
  auto style = p.xml.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", name);
  style.setAttributeNS(STYLE, "style:family", "table-cell");
  group.appendChild(style);
  return name;
}
Text inherited_cell_style(const Grid &g, XmlElement row, size_t column) {
  auto style = row.attributeNS(TABLE, "default-cell-style-name");
  if (style.empty() && column < g.model.columns.size())
    style = g.model.columns[column].default_cell_style;
  if (style.empty())
    style = g.model.source.attributeNS(TABLE, "default-cell-style-name");
  return style;
}
void retain_cell_style(OdfPackage &p, XmlElement cell, const Text &old_style,
                       const Text &inherited) {
  if (!cell.attributeNS(TABLE, "style-name").empty() || old_style == inherited)
    return;
  cell.setAttributeNS(TABLE, "table:style-name",
                      old_style.empty() ? neutral_cell_style(p) : old_style);
}
void preserve_covered(XmlElement covered, XmlElement anchor);
void edit_columns(OdfPackage &p, const Grid &g, const TextCellAddress &address,
                  WordsTableGridAction action, Names &names) {
  bool insert = action != WordsTableGridAction::DeleteColumn;
  bool after = action == WordsTableGridAction::ColumnAfter;
  size_t at = address.column + (after ? address.columns : 0);
  if (insert && (g.columns >= 1024 || g.rows > 1000000 / (g.columns + 1)))
    fail("The table exceeds the native column/grid limit");
  std::set<const void *> adjusted, done;
  for (const auto &c : g.model.cells) {
    bool changes =
        insert ? (c.column < at && at < c.column + c.columns)
               : (c.columns > 1 && c.column <= at && at < c.column + c.columns);
    if (changes && adjusted.insert(c.source.identity()).second)
      set_count(c.source, "number-columns-spanned",
                c.columns + (insert ? 1 : -1));
  }
  for (size_t r = 0; r < g.rows; ++r) {
    auto row = g.model.rows[r].source;
    if (!done.insert(row.identity()).second)
      continue;
    if (insert) {
      bool covered = at > 0 && at < g.columns && g.id(r, at - 1) != missing &&
                     g.id(r, at - 1) == g.id(r, at);
      auto source = g.cell(r, std::min(at, g.columns - 1));
      insert_slot(p, row, at, blank(p, source, covered), names);
    } else {
      auto owner = g.cell(r, at);
      size_t remove = at;
      if (owner && owner->row == r && owner->column == at &&
          owner->columns > 1) {
        retain_cell_style(p, owner->source, owner->style_name,
                          inherited_cell_style(g, row, at + 1));
        auto target = slot(row, at + 1);
        if (target.node.isNull() ||
            target.node.localName() != "covered-table-cell")
          fail("Missing horizontal merge coverage");
        auto covered = isolate(target.node, "number-columns-repeated",
                               target.instance, names);
        preserve_covered(covered, owner->source);
        remove = at + 1;
      }
      remove_slot(row, remove);
    }
  }
  column_definition(p, g, after ? at - 1 : at, insert, after, names);
}
// Covered paragraphs are source data even when not painted. Promotion of a
// vertical merge keeps those children in the surviving anchor. Cached numeric
// covered values need a richer value merge and must not be silently discarded.
void preserve_covered(XmlElement covered, XmlElement anchor) {
  for (const auto &a : covered.attributes()) {
    auto name = a.name.mid(a.name.indexOf(':') + 1);
    if (a.uri == OFFICE ||
        a.uri == TABLE && TextList{"formula", "number-matrix-columns-spanned",
                                   "number-matrix-rows-spanned"}
                              .contains(name))
      fail("This covered cell has cached data requiring a value merge");
    if (a.uri == XML && name == "id") {
      auto marker = anchor.ownerDocument().createElementNS(TEXT, "text:p");
      marker.setAttributeNS(XML, "xml:id", a.value);
      anchor.appendChild(marker);
    }
  }
  while (!covered.firstChild().isNull())
    anchor.appendChild(covered.firstChild());
}
void edit_rows(OdfPackage &p, const Grid &g, const TextCellAddress &address,
               WordsTableGridAction action, Names &names) {
  bool insert = action != WordsTableGridAction::DeleteRow;
  bool after = action == WordsTableGridAction::RowAfter;
  size_t at = address.row + (after ? address.rows : 0);
  if (insert && (g.rows >= 100000 || (g.rows + 1) > 1000000 / g.columns))
    fail("The table exceeds the native row/grid limit");
  std::set<const void *> adjusted;
  for (const auto &c : g.model.cells) {
    bool changes = c.row < at && at < c.row + c.rows;
    if (changes && adjusted.insert(c.source.identity()).second)
      set_count(c.source, "number-rows-spanned", c.rows + (insert ? 1 : -1));
  }
  auto base_row = g.model.rows[after ? at - 1 : at];
  if (!insert) {
    auto repeated = count(base_row.source, "number-rows-repeated");
    if (repeated > 1) {
      set_count(base_row.source, "number-rows-repeated", repeated - 1);
      return;
    }
    std::set<const void *> promoted;
    for (const auto &c : g.model.cells)
      if (c.row == at && c.rows > 1 &&
          promoted.insert(c.source.identity()).second) {
        auto next = g.model.rows[at + 1];
        auto row = isolate(next.source, "number-rows-repeated",
                           next.source_instance, names);
        auto repeat = count(c.source, "number-columns-repeated");
        auto template_source = c.source.cloneNode(true);
        // A repeated anchor represents several equal-width merged columns.
        // Split it only when promotion needs to retain different covered source
        // data.
        for (size_t i = 0; i < repeat; ++i) {
          auto anchor = i == 0 ? c.source : names.clone(template_source);
          if (repeat > 1)
            set_count(anchor, "number-columns-repeated", 1);
          set_count(anchor, "number-rows-spanned", c.rows - 1);
          retain_cell_style(p, anchor, c.style_name,
                            inherited_cell_style(g, row, c.column + i));
          auto target = slot(row, c.column + i);
          if (target.node.isNull() ||
              target.node.localName() != "covered-table-cell")
            fail("Missing vertical merge coverage");
          auto covered = isolate(target.node, "number-columns-repeated",
                                 target.instance, names);
          preserve_covered(covered, anchor);
          row.insertBefore(anchor, covered);
          row.removeChild(covered);
        }
      }
    base_row.source.parentNode().removeChild(base_row.source);
    return;
  }
  auto existing = isolate(base_row.source, "number-rows-repeated",
                          base_row.source_instance, names);
  auto row = p.xml.createElementNS(TABLE, "table:table-row");
  for (const auto &a : existing.attributes())
    if (a.uri == TABLE &&
        TextList{"style-name", "default-cell-style-name", "visibility"}
            .contains(a.name.mid(a.name.indexOf(':') + 1)))
      row.setAttributeNS(a.uri, a.name, a.value);
  size_t neighbor = std::min(at, g.rows - 1);
  for (size_t col = 0; col < g.columns;) {
    bool covered = at > 0 && at < g.rows && g.id(at - 1, col) != missing &&
                   g.id(at - 1, col) == g.id(at, col);
    auto source = g.cell(neighbor, col);
    auto cell = blank(p, source, covered);
    size_t width = 1;
    if (!covered && source && source->column == col) {
      width = source->columns;
      set_count(cell, "number-columns-spanned", width);
    }
    row.appendChild(cell);
    for (size_t i = 1; i < width; ++i)
      row.appendChild(blank(p, nullptr, true));
    col += width;
  }
  if (after)
    existing.parentNode().insertAfter(row, existing);
  else
    existing.parentNode().insertBefore(row, existing);
}
void prune_groups(XmlElement table) {
  std::function<bool(XmlElement)> clean = [&](XmlElement node) {
    bool populated = false;
    for (auto child = node.firstChild(); !child.isNull();) {
      auto next = child.nextSibling();
      if (child.isElement() && child.namespaceURI() == TABLE) {
        auto name = child.localName();
        if (TextList{"table-column", "table-row"}.contains(name))
          populated = true;
        else if (TextList{"table-columns", "table-column-group",
                          "table-header-columns", "table-rows",
                          "table-row-group", "table-header-rows"}
                     .contains(name)) {
          if (clean(child))
            populated = true;
          else
            node.removeChild(child);
        }
      }
      child = next;
    }
    return populated;
  };
  clean(table);
}
XmlElement isolate_cell(OdfPackage &p, const Grid &grid, size_t row,
                        size_t column, Names &names) {
  auto cell = grid.cell(row, column);
  if (!cell)
    fail("Missing editable table cell");
  auto row_source = grid.model.rows[cell->row];
  auto chosen_row = isolate(row_source.source, "number-rows-repeated",
                            row_source.source_instance, names);
  auto chosen = slot(chosen_row, cell->column);
  if (chosen.node.isNull() || chosen.node.localName() != "table-cell")
    fail("Missing table anchor source");
  return isolate(chosen.node, "number-columns-repeated", chosen.instance,
                 names);
}
std::vector<size_t> relative_path(XmlElement root, XmlElement target) {
  std::vector<size_t> result;
  for (auto n = target; n != root; n = n.parentNode()) {
    auto parent = n.parentNode();
    if (!parent.isElement())
      fail("Invalid nested table context");
    size_t index = 0;
    for (auto c = parent.firstChild(); c != n; c = c.nextSibling()) {
      if (c.isNull())
        fail("Missing nested table path");
      ++index;
    }
    result.push_back(index);
  }
  std::reverse(result.begin(), result.end());
  return result;
}
XmlElement follow_path(XmlElement root, const std::vector<size_t> &path) {
  auto n = root;
  for (auto index : path) {
    n = n.firstChild();
    while (index-- && !n.isNull())
      n = n.nextSibling();
  }
  if (!n.isElement() || n.namespaceURI() != TABLE || n.localName() != "table")
    fail("Invalid materialized nested table");
  return n;
}
size_t table_index(const TextFlow &flow, XmlElement source) {
  for (size_t i = 0; i < flow.tables.size(); ++i)
    if (flow.tables[i].source == source)
      return i;
  fail("Edited table source is missing");
}
const TextTableCell &validate_address(const TextTable &table,
                                      const TextCellAddress &address) {
  for (const auto &cell : table.cells)
    if (cell.row == address.row && cell.column == address.column &&
        cell.rows == address.rows && cell.columns == address.columns)
      return cell;
  fail("Stale table cell address");
}
XmlElement prepare_target(OdfPackage &p, const TextFlow &original,
                          const std::vector<TextCellAddress> &path,
                          Names &names) {
  std::vector<std::vector<size_t>> steps;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i].table >= original.tables.size())
      fail("Invalid table context");
    const auto &cell =
        validate_address(original.tables[path[i].table], path[i]);
    if (i + 1 < path.size()) {
      if (path[i + 1].table >= original.tables.size())
        fail("Invalid nested table context");
      steps.push_back(relative_path(cell.source,
                                    original.tables[path[i + 1].table].source));
    }
  }
  auto source = original.tables[path.front().table].source;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    auto flow = read_text_flow(p);
    Grid grid(flow.tables[table_index(flow, source)]);
    auto chosen = isolate_cell(p, grid, path[i].row, path[i].column, names);
    source = follow_path(chosen, steps[i]);
  }
  return source;
}
WordsTableEdit focused(OdfPackage p, size_t cursor) {
  auto cells = words_table_cells_at(p, cursor);
  auto cell = cells.empty() ? std::optional<TextCellAddress>{}
                            : std::optional(cells.back());
  return {std::move(p), cursor, cell, std::move(cells)};
}
WordsTableEdit finish(OdfPackage p, XmlElement source, size_t row,
                      size_t column, Names &names) {
  auto flow = read_text_flow(p);
  Grid grid(flow.tables[table_index(flow, source)]);
  row = std::min(row, grid.rows - 1);
  column = std::min(column, grid.columns - 1);
  if (!grid.cell(row, column)) {
    auto row_source =
        isolate(grid.model.rows[row].source, "number-rows-repeated",
                grid.model.rows[row].source_instance, names);
    insert_slot(p, row_source, column, blank(p), names);
    return finish(std::move(p), source, row, column, names);
  }
  auto chosen = isolate_cell(p, grid, row, column, names);
  auto ps = paragraphs(chosen);
  if (ps.empty()) {
    auto paragraph = p.xml.createElementNS(TEXT, "text:p");
    chosen.appendChild(paragraph);
    ps.push_back(paragraph);
  }
  auto offset = offset_of(p, ps[0]);
  (void)read_text_flow(p);
  return focused(std::move(p), offset);
}

WordsTableEdit unchanged_cell(const OdfPackage &p,
                              const TextCellAddress &address,
                              const std::vector<TextCellAddress> &path) {
  auto flow = read_text_flow(p);
  auto source = flow.tables[address.table].source;
  for (const auto &cell : flow.tables[address.table].cells)
    if (cell.row == address.row && cell.column == address.column)
      source = cell.source;
  size_t cursor = 0, total = 0;
  bool found = false;
  std::function<void(XmlElement)> scan = [&](XmlElement e) {
    if (found)
      return;
    if (e == source) {
      found = true;
      return;
    }
    if (is_paragraph(e)) {
      cursor += plain_text(e).size() + 1;
      return;
    }
    for (auto child = e.firstChild(); !child.isNull();
         child = child.nextSibling())
      if (child.isElement())
        scan(child);
  };
  scan(p.body());
  for (auto para : paragraphs(p.body()))
    total += plain_text(para).size() + 1;
  return {p, std::min(cursor, total ? total - 1 : 0), address, path};
}
std::vector<TextCellAddress> checked_path(const TextFlow &flow,
                                          const TextCellAddress &address,
                                          std::vector<TextCellAddress> path) {
  if (path.empty())
    path.push_back(address);
  if (path.back() != address)
    fail("Table context does not end at the selected cell");
  XmlElement parent;
  for (const auto &a : path) {
    if (a.table >= flow.tables.size())
      fail("Invalid table context");
    const auto &t = flow.tables[a.table];
    if (ancestor(t.source, "table-cell") != parent)
      fail("The complete enclosing table context is required");
    parent = validate_address(t, a).source;
  }
  return path;
}
void editable_cell_values(XmlElement node) {
  // Numeric values, formulas and matrix metadata need a separate value/ref
  // transaction; combining their display strings would silently invalidate it.
  for (auto a : node.attributes()) {
    auto name = a.name.mid(a.name.indexOf(':') + 1);
    if ((a.uri == TABLE && TextList{"formula", "number-matrix-columns-spanned",
                                    "number-matrix-rows-spanned"}
                               .contains(name)) ||
        (a.uri == OFFICE && (name != "string-value" &&
                             (name != "value-type" || a.value != "string"))))
      fail("These cells contain values or formulas requiring a value merge");
  }
}
XmlElement create_empty_cell_paragraph(OdfPackage &p, XmlElement cell) {
  if (cell.namespaceURI() != TABLE || cell.localName() != "table-cell")
    fail("The empty caret is not in an editable table cell");
  for (auto child = cell.firstChild(); !child.isNull();
       child = child.nextSibling())
    if (child.isElement() || ((child.isText() || child.isCDATASection()) &&
                              !child.nodeValue().trimmed().empty()))
      fail("This cell contains content requiring a rich object editor");
  auto paragraph = p.xml.createElementNS(TEXT, "text:p");
  cell.appendChild(paragraph);
  return paragraph;
}
void rectangle(const Grid &grid, const TextCellAddress &a, size_t rows,
               size_t columns) {
  if (!rows || !columns || a.row >= grid.rows || a.column >= grid.columns ||
      rows > grid.rows - a.row || columns > grid.columns - a.column)
    fail("The merge rectangle is outside the table");
  for (size_t r = a.row; r < a.row + rows; ++r) {
    if (grid.model.rows[r].header != grid.model.rows[a.row].header)
      fail("A merge cannot cross the repeating header boundary yet");
    for (size_t c = a.column; c < a.column + columns; ++c) {
      const auto *cell = grid.cell(r, c);
      if (cell && (cell->row < a.row || cell->column < a.column ||
                   cell->row + cell->rows > a.row + rows ||
                   cell->column + cell->columns > a.column + columns))
        fail("The merge rectangle cuts through an existing span");
      auto source = slot(grid.model.rows[r].source, c).node;
      if (!source.isNull())
        editable_cell_values(source);
    }
  }
}
std::vector<XmlElement> materialize_rectangle(OdfPackage &p, const Grid &g,
                                              size_t r, size_t c, size_t rows,
                                              size_t columns, Names &names) {
  std::vector<XmlElement> result(rows);
  // Descending instances keep the original source prefix and its indices valid.
  for (size_t i = rows; i-- > 0;) {
    const auto &row = g.model.rows[r + i];
    result[i] =
        isolate(row.source, "number-rows-repeated", row.source_instance, names);
    for (size_t j = columns; j-- > 0;) {
      auto entry = slot(result[i], c + j);
      if (entry.node.isNull())
        insert_slot(p, result[i], c + j, blank(p), names);
      else
        isolate(entry.node, "number-columns-repeated", entry.instance, names);
    }
  }
  return result;
}
XmlElement converted(OdfPackage &p, XmlElement source, bool covered) {
  auto result = p.xml.createElementNS(
      TABLE, covered ? "table:covered-table-cell" : "table:table-cell");
  for (auto a : source.attributes()) {
    auto name = a.name.mid(a.name.indexOf(':') + 1);
    if (a.uri == TABLE &&
        TextList{"number-columns-repeated", "number-columns-spanned",
                 "number-rows-spanned"}
            .contains(name))
      continue;
    result.setAttributeNS(a.uri, a.name, a.value);
  }
  while (!source.firstChild().isNull())
    result.appendChild(source.firstChild());
  source.parentNode().insertBefore(result, source);
  source.parentNode().removeChild(source);
  return result;
}
void expand_nested(XmlElement node, Names &names, size_t &budget) {
  if (++budget > 100000)
    fail("Cell materialization exceeds the native source limit");
  for (auto child = node.firstChild(); !child.isNull();) {
    auto next = child.nextSibling();
    if (child.isElement()) {
      auto attribute =
          child.namespaceURI() == TABLE && child.localName() == "table-row"
              ? Text("number-rows-repeated")
          : cell_node(child) ? Text("number-columns-repeated")
                             : Text();
      auto copies = attribute.empty() ? 1 : count(child, attribute);
      if (copies > 100000 - budget)
        fail("Cell materialization exceeds the native source limit");
      if (!attribute.empty())
        set_count(child, attribute, 1);
      auto previous = child;
      for (size_t i = 1; i < copies; ++i) {
        auto copy = names.clone(child);
        node.insertAfter(copy, previous);
        previous = copy;
        expand_nested(copy, names, budget);
      }
      expand_nested(child, names, budget);
    }
    child = next;
  }
}
const TextTable &formatted_table(const TextFlow &flow, XmlElement source) {
  const TextTable *result = nullptr;
  std::function<void(const std::vector<TextBlock> &)> walk =
      [&](const auto &blocks) {
        for (const auto &b : blocks)
          if (b.kind == TextBlock::Table) {
            const auto &t = b.table ? *b.table : flow.tables.at(b.index);
            if (t.source == source)
              result = &t;
            else
              for (const auto &cell : t.cells)
                walk(cell.blocks);
          }
      };
  walk(flow.blocks);
  if (!result)
    fail("Formatted table context is missing");
  return *result;
}
Text rgb(TextColor c) {
  const char *hex = "0123456789abcdef";
  std::string result = "#";
  for (int v : {c.red, c.green, c.blue}) {
    result += hex[(v >> 4) & 15];
    result += hex[v & 15];
  }
  return Text(result);
}
// Freeze the old inherited appearance only when moving between different cell
// styles. Inline runs, named parents and opaque properties stay on their
// source.
void freeze_paragraph(OdfPackage &p, XmlElement node,
                      const TextParagraph &value, size_t &next) {
  const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
  auto root = p.xml.documentElement(),
       group = child(root, OFFICE, "automatic-styles");
  if (group.isNull()) {
    group = p.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(group, child(root, OFFICE, "body"));
  }
  auto old = node.attributeNS(TEXT, "style-name");
  XmlElement style;
  for (auto candidate : children(group, STYLE, "style"))
    if (candidate.attributeNS(STYLE, "family") == "paragraph" &&
        candidate.attributeNS(STYLE, "name") == old)
      style = candidate.cloneNode(true);
  if (style.isNull()) {
    style = p.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:family", "paragraph");
    if (!old.empty())
      style.setAttributeNS(STYLE, "style:parent-style-name", old);
  }
  walk(style, [&](XmlElement e) { e.removeAttributeNS(XML, "id"); });
  std::set<Text> used;
  for (auto s : p.xml.elementsByTagNameNS(STYLE, "style"))
    used.insert(s.attributeNS(STYLE, "name"));
  if (!p.entry("styles.xml").empty())
    for (auto s : read_document_xml(p.entry("styles.xml"))
                      .elementsByTagNameNS(STYLE, "style"))
      used.insert(s.attributeNS(STYLE, "name"));
  Text name;
  do {
    name = "WordsMerge" + Text::number(next++);
  } while (used.contains(name));
  style.setAttributeNS(STYLE, "style:name", name);
  auto props = [&](const Text &kind) {
    auto e = child(style, STYLE, kind);
    if (e.isNull()) {
      e = p.xml.createElementNS(STYLE, "style:" + kind);
      if (kind == "paragraph-properties")
        style.insertBefore(e, child(style, STYLE, "text-properties"));
      else
        style.appendChild(e);
    }
    return e;
  };
  auto text = props("text-properties");
  auto set = [&](XmlElement e, const Text &uri, const Text &key,
                 const Text &v) { e.setAttributeNS(uri, key, v); };
  set(text, FO, "fo:font-family", value.font.family);
  for (const auto &suffix : {Text(), Text("-asian"), Text("-complex")}) {
    const auto ns = suffix.empty() ? FO : STYLE,
               prefix = suffix.empty() ? Text("fo:") : Text("style:");
    set(text, ns, prefix + "font-size" + suffix,
        Text::number(value.font.size, 'g', 17) + "pt");
    set(text, ns, prefix + "font-weight" + suffix,
        Text::number(value.font.weight));
    set(text, ns, prefix + "font-style" + suffix,
        value.font.italic ? "italic" : "normal");
    text.removeAttributeNS(STYLE, "font-name" + suffix);
  }
  set(text, FO, "fo:language",
      value.font.language.empty() ? Text("none") : value.font.language);
  text.removeAttributeNS(FO, "country");
  set(text, FO, "fo:color", rgb(value.color.value_or(TextColor{})));
  set(text, STYLE, "style:use-window-font-color", "false");
  set(text, FO, "fo:background-color",
      value.background ? rgb(*value.background) : Text("transparent"));
  auto line = [&](const Text &prefix, const TextDecoration &d) {
    const char *styles[] = {"none",      "solid",    "dotted",       "dash",
                            "long-dash", "dot-dash", "dot-dot-dash", "wave"};
    set(text, STYLE, "style:" + prefix + "-style", styles[d.style]);
    set(text, STYLE, "style:" + prefix + "-type",
        d.double_line ? "double" : "single");
    set(text, STYLE, "style:" + prefix + "-mode",
        d.skip_space ? "skip-white-space" : "continuous");
    auto width = d.width_unit == TextDecoration::Points
                     ? Text::number(d.width, 'g', 17) + "pt"
                 : d.width_unit == TextDecoration::FontSize
                     ? Text::number(d.width * 100, 'g', 17) + "%"
                     : Text(d.width == 1.5  ? "bold"
                            : d.width == .7 ? "thin"
                            : d.width == 2  ? "thick"
                                            : "auto");
    set(text, STYLE, "style:" + prefix + "-width", width);
    set(text, STYLE, "style:" + prefix + "-color",
        d.color ? rgb(*d.color) : Text("font-color"));
  };
  line("text-underline", value.decorations.underline);
  line("text-line-through", value.decorations.strike);
  auto para = props("paragraph-properties");
  const char *align[] = {"left", "center", "right", "justify", "start", "end"};
  set(para, FO, "fo:text-align", align[value.alignment]);
  for (auto [key, v] : std::initializer_list<std::pair<const char *, double>>{
           {"margin-left", value.left},
           {"margin-right", value.right},
           {"text-indent", value.indent},
           {"margin-top", value.before},
           {"margin-bottom", value.after}})
    set(para, FO, Text("fo:") + key, Text::number(v, 'g', 17) + "pt");
  set(para, FO, "fo:line-height",
      value.line_height > 0
          ? Text::number(value.line_height, 'g', 17) + "pt"
          : Text::number(value.line_factor * 100, 'g', 17) + "%");
  set(para, FO, "fo:keep-together", value.keep_together ? "always" : "auto");
  set(para, FO, "fo:keep-with-next", value.keep_next ? "always" : "auto");
  set(para, FO, "fo:break-before", value.break_before ? "page" : "auto");
  set(para, FO, "fo:break-after", value.break_after ? "page" : "auto");
  set(para, FO, "fo:orphans", Text::number(value.orphans));
  set(para, FO, "fo:widows", Text::number(value.widows));
  set(para, STYLE, "style:writing-mode",
      value.direction == TextParagraph::RTL   ? "rl-tb"
      : value.direction == TextParagraph::LTR ? "lr-tb"
                                              : "page");
  if (value.list && !value.list->style.empty())
    set(style, STYLE, "style:list-style-name", value.list->style);
  group.appendChild(style);
  node.setAttributeNS(TEXT, "text:style-name", name);
}
void freeze_cell(OdfPackage &p, XmlElement table, XmlElement cell, Names &names,
                 size_t &next,
                 std::vector<std::pair<XmlElement, WordsTabSet>> &saved_tabs) {
  size_t budget = 0;
  expand_nested(cell, names, budget);
  auto flow = read_text_flow(p);
  const auto &t = formatted_table(flow, table);
  auto sources = paragraphs(p.body());
  std::vector<size_t> offsets;
  size_t at = 0;
  for (auto s : sources) {
    offsets.push_back(at);
    at += plain_text(s).size() + 1;
  }
  std::vector<std::pair<size_t, TextParagraph>> values;
  std::function<void(const std::vector<TextBlock> &)> collect =
      [&](const auto &blocks) {
        for (const auto &b : blocks)
          if (b.kind == TextBlock::Paragraph)
            values.emplace_back(b.index, b.formatted
                                             ? *b.formatted
                                             : flow.paragraphs.at(b.index));
          else {
            const auto &nested = b.table ? *b.table : flow.tables.at(b.index);
            for (const auto &c : nested.cells)
              collect(c.blocks);
          }
      };
  for (const auto &c : t.cells)
    if (c.source == cell)
      collect(c.blocks);
  for (const auto &[index, value] : values) {
    if (index >= sources.size() || plain_text(sources[index]) != value.text)
      fail("Moved paragraph source is inconsistent");
    auto tabs = read_words_tabs(p, offsets[index], offsets[index]);
    if (tabs.stops)
      saved_tabs.emplace_back(sources[index], *tabs.stops);
    freeze_paragraph(p, sources[index], value, next);
  }
}

Text fraction_style(OdfPackage &p, XmlElement source, const Text &family,
                    const std::vector<std::pair<Text, Text>> &overrides,
                    size_t &serial) {
  auto root = p.xml.documentElement(),
       group = child(root, OFFICE, "automatic-styles");
  if (group.isNull()) {
    group = p.xml.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(group, child(root, OFFICE, "body"));
  }
  auto name = source.attributeNS(TABLE, "style-name");
  XmlElement style;
  std::set<Text> used;
  for (auto s : p.xml.elementsByTagNameNS(STYLE, "style")) {
    used.insert(s.attributeNS(STYLE, "name"));
    if (s.parentNode() == group && s.attributeNS(STYLE, "family") == family &&
        s.attributeNS(STYLE, "name") == name)
      style = s.cloneNode(true);
  }
  if (!p.entry("styles.xml").empty())
    for (auto s : read_document_xml(p.entry("styles.xml"))
                      .elementsByTagNameNS(STYLE, "style"))
      used.insert(s.attributeNS(STYLE, "name"));
  if (style.isNull()) {
    style = p.xml.createElementNS(STYLE, "style:style");
    style.setAttributeNS(STYLE, "style:family", family);
    if (!name.empty())
      style.setAttributeNS(STYLE, "style:parent-style-name", name);
  }
  walk(style, [](XmlElement e) { e.removeAttributeNS(XML, "id"); });
  do {
    name = "WordsSubdivision" + Text::number(serial++);
  } while (used.contains(name));
  style.setAttributeNS(STYLE, "style:name", name);
  auto props = child(style, STYLE, family + "-properties");
  if (props.isNull()) {
    props = p.xml.createElementNS(STYLE, "style:" + family + "-properties");
    style.appendChild(props);
  }
  for (const auto &[key, value] : overrides)
    props.setAttributeNS(STYLE, "style:" + key, value);
  group.appendChild(style);
  return name;
}
} // namespace
WordsTableSizing
words_table_sizing(const OdfPackage &p, const TextCellAddress &address,
                   const std::vector<TextCellAddress> &context) {
  auto flow = read_text_flow(p);
  (void)checked_path(flow, address, context);
  const auto &table = flow.tables[address.table];
  const auto &row = table.rows.at(address.row);
  const auto &column = table.columns.at(address.column);
  WordsTableSizing result;
  if (table.alignment != TextTable::Full &&
      table.alignment != TextTable::Margins) {
    if (table.relative_width)
      result.table = {WordsTableSize::Percentage, *table.relative_width * 100,
                      table.alignment};
    else if (table.width)
      result.table = {WordsTableSize::Fixed, *table.width, table.alignment};
  }
  if (row.height)
    result.row = {WordsRowSize::Exact, *row.height};
  else if (row.minimum > 0)
    result.row = {WordsRowSize::Minimum, row.minimum};
  if (column.relative > 0 || !column.width)
    result.column = {WordsColumnSize::Weight,
                     column.relative > 0 ? column.relative : 1};
  else
    result.column = {WordsColumnSize::Fixed, *column.width};
  return result;
}
WordsTableEdit size_words_table(const OdfPackage &original,
                                const TextCellAddress &address,
                                const WordsTableSizingChange &change,
                                const std::vector<TextCellAddress> &context,
                                std::optional<size_t> position) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto flow = read_text_flow(original);
  auto path = checked_path(flow, address, context);
  auto initial = words_table_sizing(original, address, path);
  auto wanted = initial;
  auto length = [](double n) {
    if (!std::isfinite(n) || n < 0 || n > 100000)
      fail("Dimensions must be between 0 and 100000 points");
  };
  if (change.table) {
    wanted.table = *change.table;
    auto &t = wanted.table;
    if (t.mode < WordsTableSize::Stretch || t.mode > WordsTableSize::Percentage)
      fail("Invalid table width mode");
    if (t.mode == WordsTableSize::Stretch)
      t = {};
    else {
      if (t.alignment != TextTable::Left && t.alignment != TextTable::Center &&
          t.alignment != TextTable::Right)
        fail("Choose left, center or right alignment for a sized table");
      length(t.value);
      if (t.value <= 0 ||
          (t.mode == WordsTableSize::Percentage && t.value > 10000))
        fail("Table width must be positive; percentages cannot exceed 10000");
    }
  }
  if (change.row) {
    wanted.row = *change.row;
    if (wanted.row.mode < WordsRowSize::Automatic ||
        wanted.row.mode > WordsRowSize::Exact)
      fail("Invalid row height mode");
    length(wanted.row.value);
    if (wanted.row.mode == WordsRowSize::Exact && wanted.row.value == 0)
      fail("Exact row heights must be positive; use automatic height instead");
    if (wanted.row.mode == WordsRowSize::Automatic ||
        (wanted.row.mode == WordsRowSize::Minimum && wanted.row.value == 0))
      wanted.row = {};
  }
  if (change.column) {
    wanted.column = *change.column;
    if (wanted.column.mode != WordsColumnSize::Weight &&
        wanted.column.mode != WordsColumnSize::Fixed)
      fail("Invalid column width mode");
    if (wanted.column.mode == WordsColumnSize::Fixed) {
      length(wanted.column.value);
      if (wanted.column.value == 0)
        fail("Fixed column widths must be positive; use a relative weight "
             "instead");
    } else if (!std::isfinite(wanted.column.value) || wanted.column.value < 1 ||
               wanted.column.value > 1000000000000. ||
               std::floor(wanted.column.value) != wanted.column.value)
      fail("Column weights must be whole numbers between 1 and 1000000000000");
  }
  auto near = [](double a, double b) {
    return std::abs(a - b) <= 1e-12 * std::max({1., std::abs(a), std::abs(b)});
  };
  if (wanted.table.mode == initial.table.mode &&
      wanted.table.alignment == initial.table.alignment &&
      near(wanted.table.value, initial.table.value))
    wanted.table = initial.table;
  if (wanted.row.mode == initial.row.mode &&
      near(wanted.row.value, initial.row.value))
    wanted.row = initial.row;
  if (wanted.column.mode == initial.column.mode &&
      near(wanted.column.value, initial.column.value))
    wanted.column = initial.column;
  const bool table_changed = wanted.table != initial.table,
             row_changed = wanted.row != initial.row,
             column_changed = wanted.column != initial.column;
  const auto &old = validate_address(flow.tables[address.table], address);
  auto ps = paragraphs(old.source);
  size_t caret_offset = 0;
  if (position && !ps.empty()) {
    auto begin = offset_of(original, ps.front()),
         end = offset_of(original, ps.back()) + plain_text(ps.back()).size();
    if (*position < begin || *position > end)
      fail("The caret is outside the selected cell");
    caret_offset = *position - begin;
  } else if (position &&
             *position != unchanged_cell(original, address, path).cursor)
    fail("The empty-cell caret is stale");
  if (!table_changed && !row_changed && !column_changed) {
    auto result = unchanged_cell(original, address, path);
    if (position)
      result.cursor = *position;
    return result;
  }
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto target = prepare_target(p, read_text_flow(p), path, names);
  auto current = read_text_flow(p);
  const auto &table = current.tables[table_index(current, target)];
  std::vector<XmlDocument> documents;
  XmlDocument styles_document;
  if (!p.entry("styles.xml").empty()) {
    styles_document = read_document_xml(p.entry("styles.xml"));
    documents.push_back(styles_document);
    names.reserve(styles_document.documentElement());
  }
  documents.push_back(p.xml);
  bool styles_changed = false;
  auto common_group = [&]() {
    if (styles_document.documentElement().isNull()) {
      auto version =
          p.xml.documentElement().attributeNS(OFFICE, "version", "1.3");
      styles_document = read_document_xml(
          ("<office:document-styles xmlns:office=\"" + OFFICE +
           "\" xmlns:style=\"" + STYLE + "\" office:version=\"" + version +
           "\"><office:styles/></office:document-styles>")
              .toUtf8());
    }
    auto root = styles_document.documentElement();
    auto common = child(root, OFFICE, "styles");
    if (common.isNull()) {
      common = styles_document.createElementNS(OFFICE, "office:styles");
      XmlElement next;
      for (auto node : children(root, OFFICE))
        if (node.localName() == "automatic-styles" ||
            node.localName() == "master-styles") {
          next = node;
          break;
        }
      root.insertBefore(common, next);
    }
    return common;
  };
  OdfTableModelReader catalogue(documents);
  auto group = child(p.xml.documentElement(), OFFICE, "automatic-styles");
  if (group.isNull()) {
    group = p.xml.createElementNS(OFFICE, "office:automatic-styles");
    p.xml.documentElement().insertBefore(
        group, child(p.xml.documentElement(), OFFICE, "body"));
  }
  std::set<Text> used;
  for (const auto &doc : documents)
    for (auto style : doc.elementsByTagNameNS(STYLE, "style"))
      used.insert(style.attributeNS(STYLE, "name"));
  size_t serial = 1;
  // Private copies of the whole style chain preserve unrelated properties and
  // remove obsolete dimension attributes from every inherited named layer.
  auto style_for = [&](XmlElement node, const Text &family,
                       const TextList &remove) {
    auto chain =
        catalogue.style_chain(family, node.attributeNS(TABLE, "style-name"));
    if (family == "table" && wanted.table.mode == WordsTableSize::Fixed)
      for (const auto &style : chain)
        if (style.localName() == "default-style" &&
            child(style, STYLE, "table-properties")
                .hasAttributeNS(STYLE, "rel-width"))
          fail("A relative table width in document defaults needs "
               "default-style isolation before fixed sizing");
    std::erase_if(chain, [](const auto &style) {
      return style.localName() == "default-style";
    });
    if (chain.empty())
      chain.push_back({});
    Text parent;
    XmlElement props;
    for (size_t layer = 0; layer < chain.size(); ++layer) {
      const auto &source = chain[layer];
      auto definition = p.xml.createElementNS(STYLE, "style:style");
      if (!source.isNull()) {
        for (const auto &a : source.attributes()) {
          auto local = a.name.mid(a.name.indexOf(':') + 1);
          if (!(a.uri == STYLE &&
                TextList{"name", "family", "parent-style-name", "display-name"}
                    .contains(local)))
            definition.setAttributeNS(a.uri, a.name, a.value);
        }
        for (auto n = source.firstChild(); !n.isNull(); n = n.nextSibling())
          definition.appendChild(p.xml.importNode(n, true));
      }
      definition = names.clone(definition);
      Text name;
      do {
        name = "WordsSizing" + Text::number(serial++);
      } while (used.contains(name));
      used.insert(name);
      definition.setAttributeNS(STYLE, "style:name", name);
      definition.setAttributeNS(STYLE, "style:family", family);
      if (!parent.empty())
        definition.setAttributeNS(STYLE, "style:parent-style-name", parent);
      parent = name;
      props = child(definition, STYLE, family + "-properties");
      if (props.isNull()) {
        props = p.xml.createElementNS(STYLE, "style:" + family + "-properties");
        definition.appendChild(props);
      }
      for (const auto &attribute : remove)
        props.removeAttributeNS(STYLE, attribute);
      if (layer + 1 == chain.size())
        group.appendChild(definition);
      else {
        auto common = common_group();
        common.appendChild(styles_document.importNode(definition, true));
        styles_changed = true;
      }
    }
    node.setAttributeNS(TABLE, "table:style-name", parent);
    return props;
  };
  auto number = [](double n) {
    auto text = point_length(n);
    return text.left(text.size() - 2);
  };
  if (table_changed) {
    auto props = style_for(target, "table", {"width", "rel-width"});
    if (wanted.table.mode == WordsTableSize::Fixed)
      props.setAttributeNS(STYLE, "style:width",
                           point_length(wanted.table.value));
    if (wanted.table.mode == WordsTableSize::Percentage)
      props.setAttributeNS(STYLE, "style:rel-width",
                           number(wanted.table.value) + "%");
    if (wanted.table.mode == WordsTableSize::Stretch)
      props.setAttributeNS(STYLE, "style:rel-width", "100%");
    const auto align = wanted.table.mode == WordsTableSize::Stretch
                           ? TextTable::Margins
                           : wanted.table.alignment;
    props.setAttributeNS(TABLE, "table:align",
                         align == TextTable::Left     ? "left"
                         : align == TextTable::Center ? "center"
                         : align == TextTable::Right  ? "right"
                                                      : "margins");
  }
  if (row_changed) {
    const auto &row = table.rows.at(address.row);
    auto selected =
        isolate(row.source, "number-rows-repeated", row.source_instance, names);
    auto props =
        style_for(selected, "table-row",
                  {"row-height", "min-row-height", "use-optimal-row-height"});
    props.setAttributeNS(STYLE, "style:use-optimal-row-height",
                         wanted.row.mode == WordsRowSize::Exact ? "false"
                                                                : "true");
    props.setAttributeNS(STYLE, "style:min-row-height",
                         point_length(wanted.row.mode == WordsRowSize::Minimum
                                          ? wanted.row.value
                                          : 0));
    if (wanted.row.mode == WordsRowSize::Exact)
      props.setAttributeNS(STYLE, "style:row-height",
                           point_length(wanted.row.value));
  }
  if (column_changed) {
    size_t explicit_columns = 0;
    for (auto definition : columns(target))
      explicit_columns += count(definition, "number-columns-repeated");
    complete_columns(p, target, table.columns.size());
    const Text fo =
        "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
    if (explicit_columns < table.columns.size()) {
      auto added = column_slot(target, explicit_columns).node;
      auto neutral = fraction_style(
          p, added, "table-column",
          {{"rel-column-width", "1*"}, {"use-optimal-column-width", "false"}},
          serial);
      added.setAttributeNS(TABLE, "table:style-name", neutral);
      for (auto definition : children(group, STYLE, "style"))
        if (definition.attributeNS(STYLE, "name") == neutral) {
          auto props = child(definition, STYLE, "table-column-properties");
          props.setAttributeNS(fo, "fo:break-before", "auto");
          props.setAttributeNS(fo, "fo:break-after", "auto");
        }
    }
    auto slot = column_slot(target, address.column);
    auto selected =
        isolate(slot.node, "number-columns-repeated", slot.instance, names);
    auto props = style_for(
        selected, "table-column",
        {"column-width", "rel-column-width", "use-optimal-column-width"});
    props.setAttributeNS(STYLE, "style:use-optimal-column-width", "false");
    if (address.column >= explicit_columns) {
      props.setAttributeNS(fo, "fo:break-before", "auto");
      props.setAttributeNS(fo, "fo:break-after", "auto");
    }
    props.setAttributeNS(STYLE, "style:rel-column-width",
                         wanted.column.mode == WordsColumnSize::Fixed
                             ? "0*"
                             : number(wanted.column.value) + "*");
    if (wanted.column.mode == WordsColumnSize::Fixed)
      props.setAttributeNS(STYLE, "style:column-width",
                           point_length(wanted.column.value));
  }
  if (styles_changed)
    p.set_entry("styles.xml", styles_document.toByteArray(), "text/xml");
  auto final_flow = read_text_flow(p);
  auto cells = path;
  auto source = target;
  for (size_t i = cells.size(); i-- > 0;) {
    cells[i].table = table_index(final_flow, source);
    (void)validate_address(final_flow.tables[cells[i].table], cells[i]);
    source = ancestor(ancestor(source, "table-cell"), "table");
  }
  auto result = unchanged_cell(p, cells.back(), cells);
  result.cursor += caret_offset;
  return result;
}
WordsSelectionEdit materialize_words_selection(const OdfPackage &original,
                                               const WordsTextPoint &anchor,
                                               const WordsTextPoint &cursor) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto original_flow = read_text_flow(original);
  WordsDisplayMap display(original_flow);
  auto span = display.span(anchor, cursor);
  auto slices = display.slices(span);
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Text marker = "domed-range-key";
  for (size_t serial = 0;; ++serial) {
    bool used = false;
    walk(p.xml.documentElement(),
         [&](auto node) { used = used || node.hasAttribute(marker); });
    if (!used)
      break;
    marker = "domed-range-key" + Text::number(serial);
  }
  auto flow = read_text_flow(p);
  auto ps = paragraphs(p.body());
  if (ps.empty() && original_flow.empty_cells.empty())
    return {p, anchor, cursor, {{0, 0}}};
  for (size_t i = 0; i < ps.size(); ++i)
    ps[i].setAttribute(marker, "p" + Text::number(i));
  for (size_t i = 0; i < flow.tables.size(); ++i)
    flow.tables[i].source.setAttribute(marker, "t" + Text::number(i));
  Names names(p);
  auto find = [&](XmlElement root, const Text &value) {
    XmlElement result;
    walk(root, [&](auto node) {
      if (node.attribute(marker) == value && result.isNull())
        result = node;
    });
    if (result.isNull())
      fail("Missing displayed selection source");
    return result;
  };
  std::vector<std::tuple<XmlElement, size_t, size_t, bool>> mapped;
  std::vector<size_t> starts;
  size_t total = 0;
  for (const auto &paragraph : original_flow.paragraphs) {
    starts.push_back(total);
    total += paragraph.text.size() + 1;
  }
  for (const auto &slice : slices) {
    XmlElement root = p.body();
    for (const auto &address : slice.cells) {
      auto table = find(root, "t" + Text::number(address.table));
      XmlElement row;
      size_t base = 0, instance = 0;
      auto scan = [&](auto &&self, XmlElement parent) -> void {
        for (auto node : children(parent, TABLE)) {
          if (!row.isNull())
            return;
          if (node.localName() == "table-row") {
            size_t repeated = count(node, "number-rows-repeated");
            if (address.row >= base && address.row - base < repeated) {
              row = node;
              instance = address.row - base;
              return;
            }
            base += repeated;
          } else if (TextList{"table-rows", "table-header-rows",
                              "table-row-group"}
                         .contains(node.localName()))
            self(self, node);
        }
      };
      scan(scan, table);
      if (row.isNull())
        fail("Missing displayed selection row");
      row = isolate(row, "number-rows-repeated", instance, names);
      auto chosen = slot(row, address.column);
      if (chosen.node.isNull() || chosen.node.localName() != "table-cell")
        fail("Missing displayed selection cell");
      root = isolate(chosen.node, "number-columns-repeated", chosen.instance,
                     names);
    }
    const bool empty = slice.paragraph >= original_flow.paragraphs.size();
    auto paragraph = empty ? create_empty_cell_paragraph(p, root)
                           : find(root, "p" + Text::number(slice.paragraph));
    const auto start = empty ? slice.begin : starts.at(slice.paragraph);
    mapped.emplace_back(paragraph, slice.begin - start, slice.end - start,
                        slice.active);
  }
  walk(p.xml.documentElement(),
       [&](auto node) { node.removeAttribute(marker); });
  WordsSelectionEdit result{p, {}, {}, {}};
  std::map<const void *, size_t> offsets;
  size_t offset = 0;
  for (auto paragraph : paragraphs(p.body())) {
    offsets.emplace(paragraph.identity(), offset);
    offset += plain_text(paragraph).size() + 1;
    if (offset > 16000001)
      fail("Materialized selection text exceeds native limit");
  }
  for (const auto &[paragraph, begin, end, active] : mapped) {
    size_t base = offsets.at(paragraph.identity());
    if (active)
      result.ranges.emplace_back(base + begin, base + end);
  }
  auto low = offsets.at(std::get<0>(mapped.front()).identity()) +
             std::get<1>(mapped.front());
  auto high = offsets.at(std::get<0>(mapped.back()).identity()) +
              std::get<2>(mapped.back());
  auto point = [&](size_t position) {
    return WordsTextPoint{position, words_table_cells_at(p, position)};
  };
  result.anchor = point(span.reverse ? high : low);
  result.cursor = point(span.reverse ? low : high);
  (void)read_text_flow(p);
  return result;
}

std::optional<WordsTableRangeEdit>
materialize_words_table_range(const OdfPackage &original,
                              const std::vector<TextCellAddress> &context,
                              size_t begin, size_t end) {
  if (context.empty())
    return {};
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  if (begin > end)
    fail("Invalid table text range");
  auto original_flow = read_text_flow(original);
  XmlElement previous_cell, leaf;
  bool repeated = false;
  for (const auto &address : context) {
    if (address.table >= original_flow.tables.size())
      fail("Invalid table context");
    const auto &table = original_flow.tables[address.table];
    const auto &cell = validate_address(table, address);
    auto enclosing = ancestor(table.source, "table-cell");
    if (enclosing != previous_cell)
      fail("The complete enclosing table context is required");
    repeated = repeated ||
               count(table.rows[cell.row].source, "number-rows-repeated") > 1 ||
               count(cell.source, "number-columns-repeated") > 1;
    previous_cell = leaf = cell.source;
  }
  auto old_paragraphs = paragraphs(leaf);
  const bool empty = old_paragraphs.empty();
  size_t first, last;
  if (empty) {
    auto found = std::find_if(
        original_flow.empty_cells.begin(), original_flow.empty_cells.end(),
        [&](const auto &entry) { return entry.source == leaf; });
    if (found == original_flow.empty_cells.end())
      fail("This table cell has no editable empty caret");
    first = last = found->position;
    for (const auto &address : context)
      editable_cell_values(
          validate_address(original_flow.tables[address.table], address)
              .source);
  } else {
    if (!repeated)
      return {};
    first = offset_of(original, old_paragraphs.front());
    last = offset_of(original, old_paragraphs.back()) +
           plain_text(old_paragraphs.back()).size();
  }
  if (begin < first || end > last)
    fail("The text selection crosses its table cell");
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto flow = read_text_flow(p);
  auto source = prepare_target(p, flow, context, names);
  flow = read_text_flow(p);
  Grid grid(flow.tables[table_index(flow, source)]);
  const auto &address = context.back();
  auto chosen = isolate_cell(p, grid, address.row, address.column, names);
  auto chosen_paragraphs = paragraphs(chosen);
  if (empty)
    chosen_paragraphs.push_back(create_empty_cell_paragraph(p, chosen));
  const auto first_chosen = offset_of(p, chosen_paragraphs.front());
  const auto mapped_begin = first_chosen + begin - first,
             mapped_end = first_chosen + end - first;
  (void)read_text_flow(p);
  auto cells = words_table_cells_at(p, mapped_end);
  return WordsTableRangeEdit{std::move(p), mapped_begin, mapped_end,
                             std::move(cells)};
}
std::vector<TextCellAddress> words_table_cells_at(const OdfPackage &p,
                                                  size_t position) {
  if (p.kind() != Kind::Words)
    return {};
  size_t offset = 0;
  XmlElement paragraph;
  for (auto e : paragraphs(p.body())) {
    auto size = plain_text(e).size();
    if (position >= offset && position <= offset + size) {
      paragraph = e;
      break;
    }
    offset += size + 1;
  }
  if (paragraph.isNull())
    return {};
  std::vector<TextCellAddress> result;
  auto flow = read_text_flow(p);
  for (auto node = paragraph.parentNode(); node.isElement();
       node = node.parentNode())
    if (node.namespaceURI() == TABLE && node.localName() == "table-cell") {
      auto table = ancestor(node, "table");
      auto index = table_index(flow, table);
      for (const auto &cell : flow.tables[index].cells)
        if (cell.source == node) {
          result.push_back(
              {index, cell.row, cell.column, cell.rows, cell.columns});
          break;
        }
    }
  std::reverse(result.begin(), result.end());
  return result;
}
std::optional<TextCellAddress> words_table_cell_at(const OdfPackage &p,
                                                   size_t position) {
  auto path = words_table_cells_at(p, position);
  return path.empty() ? std::optional<TextCellAddress>{}
                      : std::optional(path.back());
}
WordsTableEdit edit_words_table_grid(
    const OdfPackage &original, const TextCellAddress &address,
    WordsTableGridAction action, const std::vector<TextCellAddress> &context) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  auto flow = read_text_flow(p);
  if (address.table >= flow.tables.size())
    fail("Invalid table source");
  auto path = context.empty() ? std::vector<TextCellAddress>{address} : context;
  if (path.back() != address)
    fail("Stale table context path");
  Names names(p);
  auto source = prepare_target(p, flow, path, names);
  flow = read_text_flow(p);
  auto index = table_index(flow, source);
  Grid grid(flow.tables[index]);
  auto selected = grid.cell(address.row, address.column);
  if (!selected || selected->row != address.row ||
      selected->column != address.column || selected->rows != address.rows ||
      selected->columns != address.columns)
    fail("Stale table cell address");
  bool row = action == WordsTableGridAction::RowBefore ||
             action == WordsTableGridAction::RowAfter ||
             action == WordsTableGridAction::DeleteRow;
  bool column = action == WordsTableGridAction::ColumnBefore ||
                action == WordsTableGridAction::ColumnAfter ||
                action == WordsTableGridAction::DeleteColumn;
  if (!row && !column)
    fail("Invalid table grid action");
  if ((action == WordsTableGridAction::DeleteRow && grid.rows == 1) ||
      (action == WordsTableGridAction::DeleteColumn && grid.columns == 1)) {
    auto erased = delete_words_table_index(p, index);
    return focused(std::move(erased.document), erased.cursor);
  }
  // Formula values remain retained until their structural references can be
  // adjusted as part of the same transaction.
  walk(grid.model.source, [&](XmlElement e) {
    if (e.hasAttributeNS(TABLE, "formula"))
      fail("Table formulas need reference adjustment before row or column "
           "editing");
  });
  if (row)
    edit_rows(p, grid, address, action, names);
  else
    edit_columns(p, grid, address, action, names);
  prune_groups(grid.model.source);
  size_t next_row =
             address.row +
             (action == WordsTableGridAction::RowAfter ? address.rows : 0),
         next_column =
             address.column + (action == WordsTableGridAction::ColumnAfter
                                   ? address.columns
                                   : 0);
  return finish(std::move(p), grid.model.source, next_row, next_column, names);
}

WordsTableEdit merge_words_table_cells(
    const OdfPackage &original, const TextCellAddress &address, size_t rows,
    size_t columns, const std::vector<TextCellAddress> &context) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto original_flow = read_text_flow(original);
  auto path = checked_path(original_flow, address, context);
  Grid original_grid(original_flow.tables[address.table]);
  rectangle(original_grid, address, rows, columns);
  if (rows == address.rows && columns == address.columns)
    return unchanged_cell(original, address, path);
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto flow = read_text_flow(p);
  auto source = prepare_target(p, flow, path, names);
  flow = read_text_flow(p);
  Grid grid(flow.tables[table_index(flow, source)]);
  auto row_nodes = materialize_rectangle(p, grid, address.row, address.column,
                                         rows, columns, names);
  flow = read_text_flow(p);
  Grid local(flow.tables[table_index(flow, source)]);
  auto anchor = slot(row_nodes[0], address.column).node;
  const auto anchor_style = local.cell(address.row, address.column)->style_name;
  size_t next_style = 1;
  std::vector<std::pair<XmlElement, WordsTabSet>> saved_tabs;
  for (const auto &c : local.model.cells)
    if (c.source != anchor && c.row >= address.row &&
        c.row < address.row + rows && c.column >= address.column &&
        c.column < address.column + columns && c.style_name != anchor_style)
      freeze_cell(p, source, c.source, names, next_style, saved_tabs);
  for (size_t r = 0; r < rows; ++r)
    for (size_t c = 0; c < columns; ++c) {
      if (!r && !c)
        continue;
      auto entry = slot(row_nodes[r], address.column + c).node;
      // Covered data is retained and made visible rather than silently
      // discarded.
      while (!entry.firstChild().isNull())
        anchor.appendChild(entry.firstChild());
      entry.removeAttributeNS(OFFICE, "value-type");
      entry.removeAttributeNS(OFFICE, "string-value");
      converted(p, entry, true);
    }
  anchor.removeAttributeNS(OFFICE, "string-value");
  set_count(anchor, "number-rows-spanned", rows);
  set_count(anchor, "number-columns-spanned", columns);
  // Apply captured tabs after moving: effective-equal tabs before moving still
  // need an explicit override in the new cell. The public formatter also
  // rebinds leader-style references and may replace the candidate XML document.
  auto table_path = relative_path(p.xml.documentElement(), source);
  std::vector<size_t> tab_offsets;
  for (const auto &saved : saved_tabs)
    tab_offsets.push_back(offset_of(p, saved.first));
  for (size_t i = 0; i < saved_tabs.size(); ++i) {
    WordsTabsFormat format;
    format.stops = saved_tabs[i].second;
    format_words_tabs(p, tab_offsets[i], tab_offsets[i], format);
  }
  source = follow_path(p.xml.documentElement(), table_path);
  return finish(std::move(p), source, address.row, address.column, names);
}
WordsTableEdit
split_words_table_cell(const OdfPackage &original,
                       const TextCellAddress &address,
                       const std::vector<TextCellAddress> &context) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto original_flow = read_text_flow(original);
  auto path = checked_path(original_flow, address, context);
  const auto &old =
      validate_address(original_flow.tables[address.table], address);
  if (address.rows == 1 && address.columns == 1)
    return unchanged_cell(original, address, path);
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto flow = read_text_flow(p);
  auto source = prepare_target(p, flow, path, names);
  flow = read_text_flow(p);
  Grid grid(flow.tables[table_index(flow, source)]);
  const auto anchor_style = validate_address(grid.model, address).style_name;
  auto row_nodes = materialize_rectangle(p, grid, address.row, address.column,
                                         address.rows, address.columns, names);
  for (size_t r = 0; r < address.rows; ++r)
    for (size_t c = 0; c < address.columns; ++c) {
      auto entry = slot(row_nodes[r], address.column + c).node;
      if (r || c) {
        entry = converted(p, entry, false);
        // Explicit covered-cell styles survive; empty covered slots inherit the
        // merged anchor's cell/paragraph style while keeping their grid
        // position.
        if (entry.attributeNS(TABLE, "style-name").empty())
          entry.setAttributeNS(TABLE, "table:style-name",
                               anchor_style.empty() ? neutral_cell_style(p)
                                                    : anchor_style);
        if (paragraphs(entry).empty()) {
          auto empty = blank(p, &old);
          while (!empty.firstChild().isNull())
            entry.appendChild(empty.firstChild());
        }
      }
      entry.removeAttributeNS(TABLE, "number-rows-spanned");
      entry.removeAttributeNS(TABLE, "number-columns-spanned");
    }
  return finish(std::move(p), source, address.row, address.column, names);
}

TextCellStyle
words_table_cell_format(const OdfPackage &p, const TextCellAddress &address,
                        const std::vector<TextCellAddress> &context) {
  auto flow = read_text_flow(p);
  (void)checked_path(flow, address, context);
  return validate_address(flow.tables[address.table], address).style;
}
WordsTableEdit format_words_table_cell(
    const OdfPackage &original, const TextCellAddress &address,
    const WordsCellFormat &format, const std::vector<TextCellAddress> &context,
    std::optional<size_t> position) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  auto flow = read_text_flow(original);
  auto path = checked_path(flow, address, context);
  const auto &old = validate_address(flow.tables[address.table], address);
  auto old_paragraphs = paragraphs(old.source);
  size_t caret_offset = 0;
  if (position && !old_paragraphs.empty()) {
    size_t first = offset_of(original, old_paragraphs.front());
    size_t last = offset_of(original, old_paragraphs.back()) +
                  plain_text(old_paragraphs.back()).size();
    if (*position < first || *position > last)
      fail("The caret is outside the selected cell");
    caret_offset = *position - first;
  }
  auto desired = old.style;
  auto valid_length = [](double value) {
    if (!std::isfinite(value) || value < 0 || value > 100000)
      fail("Cell lengths must be between 0 and 100000 points");
  };
  auto color_text = [](const TextColor &color) {
    if (color.alpha == 0)
      return Text("transparent");
    if (color.alpha != 1)
      fail("Cell colors must be opaque or transparent");
    static const char digits[] = "0123456789abcdef";
    std::string result = "#";
    for (unsigned char v : {color.red, color.green, color.blue}) {
      result += digits[v >> 4];
      result += digits[v & 15];
    }
    return Text(result);
  };
  if (format.background) {
    (void)color_text(*format.background);
    desired.background = format.background;
    // Missing and transparent paint both reveal the table/row background.
    if (format.background->alpha == 0 && !old.style.background)
      desired.background.reset();
  }
  if (format.vertical) {
    if (*format.vertical < TextCellStyle::Top ||
        *format.vertical > TextCellStyle::Bottom)
      fail("Invalid cell vertical alignment");
    desired.vertical = *format.vertical;
  }
  for (size_t i = 0; i < 4; ++i) {
    if (format.padding[i]) {
      valid_length(*format.padding[i]);
      desired.padding[i] = *format.padding[i];
    }
    if (format.borders[i]) {
      auto border = *format.borders[i];
      if (border.style < TextTableBorder::None ||
          border.style > TextTableBorder::Outset)
        fail("Invalid cell border style");
      valid_length(border.width);
      (void)color_text(border.color);
      if (border.double_widths) {
        double total = 0;
        for (double width : *border.double_widths) {
          valid_length(width);
          total += width;
        }
        valid_length(total);
      }
      if (border.style == TextTableBorder::None || border.width == 0) {
        border.style = TextTableBorder::None;
        border.width = 0;
      }
      desired.borders[i] = border;
    }
  }
  auto canonical = [](TextCellStyle style) {
    if (style.background && style.background->alpha == 0)
      style.background.reset();
    for (auto &edge : style.borders) {
      if (edge.style == TextTableBorder::None || edge.width == 0)
        edge = {};
      else if (edge.style != TextTableBorder::Double)
        edge.double_widths.reset();
      else if (!edge.double_widths)
        edge.double_widths = std::array<double, 3>{
            edge.width / 3, edge.width / 3, edge.width / 3};
    }
    return style;
  };
  if (canonical(desired) == canonical(old.style)) {
    auto result = unchanged_cell(original, address, path);
    if (position)
      result.cursor = *position;
    return result;
  }
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto target = prepare_target(p, read_text_flow(p), path, names);
  auto current = read_text_flow(p);
  Grid grid(current.tables[table_index(current, target)]);
  auto selected = isolate_cell(p, grid, address.row, address.column, names);
  // A cell may obtain its complete style from a row/column/table default.
  // Derive from that resolved style, not merely its explicit attribute.
  auto prototype = selected.cloneNode(false);
  prototype.setAttributeNS(TABLE, "table:style-name", old.style_name);
  size_t serial = 1;
  auto name = fraction_style(p, prototype, "table-cell", {}, serial);
  XmlElement style;
  for (auto node : p.xml.elementsByTagNameNS(STYLE, "style"))
    if (node.attributeNS(STYLE, "name") == name)
      style = node;
  auto props = child(style, STYLE, "table-cell-properties");
  const Text fo = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
  if (format.background)
    props.setAttributeNS(fo, "fo:background-color",
                         color_text(*format.background));
  if (format.vertical) {
    const char *values[] = {"top", "middle", "bottom"};
    props.setAttributeNS(STYLE, "style:vertical-align",
                         values[*format.vertical]);
  }
  const char *sides[] = {"top", "right", "bottom", "left"};
  const char *styles[] = {"none",   "solid", "double", "dashed", "dotted",
                          "groove", "ridge", "inset",  "outset"};
  for (size_t i = 0; i < 4; ++i) {
    if (format.padding[i])
      props.setAttributeNS(fo, Text("fo:padding-") + sides[i],
                           point_length(*format.padding[i]));
    if (format.borders[i]) {
      const auto &border = desired.borders[i];
      props.setAttributeNS(fo, Text("fo:border-") + sides[i],
                           border.style == TextTableBorder::None
                               ? Text("none")
                               : point_length(border.width) + " " +
                                     styles[border.style] + " " +
                                     color_text(border.color));
      if (border.style == TextTableBorder::Double) {
        auto widths = border.double_widths.value_or(std::array<double, 3>{
            border.width / 3, border.width / 3, border.width / 3});
        for (double width : widths)
          if (!std::isfinite(width) || width <= 0)
            fail("Double border line widths must be positive");
        props.setAttributeNS(STYLE, Text("style:border-line-width-") + sides[i],
                             point_length(widths[0]) + " " +
                                 point_length(widths[1]) + " " +
                                 point_length(widths[2]));
      }
    }
  }
  selected.setAttributeNS(TABLE, "table:style-name", name);
  auto result_flow = read_text_flow(p);
  // Formatting does not add paragraphs, even to an originally empty cell.
  std::vector<TextCellAddress> cells;
  for (auto node = selected; node.isElement(); node = node.parentNode())
    if (node.namespaceURI() == TABLE && node.localName() == "table-cell") {
      auto index = table_index(result_flow, ancestor(node, "table"));
      for (const auto &cell : result_flow.tables[index].cells)
        if (cell.source == node) {
          cells.push_back(
              {index, cell.row, cell.column, cell.rows, cell.columns});
          break;
        }
    }
  std::reverse(cells.begin(), cells.end());
  auto result = unchanged_cell(p, cells.back(), cells);
  result.cursor += caret_offset;
  return result;
}

WordsTableEdit subdivide_words_table_cell(
    const OdfPackage &original, const TextCellAddress &address, size_t rows,
    size_t columns, const std::vector<TextCellAddress> &context,
    std::optional<double> body_width, bool equal_physical,
    NativeTextShaper *provided_shaper) {
  if (original.kind() != Kind::Words || original.read_only())
    fail("This document cannot be edited");
  if (!rows || !columns || rows > 100000 || columns > 1024)
    fail("Invalid cell subdivision dimensions");
  auto original_flow = read_text_flow(original);
  auto path = checked_path(original_flow, address, context);
  const auto &old_table = original_flow.tables[address.table];
  if (rows == 1 && columns == 1)
    return unchanged_cell(original, address, path);
  if (!equal_physical && rows == address.rows && columns == address.columns)
    return split_words_table_cell(original, address, path);
  std::optional<NativeTextShaper> owned_shaper;
  if (!provided_shaper) {
    owned_shaper.emplace();
    provided_shaper = &*owned_shaper;
  }
  auto &shaper = *provided_shaper;
  auto measured =
      measure_words_table_geometry(original_flow, path, body_width, shaper);
  bool fixed_shrunk = false;
  for (size_t c = 0; c < old_table.columns.size(); ++c) {
    const auto &col = old_table.columns[c];
    if (col.visible && col.width && col.relative <= 0 &&
        measured.columns[c] < *col.width - 1e-7)
      fixed_shrunk = true;
  }
  RefinedAxis rr =
      equal_physical && rows > 1
          ? RefinedAxis(measured.rows, address.row, address.rows, rows)
          : RefinedAxis(old_table.rows.size(), address.row, address.rows, rows);
  RefinedAxis cc = equal_physical && columns > 1
                       ? RefinedAxis(measured.columns, address.column,
                                     address.columns, columns)
                       : RefinedAxis(old_table.columns.size(), address.column,
                                     address.columns, columns);
  bool point_columns =
      (equal_physical && columns > 1) ||
      (fixed_shrunk && cc.edges.back() != old_table.columns.size());
  bool point_rows = equal_physical && rows > 1;
  auto nr = rr.edges.back(), nc = cc.edges.back();
  if (nr > 100000 || nc > 1024 || nr > 1000000 / nc)
    fail("The subdivided table exceeds the native grid limit");
  // Refinement changes logical reference coordinates across the target table.
  walk(old_table.source, [&](XmlElement e) {
    if (e.hasAttributeNS(TABLE, "formula") ||
        e.hasAttributeNS(TABLE, "number-matrix-columns-spanned") ||
        e.hasAttributeNS(TABLE, "number-matrix-rows-spanned"))
      fail("Table formulas need reference adjustment before subdivision");
  });
  auto p = original;
  p.xml = read_document_xml(original.xml.toByteArray());
  Names names(p);
  auto flow = read_text_flow(p);
  auto source = prepare_target(p, flow, path, names);
  flow = read_text_flow(p);
  Grid grid(flow.tables[table_index(flow, source)]);
  auto selected = validate_address(grid.model, address);
  auto old_rows =
      materialize_rectangle(p, grid, 0, 0, grid.rows, grid.columns, names);
  std::vector<std::vector<XmlElement>> old_slots(
      grid.rows, std::vector<XmlElement>(grid.columns));
  for (size_t r = 0; r < grid.rows; ++r)
    for (size_t c = 0; c < grid.columns; ++c)
      old_slots[r][c] = slot(old_rows[r], c).node;
  complete_columns(p, source, grid.columns);
  std::vector<XmlElement> old_columns(grid.columns);
  for (size_t c = grid.columns; c-- > 0;) {
    auto s = column_slot(source, c);
    old_columns[c] =
        isolate(s.node, "number-columns-repeated", s.instance, names);
  }
  size_t serial = 1;
  OdfPackage *style_package = &p;
  using StyleKey = std::tuple<Text, Text, std::vector<std::pair<Text, Text>>>;
  std::map<StyleKey, Text> style_cache;
  auto style_for = [&](XmlElement node, const Text &family,
                       const std::vector<std::pair<Text, Text>> &props) {
    StyleKey key{family, node.attributeNS(TABLE, "style-name"), props};
    auto found = style_cache.find(key);
    if (found != style_cache.end())
      return found->second;
    auto name = fraction_style(*style_package, node, family, props, serial);
    style_cache.emplace(std::move(key), name);
    return name;
  };
  if (point_columns)
    source.setAttributeNS(
        TABLE, "table:style-name",
        style_for(source, "table",
                  {{"width", point_length(std::accumulate(
                                 measured.columns.begin(),
                                 measured.columns.end(), 0.))}}));
  std::vector<long long> weights(grid.columns);
  long long divisor = 0;
  if (!point_columns) {
    for (size_t c = 0; c < grid.columns; ++c) {
      const auto &m = grid.model.columns[c];
      if (m.relative > 0 || !m.width) {
        double w = m.relative > 0 ? m.relative : 1;
        if (std::abs(w - std::round(w)) > 1e-6)
          fail("Relative column weights need integer normalization before "
               "subdivision");
        weights[c] =
            std::llround(w) *
            static_cast<long long>(cc.factor / (cc.edges[c + 1] - cc.edges[c]));
        divisor = std::gcd(divisor, weights[c]);
      }
    }
    if (divisor)
      for (auto &w : weights)
        w /= divisor;
  }
  for (size_t c = 0; c < grid.columns; ++c) {
    auto count = cc.edges[c + 1] - cc.edges[c];
    const auto &m = grid.model.columns[c];
    auto prototype = old_columns[c].cloneNode(false);
    auto previous = old_columns[c];
    for (size_t i = 0; i < count; ++i) {
      auto node = i ? names.clone(prototype) : old_columns[c];
      if (i) {
        previous.parentNode().insertAfter(node, previous);
        previous = node;
      }
      std::vector<std::pair<Text, Text>> props;
      if (point_columns) {
        double width = cc.lengths.empty() ? measured.columns[c] / count
                                          : cc.lengths[cc.edges[c] + i];
        if (width == 0)
          node.setAttributeNS(TABLE, "table:visibility", "collapse");
        else
          props = {{"column-width", point_length(width)},
                   {"rel-column-width", "0*"},
                   {"use-optimal-column-width", "false"}};
      } else {
        if (m.width && count > 1)
          props.emplace_back("column-width", point_length(*m.width / count));
        if ((m.relative > 0 || !m.width) &&
            (count > 1 || weights[c] != (m.relative > 0 ? m.relative : 1)))
          props.emplace_back("rel-column-width",
                             Text::number(weights[c]) + "*");
      }
      if (!props.empty())
        node.setAttributeNS(TABLE, "table:style-name",
                            style_for(prototype, "table-column", props));
    }
  }
  std::vector<XmlElement> new_rows(nr);
  for (size_t r = 0; r < grid.rows; ++r) {
    auto count = rr.edges[r + 1] - rr.edges[r];
    const auto &m = grid.model.rows[r];
    auto prototype = old_rows[r].cloneNode(false);
    auto previous = old_rows[r];
    for (size_t i = 0; i < count; ++i) {
      auto node = old_rows[r];
      if (i) {
        node = p.xml.createElementNS(TABLE, "table:table-row");
        for (auto a : prototype.attributes())
          if (a.uri == TABLE &&
              TextList{"style-name", "default-cell-style-name", "visibility"}
                  .contains(a.name.mid(a.name.indexOf(':') + 1)))
            node.setAttributeNS(a.uri, a.name, a.value);
        previous.parentNode().insertAfter(node, previous);
        previous = node;
      }
      new_rows[rr.edges[r] + i] = node;
      std::vector<std::pair<Text, Text>> props;
      if (point_rows) {
        if (rr.lengths[rr.edges[r] + i] == 0)
          node.setAttributeNS(TABLE, "table:visibility", "collapse");
        else
          props = {{"row-height", point_length(rr.lengths[rr.edges[r] + i])},
                   {"min-row-height", "0pt"},
                   {"use-optimal-row-height", "false"}};
      } else if (count > 1) {
        props = {{"min-row-height", point_length(m.minimum / count)},
                 {"use-optimal-row-height", m.height ? "false" : "true"}};
        if (m.height)
          props.emplace_back("row-height", point_length(*m.height / count));
      }
      if (!props.empty())
        node.setAttributeNS(TABLE, "table:style-name",
                            style_for(prototype, "table-row", props));
    }
  }
  std::vector<std::vector<XmlElement>> slots(nr, std::vector<XmlElement>(nc));
  for (size_t r = 0; r < grid.rows; ++r)
    for (size_t c = 0; c < grid.columns; ++c) {
      auto original_node = old_slots[r][c];
      for (size_t ir = rr.edges[r]; ir < rr.edges[r + 1]; ++ir)
        for (size_t ic = cc.edges[c]; ic < cc.edges[c + 1]; ++ic) {
          auto node = ir == rr.edges[r] && ic == cc.edges[c]
                          ? original_node
                          : blank(p, nullptr, true);
          slots[ir][ic] = node;
        }
      if (original_node.localName() == "table-cell") {
        const auto *cell = grid.cell(r, c);
        size_t end_r = cell ? cell->row + cell->rows : r + 1,
               end_c = cell ? cell->column + cell->columns : c + 1;
        set_count(original_node, "number-rows-spanned",
                  rr.edges[end_r] - rr.edges[r]);
        set_count(original_node, "number-columns-spanned",
                  cc.edges[end_c] - cc.edges[c]);
      }
    }
  // Detach only grid cells. Row groups, row identities and opaque row children
  // remain in their original containers; each old cell node is reused once.
  for (auto row : old_rows)
    for (auto node : children(row, TABLE))
      if (cell_node(node))
        row.removeChild(node);
  for (size_t r = 0; r < nr; ++r)
    for (size_t c = 0; c < nc; ++c)
      new_rows[r].appendChild(slots[r][c]);
  auto start_r = rr.edges[address.row], start_c = cc.edges[address.column];
  for (size_t r = 0; r < rows; ++r)
    for (size_t c = 0; c < columns; ++c) {
      auto ir = rr.parts[r], ic = cc.parts[c];
      auto cell = slots[ir][ic];
      if (r || c) {
        cell = converted(p, cell, false);
        slots[ir][ic] = cell;
        if (cell.attributeNS(TABLE, "style-name").empty())
          cell.setAttributeNS(TABLE, "table:style-name",
                              selected.style_name.empty()
                                  ? neutral_cell_style(p)
                                  : selected.style_name);
        if (paragraphs(cell).empty()) {
          auto empty = blank(p, &selected);
          while (!empty.firstChild().isNull())
            cell.appendChild(empty.firstChild());
        }
      }
      set_count(cell, "number-rows-spanned", rr.parts[r + 1] - rr.parts[r]);
      set_count(cell, "number-columns-spanned", cc.parts[c + 1] - cc.parts[c]);
    }
  auto result = finish(std::move(p), source, start_r, start_c, names);
  style_package = &result.document;
  if (point_rows) {
    auto candidate = read_text_flow(result.document);
    double scale = 1;
    inspect_words_table_geometry(
        candidate, result.cells, measured.body_width, shaper,
        [&](auto &, const auto &layout, const auto &) {
          for (size_t r = 0; r < rows; ++r)
            for (size_t c = 0; c < columns; ++c) {
              auto cell = std::find_if(layout.cells.begin(), layout.cells.end(),
                                       [&](const auto &v) {
                                         return v.box.row == rr.parts[r] &&
                                                v.box.column == cc.parts[c];
                                       });
              if (cell == layout.cells.end() || cell->box.rect.height <= 0)
                fail("Cannot measure the new physical part");
              scale = std::max(scale, (cell->text->height + cell->insets[0] +
                                       cell->insets[2]) /
                                          cell->box.rect.height);
            }
        });
    // Narrowing can need more text height. Grow the selected band uniformly
    // so physical row parts stay equal and the source content remains visible.
    if (scale > 1 + 1e-8)
      for (size_t r = rr.edges[address.row];
           r < rr.edges[address.row + address.rows]; ++r) {
        if (rr.lengths[r] == 0)
          continue;
        double height = rr.lengths[r] * scale;
        if (!std::isfinite(height) || height > 10000000)
          fail("The physical row subdivision exceeds the native height limit");
        new_rows[r].setAttributeNS(
            TABLE, "table:style-name",
            style_for(new_rows[r], "table-row",
                      {{"row-height", point_length(height)}}));
      }
    (void)read_text_flow(result.document);
  }
  return result;
}
} // namespace DomeOffice
