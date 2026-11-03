// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "native_stage_source.h"
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
template <class F> void walk(XmlElement root, F visit, XmlElement skip = {}) {
  size_t count = 0;
  auto next = [&](auto &&self, XmlElement node) -> void {
    if (node == skip)
      return;
    if (++count > 1000000)
      fail("Presentation reference scan exceeds the limit");
    if (node.isElement())
      visit(node);
    for (auto child = node.firstChild(); !child.isNull();
         child = child.nextSibling())
      self(self, child);
  };
  next(next, root);
}
Text unique(const std::set<Text> &names, const Text &prefix) {
  for (size_t n = 1; n <= 1000001; ++n) {
    auto name = prefix + Text::number(n);
    if (!names.contains(name))
      return name;
  }
  fail("Presentation name limit exceeded");
}
Text pt(double n) { return Text::number(n, 'g', 17) + "pt"; }
void geometry(NativeShapeKind kind, SceneRect rect, const Text &text) {
  for (auto value : {rect.x, rect.y, rect.width, rect.height,
                     rect.x + rect.width, rect.y + rect.height})
    if (!std::isfinite(value) || std::abs(value) > 1e7)
      fail("Shape coordinates exceed the native limit");
  if (kind == NativeShapeKind::Line) {
    if (rect.x + rect.width == rect.x && rect.y + rect.height == rect.y)
      fail("A line needs two different endpoints");
  } else if (rect.width <= 0 || rect.height <= 0)
    fail("Shape width and height must be positive");
  switch (kind) {
  case NativeShapeKind::Rectangle:
  case NativeShapeKind::Ellipse:
  case NativeShapeKind::Line:
  case NativeShapeKind::TextBox:
    break;
  default:
    fail("Unsupported shape kind");
  }
  if (kind == NativeShapeKind::Line && !text.empty())
    fail("A line cannot contain editable text");
  if (text.size() > 4000000)
    fail("Text frame exceeds native layout text limit");
  size_t line = 0, paragraphs = 1;
  for (auto c : text) {
    if (c == '\n') {
      line = 0;
      if (++paragraphs > 100000)
        fail("Too many text paragraphs");
    } else if (++line > 1000000)
      fail("Paragraph exceeds layout text limit");
  }
}
std::vector<Text> tokens(const Text &text) {
  std::istringstream in(text.toStdString());
  std::string token;
  std::vector<Text> out;
  while (in >> token)
    out.emplace_back(token);
  return out;
}
Text unescape(Text value) {
  auto bytes = value.toStdString();
  std::string result;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] == '%' && i + 2 < bytes.size() && hex(bytes[i + 1]) >= 0 &&
        hex(bytes[i + 2]) >= 0) {
      result += char(hex(bytes[i + 1]) * 16 + hex(bytes[i + 2]));
      i += 2;
    } else
      result += bytes[i];
  }
  return Text(result);
}
void external_references(XmlElement root, XmlElement removed,
                         const std::set<Text> &ids,
                         const std::set<Text> &names) {
  size_t comparisons = 0;
  walk(
      root,
      [&](XmlElement node) {
        for (const auto &a : node.attributes()) {
          auto name = a.name.mid(a.name.indexOf(':') + 1);
          bool idref =
              (a.uri == DRAW && (name == "start-shape" || name == "end-shape" ||
                                 name == "caption-id")) ||
              (a.uri == SMIL && name == "targetElement") ||
              (a.uri == TEXT && name == "ref-name");
          bool named =
              a.uri == DRAW && (name == "copy-of" || name == "chain-next-name");
          if ((idref && (ids.contains(a.value) || names.contains(a.value))) ||
              (named && names.contains(a.value)))
            fail("This object is referenced by an animation, link, connector "
                 "or chained frame");
          if (a.uri == XLINK && name == "href" && a.value.startsWith('#')) {
            auto target = unescape(a.value.mid(1));
            if (ids.contains(target) || names.contains(target))
              fail("This object is the target of a document link");
          }
          if (a.uri == SMIL && (name == "begin" || name == "end")) {
            for (const auto &term : a.value.split(';'))
              for (const auto &id : ids) {
                if (++comparisons > 1000000)
                  fail("Presentation reference scan exceeds the limit");
                if (term.trimmed().startsWith(id + "."))
                  fail("This object is referenced by an animation event");
              }
          }
        }
      },
      removed);
}
} // namespace
PresentationPosition Presentation::insert_shape(int slide_index,
                                                NativeShapeKind kind,
                                                SceneRect rect,
                                                const Text &text,
                                                PresentationPosition previous) {
  geometry(kind, rect, text);
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    fail("This presentation is read-only");
  (void)scene(slide_index);
  auto before = package_.xml.toByteArray();
  auto candidate = read_document_xml(before);
  auto root = candidate.documentElement();
  auto body = child(child(root, OFFICE, "body"), OFFICE, "presentation");
  auto page = children(body, DRAW, "page").at(slide_index);
  std::set<Text> names;
  auto collect = [&](XmlElement e) {
    for (const auto &a : e.attributes()) {
      auto n = a.name.mid(a.name.indexOf(':') + 1);
      // Reserve references too: filling an originally missing style/ID would
      // change unrelated source behavior even though no definition collides.
      if (n == "name" || n == "id" || n.endsWith("-name") ||
          n.endsWith("-id") ||
          (a.uri == DRAW &&
           (n == "start-shape" || n == "end-shape" || n == "copy-of")) ||
          (a.uri == SMIL && n == "targetElement"))
        names.insert(a.value);
      if (n.endsWith("-names") || (a.uri == DRAW && n == "nav-order"))
        for (const auto &name : tokens(a.value))
          names.insert(name);
      if (a.uri == XLINK && n == "href" && a.value.startsWith('#'))
        names.insert(unescape(a.value.mid(1)));
      if (a.uri == SMIL && (n == "begin" || n == "end"))
        for (const auto &term : a.value.split(';')) {
          auto event = term.trimmed();
          auto dot = event.indexOf('.');
          if (dot >= 0)
            names.insert(event.left(dot));
        }
    }
  };
  walk(root, collect);
  auto styles = package_.entry("styles.xml");
  if (!styles.empty())
    walk(read_document_xml(styles).documentElement(), collect);
  auto object_name = unique(names, "NativeObject");
  names.insert(object_name);
  auto identifier = unique(names, "native-object-");
  names.insert(identifier);
  auto style_name = unique(names, "NativeGraphic");
  int z = 0;
  for (auto e : children(page, DRAW)) {
    bool ok = true;
    int old = e.attributeNS(DRAW, "z-index", Text::number(z)).toInt(&ok);
    if (!ok || old < 0 || old == std::numeric_limits<int>::max())
      fail("Presentation stacking order exceeds the limit");
    z = std::max(z, old + 1);
  }
  auto automatic = child(root, OFFICE, "automatic-styles");
  if (automatic.isNull()) {
    automatic = candidate.createElementNS(OFFICE, "office:automatic-styles");
    root.insertBefore(automatic, child(root, OFFICE, "body"));
  }
  auto style = candidate.createElementNS(STYLE, "style:style");
  style.setAttributeNS(STYLE, "style:name", style_name);
  style.setAttributeNS(STYLE, "style:family", "graphic");
  auto paint = candidate.createElementNS(STYLE, "style:graphic-properties");
  bool frame = kind == NativeShapeKind::TextBox,
       line = kind == NativeShapeKind::Line;
  paint.setAttributeNS(DRAW, "draw:fill", frame || line ? "none" : "solid");
  if (!frame && !line)
    paint.setAttributeNS(DRAW, "draw:fill-color", "#4477aa");
  paint.setAttributeNS(DRAW, "draw:stroke", frame ? "none" : "solid");
  if (!frame) {
    paint.setAttributeNS(SVG, "svg:stroke-color", "#203040");
    paint.setAttributeNS(SVG, "svg:stroke-width", "1pt");
  }
  style.appendChild(paint);
  if (!line) {
    auto font = candidate.createElementNS(STYLE, "style:text-properties");
    font.setAttributeNS(FO, "fo:font-size", "18pt");
    font.setAttributeNS(FO, "fo:color", "#000000");
    style.appendChild(font);
  }
  automatic.appendChild(style);
  auto tag = frame                                ? "draw:frame"
             : line                               ? "draw:line"
             : kind == NativeShapeKind::Rectangle ? "draw:rect"
                                                  : "draw:ellipse";
  auto shape = candidate.createElementNS(DRAW, tag);
  shape.setAttributeNS(DRAW, "draw:name", object_name);
  shape.setAttributeNS(XML, "xml:id", identifier);
  shape.setAttributeNS(DRAW, "draw:style-name", style_name);
  shape.setAttributeNS(DRAW, "draw:z-index", z);
  if (line) {
    shape.setAttributeNS(SVG, "svg:x1", pt(rect.x));
    shape.setAttributeNS(SVG, "svg:y1", pt(rect.y));
    shape.setAttributeNS(SVG, "svg:x2", pt(rect.x + rect.width));
    shape.setAttributeNS(SVG, "svg:y2", pt(rect.y + rect.height));
  } else {
    shape.setAttributeNS(SVG, "svg:x", pt(rect.x));
    shape.setAttributeNS(SVG, "svg:y", pt(rect.y));
    shape.setAttributeNS(SVG, "svg:width", pt(rect.width));
    shape.setAttributeNS(SVG, "svg:height", pt(rect.height));
  }
  if (!line) {
    auto box = shape;
    if (frame) {
      shape.setAttributeNS(PRESENTATION, "presentation:user-transformed",
                           "true");
      box = candidate.createElementNS(DRAW, "draw:text-box");
      shape.appendChild(box);
    }
    for (const auto &part : text.split('\n')) {
      auto p = candidate.createElementNS(TEXT, "text:p");
      set_plain_text(p, part);
      box.appendChild(p);
    }
  }
  page.appendChild(shape);
  if (page.hasAttributeNS(DRAW, "nav-order")) {
    auto order = page.attributeNS(DRAW, "nav-order").trimmed();
    page.setAttributeNS(DRAW, "draw:nav-order",
                        order.empty() ? identifier : order + " " + identifier);
  }
  restore_content(candidate.toByteArray());
  const auto &model = scene(slide_index);
  for (size_t i = 0; i < model.shapes.size(); ++i)
    if (!model.shapes[i].master && model.shapes[i].name == object_name) {
      PresentationPosition next{slide_index, model.shapes[i].editor_index,
                                int(i)};
      remember(before, previous, next);
      return next;
    }
  fail("Inserted shape has no native scene");
}
PresentationPosition Presentation::remove_shape(int slide_index,
                                                int shape_index) {
  finish_handle_drag();
  end_text_edit();
  if (read_only())
    fail("This presentation is read-only");
  const auto &shape = scene(slide_index).shapes.at(shape_index);
  if (shape.master || shape.source.isNull())
    fail("Master objects are not editable");
  std::set<Text> ids, names;
  walk(shape.source, [&](XmlElement e) {
    for (const auto &uri : {XML, DRAW})
      if (e.hasAttributeNS(uri, "id"))
        ids.insert(e.attributeNS(uri, "id"));
    if (e.hasAttributeNS(DRAW, "name"))
      names.insert(e.attributeNS(DRAW, "name"));
    if (e.namespaceURI() == TEXT &&
        (e.localName() == "bookmark" || e.localName() == "bookmark-start" ||
         e.localName() == "reference-mark" ||
         e.localName() == "reference-mark-start"))
      names.insert(e.attributeNS(TEXT, "name"));
  });
  external_references(package_.xml.documentElement(), shape.source, ids, names);
  auto styles = package_.entry("styles.xml");
  if (!styles.empty())
    external_references(read_document_xml(styles).documentElement(), {}, ids,
                        names);
  auto before = package_.xml.toByteArray();
  auto candidate = read_document_xml(before);
  auto target = stage_candidate_source(shape.source, candidate);
  target.parentNode().removeChild(target);
  walk(candidate.documentElement(), [&](XmlElement e) {
    if (!e.hasAttributeNS(DRAW, "nav-order"))
      return;
    auto order = tokens(e.attributeNS(DRAW, "nav-order"));
    TextList kept;
    bool changed = false;
    for (const auto &id : order) {
      if (ids.contains(id))
        changed = true;
      else
        kept << id;
    }
    if (changed) {
      if (kept.empty())
        e.removeAttributeNS(DRAW, "nav-order");
      else
        e.setAttributeNS(DRAW, "draw:nav-order", kept.join(' '));
    }
  });
  PresentationPosition previous{slide_index, shape.editor_index, shape_index},
      next{slide_index, -1, -1};
  restore_content(candidate.toByteArray());
  const auto &model = scene(slide_index);
  for (size_t i = 0; i < model.shapes.size(); ++i)
    if (!model.shapes[i].master) {
      next = {slide_index, model.shapes[i].editor_index, int(i)};
      if (int(i) >= shape_index)
        break;
    }
  remember(before, previous, next);
  return next;
}
} // namespace DomeOffice
