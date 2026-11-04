// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_materialize.h"
#include "native_stage_context.h"
#include "native_stage_styles.h"
#include "native_stage_repeat.h"
#include "native_table_layout.h"
#include "native_stage_source.h"
#include <unicode/ubidi.h>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
XmlElement group(XmlDocument doc, const Text &name) {
  auto root = doc.documentElement();
  auto found = child(root, OFFICE, name);
  if (!found.isNull())
    return found;
  auto g = doc.createElementNS(OFFICE, "office:" + name);
  const TextList order{"scripts",          "font-face-decls", "styles",
                       "automatic-styles", "master-styles",   "body"};
  auto rank = [&](const Text &v) {
    return std::find(order.begin(), order.end(), v) - order.begin();
  };
  for (auto n : children(root, OFFICE))
    if (rank(n.localName()) > rank(name)) {
      root.insertBefore(g, n);
      return g;
    }
  root.appendChild(g);
  return g;
}
bool identified(XmlElement n) {
  if (n.hasAttributeNS(XML, "id"))
    return true;
  for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
    if (c.isElement() && identified(c))
      return true;
  return false;
}
int property_order(const Text &v) {
  if (v == "text-properties")
    return 2;
  if (v == "paragraph-properties")
    return 1;
  if (v.endsWith("-properties"))
    return 0;
  return 3; // conditional style maps follow property groups
}
void overlay(XmlElement destination, XmlElement known) {
  for (auto p : children(known, STYLE)) {
    auto target = child(destination, STYLE, p.localName());
    if (target.isNull()) {
      target = destination.ownerDocument().importNode(p, true).toElement();
      XmlElement before;
      for (auto n : children(destination, STYLE))
        if (property_order(n.localName()) > property_order(p.localName())) {
          before = n;
          break;
        }
      destination.insertBefore(target, before);
      continue;
    }
    if (p.localName() == "text-properties") {
      if (p.hasAttributeNS(FO, "font-family"))
        target.removeAttributeNS(STYLE, "font-name");
      if (p.hasAttributeNS(STYLE, "font-name"))
        target.removeAttributeNS(FO, "font-family");
    }
    for (const auto &a : p.attributes())
      target.setAttributeNS(a.uri, a.name, a.value);
    for (auto c : children(p, STYLE)) {
      for (auto old : children(target, c.namespaceURI(), c.localName())) {
        if (identified(old))
          fail("Replacing identified style property content needs scoped "
               "reference rewriting");
        target.removeChild(old);
      }
      target.appendChild(target.ownerDocument().importNode(c, true));
    }
  }
}
bool rtl(const TextParagraph &p) {
  if (p.direction != TextParagraph::Auto)
    return p.direction == TextParagraph::RTL;
  std::vector<UChar> text;
  for (auto c : p.text) {
    if (c == U'\n')
      c = 0x2028;
    if (c <= 0xffff)
      text.push_back(UChar(c));
    else {
      c -= 0x10000;
      text.push_back(UChar(0xd800 + (c >> 10)));
      text.push_back(UChar(0xdc00 + (c & 1023)));
    }
  }
  UErrorCode error = U_ZERO_ERROR;
  auto b =
      std::unique_ptr<UBiDi, decltype(&ubidi_close)>(ubidi_open(), ubidi_close);
  if (!b)
    fail("Cannot resolve imported paragraph direction");
  const UChar empty = 0;
  ubidi_setPara(b.get(), text.empty() ? &empty : text.data(),
                int32_t(text.size()), UBIDI_DEFAULT_LTR, nullptr, &error);
  if (U_FAILURE(error))
    fail("Cannot resolve imported paragraph direction");
  return (ubidi_getParaLevel(b.get()) & 1) != 0;
}
struct Materializer {
  NativeStageMergeDraft &draft;
  NativeStageNames names;
  NativeStageStyleCatalog catalog;
  XmlDocument external;
  std::unique_ptr<NativeStageContextStyles> encoder;
  std::map<Text, Text> prefixes;
  OdfPackage shadow{Kind::Stage};
  std::map<const void *, XmlElement> originals;
  std::shared_ptr<OdfTextReader> reader;
  size_t work = 0;
  Materializer(NativeStageMergeDraft &d)
      : draft(d), names(d.package), catalog(d.source) {
    names.reserve(d.source);
    // Use the retained style document, whose nodes are keys in draft.imported.
    // The catalogue's own parsed definitions are used only for name lookup.
    for (const auto &def : catalog.definitions) {
      if (std::get<0>(def.key) == "default-style")
        continue;
      XmlElement original =
          def.source == 0
              ? def.node
              : stage_candidate_source(def.node, draft.source_styles);
      auto target = draft.candidate_node(original);
      if (target.ownerDocument().documentElement().localName() ==
          "document-styles") {
        external = target.ownerDocument();
        break;
      }
    }
    if (external.documentElement().isNull()) {
      auto bytes = draft.package.entry("styles.xml");
      if (!bytes.empty())
        external = read_document_xml(bytes);
      else
        external =
            read_document_xml(("<office:document-styles xmlns:office=\"" +
                               OFFICE + "\" office:version=\"" +
                               draft.package.xml.documentElement().attributeNS(
                                   OFFICE, "version", "1.2") +
                               "\"/>")
                                  .toUtf8());
    }
    encoder = std::make_unique<NativeStageContextStyles>(
        draft.package.xml, [&] { return names.take(); });
    prefix_definitions();
    shadow = draft.source;
    shadow.xml = read_document_xml(draft.source.xml.toByteArray());
    pair(draft.source.xml.documentElement(), shadow.xml.documentElement());
    reveal(shadow.xml);
    if (!shadow.entry("styles.xml").empty()) {
      auto s = read_document_xml(shadow.entry("styles.xml"));
      reveal(s);
      shadow.set_entry("styles.xml", s.toByteArray(), "text/xml");
    }
    reader = std::make_shared<OdfTextReader>(shadow);
  }
  void pair(XmlElement a, XmlElement b) {
    if (++work > 1000000)
      fail("Imported formatting map exceeds the limit");
    originals.emplace(b.identity(), a);
    auto c = a.firstChild(), d = b.firstChild();
    while (!c.isNull() && !d.isNull()) {
      if (c.isElement())
        pair(c.toElement(), d.toElement());
      c = c.nextSibling();
      d = d.nextSibling();
    }
    if (!c.isNull() || !d.isNull())
      fail("Invalid imported formatting source map");
  }
  static void reveal(XmlDocument doc) {
    for (auto layer : doc.elementsByTagNameNS(DRAW, "layer"))
      layer.setAttributeNS(DRAW, "draw:display", "always");
    auto visit = [&](auto &&self, XmlElement n, int depth) -> void {
      if (depth > 256)
        fail("Imported visibility traversal exceeds the limit");
      if (n.hasAttributeNS(DRAW, "display") && n.localName() != "layer")
        n.setAttributeNS(DRAW, "draw:display", "always");
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        if (c.isElement())
          self(self, c, depth + 1);
    };
    visit(visit, doc.documentElement(), 0);
  }
  XmlElement candidate(XmlElement shadow_node) {
    return draft.candidate_node(originals.at(shadow_node.identity()));
  }
  XmlElement source_definition(const NativeStageStyleCatalog::Definition &d) {
    return d.source == 0 ? d.node
                         : stage_candidate_source(d.node, draft.source_styles);
  }
  Text prefix(const Text &family) {
    if (auto at = prefixes.find(family); at != prefixes.end())
      return at->second;
    Text previous;
    TextList defaults;
    if (family == "paragraph" || family == "graphic" ||
        family == "presentation" || family == "table-cell")
      defaults.push_back("text");
    if (family == "graphic" || family == "presentation" ||
        family == "table-cell")
      defaults.push_back("paragraph");
    defaults.push_back(family);
    for (auto f : defaults)
      if (auto at = catalog.defaults.find(f); at != catalog.defaults.end()) {
        auto node = clone_stage_source_identities(
            catalog.definitions[at->second].node, names);
        node.renameLocal("style");
        node.setAttributeNS(STYLE, "style:name", names.take());
        node.setAttributeNS(STYLE, "style:family", family);
        if (!previous.empty())
          node.setAttributeNS(STYLE, "style:parent-style-name", previous);
        for (auto p : children(node, STYLE, "paragraph-properties")) {
          p.removeAttributeNS(STYLE, "tab-stop-distance");
          p.removeAttributeNS(TEXT, "relative-tab-stop-position");
        }
        previous = node.attributeNS(STYLE, "name");
        group(external, "styles").appendChild(external.importNode(node, true));
      }
    prefixes[family] = previous;
    return previous;
  }
  void prefix_definitions() {
    for (const auto &d : catalog.definitions) {
      auto [kind, family, name] = d.key;
      if (kind != "style" || family == "text" || family == "paragraph" ||
          d.node.hasAttributeNS(STYLE, "parent-style-name"))
        continue;
      auto parent = prefix(family);
      if (parent.empty())
        continue;
      draft.candidate_node(source_definition(d))
          .setAttributeNS(STYLE, "style:parent-style-name", parent);
    }
  }
  XmlElement snapshot(const Text &family, const Text &old, XmlElement known) {
    XmlElement result;
    if (!old.empty()) {
      const auto &d =
          catalog.definitions.at(catalog.resolve({"style", family, old}, 0));
      result = clone_stage_source_identities(d.node, names);
      result = draft.package.xml.importNode(result, true).toElement();
    } else
      result = draft.package.xml.createElementNS(STYLE, "style:style");
    result.setAttributeNS(STYLE, "style:name",
                          known.attributeNS(STYLE, "name"));
    result.setAttributeNS(STYLE, "style:family", family);
    if (!result.hasAttributeNS(STYLE, "parent-style-name")) {
      auto parent = prefix(family);
      if (!parent.empty())
        result.setAttributeNS(STYLE, "style:parent-style-name", parent);
    }
    overlay(result, known);
    group(draft.package.xml, "automatic-styles").appendChild(result);
    return result;
  }
  void assign(XmlElement node, const Text &uri, const Text &attribute,
              const Text &family, XmlElement known,
              const Text &source_name = {}) {
    auto old =
        source_name.empty() ? node.attributeNS(uri, attribute) : source_name;
    auto style = snapshot(family, old, known);
    node.setAttributeNS(uri,
                        (uri == DRAW           ? Text("draw:")
                         : uri == PRESENTATION ? Text("presentation:")
                         : uri == TABLE        ? Text("table:")
                                               : Text("text:")) +
                            attribute,
                        style.attributeNS(STYLE, "name"));
  }
  TextParagraph base(const SceneShape &s) {
    TextParagraph p;
    p.font = s.paint.font;
    if (!s.paint.use_window_text_color)
      p.color = s.paint.text_color;
    return p;
  }
  void paragraph(const SceneShape &s, const TextFlow &flow,
                 const TextBlock &block, double extent) {
    auto value = block_paragraph(flow, block);
    auto original = flow.paragraph_sources.at(block.index);
    value.tabs =
        native_stage_portable_tabs(value, receiving, extent, rtl(value));
    if (value.list && value.list->alignment_mode)
      value.indent = value.list->indent;
    assign(candidate(original), TEXT, "style-name", "paragraph",
           encoder->paragraph(value));
  }
  TextParagraph receiving;
  void blocks(const SceneShape &s, const TextFlow &flow,
              const std::vector<TextBlock> &content, double available,
              std::vector<Text> cells, NativeBlockLayouter &layout) {
    if (++work > 1000000 || cells.size() > 16)
      fail("Imported table context exceeds the limit");
    for (const auto &block : content) {
      if (block.kind == TextBlock::Paragraph) {
        paragraph(s, flow, block, available);
        continue;
      }
      const auto &table = block_table(flow, block);
      auto geometry = layout.table(block, available);
      auto target = candidate(table.source);
      assign(target, TABLE, "style-name", "table",
             encoder->table(table, geometry->width, available));
      for (size_t i = 0; i < table.columns.size(); ++i) {
        const auto &column = table.columns[i];
        XmlElement c;
        if (column.source.isNull()) {
          c = draft.package.xml.createElementNS(TABLE, "table:table-column");
          XmlElement before;
          for (auto n : children(target, TABLE))
            if (n.localName().startsWith("table-row") ||
                n.localName() == "table-header-rows") {
              before = n;
              break;
            }
          target.insertBefore(c, before);
        } else
          c = candidate(column.source);
        assign(c, TABLE, "style-name", "table-column",
               encoder->column(column, geometry->column_edges[i + 1] -
                                           geometry->column_edges[i]));
      }
      for (const auto &row : table.rows)
        assign(candidate(row.source), TABLE, "style-name", "table-row",
               encoder->row(row));
      for (size_t i = 0; i < table.cells.size(); ++i) {
        const auto &cell = table.cells[i];
        auto contexts = cells;
        contexts.push_back(cell.style_name);
        auto context = reader->paragraph_context({}, base(s), {}, contexts);
        auto cell_node = candidate(cell.source);
        assign(cell_node, TABLE, "style-name", "table-cell",
               encoder->cell(cell.style, context), cell.style_name);
        blocks(s, flow, cell.blocks,
               std::max(.001, geometry->cells.at(i).content_clip.width),
               std::move(contexts), layout);
      }
    }
  }
  void run() {
    receiving = OdfTextReader(draft.package).paragraph_context({}, {});
    auto scenes = read_presentation_scenes(shadow, reader);
    auto measure = [](const Text &text, const TextFont &font) {
      return TextMetrics{double(text.size()) * font.size / 2, font.size * .8,
                         font.size * .2};
    };
    for (const auto &scene : scenes)
      for (const auto &shape : scene.shapes) {
        if (shape.master)
          continue;
        auto node = candidate(shape.source);
        bool presentation = node.hasAttributeNS(PRESENTATION, "style-name");
        assign(node, presentation ? PRESENTATION : DRAW, "style-name",
               presentation ? Text("presentation") : Text("graphic"),
               encoder->graphic(shape.paint, presentation ? Text("presentation")
                                                          : Text("graphic")));
        if (shape.text_flow.blocks.empty())
          continue;
        const auto area = shape.text_area.value_or(
            SceneRect{0, 0, shape.width, shape.height});
        double width =
            area.width - shape.paint.padding_left - shape.paint.padding_right;
        if (width <= 0)
          fail("Imported text/table formatting needs a positive source extent");
        NativeBlockLayouter layout(
            shape.text_flow,
            table_paragraph_layouter(shape.text_flow, measure));
        blocks(shape, shape.text_flow, shape.text_flow.blocks, width, {},
               layout);
      }
    for (auto style : encoder->supporting_styles())
      group(external, "styles").appendChild(external.importNode(style, true));
    for (auto face : encoder->font_declarations())
      group(external, "font-face-decls")
          .appendChild(external.importNode(face, true));
    draft.package.set_entry("styles.xml", external.toByteArray(), "text/xml");
    draft.package.validate_archive_limits();
    auto final = std::make_shared<OdfTextReader>(draft.package);
    (void)read_presentation_scenes(draft.package, final);
    (void)read_presentation_scenes(draft.package, final, SceneTarget::Print);
  }
};
} // namespace
void materialize_stage_import_contexts(NativeStageMergeDraft &draft) {
  Materializer(draft).run();
}
} // namespace DomeOffice
