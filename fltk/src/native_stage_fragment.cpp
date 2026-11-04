// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fragment.h"
#include "native_image.h"
#include "text_documents.h"
#include <array>
#include <deque>
#include <tuple>
#include <unordered_set>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text NUMBER = "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0";
const Text PRESENTATION =
    "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
Text local(const XmlNode::Attribute &a) {
  return a.name.mid(a.name.indexOf(':') + 1);
}
std::vector<Text> tokens(const Text &value) {
  std::istringstream input(value.toStdString());
  std::vector<Text> result;
  for (std::string token; input >> token;)
    result.emplace_back(token);
  return result;
}
struct Budget {
  size_t nodes = 0;
  template <class F> void walk(XmlElement root, F visit) {
    auto descend = [&](auto &&self, XmlElement n, int depth) -> void {
      if (++nodes > 1000000 || depth > 256)
        fail("Clipboard dependency traversal exceeds the limit");
      if (n.isElement())
        visit(n);
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        self(self, c, depth + 1);
    };
    descend(descend, root, 0);
  }
};
using Key = std::tuple<Text, Text, Text>; // kind, style family, source name
struct Definition {
  XmlElement node;
  Key key;
  int source;
  Text group;
};
struct Fragment {
  const OdfPackage &original;
  const SlideScene &scene;
  OdfPackage output{Kind::Stage};
  XmlDocument styles;
  std::array<XmlDocument, 2> documents;
  std::array<std::map<Key, size_t>, 2> automatic, fonts;
  std::map<Key, size_t> common;
  std::map<Text, size_t> defaults;
  std::vector<Definition> definitions;
  std::set<size_t> included;
  std::deque<size_t> pending;
  std::map<Text, XmlElement> layers;
  std::map<Text, Text> media;
  std::set<Text> assets;
  std::set<Text> directories;
  XmlElement page, layer_set;
  Budget budget;
  std::unordered_set<const void *> wanted, ancestors;

  Fragment(const OdfPackage &package, const SlideScene &model)
      : original(package), scene(model),
        styles(read_document_xml(output.entry("styles.xml"))) {
    if (package.kind() != Kind::Stage ||
        model.source.ownerDocument().documentElement() !=
            package.xml.documentElement())
      fail("Clipboard selection does not belong to this presentation");
    documents[0] = package.xml;
    if (!package.entry("styles.xml").empty())
      documents[1] = read_document_xml(package.entry("styles.xml"));
    std::pair<int, int> version_number{1, 2};
    for (const auto &doc : documents) {
      auto source_version =
          doc.documentElement().attributeNS(OFFICE, "version");
      if (source_version.empty())
        continue;
      auto terms = source_version.split('.');
      bool major_ok = false, minor_ok = false;
      if (terms.size() != 2)
        fail("Clipboard source has an unsupported document version");
      int major = terms[0].toInt(&major_ok), minor = terms[1].toInt(&minor_ok);
      if (!major_ok || !minor_ok || major < 0 || minor < 0)
        fail("Clipboard source has an unsupported document version");
      version_number = std::max(version_number, std::pair(major, minor));
    }
    auto version = Text::number(version_number.first) + "." +
                   Text::number(version_number.second);
    output.xml.documentElement().setAttributeNS(OFFICE, "office:version",
                                                version);
    styles.documentElement().setAttributeNS(OFFICE, "office:version", version);
    // Match the native common/default cascade: content follows styles.xml.
    index(1);
    index(0);
    auto manifest = read_document_xml(original.entry("META-INF/manifest.xml"));
    for (auto e : children(manifest.documentElement(), MANIFEST, "file-entry"))
      media[e.attributeNS(MANIFEST, "full-path")] =
          e.attributeNS(MANIFEST, "media-type");
    auto layout =
        styles.elementsByTagNameNS(STYLE, "page-layout-properties").front();
    layout.setAttributeNS(FO, "fo:page-width",
                          Text::number(scene.width, 'g', 17) + "pt");
    layout.setAttributeNS(FO, "fo:page-height",
                          Text::number(scene.height, 'g', 17) + "pt");
    page = output.xml.createElementNS(DRAW, "draw:page");
    page.setAttributeNS(DRAW, "draw:name", "Clipboard");
    page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
    output.body().appendChild(page);
    for (int source : {1, 0})
      for (auto root : children(documents[source].documentElement(), OFFICE,
                                "master-styles"))
        read_layers(root);
    read_layers(scene.source);
  }
  static std::optional<Key> key(XmlElement node) {
    auto uri = node.namespaceURI(), name = node.localName();
    if (uri == STYLE && (name == "style" || name == "default-style"))
      return Key{name, node.attributeNS(STYLE, "family"),
                 node.attributeNS(STYLE, "name")};
    if (uri == STYLE && name == "font-face")
      return Key{"font", {}, node.attributeNS(STYLE, "name")};
    if (uri == TEXT && name == "list-style")
      return Key{"list", {}, node.attributeNS(STYLE, "name")};
    if (uri == NUMBER && name.endsWith("-style"))
      return Key{"data", {}, node.attributeNS(STYLE, "name")};
    if (uri == TABLE && name == "table-template")
      return Key{"table-template", {}, node.attributeNS(TABLE, "name")};
    if (uri == DRAW && TextList{"gradient", "hatch", "fill-image", "opacity",
                                "stroke-dash", "marker"}
                           .contains(name))
      return Key{name, {}, node.attributeNS(DRAW, "name")};
    return {};
  }
  void index(int source) {
    for (auto group : children(documents[source].documentElement(), OFFICE)) {
      auto name = group.localName();
      if (name != "styles" && name != "automatic-styles" &&
          name != "font-face-decls")
        continue;
      for (auto node = group.firstChild(); !node.isNull();
           node = node.nextSibling())
        if (node.isElement()) {
          auto k = key(node);
          if (!k)
            continue;
          if (definitions.size() >= 100000)
            fail("Clipboard source style inventory exceeds the limit");
          auto id = definitions.size();
          definitions.push_back({node, *k, source, name});
          if (std::get<0>(*k) == "default-style")
            defaults[std::get<1>(*k)] = id;
          else if (name == "font-face-decls")
            fonts[source][*k] = id;
          else if (name == "automatic-styles")
            automatic[source][*k] = id;
          else
            common[*k] = id;
        }
    }
  }
  void read_layers(XmlElement root) {
    auto sets = children(root, DRAW, "layer-set");
    if (sets.empty())
      return;
    if (sets.size() != 1)
      fail("Clipboard source has ambiguous layer definitions");
    layers.clear();
    for (auto node : children(sets.front(), DRAW, "layer"))
      if (!layers.emplace(node.attributeNS(DRAW, "name"), node).second)
        fail("Clipboard source has duplicate layers");
  }
  XmlElement group(XmlDocument &doc, const Text &name) {
    auto root = doc.documentElement();
    auto found = child(root, OFFICE, name);
    if (!found.isNull())
      return found;
    found = doc.createElementNS(OFFICE, "office:" + name);
    root.insertBefore(found, root.firstChild());
    return found;
  }
  void include(size_t id) {
    if (!included.insert(id).second)
      return;
    if (included.size() > 100000)
      fail("Clipboard style dependency count exceeds the limit");
    pending.push_back(id);
  }
  void reference(const Key &k, int source, bool common_only = false) {
    if (std::get<2>(k).empty())
      return;
    if (std::get<0>(k) == "font") {
      if (auto at = fonts[source].find(k); at != fonts[source].end()) {
        include(at->second);
        return;
      }
      if (source == 0)
        if (auto at = fonts[1].find(k); at != fonts[1].end()) {
          include(at->second);
          return;
        }
    } else {
      if (!common_only)
        if (auto at = automatic[source].find(k);
            at != automatic[source].end()) {
          include(at->second);
          return;
        }
      if (auto at = common.find(k); at != common.end()) {
        include(at->second);
        return;
      }
    }
    fail("A selected object has a missing style, font or drawing dependency");
  }
  void family(const Text &family) {
    if (auto at = defaults.find(family); at != defaults.end())
      include(at->second);
  }
  Text text_family(XmlElement node) {
    auto name = node.localName();
    if (name == "p" || name == "h")
      return "paragraph";
    if (name == "section" ||
        TextList{"table-of-content", "user-index", "alphabetical-index",
                 "table-index", "object-index", "illustration-index",
                 "bibliography"}
            .contains(name))
      return "section";
    return name == "ruby" ? Text("ruby") : Text("text");
  }
  Text table_family(XmlElement node) {
    auto name = node.localName();
    if (name == "table")
      return "table";
    if (name == "table-row")
      return "table-row";
    if (name == "table-column")
      return "table-column";
    return "table-cell";
  }
  void asset(Text path, bool allow_directory = false) {
    if (TextList{"mimetype", "content.xml", "styles.xml", "meta.xml",
                 "settings.xml", "manifest.rdf"}
            .contains(path) ||
        path.startsWith("META-INF/"))
      fail("A clipboard dependency points into unrelated document source or "
           "metadata");
    if (assets.contains(path))
      return;
    if (directories.contains(path)) {
      if (!allow_directory)
        fail("An image or font resource cannot reference an object directory");
      return;
    }
    if (auto data = original.entry_data(path)) {
      assets.insert(path);
      output.set_entry(path, *data,
                       media.contains(path) ? media.at(path)
                                            : Text("application/octet-stream"));
      // Embedded object XML can refer to other parts. Resolve those links
      // relative to the XML part, never to the receiving document or network.
      if (!path.endsWith('/') &&
          (path.endsWith(".xml") || path.endsWith(".svg") ||
           media[path] == "text/xml")) {
        auto doc = read_document_xml(*data);
        auto at = path.lastIndexOf('/');
        auto base = at < 0 ? Text() : path.left(at + 1);
        budget.walk(doc.documentElement(), [&](XmlElement node) {
          if (node.hasAttributeNS(XML, "base"))
            fail("Clipboard asset xml:base resolution is not implemented yet");
          for (const auto &a : node.attributes())
            if ((a.uri == XLINK && local(a) == "href") ||
                (a.uri.empty() && local(a) == "href"))
              resource(a.value, base,
                       node.namespaceURI() == DRAW &&
                           node.localName() == "object");
        });
      }
      return;
    }
    const auto prefix = path + "/";
    // An image/font/SVG reference to a folder is not a dependency on every
    // unrelated part in it. Only a declared embedded ODF object owns a whole
    // subpackage directory, including its metadata and opaque parts.
    if (!allow_directory || !media.contains(prefix) ||
        !media.at(prefix).startsWith("application/vnd.oasis.opendocument."))
      fail("A selected resource is not a file or a declared embedded ODF "
           "object");
    auto type = media.at(prefix).mid(
        Text("application/vnd.oasis.opendocument.").size());
    if (type.endsWith("-template"))
      type = type.left(type.size() - 9);
    const std::map<Text, Text> bodies{{"text", "text"},
                                      {"spreadsheet", "spreadsheet"},
                                      {"presentation", "presentation"},
                                      {"graphics", "drawing"},
                                      {"chart", "chart"},
                                      {"image", "image"},
                                      {"base", "database"},
                                      {"database", "database"}};
    auto embedded = read_document_xml(original.entry(prefix + "content.xml"))
                        .documentElement();
    if (type == "formula") {
      if (embedded.namespaceURI() != "http://www.w3.org/1998/Math/MathML" ||
          embedded.localName() != "math")
        fail("Embedded formula content does not match its declared media type");
    } else if (!bodies.contains(type) || embedded.namespaceURI() != OFFICE ||
               (embedded.localName() != "document-content" &&
                embedded.localName() != "document") ||
               child(child(embedded, OFFICE, "body"), OFFICE, bodies.at(type))
                   .isNull())
      fail("Embedded object content does not match its declared media type");
    directories.insert(path);
    bool found = false;
    for (auto name : original.entry_names())
      if (name.startsWith(prefix)) {
        found = true;
        asset(name);
      }
    if (!found)
      fail("A selected object has a missing package resource");
    if (media.contains(prefix) && assets.insert(prefix).second)
      output.set_entry(prefix, {}, media.at(prefix));
  }
  void resource(const Text &href, const Text &base = {},
                bool allow_directory = false) {
    if (href.empty() || href.startsWith('#'))
      return;
    auto text = href;
    auto hash = text.indexOf('#');
    if (hash >= 0)
      text = text.left(hash);
    // Absolute external links remain explicit references, without fetching.
    auto colon = text.indexOf(':');
    auto slash = text.indexOf('/');
    if (colon >= 0 && (slash < 0 || colon < slash))
      return;
    auto path = package_image_path(base + text);
    if (!path)
      fail("Clipboard resource has an invalid package-relative path");
    asset(*path, allow_directory);
  }
  void scan(XmlElement root, int source, const Text &context_family = {},
            const Text &context_kind = {}) {
    budget.walk(root, [&](XmlElement node) {
      auto uri = node.namespaceURI(), name = node.localName();
      if (node.hasAttributeNS(XML, "base"))
        fail("Clipboard object xml:base resolution is not implemented yet");
      if (uri == DRAW)
        family("graphic");
      if (uri == TEXT &&
          (name == "p" || name == "h" || name == "span" || name == "a")) {
        family("paragraph");
        family("text");
      }
      if (uri == TABLE)
        family(table_family(node));
      for (const auto &a : node.attributes()) {
        auto attr = local(a);
        if (a.uri == DRAW && (attr == "style-name" || attr == "class-names")) {
          auto f = name == "page" ? Text("drawing-page") : Text("graphic");
          family(f);
          for (auto n : tokens(a.value))
            reference({"style", f, n}, source, attr == "class-names");
        } else if (a.uri == DRAW && attr == "text-style-name") {
          family("paragraph");
          reference({"style", "paragraph", a.value}, source);
        } else if (a.uri == PRESENTATION &&
                   (attr == "style-name" || attr == "class-names")) {
          family("presentation");
          for (auto n : tokens(a.value))
            reference({"style", "presentation", n}, source,
                      attr == "class-names");
        } else if (a.uri == TEXT &&
                   (attr == "style-name" || attr == "class-names")) {
          if (name == "list" || name == "numbered-paragraph")
            reference({"list", {}, a.value}, source);
          else {
            auto f = text_family(node);
            family(f);
            for (auto n : tokens(a.value))
              reference({"style", f, n}, source, attr == "class-names");
          }
        } else if (a.uri == TABLE &&
                   (attr == "style-name" || attr == "default-cell-style-name" ||
                    attr == "paragraph-style-name")) {
          auto f = attr == "default-cell-style-name" ? Text("table-cell")
                   : attr == "paragraph-style-name"  ? Text("paragraph")
                                                     : table_family(node);
          family(f);
          reference({"style", f, a.value}, source);
        } else if (a.uri == TABLE && attr == "template-name")
          reference({"table-template", {}, a.value}, source);
        else if (a.uri == STYLE &&
                 (attr == "parent-style-name" || attr == "next-style-name") &&
                 !context_family.empty())
          reference({"style", context_family, a.value}, source, true);
        else if (a.uri == STYLE && (attr == "data-style-name" ||
                                    attr == "percentage-data-style-name"))
          reference({"data", {}, a.value}, source);
        else if (a.uri == STYLE && attr == "list-style-name")
          reference({"list", {}, a.value}, source);
        else if (a.uri == STYLE && attr == "leader-text-style")
          reference({"style", "text", a.value}, source);
        else if (a.uri == STYLE && attr == "apply-style-name")
          reference({context_kind == "data" ? Text("data") : Text("style"),
                     context_family, a.value},
                    source);
        else if (a.uri == STYLE &&
                 (attr == "font-name" || attr == "font-name-asian" ||
                  attr == "font-name-complex"))
          reference({"font", {}, a.value}, source);
        else if (a.uri == DRAW && attr == "layer") {
          if (layer_set.isNull()) {
            layer_set = output.xml.createElementNS(DRAW, "draw:layer-set");
            page.insertBefore(layer_set, page.firstChild());
          }
          bool exists = false;
          for (auto l : children(layer_set, DRAW, "layer"))
            if (l.attributeNS(DRAW, "name") == a.value)
              exists = true;
          if (!exists) {
            auto l = layers.contains(a.value)
                         ? output.xml.importNode(layers.at(a.value), true)
                         : output.xml.createElementNS(DRAW, "draw:layer");
            l.setAttributeNS(DRAW, "draw:name", a.value);
            layer_set.appendChild(l);
          }
        } else if (a.uri == DRAW) {
          Text kind;
          if (attr == "fill-gradient-name")
            kind = "gradient";
          else if (attr == "fill-hatch-name")
            kind = "hatch";
          else if (attr == "fill-image-name")
            kind = "fill-image";
          else if (attr == "opacity-name")
            kind = "opacity";
          else if (attr == "stroke-dash")
            kind = "stroke-dash";
          else if (attr == "marker-start" || attr == "marker-end")
            kind = "marker";
          if (!kind.empty())
            reference({kind, {}, a.value}, source);
        }
        if (a.uri == XLINK && attr == "href" && !(uri == TEXT && name == "a"))
          resource(a.value, {}, uri == DRAW && name == "object");
        if (a.uri == XLINK && attr == "href" && uri == TEXT && name == "a" &&
            !a.value.empty() && !a.value.startsWith('#') &&
            a.value.indexOf(':') < 0)
          fail("Clipboard relative hyperlinks require a source document base "
               "URI");
      }
    });
  }
  XmlElement prune(XmlElement node) {
    if (wanted.contains(node.identity()))
      return output.xml.importNode(node, true);
    if (!ancestors.contains(node.identity()))
      return {};
    if (node.namespaceURI() != DRAW ||
        (node.localName() != "g" && node.localName() != "a"))
      fail("Clipboard object is inside an unsupported structural container");
    auto clone = output.xml.importNode(node, false);
    for (auto n = node.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        auto child = prune(n);
        if (!child.isNull())
          clone.appendChild(child);
      }
    return clone;
  }
  NativeStageFragment make(const std::vector<NativeObjectRef> &selection) {
    auto refs = native_object_selection(scene, selection);
    if (refs.empty())
      fail("Select at least one object to copy");
    for (auto ref : refs) {
      auto node = native_object_source(scene, ref);
      wanted.insert(node.identity());
      for (auto p = node.parentNode().toElement(); p != scene.source;
           p = p.parentNode().toElement()) {
        if (p.isNull())
          fail("Clipboard selection is outside this slide");
        ancestors.insert(p.identity());
      }
    }
    for (auto n = scene.source.firstChild(); !n.isNull(); n = n.nextSibling())
      if (n.isElement()) {
        auto clone = prune(n);
        if (!clone.isNull()) {
          page.appendChild(clone);
          scan(clone, 0);
        }
      }
    while (!pending.empty()) {
      auto id = pending.front();
      pending.pop_front();
      const auto &d = definitions.at(id);
      auto &doc = d.source == 0 ? output.xml : styles;
      group(doc, d.group).appendChild(doc.importNode(d.node, true));
      auto [kind, f, name] = d.key;
      if (!f.empty())
        family(f);
      scan(d.node, d.source, f, kind);
    }
    output.set_entry("styles.xml", styles.toByteArray(), "text/xml");
    // Keep only the source's selected navigation targets, never references to
    // unselected objects. Detailed object-reference closure is validated below.
    std::set<Text> ids, names;
    auto collect_targets = [&](XmlElement n) {
      for (const auto &a : n.attributes()) {
        auto attr = local(a);
        if ((a.uri == XML || a.uri == DRAW) && attr == "id")
          ids.insert(a.value);
        if ((a.uri == DRAW || a.uri == TEXT) && attr == "name")
          names.insert(a.value);
      }
    };
    // Pruned ancestor wrappers and copied layer definitions provide context;
    // they are not selected targets. Their names/IDs must not make a link to
    // an unselected object appear closed inside the fragment.
    for (auto ref : refs)
      budget.walk(native_object_source(scene, ref), collect_targets);
    TextList nav;
    for (auto id : tokens(scene.source.attributeNS(DRAW, "nav-order")))
      if (ids.contains(id))
        nav.push_back(id);
    if (!nav.empty())
      page.setAttributeNS(DRAW, "draw:nav-order", nav.join(' '));
    budget.walk(page, [&](XmlElement n) {
      for (const auto &a : n.attributes()) {
        auto attr = local(a);
        Text target;
        if ((a.uri == DRAW && TextList{"start-shape", "end-shape", "caption-id",
                                       "copy-of", "chain-next-name"}
                                  .contains(attr)) ||
            (a.uri == TEXT &&
             (attr == "ref-name" || attr == "continue-list")) ||
            (a.uri == SMIL && attr == "targetElement"))
          target = a.value;
        if (a.uri == XLINK && attr == "href" && a.value.startsWith('#'))
          target = Url::fromPercentEncoding(a.value.mid(1).toUtf8());
        if (!target.empty() && !ids.contains(target) && !names.contains(target))
          fail(
              "A copied object references an unselected object or text target");
        if (a.uri == SMIL && (attr == "begin" || attr == "end"))
          for (auto term : a.value.split(';')) {
            term = term.trimmed();
            auto dot = term.indexOf('.');
            if (dot > 0 &&
                ((term[0] >= U'a' && term[0] <= U'z') ||
                 (term[0] >= U'A' && term[0] <= U'Z') || term[0] == U'_'))
              if (!ids.contains(term.left(dot)))
                fail("A copied timing expression references an unselected "
                     "object");
          }
      }
    });
    TextList paragraphs;
    budget.walk(page, [&](XmlElement n) {
      if (n.namespaceURI() == TEXT &&
          (n.localName() == "p" || n.localName() == "h"))
        paragraphs.push_back(plain_text(n));
    });
    output.validate_archive_limits();
    return {output.archive_bytes(), paragraphs.join('\n')};
  }
};
} // namespace
NativeStageFragment
native_stage_fragment(const OdfPackage &package, const SlideScene &scene,
                      const std::vector<NativeObjectRef> &selection) {
  return Fragment(package, scene).make(selection);
}
NativeStageFragment Presentation::copy_objects(
    int slide, const std::vector<NativeObjectRef> &selection) const {
  if (drag_)
    fail("Finish the current object gesture before copying");
  return native_stage_fragment(package_, scene(slide), selection);
}
} // namespace DomeOffice
