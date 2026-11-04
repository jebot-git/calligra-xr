// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_context.h"
#include <charconv>
#include <libxml/tree.h>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
Text number(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1000000000)
    fail("Invalid materialized style number");
  if (value == 0)
    return "0";
  std::array<char, 512> buffer;
  auto r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                         std::chars_format::fixed);
  if (r.ec != std::errc{})
    fail("Cannot represent materialized style number");
  return Text(std::string(buffer.data(), r.ptr));
}
Text pt(double value) { return number(value) + "pt"; }
Text rgb(TextColor value) {
  if (value.alpha != 1)
    fail("A materialized text/paint color requires opaque RGB");
  const char h[] = "0123456789abcdef";
  Text result = "#";
  for (auto component : {value.red, value.green, value.blue}) {
    result.push_back(h[component >> 4]);
    result.push_back(h[component & 15]);
  }
  return result;
}
Text rgb_opaque(TextColor value) {
  value.alpha = 1;
  return rgb(value);
}
void language(XmlElement p, const Text &value) {
  if (value.empty()) {
    p.setAttributeNS(FO, "fo:language", "none");
    p.setAttributeNS(FO, "fo:country", "none");
    return;
  }
  auto dash = value.indexOf('-');
  auto primary = dash < 0 ? value : value.left(dash);
  auto country = dash < 0 ? Text("none") : value.mid(dash + 1);
  if (primary.empty() || primary.size() > 8 || country.empty() ||
      country.size() > 8 ||
      !std::all_of(primary.begin(), primary.end(),
                   [](char32_t c) {
                     return (c >= U'A' && c <= U'Z') ||
                            (c >= U'a' && c <= U'z');
                   }) ||
      !std::all_of(country.begin(), country.end(), [](char32_t c) {
        return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
               (c >= U'0' && c <= U'9');
      }))
    fail("This language context needs native script/RFC tag materialization");
  p.setAttributeNS(FO, "fo:language", primary);
  p.setAttributeNS(FO, "fo:country", country);
}
XmlElement add(XmlElement parent, const Text &name) {
  auto node = parent.ownerDocument().createElementNS(STYLE, "style:" + name);
  parent.appendChild(node);
  return node;
}
void set(XmlElement node, const Text &uri, const char *name,
         const Text &value) {
  node.setAttributeNS(uri,
                      Text(uri == FO      ? "fo:"
                           : uri == DRAW  ? "draw:"
                           : uri == SVG   ? "svg:"
                           : uri == TABLE ? "table:"
                                          : "style:") +
                          name,
                      value);
}
Text line_style(TextDecoration::LineStyle style) {
  const TextList names{"none",      "solid",    "dotted",       "dash",
                       "long-dash", "dot-dash", "dot-dot-dash", "wave"};
  if (style < TextDecoration::None || style > TextDecoration::Wave)
    fail("Invalid materialized text line style");
  return names.at(size_t(style));
}
Text automatic_width(const TextDecoration &line) {
  if (line.width_unit == TextDecoration::FontSize)
    return number(line.width * 100) + "%";
  if (line.width_unit == TextDecoration::Points)
    return pt(line.width);
  if (line.width == 1)
    return "auto";
  if (line.width == 1.5)
    return "bold";
  if (line.width == .7)
    return "thin";
  if (line.width == 2)
    return "thick";
  fail("Unsupported materialized automatic text line width");
}
} // namespace
std::vector<TextTabStop>
native_stage_portable_tabs(const TextParagraph &source,
                           const TextParagraph &receiving, double extent,
                           bool rtl) {
  double body = source.left;
  if (source.list && !source.list->alignment_mode)
    body += source.list->space_before + source.list->min_width;
  for (double value : {source.default_tab, receiving.default_tab})
    if (!std::isfinite(value) || value <= 0 || value > 100000)
      fail("Invalid portable default tab interval");
  if (!std::isfinite(extent) || extent < 0 || extent > 100000 ||
      !std::isfinite(body) || std::abs(body) > 100000 ||
      source.tabs.size() > 4096)
    fail("Invalid portable tab extent or context");
  std::optional<double> last;
  for (const auto &tab : source.tabs) {
    if (!std::isfinite(tab.position) || std::abs(tab.position) > 100000 ||
        (last && tab.position <= *last))
      fail("Portable tab stops must be finite and ordered");
    last = tab.position;
  }
  if (source.default_tab == receiving.default_tab &&
      source.relative_tabs == receiving.relative_tabs)
    return source.tabs;
  if (source.list && source.list->visible && source.list->alignment_mode &&
      source.list->follow == TextListLabel::Tab)
    fail("Portable tabs for a tab-followed list label need list geometry "
         "isolation");
  const double from = source.relative_tabs ? body : 0;
  const double to = receiving.relative_tabs ? body : 0;
  auto result = source.tabs;
  std::optional<double> converted;
  for (auto &tab : result) {
    tab.position += from - to;
    if (!std::isfinite(tab.position) || std::abs(tab.position) > 100000 ||
        (converted && tab.position <= *converted))
      fail("Portable tab origin conversion exceeds representable limits");
    converted = tab.position;
  }
  double first =
      last ? std::max(0., std::floor(*last / source.default_tab) + 1) : 0;
  double final =
      std::floor((extent - from + source.default_tab) / source.default_tab);
  if (!std::isfinite(final) || final - first + 1 > double(4096 - result.size()))
    fail("Portable default tab coverage exceeds the limit");
  size_t count = final >= first ? size_t(final - first + 1) : 0;
  for (size_t i = 0; i < count; ++i) {
    TextTabStop tab;
    tab.position = (first + double(i)) * source.default_tab + from - to;
    if (!std::isfinite(tab.position) || std::abs(tab.position) > 100000 ||
        (converted && tab.position <= *converted))
      fail("Portable tab position exceeds the limit");
    converted = tab.position;
    tab.type = rtl ? TextTabStop::Right : TextTabStop::Left;
    result.push_back(std::move(tab));
  }
  return result;
}
NativeStageContextStyles::NativeStageContextStyles(
    XmlDocument document, std::function<Text()> fresh_name)
    : document_(std::move(document)), fresh_(std::move(fresh_name)) {
  if (document_.documentElement().isNull() || !fresh_)
    fail("Materialized styles require a document and a name allocator");
}
Text NativeStageContextStyles::name() {
  if (++definitions_ > 100000)
    fail("Materialized style count exceeds the limit");
  auto name = fresh_();
  auto utf8 = name.toUtf8();
  if (name.empty() || name.size() > 1024 ||
      xmlValidateNCName(reinterpret_cast<const xmlChar *>(utf8.c_str()), 0) !=
          0 ||
      !allocated_.insert(name).second)
    fail("Invalid allocated materialized style name");
  return name;
}
XmlElement NativeStageContextStyles::style(const Text &family) {
  auto node = document_.createElementNS(STYLE, "style:style");
  set(node, STYLE, "name", name());
  set(node, STYLE, "family", family);
  return node;
}
void NativeStageContextStyles::font(XmlElement properties,
                                    const TextFont &font) {
  if (!std::isfinite(font.size) || font.size <= 0 || font.size > 1000 ||
      font.weight < 1 || font.weight > 1000)
    fail("Invalid materialized font");
  if (font.family.empty()) {
    // Referencing an unconstrained, standard font declaration explicitly
    // restores the native consumer's default-font policy. Pinning an arbitrary
    // family here would change DomeD theme behavior and headless print policy.
    if (unconstrained_font_.empty()) {
      unconstrained_font_ = name();
      auto face = document_.createElementNS(STYLE, "style:font-face");
      set(face, STYLE, "name", unconstrained_font_);
      fonts_.push_back(face);
    }
    set(properties, STYLE, "font-name", unconstrained_font_);
  } else
    set(properties, FO, "font-family", font.family);
  set(properties, FO, "font-size", pt(font.size));
  set(properties, FO, "font-weight", Text::number(font.weight));
  set(properties, FO, "font-style", font.italic ? "italic" : "normal");
  language(properties, font.language);
}
void NativeStageContextStyles::decoration(XmlElement p, const Text &stem,
                                          const TextDecoration &line) {
  if (!line.valid())
    fail("Invalid materialized text decoration");
  p.setAttributeNS(STYLE, "style:" + stem + "-style", line_style(line.style));
  p.setAttributeNS(STYLE, "style:" + stem + "-type",
                   line.style == TextDecoration::None ? Text("none")
                   : line.double_line                 ? Text("double")
                                                      : Text("single"));
  p.setAttributeNS(STYLE, "style:" + stem + "-mode",
                   line.skip_space ? Text("skip-white-space")
                                   : Text("continuous"));
  p.setAttributeNS(STYLE, "style:" + stem + "-color",
                   line.color ? rgb(*line.color) : Text("font-color"));
  p.setAttributeNS(STYLE, "style:" + stem + "-width", automatic_width(line));
}
void NativeStageContextStyles::characters(XmlElement p,
                                          const TextParagraph &value) {
  font(p, value.font);
  set(p, STYLE, "use-window-font-color", value.color ? "false" : "true");
  if (value.color)
    set(p, FO, "color", rgb(*value.color));
  set(p, FO, "background-color",
      value.background ? rgb(*value.background) : Text("transparent"));
  decoration(p, "text-underline", value.decorations.underline);
  decoration(p, "text-line-through", value.decorations.strike);
}
void NativeStageContextStyles::tab_stops(XmlElement p,
                                         const std::vector<TextTabStop> &tabs) {
  auto group = add(p, "tab-stops");
  for (const auto &tab : tabs) {
    if (++tabs_ > 262144 || tabs.size() > 4096)
      fail("Materialized tab count exceeds the limit");
    auto node = add(group, "tab-stop");
    set(node, STYLE, "position", pt(tab.position));
    set(node, STYLE, "type",
        tab.type == TextTabStop::Right       ? "right"
        : tab.type == TextTabStop::Center    ? "center"
        : tab.type == TextTabStop::Character ? "char"
                                             : "left");
    if (tab.type == TextTabStop::Character)
      set(node, STYLE, "char", Text(1, tab.delimiter));
    set(node, STYLE, "leader-style", line_style(tab.line_style));
    set(node, STYLE, "leader-type",
        tab.line_style == TextTabStop::None ? "none"
        : tab.double_line                   ? "double"
                                            : "single");
    set(node, STYLE, "leader-width",
        tab.width ? pt(*tab.width) : number(tab.width_scale * 100) + "%");
    set(node, STYLE, "leader-color",
        tab.color ? rgb(*tab.color) : Text("font-color"));
    if (!tab.leader.empty())
      set(node, STYLE, "leader-text", tab.leader);
    const auto &patch = tab.leader_font;
    if (patch.family || patch.language || patch.weight || patch.italic ||
        patch.size || patch.scale != 1 || patch.color_set) {
      auto support = style("text");
      auto properties = add(support, "text-properties");
      if (patch.family) {
        TextFont f;
        f.family = *patch.family;
        font(properties, f);
        for (const auto &key :
             {"font-size", "font-weight", "font-style", "language", "country"})
          properties.removeAttributeNS(FO, key);
      }
      if (patch.language) {
        language(properties, *patch.language);
      }
      if (patch.weight)
        set(properties, FO, "font-weight", Text::number(*patch.weight));
      if (patch.italic)
        set(properties, FO, "font-style", *patch.italic ? "italic" : "normal");
      if (patch.size || patch.scale != 1)
        set(properties, FO, "font-size",
            patch.size ? pt(*patch.size * patch.scale)
                       : number(patch.scale * 100) + "%");
      if (patch.color_set) {
        set(properties, STYLE, "use-window-font-color",
            patch.color ? "false" : "true");
        if (patch.color)
          set(properties, FO, "color", rgb(*patch.color));
      }
      set(node, STYLE, "leader-text-style", support.attributeNS(STYLE, "name"));
      supporting_.push_back(support);
    }
  }
}
XmlElement NativeStageContextStyles::paragraph(const TextParagraph &value) {
  auto node = style("paragraph");
  auto p = add(node, "paragraph-properties");
  for (auto [key, v] : std::initializer_list<std::pair<const char *, double>>{
           {"margin-left", value.left},
           {"margin-right", value.right},
           {"margin-top", value.before},
           {"margin-bottom", value.after},
           {"text-indent", value.indent}})
    set(p, FO, key, pt(v));
  const TextList alignment{"left",    "center", "right",
                           "justify", "start",  "end"};
  set(p, FO, "text-align", alignment.at(size_t(value.alignment)));
  set(p, FO, "line-height",
      value.line_height > 0 ? pt(value.line_height)
                            : number(value.line_factor * 100) + "%");
  set(p, FO, "break-before", value.break_before ? "page" : "auto");
  set(p, FO, "break-after", value.break_after ? "page" : "auto");
  set(p, FO, "keep-together", value.keep_together ? "always" : "auto");
  set(p, FO, "keep-with-next", value.keep_next ? "always" : "auto");
  set(p, FO, "orphans", Text::number(value.orphans));
  set(p, FO, "widows", Text::number(value.widows));
  set(p, STYLE, "writing-mode",
      value.direction == TextParagraph::RTL   ? "rl-tb"
      : value.direction == TextParagraph::LTR ? "lr-tb"
                                              : "page");
  set(p, STYLE, "page-number",
      value.page_number ? Text::number(*value.page_number) : Text("auto"));
  tab_stops(p, value.tabs);
  characters(add(node, "text-properties"), value);
  return node;
}
XmlElement NativeStageContextStyles::graphic(const ScenePaint &value,
                                             const Text &family) {
  if (family != "graphic" && family != "presentation")
    fail("Paint context requires a graphic or presentation style");
  for (double alpha :
       {value.fill_color.alpha, value.stroke_color.alpha, value.image_opacity})
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
      fail("Invalid materialized paint opacity");
  for (double v :
       {value.stroke_width, value.padding, value.padding_left,
        value.padding_right, value.padding_top, value.padding_bottom})
    if (!std::isfinite(v) || v < 0 || v > 100000)
      fail("Invalid materialized paint geometry");
  auto node = style(family);
  auto p = add(node, "graphic-properties");
  set(p, DRAW, "fill", value.fill ? "solid" : "none");
  set(p, DRAW, "fill-color", rgb_opaque(value.fill_color));
  set(p, DRAW, "opacity", number(value.fill_color.alpha * 100) + "%");
  set(p, DRAW, "stroke",
      value.stroke ? (value.dashed ? "dash" : "solid") : "none");
  set(p, DRAW, "stroke-dash", value.dash_name);
  set(p, DRAW, "stroke-dash-names", value.dash_names.join(' '));
  set(p, SVG, "stroke-color", rgb_opaque(value.stroke_color));
  set(p, SVG, "stroke-opacity", number(value.stroke_color.alpha * 100) + "%");
  set(p, SVG, "stroke-width", pt(value.stroke_width));
  set(p, SVG, "fill-rule", value.even_odd ? "evenodd" : "nonzero");
  set(p, SVG, "stroke-linecap",
      value.stroke_cap == ScenePaint::Cap::Round    ? "round"
      : value.stroke_cap == ScenePaint::Cap::Square ? "square"
                                                    : "butt");
  set(p, DRAW, "stroke-linejoin",
      value.stroke_join == ScenePaint::Join::Round   ? "round"
      : value.stroke_join == ScenePaint::Join::Bevel ? "bevel"
      : value.stroke_join == ScenePaint::Join::None  ? "none"
                                                     : "miter");
  set(p, DRAW, "image-opacity", number(value.image_opacity * 100) + "%");
  set(p, FO, "padding", pt(value.padding));
  for (auto [key, v] : std::initializer_list<std::pair<const char *, double>>{
           {"padding-left", value.padding_left},
           {"padding-right", value.padding_right},
           {"padding-top", value.padding_top},
           {"padding-bottom", value.padding_bottom}})
    set(p, FO, key, pt(v));
  set(p, DRAW, "textarea-vertical-align",
      value.vertical_alignment == ScenePaint::Middle   ? "middle"
      : value.vertical_alignment == ScenePaint::Bottom ? "bottom"
                                                       : "top");
  auto t = add(node, "text-properties");
  font(t, value.font);
  set(t, STYLE, "use-window-font-color",
      value.use_window_text_color ? "true" : "false");
  if (!value.use_window_text_color)
    set(t, FO, "color", rgb(value.text_color));
  return node;
}

XmlElement NativeStageContextStyles::cell(const TextCellStyle &value,
                                          const TextParagraph &context) {
  auto node = paragraph(context);
  set(node, STYLE, "family", "table-cell");
  auto p = document_.createElementNS(STYLE, "style:table-cell-properties");
  node.insertBefore(p, node.firstChild());
  set(p, FO, "background-color",
      !value.background || value.background->alpha == 0
          ? Text("transparent")
          : rgb(*value.background));
  set(p, STYLE, "vertical-align",
      value.vertical == TextCellStyle::Top      ? "top"
      : value.vertical == TextCellStyle::Middle ? "middle"
                                                : "bottom");
  const TextList sides{"top", "right", "bottom", "left"};
  const TextList styles{"none",   "solid", "double", "dashed", "dotted",
                        "groove", "ridge", "inset",  "outset"};
  for (size_t i = 0; i < 4; ++i) {
    set(p, FO, ("padding-" + sides[i]).toUtf8().c_str(), pt(value.padding[i]));
    const auto &b = value.borders[i];
    if (b.style < TextTableBorder::None || b.style > TextTableBorder::Outset)
      fail("Invalid materialized cell border");
    auto color = b.color.alpha == 0 ? Text("transparent") : rgb(b.color);
    set(p, FO, ("border-" + sides[i]).toUtf8().c_str(),
        pt(b.width) + " " + styles.at(size_t(b.style)) + " " + color);
    if (b.style == TextTableBorder::Double && b.width > 0) {
      auto widths = b.double_widths.value_or(
          std::array<double, 3>{b.width / 3, b.width / 3, b.width / 3});
      if (std::any_of(widths.begin(), widths.end(), [](double value) {
            return !std::isfinite(value) || value <= 0;
          }))
        fail("A portable double border needs three positive components");
      set(p, STYLE, ("border-line-width-" + sides[i]).toUtf8().c_str(),
          pt(widths[0]) + " " + pt(widths[1]) + " " + pt(widths[2]));
    }
  }
  return node;
}
XmlElement NativeStageContextStyles::table(const TextTable &value, double width,
                                           double available) {
  if (!std::isfinite(width) || width <= 0 || !std::isfinite(available) ||
      available <= 0)
    fail("Materialized table has no positive width");
  auto node = style("table");
  auto p = add(node, "table-properties");
  set(p, STYLE, "width", pt(width));
  set(p, STYLE, "rel-width",
      number(value.relative_width.value_or(width / available) * 100) + "%");
  for (auto [key, v] : std::initializer_list<std::pair<const char *, double>>{
           {"margin-left", value.left},
           {"margin-right", value.right},
           {"margin-top", value.before},
           {"margin-bottom", value.after}})
    set(p, FO, key, pt(v));
  const TextList align{"margins", "left", "center", "right", "margins"};
  set(p, TABLE, "align", align.at(size_t(value.alignment)));
  set(p, TABLE, "border-model", value.collapsing ? "collapsing" : "separating");
  set(p, TABLE, "display", value.visible ? "true" : "false");
  set(p, STYLE, "writing-mode", value.rtl ? "rl-tb" : "lr-tb");
  set(p, FO, "background-color",
      !value.background || value.background->alpha == 0
          ? Text("transparent")
          : rgb(*value.background));
  set(p, FO, "break-before", value.break_before ? "page" : "auto");
  set(p, FO, "break-after", value.break_after ? "page" : "auto");
  set(p, FO, "keep-with-next", value.keep_next ? "always" : "auto");
  set(p, STYLE, "may-break-between-rows",
      value.may_break_rows ? "true" : "false");
  set(p, STYLE, "page-number",
      value.page_number ? Text::number(*value.page_number) : Text("auto"));
  return node;
}
XmlElement NativeStageContextStyles::row(const TextTableRow &value) {
  auto node = style("table-row");
  auto p = add(node, "table-row-properties");
  set(p, STYLE, "min-row-height", pt(value.minimum));
  set(p, STYLE, "use-optimal-row-height", value.height ? "false" : "true");
  if (value.height)
    set(p, STYLE, "row-height", pt(*value.height));
  set(p, FO, "keep-together", value.keep_together ? "always" : "auto");
  set(p, FO, "background-color",
      !value.background || value.background->alpha == 0
          ? Text("transparent")
          : rgb(*value.background));
  return node;
}
XmlElement NativeStageContextStyles::column(const TextTableColumn &value,
                                            double width) {
  auto node = style("table-column");
  auto p = add(node, "table-column-properties");
  set(p, STYLE, "column-width", pt(width));
  set(p, STYLE, "rel-column-width",
      number(value.relative > 0 ? value.relative
             : !value.width     ? 1
                                : 0) +
          "*");
  set(p, STYLE, "use-optimal-column-width", value.optimal ? "true" : "false");
  return node;
}
} // namespace DomeOffice
