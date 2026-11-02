// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_tables.h"
#include <functional>
#include <limits>
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
  explicit Names(const OdfPackage &p) {
    walk(p.xml.documentElement(), [&](XmlElement e) {
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
} // namespace
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
  if (!repeated)
    return {};
  auto old_paragraphs = paragraphs(leaf);
  if (old_paragraphs.empty())
    fail("This table cell has no editable paragraph");
  const size_t first = offset_of(original, old_paragraphs.front()),
               last = offset_of(original, old_paragraphs.back()) +
                      plain_text(old_paragraphs.back()).size();
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
} // namespace DomeOffice
