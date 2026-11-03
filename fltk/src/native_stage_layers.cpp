// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_layers.h"
#include "text_documents.h"
#include "native_stage_source.h"
#include <functional>
#include <set>
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
[[noreturn]] void fail(const char *text) { throw std::runtime_error(text); }
void xml_text_valid(const Text &text) {
  for (auto c : text)
    if ((c < 32 && c != 9 && c != 10 && c != 13) ||
        (c >= 0xD800 && c <= 0xDFFF) || c == 0xFFFE || c == 0xFFFF ||
        c > 0x10FFFF)
      fail("Layer text contains a character XML cannot store");
}
void name_valid(const Text &name) {
  xml_text_valid(name);
  if (name.trimmed().empty() || name.size() > 1024)
    fail("Choose a layer name of 1 to 1024 characters");
  for (auto c : name)
    if (c < 32 || c == 127)
      fail("Layer names cannot contain control characters");
}
void walk(XmlElement root, const std::function<void(XmlElement)> &visit) {
  struct Entry {
    XmlElement node;
    int depth;
  };
  std::vector<Entry> pending{{root, 0}};
  size_t count = 0;
  while (!pending.empty()) {
    auto item = pending.back();
    pending.pop_back();
    if (item.node.isNull())
      continue;
    if (++count > 1000000 || item.depth > 256)
      fail("Layer reference traversal exceeds the limit");
    if (item.node.namespaceURI() == DRAW &&
        item.node.localName() == "layer-set")
      continue;
    visit(item.node);
    for (auto node = item.node.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (node.isElement())
        pending.push_back({node.toElement(), item.depth + 1});
  }
}
XmlElement definition(XmlElement set, const Text &name) {
  for (auto node : children(set, DRAW, "layer"))
    if (node.attributeNS(DRAW, "name") == name)
      return node;
  return {};
}
SceneLayer properties(XmlElement node) {
  SceneLayer p;
  auto d = node.attributeNS(DRAW, "display", "always");
  if (d == "screen")
    p.display = SceneLayer::Screen;
  else if (d == "printer")
    p.display = SceneLayer::Printer;
  else if (d == "none")
    p.display = SceneLayer::None;
  auto b = node.attributeNS(DRAW, "protected", "false").trimmed();
  p.protected_objects = b == "true" || b == "1";
  return p;
}
Text display_name(SceneLayer::Display d) {
  switch (d) {
  case SceneLayer::Always:
    return "always";
  case SceneLayer::Screen:
    return "screen";
  case SceneLayer::Printer:
    return "printer";
  case SceneLayer::None:
    return "none";
  }
  fail("Invalid layer display mode");
}
void insert_set(XmlElement parent, XmlElement set) {
  // On pages the set follows title/description; on masters it follows stories.
  for (auto node = parent.firstChild(); !node.isNull();
       node = node.nextSibling()) {
    if (!node.isElement())
      continue;
    const auto uri = node.namespaceURI(), name = node.localName();
    if ((uri == SVG && (name == "title" || name == "desc")) ||
        (uri == STYLE &&
         TextList{"header", "header-left", "footer", "footer-left"}.contains(
             name)))
      continue;
    parent.insertBefore(set, node);
    return;
  }
  parent.appendChild(set);
}
std::vector<XmlElement>
assignment_objects(const std::vector<XmlElement> &roots) {
  std::vector<XmlElement> result;
  auto collect = [&](auto &&self, XmlElement node, int depth) -> void {
    if (depth > 256 || result.size() >= 100000)
      fail("Layer assignment exceeds the object limit");
    if (node.namespaceURI() == DRAW &&
        (node.localName() == "g" || node.localName() == "a")) {
      for (auto item : native_stacking_children(node))
        self(self, item.node, depth + 1);
    } else if ((node.namespaceURI() == DRAW &&
                TextList{"rect", "line", "polyline", "polygon", "circle",
                         "ellipse", "path", "caption", "connector", "measure",
                         "control", "page-thumbnail", "regular-polygon",
                         "custom-shape", "frame"}
                    .contains(node.localName())) ||
               (node.namespaceURI() ==
                    "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0" &&
                node.localName() == "scene") ||
               (node.namespaceURI() == OFFICE &&
                node.localName() == "annotation"))
      result.push_back(node);
    else
      fail("This selected object does not support native layer assignment");
  };
  for (auto root : roots)
    collect(collect, root, 0);
  return result;
}
} // namespace
struct NativeStageLayerEdit::State {
  OdfPackage package;
  XmlDocument styles;
  bool styles_present = false, styles_dirty = false;
  Bytes initial_styles_canonical, initial_content;
  std::vector<XmlElement> selected;
  int slide;
  State(const OdfPackage &source, int index,
        const std::vector<XmlElement> &nodes)
      : package(source), slide(index) {
    initial_content = source.xml.toByteArray();
    package.xml = read_document_xml(initial_content);
    const auto bytes = source.entry("styles.xml");
    styles_present = !bytes.empty();
    if (styles_present) {
      styles = read_document_xml(bytes);
      initial_styles_canonical = styles.toByteArray();
    }
    for (auto node : nodes)
      selected.push_back(stage_candidate_source(node, package.xml));
  }
  State(const State &other)
      : package(other.package), styles_present(other.styles_present),
        styles_dirty(other.styles_dirty),
        initial_styles_canonical(other.initial_styles_canonical),
        initial_content(other.initial_content), slide(other.slide) {
    package.xml = read_document_xml(other.package.xml.toByteArray());
    if (styles_present)
      styles = read_document_xml(other.styles.toByteArray());
    for (auto node : other.selected)
      selected.push_back(stage_candidate_source(node, package.xml));
  }
  XmlElement page() const {
    return children(package.body(), DRAW, "page").at(slide);
  }
  XmlElement master() const {
    const auto name = page().attributeNS(DRAW, "master-page-name");
    if (name.empty())
      return {};
    XmlElement result;
    for (auto doc : {styles, package.xml})
      for (auto node :
           children(child(doc.documentElement(), OFFICE, "master-styles"),
                    STYLE, "master-page"))
        if (node.attributeNS(STYLE, "name") == name)
          result = node;
    return result;
  }
  XmlElement global_parent() const {
    auto content =
        child(package.xml.documentElement(), OFFICE, "master-styles");
    if (!child(content, DRAW, "layer-set").isNull())
      return content;
    auto external = child(styles.documentElement(), OFFICE, "master-styles");
    if (!external.isNull())
      return external;
    return content;
  }
  XmlElement parent(NativeLayerScope scope) const {
    switch (scope) {
    case NativeLayerScope::Document:
      return global_parent();
    case NativeLayerScope::Slide:
      return page();
    case NativeLayerScope::Master:
      return master();
    }
    fail("Invalid layer scope");
  }
  XmlElement own(NativeLayerScope scope) const {
    return child(parent(scope), DRAW, "layer-set");
  }
  XmlElement effective(NativeLayerScope scope) const {
    auto set = own(scope);
    return !set.isNull() || scope == NativeLayerScope::Document
               ? set
               : own(NativeLayerScope::Document);
  }
  XmlElement ensure_global_parent() {
    auto root = global_parent();
    if (!root.isNull())
      return root;
    if (!styles_present) {
      styles = read_document_xml(
          ("<office:document-styles xmlns:office=\"" + OFFICE +
           "\" office:version=\"" +
           package.xml.documentElement().attributeNS(OFFICE, "version", "1.2") +
           "\"/>")
              .toUtf8());
      styles_present = true;
    }
    root = styles.createElementNS(OFFICE, "office:master-styles");
    styles.documentElement().appendChild(root);
    styles_dirty = true;
    return root;
  }
  void touched(XmlElement node) {
    if (node.ownerDocument().documentElement() == styles.documentElement())
      styles_dirty = true;
  }
  std::vector<XmlElement> consumers(NativeLayerScope scope) const {
    if (scope != NativeLayerScope::Document) {
      auto root = parent(scope);
      if (root.isNull())
        fail("This slide has no resolved master page");
      return {root};
    }
    std::vector<XmlElement> result;
    for (auto page : children(package.body(), DRAW, "page"))
      if (child(page, DRAW, "layer-set").isNull())
        result.push_back(page);
    for (auto doc : {styles, package.xml})
      for (auto master :
           children(child(doc.documentElement(), OFFICE, "master-styles"),
                    STYLE, "master-page"))
        if (child(master, DRAW, "layer-set").isNull())
          result.push_back(master);
    return result;
  }
  std::vector<XmlElement> references(NativeLayerScope scope,
                                     const Text &name) const {
    std::vector<XmlElement> result;
    for (auto root : consumers(scope))
      walk(root, [&](XmlElement node) {
        if (node.hasAttributeNS(DRAW, "layer") &&
            node.attributeNS(DRAW, "layer") == name)
          result.push_back(node);
      });
    return result;
  }
  void local(NativeLayerScope scope, bool copy) {
    if (!own(scope).isNull())
      return;
    auto root = scope == NativeLayerScope::Document ? ensure_global_parent()
                                                    : parent(scope);
    if (root.isNull())
      fail("This slide has no resolved master page");
    auto doc = root.ownerDocument();
    auto global = own(NativeLayerScope::Document);
    auto set = copy && !global.isNull() && scope != NativeLayerScope::Document
                   ? doc.importNode(global, true).toElement()
                   : doc.createElementNS(DRAW, "draw:layer-set");
    insert_set(root, set);
    touched(root);
    if (!copy) {
      for (auto node : consumers(scope))
        walk(node, [&](XmlElement object) {
          if (object.hasAttributeNS(DRAW, "layer"))
            fail("Assign objects before creating an empty layer set");
        });
    }
  }
  OdfPackage snapshot() const {
    auto result = package;
    if (styles_dirty) {
      const auto bytes = styles.toByteArray();
      if (bytes != initial_styles_canonical)
        result.set_entry("styles.xml", bytes, "text/xml");
    }
    return result;
  }
};
NativeStageLayerEdit::NativeStageLayerEdit(
    const OdfPackage &source, int slide, uint64_t revision,
    const std::vector<NativeObjectRef> &refs,
    const std::vector<XmlElement> &nodes, std::shared_ptr<const char> identity)
    : identity_(std::move(identity)),
      state_(std::make_unique<State>(source, slide, nodes)), slide_(slide),
      revision_(revision), original_refs_(refs) {}
NativeStageLayerEdit::~NativeStageLayerEdit() = default;
NativeStageLayerEdit::NativeStageLayerEdit(NativeStageLayerEdit &&) noexcept =
    default;
NativeStageLayerEdit &
NativeStageLayerEdit::operator=(NativeStageLayerEdit &&) noexcept = default;
template <class F> void NativeStageLayerEdit::change(F action) {
  if (read_only())
    fail("This presentation is read-only");
  auto next = std::make_unique<State>(*state_);
  action(*next);
  state_ = std::move(next);
}
bool NativeStageLayerEdit::read_only() const {
  return state_->package.read_only();
}
bool NativeStageLayerEdit::available(NativeLayerScope scope) const {
  return scope != NativeLayerScope::Master || !state_->master().isNull();
}
bool NativeStageLayerEdit::own_set(NativeLayerScope scope) const {
  return !state_->own(scope).isNull();
}
std::vector<NativeLayerInfo>
NativeStageLayerEdit::layers(NativeLayerScope scope) const {
  std::map<Text, size_t> counts;
  for (auto root : state_->consumers(scope))
    walk(root, [&](XmlElement node) {
      if (node.hasAttributeNS(DRAW, "layer"))
        ++counts[node.attributeNS(DRAW, "layer")];
    });
  std::vector<NativeLayerInfo> result;
  for (auto node : children(state_->effective(scope), DRAW, "layer")) {
    const auto name = node.attributeNS(DRAW, "name");
    result.push_back({name, plain_text(child(node, SVG, "title")),
                      plain_text(child(node, SVG, "desc")), properties(node),
                      counts[name]});
  }
  return result;
}
std::vector<Text>
NativeStageLayerEdit::missing_layers(NativeLayerScope scope) const {
  std::set<Text> missing, defined;
  for (auto node : children(state_->effective(scope), DRAW, "layer"))
    defined.insert(node.attributeNS(DRAW, "name"));
  for (auto root : state_->consumers(scope))
    walk(root, [&](XmlElement node) {
      if (node.hasAttributeNS(DRAW, "layer") &&
          !defined.contains(node.attributeNS(DRAW, "layer")))
        missing.insert(node.attributeNS(DRAW, "layer"));
    });
  return {missing.begin(), missing.end()};
}
Text NativeStageLayerEdit::new_name(NativeLayerScope scope) const {
  std::set<Text> used;
  for (auto info : layers(scope))
    used.insert(info.name);
  for (auto missing : missing_layers(scope))
    used.insert(missing);
  for (size_t i = 1; i <= used.size() + 1; ++i) {
    auto name = Text("Layer %1").arg(int(i));
    if (!used.contains(name))
      return name;
  }
  fail("No unused layer name is available");
}
void NativeStageLayerEdit::make_local(NativeLayerScope scope, bool copy) {
  change([&](State &s) { s.local(scope, copy); });
}
void NativeStageLayerEdit::inherit(NativeLayerScope scope) {
  if (scope == NativeLayerScope::Document)
    fail("Document layers have no parent set");
  change([&](State &s) {
    auto set = s.own(scope);
    if (set.isNull())
      return;
    auto global = s.own(NativeLayerScope::Document);
    for (auto root : s.consumers(scope))
      walk(root, [&](XmlElement node) {
        if (node.hasAttributeNS(DRAW, "layer") &&
            definition(global, node.attributeNS(DRAW, "layer")).isNull() &&
            !definition(set, node.attributeNS(DRAW, "layer")).isNull())
          fail("Reassign objects that use local-only layers before inheriting "
               "document layers");
      });
    auto parent = set.parentNode();
    parent.removeChild(set);
    s.touched(parent.toElement());
  });
}
void NativeStageLayerEdit::create(NativeLayerScope scope, const Text &name,
                                  bool repair) {
  name_valid(name);
  change([&](State &s) {
    s.local(scope, true);
    auto set = s.own(scope);
    if (!definition(set, name).isNull())
      fail("A layer with this name already exists");
    if (!repair && !s.references(scope, name).empty())
      fail("This name has unresolved references; use Define missing layer");
    if (children(set, DRAW, "layer").size() >= 100000)
      fail("Presentation layer count exceeds the limit");
    auto node = set.ownerDocument().createElementNS(DRAW, "draw:layer");
    node.setAttributeNS(DRAW, "draw:name", name);
    set.appendChild(node);
    s.touched(set);
  });
}
void NativeStageLayerEdit::update(NativeLayerScope scope, const Text &old,
                                  const NativeLayerPatch &patch) {
  if (patch.name && *patch.name != old)
    name_valid(*patch.name);
  if (patch.display)
    (void)display_name(*patch.display);
  for (auto value : {patch.title, patch.description})
    if (value) {
      xml_text_valid(*value);
      if (value->size() > 65536)
        fail("Layer description exceeds the text limit");
    }
  change([&](State &s) {
    if (s.own(scope).isNull())
      fail("Create a local set to edit inherited layer properties");
    auto set = s.own(scope), node = definition(set, old);
    if (node.isNull())
      fail("The layer no longer exists");
    bool changed = false;
    if (patch.name && *patch.name != old) {
      if (!definition(set, *patch.name).isNull() ||
          !s.references(scope, *patch.name).empty())
        fail("The layer name is already defined or referenced");
      auto refs = s.references(scope, old);
      for (auto object : refs) {
        object.setAttributeNS(DRAW, "draw:layer", *patch.name);
        s.touched(object);
      }
      node.setAttributeNS(DRAW, "draw:name", *patch.name);
      changed = true;
    }
    const auto current = properties(node);
    if (patch.display && *patch.display != current.display) {
      node.setAttributeNS(DRAW, "draw:display", display_name(*patch.display));
      changed = true;
    }
    if (patch.protected_objects &&
        *patch.protected_objects != current.protected_objects) {
      node.setAttributeNS(DRAW, "draw:protected",
                          *patch.protected_objects ? "true" : "false");
      changed = true;
    }
    for (auto pair : {std::pair(Text("title"), patch.title),
                      std::pair(Text("desc"), patch.description)})
      if (pair.second) {
        auto child_node = child(node, SVG, pair.first);
        if (plain_text(child_node) == *pair.second)
          continue;
        if (pair.second->empty()) {
          if (!child_node.isNull())
            node.removeChild(child_node);
        } else {
          if (child_node.isNull()) {
            child_node =
                node.ownerDocument().createElementNS(SVG, "svg:" + pair.first);
            node.appendChild(child_node);
          }
          while (!child_node.firstChild().isNull())
            child_node.removeChild(child_node.firstChild());
          child_node.appendChild(
              child_node.ownerDocument().createTextNode(*pair.second));
        }
        changed = true;
      }
    if (changed)
      s.touched(node);
  });
}
void NativeStageLayerEdit::remove(NativeLayerScope scope, const Text &name) {
  change([&](State &s) {
    auto set = s.own(scope);
    if (set.isNull())
      fail("Create a local set before removing inherited layers");
    auto node = definition(set, name);
    if (node.isNull())
      fail("The layer no longer exists");
    if (!s.references(scope, name).empty())
      fail("Reassign objects before deleting a used layer");
    set.removeChild(node);
    s.touched(set);
  });
}
void NativeStageLayerEdit::validate() const {
  auto candidate = state_->snapshot();
  if (candidate.xml.toByteArray() == state_->initial_content &&
      candidate.part_changes_from(state_->package).empty())
    return;
  candidate.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(candidate);
  (void)read_presentation_scenes(candidate, reader);
  (void)read_presentation_scenes(candidate, reader, SceneTarget::Print);
}
bool NativeStageLayerEdit::has_selection() const {
  return !state_->selected.empty();
}
std::vector<std::optional<Text>> NativeStageLayerEdit::selected_layers() const {
  std::vector<std::optional<Text>> result;
  for (auto node : assignment_objects(state_->selected))
    result.push_back(node.hasAttributeNS(DRAW, "layer")
                         ? std::optional(node.attributeNS(DRAW, "layer"))
                         : std::nullopt);
  return result;
}
void NativeStageLayerEdit::assign(std::optional<Text> layer) {
  change([&](State &s) {
    if (s.selected.empty())
      fail("Select objects to assign to a layer");
    if (layer &&
        definition(s.effective(NativeLayerScope::Slide), *layer).isNull())
      fail("Choose a layer in this slide's effective set");
    SceneLayers global;
    global = global.scoped(s.global_parent());
    SlideScene scene;
    scene.source = s.page();
    scene.layers = global.scoped(s.page());
    for (auto root : s.selected)
      scene.require_editable(root);
    for (auto node : assignment_objects(s.selected)) {
      if (layer) {
        if (node.attributeNS(DRAW, "layer") == *layer &&
            node.hasAttributeNS(DRAW, "layer"))
          continue;
        node.setAttributeNS(DRAW, "draw:layer", *layer);
      } else
        node.removeAttributeNS(DRAW, "layer");
    }
  });
}
NativeStageLayerEdit
Presentation::edit_layers(int slide,
                          const std::vector<NativeObjectRef> &selection) {
  finish_handle_drag();
  const auto &model = scene(slide);
  auto refs = native_object_selection(model, selection);
  std::vector<XmlElement> nodes;
  for (auto ref : refs)
    nodes.push_back(native_object_source(model, ref));
  return NativeStageLayerEdit(package_, slide, revision_, refs, nodes,
                              document_identity_);
}
std::optional<PresentationPosition>
Presentation::commit_layers(NativeStageLayerEdit edit,
                            std::optional<TextSelection> text_selection) {
  if (read_only())
    fail("This presentation is read-only");
  if (drag_ || edit.identity_ != document_identity_ ||
      edit.revision_ != revision_)
    fail("The presentation changed while layers were being edited");
  auto candidate = edit.state_->snapshot();
  auto before = package_.xml.toByteArray();
  auto parts = candidate.part_changes_from(package_);
  if (before == candidate.xml.toByteArray() && parts.empty())
    return {};
  candidate.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(candidate);
  auto scenes = read_presentation_scenes(candidate, reader);
  (void)read_presentation_scenes(candidate, reader, SceneTarget::Print);
  PresentationPosition previous{edit.slide_, -1, -1}, next{edit.slide_, -1, -1};
  previous.objects = edit.original_refs_;
  previous.selection = text_selection;
  if (!previous.objects.empty()) {
    previous.shape = previous.objects.front().shape;
    previous.box = scene(edit.slide_).shapes.at(previous.shape).editor_index;
  }
  auto refs = native_object_selection(
      scenes.at(edit.slide_),
      native_object_references(scenes.at(edit.slide_), edit.state_->selected));
  if (!refs.empty()) {
    next.objects = refs;
    next.shape = refs.front().shape;
    next.box = scenes.at(edit.slide_).shapes.at(next.shape).editor_index;
    if (previous.objects.size() == 1 && refs.size() == 1 &&
        previous.objects.front().ancestors == 0 && refs.front().ancestors == 0)
      next.selection = text_selection;
  }
  package_ = std::move(candidate);
  scenes_ = std::move(scenes);
  text_reader_ = std::move(reader);
  ++revision_;
  end_text_edit();
  remember(before, previous, next, false, std::move(parts));
  return next;
}
} // namespace DomeOffice
