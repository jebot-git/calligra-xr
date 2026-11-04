// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_paste.h"
#include "native_stage_materialize.h"
#include "native_stage_identity.h"
#include "native_stage_order.h"
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
TextList tokens(const Text &value) {
  TextList out;
  std::istringstream stream(value.toStdString());
  for (std::string s; stream >> s;) {
    if (out.size() >= 100000)
      fail("Paste navigation exceeds the limit");
    out.push_back(s);
  }
  return out;
}
void preflight(const NativeStageMergeDraft &draft) {
  auto version = [](const Text &value) {
    const TextList order{"1.0", "1.1", "1.2", "1.3", "1.4"};
    auto at = std::find(order.begin(), order.end(), value);
    if (at == order.end())
      fail("Paste requires a known OpenDocument version");
    return int(at - order.begin());
  };
  auto target = version(draft.package.xml.documentElement().attributeNS(
      OFFICE, "version", "1.2"));
  if (target < 2 || version(draft.source.xml.documentElement().attributeNS(
                        OFFICE, "version", "1.2")) > target)
    fail("This paste requires an explicit OpenDocument version upgrade");
  size_t work = 0;
  auto walk = [&](auto &&self, XmlElement n, int depth) -> void {
    if (++work > 1000000 || depth > 256)
      fail("Paste semantic traversal exceeds the limit");
    if (n.namespaceURI() == DRAW &&
        TextList{"object", "object-ole"}.contains(n.localName()))
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        if (c.isElement())
          fail("Pasting inline embedded documents needs scoped style and "
               "reference isolation");
    if (n.namespaceURI() == TEXT &&
        (n.localName().startsWith("variable-") ||
         n.localName().startsWith("user-field-") ||
         TextList{"sequence", "sequence-decls", "sequence-decl", "expression"}
             .contains(n.localName())))
      fail("Pasting document-global fields needs declaration isolation");
    if (n.namespaceURI() == STYLE && n.localName() == "map")
      fail("Pasting conditional style maps needs native condition semantics");
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        self(self, c, depth + 1);
  };
  walk(walk, draft.source.xml.documentElement(), 0);
  if (!draft.source.entry("styles.xml").empty())
    walk(walk, draft.source_styles.documentElement(), 0);
}
void navigation(NativeStageMergeDraft &draft, int slide) {
  auto source = children(draft.source.body(), DRAW, "page").at(0);
  auto target = children(draft.package.body(), DRAW, "page").at(slide);
  auto requested = tokens(source.attributeNS(DRAW, "nav-order"));
  TextList physical;
  auto collect = [&](auto &&self, XmlElement n) -> void {
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement() && c.namespaceURI() == DRAW) {
        if (c.localName() == "g")
          self(self, c);
        else if (native_stage_object_identity(c)) {
          auto id = c.attributeNS(XML, "id", c.attributeNS(DRAW, "id"));
          if (!id.empty())
            physical.push_back(id);
        }
      }
  };
  collect(collect, source);
  TextList declared;
  for (auto id : physical)
    if (requested.contains(id))
      declared.push_back(id);
  if (requested != declared)
    fail("This source's explicit object navigation needs ordered import "
         "support");
  // An absent receiving declaration follows preserved physical XML order.
  // Existing explicit orders retain their spelling and gain new source targets.
  if (target.hasAttributeNS(DRAW, "nav-order")) {
    auto old = target.attributeNS(DRAW, "nav-order");
    for (auto id : physical) {
      if (!old.empty())
        old += " ";
      old += id;
    }
    target.setAttributeNS(DRAW, "draw:nav-order", old);
  }
}
} // namespace
struct NativeStagePaste::State {
  const Presentation *owner;
  std::shared_ptr<const char> identity;
  uint64_t revision;
  NativeStageMergeDraft draft;
  PresentationPosition previous;
};
NativeStagePaste::NativeStagePaste(std::unique_ptr<State> s)
    : state_(std::move(s)) {}
NativeStagePaste::~NativeStagePaste() = default;
NativeStagePaste::NativeStagePaste(NativeStagePaste &&) noexcept = default;
NativeStagePaste &
NativeStagePaste::operator=(NativeStagePaste &&) noexcept = default;
NativeStagePaste
Presentation::prepare_paste_objects(int slide, const Bytes &wire,
                                    PresentationPosition previous) const {
  if (read_only())
    fail("This presentation is read-only");
  if (drag_)
    fail("Finish the current object gesture before pasting");
  (void)scene(slide);
  previous.slide = slide;
  auto draft = prepare_stage_merge(package_, slide, wire);
  preflight(draft);
  materialize_stage_import_contexts(draft);
  navigation(draft, slide);
  auto reader = std::make_shared<OdfTextReader>(draft.package);
  (void)read_presentation_scenes(draft.package, reader);
  (void)read_presentation_scenes(draft.package, reader, SceneTarget::Print);
  return NativeStagePaste(std::make_unique<NativeStagePaste::State>(
      NativeStagePaste::State{this, document_identity_, revision_,
                              std::move(draft), std::move(previous)}));
}
PresentationPosition
Presentation::commit_paste_objects(NativeStagePaste paste) {
  if (read_only())
    fail("This presentation is read-only");
  if (!paste.state_ || drag_ || paste.state_->owner != this ||
      paste.state_->identity != document_identity_ ||
      paste.state_->revision != revision_)
    fail("The presentation changed while the paste was pending");
  auto &state = *paste.state_;
  auto result =
      commit_object_structure(std::move(state.draft.package),
                              state.draft.selected, state.previous, true);
  end_text_edit();
  return result;
}
} // namespace DomeOffice
