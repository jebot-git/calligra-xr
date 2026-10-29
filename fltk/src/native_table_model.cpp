// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_table_model.h"
#include <charconv>
#include <cmath>
#include <sstream>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
size_t count(const XmlElement &node, const char *name, size_t fallback = 1) {
  auto value = node.attributeNS(TABLE, name, Text::number(fallback))
                   .trimmed()
                   .toStdString();
  std::string_view s = value;
  if (s.starts_with('+'))
    s.remove_prefix(1);
  size_t n = 0;
  auto result = std::from_chars(s.data(), s.data() + s.size(), n);
  if (result.ec != std::errc() || result.ptr != s.data() + s.size() || !n ||
      n > 1000000)
    fail("Invalid table repetition or span");
  return n;
}
double length(const Text &input) {
  auto value = input.trimmed();
  static const Regex pattern(
      "^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))(cm|mm|in|pt|pc|px)$");
  auto m = pattern.match(value);
  if (!m.hasMatch())
    fail("Invalid table length");
  auto unit = m.captured(2);
  double scale = unit == "cm"   ? 72 / 2.54
                 : unit == "mm" ? 72 / 25.4
                 : unit == "in" ? 72
                 : unit == "pc" ? 12
                 : unit == "px" ? .75
                                : 1;
  double n = m.captured(1).toDouble() * scale;
  if (!std::isfinite(n) || std::abs(n) > 100000)
    fail("Table length exceeds native limit");
  return n;
}
double positive_length(const Text &input) {
  auto n = length(input);
  if (n < 0)
    fail("Negative table dimension");
  return n;
}
bool boolean(const Text &text) {
  auto value = text.trimmed();
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  fail("Invalid table boolean");
}
TextColor color(const Text &value) {
  if (value == "transparent")
    return {0, 0, 0, 0};
  if (value == "black")
    return {};
  if (value == "white")
    return {255, 255, 255};
  auto s = value.toStdString();
  unsigned n = 0;
  if (s.size() != 7 || s.front() != '#')
    fail("Invalid table color");
  auto r = std::from_chars(s.data() + 1, s.data() + 7, n, 16);
  if (r.ec != std::errc() || r.ptr != s.data() + 7)
    fail("Invalid table color");
  return {static_cast<unsigned char>(n >> 16),
          static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)};
}
TextTableBorder border(const Text &value) {
  TextTableBorder result;
  result.style = TextTableBorder::Solid;
  result.width = .75;
  bool found_style = false, found_width = false, found_color = false;
  static const std::map<Text, TextTableBorder::Style> styles = {
      {"none", TextTableBorder::None},     {"hidden", TextTableBorder::None},
      {"solid", TextTableBorder::Solid},   {"double", TextTableBorder::Double},
      {"dashed", TextTableBorder::Dashed}, {"dotted", TextTableBorder::Dotted},
      {"groove", TextTableBorder::Groove}, {"ridge", TextTableBorder::Ridge},
      {"inset", TextTableBorder::Inset},   {"outset", TextTableBorder::Outset}};
  std::istringstream input(value.toStdString());
  std::string part;
  while (input >> part) {
    Text token(part);
    if (auto it = styles.find(token); it != styles.end()) {
      if (found_style)
        fail("Repeated table border style");
      found_style = true;
      result.style = it->second;
    } else if (token.startsWith('#') || token == "black" || token == "white" ||
               token == "transparent") {
      if (found_color)
        fail("Repeated table border color");
      found_color = true;
      result.color = color(token);
    } else {
      if (found_width)
        fail("Repeated table border width");
      found_width = true;
      result.width = token == "thin"     ? .5
                     : token == "medium" ? .75
                     : token == "thick"  ? 1.5
                                         : positive_length(token);
    }
  }
  if (!found_style && !found_width && !found_color)
    fail("Empty table border");
  if (result.style == TextTableBorder::None || result.width == 0) {
    result.style = TextTableBorder::None;
    result.width = 0;
  }
  return result;
}
void cell_properties(TextCellStyle &cell, const XmlElement &style) {
  auto p = child(style, STYLE, "table-cell-properties");
  if (p.hasAttributeNS(FO, "background-color"))
    cell.background = color(p.attributeNS(FO, "background-color"));
  if (p.hasAttributeNS(FO, "padding"))
    cell.padding.fill(positive_length(p.attributeNS(FO, "padding")));
  if (p.hasAttributeNS(FO, "border"))
    cell.borders.fill(border(p.attributeNS(FO, "border")));
  const char *sides[] = {"top", "right", "bottom", "left"};
  for (size_t i = 0; i < 4; ++i) {
    Text pad = Text("padding-") + sides[i], edge = Text("border-") + sides[i];
    if (p.hasAttributeNS(FO, pad))
      cell.padding[i] = positive_length(p.attributeNS(FO, pad));
    if (p.hasAttributeNS(FO, edge))
      cell.borders[i] = border(p.attributeNS(FO, edge));
  }
  auto double_widths = [&](const Text &value) {
    std::istringstream input(value.toStdString());
    std::array<double, 3> widths;
    std::string token;
    for (auto &width : widths) {
      if (!(input >> token))
        fail("Invalid double table border widths");
      width = positive_length(Text(token));
    }
    if ((input >> token) || widths[0] + widths[1] + widths[2] > 100000)
      fail("Invalid double table border widths");
    return widths;
  };
  if (p.hasAttributeNS(STYLE, "border-line-width")) {
    auto widths = double_widths(p.attributeNS(STYLE, "border-line-width"));
    for (auto &edge : cell.borders)
      edge.double_widths = widths;
  }
  for (size_t i = 0; i < 4; ++i) {
    Text name = Text("border-line-width-") + sides[i];
    if (p.hasAttributeNS(STYLE, name))
      cell.borders[i].double_widths = double_widths(p.attributeNS(STYLE, name));
  }
  if (p.hasAttributeNS(STYLE, "vertical-align")) {
    auto a = p.attributeNS(STYLE, "vertical-align");
    if (a == "top" || a == "automatic")
      cell.vertical = TextCellStyle::Top;
    else if (a == "middle")
      cell.vertical = TextCellStyle::Middle;
    else if (a == "bottom")
      cell.vertical = TextCellStyle::Bottom;
    else
      fail("Invalid table cell vertical alignment");
  }
}
} // namespace
struct OdfTableModelReader::Impl {
  using Key = std::pair<Text, Text>;
  std::map<Key, XmlElement> common, content, master;
  std::map<Text, XmlElement> defaults;
  Impl(const std::vector<XmlDocument> &documents) {
    for (const auto &doc : documents) {
      auto root = doc.documentElement();
      bool styles = root.localName() == "document-styles";
      for (auto group : children(root, OFFICE)) {
        bool automatic = group.localName() == "automatic-styles";
        if (!automatic && group.localName() != "styles")
          continue;
        for (auto node : children(group, STYLE)) {
          auto family = node.attributeNS(STYLE, "family");
          if (!family.startsWith("table"))
            continue;
          if (node.localName() == "default-style")
            defaults[family] = node;
          else if (node.localName() == "style")
            (automatic ? (styles ? master : content)
                       : common)[{family, node.attributeNS(STYLE, "name")}] =
                node;
        }
      }
    }
  }
  template <class Apply>
  void chain(const Text &family, const Text &name, bool styles_source,
             Apply apply) const {
    if (auto it = defaults.find(family); it != defaults.end())
      apply(it->second);
    const auto &automatic = styles_source ? master : content;
    XmlElement node;
    Key key{family, name};
    if (auto it = automatic.find(key); it != automatic.end())
      node = it->second;
    else if (auto it = common.find(key); it != common.end())
      node = it->second;
    std::vector<XmlElement> stack;
    while (!node.isNull()) {
      if (stack.size() >= 256 ||
          std::find(stack.begin(), stack.end(), node) != stack.end())
        fail("Cyclic or excessive table style inheritance");
      stack.push_back(node);
      auto parent = node.attributeNS(STYLE, "parent-style-name");
      node = {};
      if (parent.empty())
        break;
      Key next{family, parent};
      if (auto it = common.find(next); it != common.end())
        node = it->second;
      else if (auto it = automatic.find(next); it != automatic.end())
        node = it->second;
    }
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
      apply(*it);
  }
};
OdfTableModelReader::OdfTableModelReader(
    const std::vector<XmlDocument> &documents)
    : impl_(std::make_shared<Impl>(documents)) {}
TextTable OdfTableModelReader::read(const XmlElement &node,
                                    const ReadCell &read_cell, size_t &budget,
                                    bool styles_source) const {
  TextTable table;
  table.name = node.attributeNS(TABLE, "name");
  auto warn = [&](const Text &text) {
    if (std::find(table.unsupported.begin(), table.unsupported.end(), text) ==
        table.unsupported.end())
      table.unsupported.push_back(text);
  };
  impl_->chain("table", node.attributeNS(TABLE, "style-name"), styles_source,
               [&](const XmlElement &s) {
                 if (s.hasAttributeNS(STYLE, "master-page-name"))
                   table.master = s.attributeNS(STYLE, "master-page-name");
                 auto p = child(s, STYLE, "table-properties");
                 if (p.hasAttributeNS(STYLE, "width"))
                   table.width = positive_length(p.attributeNS(STYLE, "width"));
                 if (p.hasAttributeNS(STYLE, "rel-width")) {
                   auto v = p.attributeNS(STYLE, "rel-width");
                   bool ok = false;
                   if (!v.endsWith('%'))
                     fail("Invalid relative table width");
                   double n = v.left(v.size() - 1).toDouble(&ok) / 100;
                   if (!ok || !std::isfinite(n) || n <= 0 || n > 100)
                     fail("Invalid relative table width");
                   table.relative_width = n;
                 }
                 if (p.hasAttributeNS(FO, "margin"))
                   table.left = table.right = table.before = table.after =
                       length(p.attributeNS(FO, "margin"));
                 for (auto [name, target] :
                      std::initializer_list<std::pair<const char *, double *>>{
                          {"margin-left", &table.left},
                          {"margin-right", &table.right},
                          {"margin-top", &table.before},
                          {"margin-bottom", &table.after}})
                   if (p.hasAttributeNS(FO, name))
                     *target = length(p.attributeNS(FO, name));
                 if (p.hasAttributeNS(FO, "background-color"))
                   table.background =
                       color(p.attributeNS(FO, "background-color"));
                 if (p.hasAttributeNS(TABLE, "align")) {
                   auto a = p.attributeNS(TABLE, "align");
                   if (a == "left")
                     table.alignment = TextTable::Left;
                   else if (a == "right")
                     table.alignment = TextTable::Right;
                   else if (a == "center")
                     table.alignment = TextTable::Center;
                   else if (a == "margins")
                     table.alignment = TextTable::Margins;
                   else if (a == "automatic")
                     table.alignment = TextTable::Full;
                   else
                     fail("Invalid table alignment");
                 }
                 if (p.hasAttributeNS(TABLE, "border-model")) {
                   auto m = p.attributeNS(TABLE, "border-model");
                   if (m != "collapsing" && m != "separating")
                     fail("Invalid table border model");
                   table.collapsing = m == "collapsing";
                 }
                 if (p.hasAttributeNS(TABLE, "display"))
                   table.visible = boolean(p.attributeNS(TABLE, "display"));
                 if (p.hasAttributeNS(STYLE, "writing-mode")) {
                   auto m = p.attributeNS(STYLE, "writing-mode");
                   table.rtl = m == "rl-tb";
                   if (m != "rl-tb" && m != "lr-tb" && m != "page")
                     warn("Vertical table writing mode");
                 }
                 for (auto [name, target] :
                      std::initializer_list<std::pair<const char *, bool *>>{
                          {"break-before", &table.break_before},
                          {"break-after", &table.break_after}})
                   if (p.hasAttributeNS(FO, name)) {
                     auto v = p.attributeNS(FO, name);
                     *target = v == "page";
                     if (v != "page" && v != "auto")
                       warn("Table column break");
                   }
                 if (p.hasAttributeNS(FO, "keep-with-next"))
                   table.keep_next =
                       p.attributeNS(FO, "keep-with-next") == "always";
                 if (p.hasAttributeNS(STYLE, "may-break-between-rows"))
                   table.may_break_rows =
                       boolean(p.attributeNS(STYLE, "may-break-between-rows"));
                 if (!child(p, STYLE, "background-image").isNull() ||
                     p.hasAttributeNS(STYLE, "shadow"))
                   warn("Table background images and shadows");
               });
  struct RawRow {
    XmlElement node;
    bool header = false;
  };
  std::vector<RawRow> rows;
  std::function<void(const XmlElement &, bool, bool, size_t)> collect =
      [&](const XmlElement &parent, bool header, bool column_header,
          size_t depth) {
        if (depth > 64)
          fail("Excessive table group depth");
        for (auto e : children(parent, TABLE)) {
          auto name = e.localName();
          if (name == "table-column") {
            TextTableColumn column;
            column.header = column_header;
            auto visibility = e.attributeNS(TABLE, "visibility", "visible");
            column.visible = visibility == "visible";
            if (visibility != "visible" && visibility != "collapse" &&
                visibility != "filter")
              fail("Invalid table column visibility");
            column.default_cell_style =
                e.attributeNS(TABLE, "default-cell-style-name");
            impl_->chain(
                "table-column", e.attributeNS(TABLE, "style-name"),
                styles_source, [&](const XmlElement &s) {
                  auto p = child(s, STYLE, "table-column-properties");
                  if (p.hasAttributeNS(STYLE, "column-width"))
                    column.width =
                        positive_length(p.attributeNS(STYLE, "column-width"));
                  if (p.hasAttributeNS(STYLE, "rel-column-width")) {
                    auto value = p.attributeNS(STYLE, "rel-column-width");
                    bool ok = false;
                    if (!value.endsWith('*'))
                      fail("Invalid relative table column width");
                    double n = value.left(value.size() - 1).toDouble(&ok);
                    if (!ok || !std::isfinite(n) || n < 0 || n > 1000000)
                      fail("Invalid relative table column width");
                    column.relative = n;
                  }
                  if (p.hasAttributeNS(STYLE, "use-optimal-column-width"))
                    column.optimal = boolean(
                        p.attributeNS(STYLE, "use-optimal-column-width"));
                });
            auto n = count(e, "number-columns-repeated");
            if (n > 1024 - table.columns.size())
              fail("Too many native table columns");
            table.columns.insert(table.columns.end(), n, column);
          } else if (name == "table-row") {
            auto n = count(e, "number-rows-repeated");
            if (n > 100000 - rows.size())
              fail("Too many native table rows");
            rows.insert(rows.end(), n, {e, header});
          } else if (name == "table-header-rows" || name == "table-rows" ||
                     name == "table-row-group" ||
                     name == "table-header-columns" ||
                     name == "table-columns" || name == "table-column-group")
            collect(e, header || name == "table-header-rows",
                    column_header || name == "table-header-columns", depth + 1);
        }
      };
  collect(node, false, false, 0);
  // Covered cells occupy no new grid slot; any hidden source paragraphs still
  // need stable editing identities and must not disappear from the source flow.
  std::vector<std::vector<unsigned char>> occupied(rows.size());
  size_t inferred_columns = table.columns.size();
  std::map<const void *, std::vector<TextBlock>> content_cache;
  size_t charged_columns = 0;
  auto reserve_columns = [&](size_t columns) {
    if (columns <= charged_columns)
      return;
    size_t additional = (columns - charged_columns) * rows.size();
    if (additional > 1000000 - budget)
      fail("Document exceeds native table expansion limit");
    budget += additional;
    charged_columns = columns;
  };
  if (budget > 1000000)
    fail("Invalid native table budget");
  reserve_columns(std::max<size_t>(1, table.columns.size()));
  auto read_content =
      [&](const XmlElement &cell) -> const std::vector<TextBlock> & {
    auto cached = content_cache.find(cell.identity());
    if (cached == content_cache.end())
      cached = content_cache.emplace(cell.identity(), read_cell(cell)).first;
    return cached->second;
  };
  for (size_t row = 0; row < rows.size(); ++row) {
    auto e = rows[row].node;
    TextTableRow properties;
    properties.header = rows[row].header;
    auto visibility = e.attributeNS(TABLE, "visibility", "visible");
    properties.visible = visibility == "visible";
    if (visibility != "visible" && visibility != "collapse" &&
        visibility != "filter")
      fail("Invalid table row visibility");
    impl_->chain("table-row", e.attributeNS(TABLE, "style-name"), styles_source,
                 [&](const XmlElement &s) {
                   auto p = child(s, STYLE, "table-row-properties");
                   if (p.hasAttributeNS(STYLE, "row-height"))
                     properties.height =
                         positive_length(p.attributeNS(STYLE, "row-height"));
                   if (p.hasAttributeNS(STYLE, "min-row-height"))
                     properties.minimum = positive_length(
                         p.attributeNS(STYLE, "min-row-height"));
                   if (p.hasAttributeNS(STYLE, "use-optimal-row-height") &&
                       boolean(p.attributeNS(STYLE, "use-optimal-row-height")))
                     properties.height.reset();
                   if (p.hasAttributeNS(FO, "keep-together"))
                     properties.keep_together =
                         p.attributeNS(FO, "keep-together") == "always";
                   if (p.hasAttributeNS(FO, "background-color"))
                     properties.background =
                         color(p.attributeNS(FO, "background-color"));
                 });
    table.rows.push_back(properties);
    size_t column = 0;
    for (auto cell : children(e, TABLE)) {
      bool covered = cell.localName() == "covered-table-cell";
      if (!covered && cell.localName() != "table-cell")
        continue;
      auto repeat = count(cell, "number-columns-repeated");
      if (repeat > 1024 - column)
        fail("Too many native table cells in a row");
      auto row_span = covered ? 1 : count(cell, "number-rows-spanned"),
           column_span = covered ? 1 : count(cell, "number-columns-spanned");
      if (row_span > rows.size() - row || column_span > 1024 - column)
        fail("Table span exceeds its grid");
      for (size_t instance = 0; instance < repeat; ++instance, ++column) {
        if (column_span > 1024 - column)
          fail("Table span exceeds column limit");
        if (covered) {
          if (column >= occupied[row].size() || occupied[row][column] != 2)
            fail("Orphan covered table cell");
          occupied[row][column] = 1;
          read_content(cell);
          continue;
        }
        reserve_columns(column + column_span);
        for (size_t r = row; r < row + row_span; ++r) {
          if (occupied[r].size() < column + column_span)
            occupied[r].resize(column + column_span);
          for (size_t c = column; c < column + column_span; ++c) {
            if (occupied[r][c])
              fail("Overlapping table cells");
            occupied[r][c] = r == row && c == column ? 1 : 2;
          }
        }
        TextTableCell result;
        result.row = row;
        result.column = column;
        result.rows = row_span;
        result.columns = column_span;
        auto style = cell.attributeNS(TABLE, "style-name");
        if (style.empty())
          style = e.attributeNS(TABLE, "default-cell-style-name");
        if (style.empty() && column < table.columns.size())
          style = table.columns[column].default_cell_style;
        if (style.empty())
          style = node.attributeNS(TABLE, "default-cell-style-name");
        result.style_name = style;
        impl_->chain(
            "table-cell", style, styles_source,
            [&](const XmlElement &s) { cell_properties(result.style, s); });
        const auto &content = read_content(cell);
        if (content.size() > 1000000 - budget)
          fail("Document exceeds native table block reference limit");
        budget += content.size();
        result.blocks = content;
        table.cells.push_back(std::move(result));
      }
      inferred_columns = std::max(inferred_columns, column);
    }
    inferred_columns = std::max(inferred_columns, occupied[row].size());
  }
  for (const auto &row : occupied)
    if (std::find(row.begin(), row.end(), 2) != row.end())
      fail("Missing covered table cell");
  if (inferred_columns > 1024)
    fail("Too many native table columns");
  if (table.columns.empty())
    table.columns.resize(inferred_columns);
  else if (inferred_columns > table.columns.size())
    fail("Table cells exceed declared columns");
  return table;
}
} // namespace DomeOffice
