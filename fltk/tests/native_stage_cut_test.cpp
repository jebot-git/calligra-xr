// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_fixture.h"
#include "native_stage_cut.h"
namespace {
Bytes source(const Presentation &p) {
  return p.scene(0).source.ownerDocument().toByteArray();
}
struct Snapshot {
  Bytes xml;
  uint64_t revision;
  bool modified, undo, redo;
  explicit Snapshot(const Presentation &p)
      : xml(source(p)), revision(p.revision()), modified(p.modified()),
        undo(p.can_undo()), redo(p.can_redo()) {}
  void same(const Presentation &p) const {
    assert(xml == source(p) && revision == p.revision() &&
           modified == p.modified() && undo == p.can_undo() &&
           redo == p.can_redo());
  }
};
void parts_unchanged(const Presentation &p, const Map<Text, Bytes> &original) {
  for (auto at = original.cbegin(); at != original.cend(); ++at)
    if (at.key() != "content.xml")
      assert(p.image(at.key()) == at.value());
}
std::vector<Text> selected_names(const NativeStageFragment &fragment) {
  OdfPackage p(Kind::Stage);
  p.load_bytes(fragment.archive);
  std::vector<Text> result;
  for (auto node : stage_fragment_nodes(p, fragment.selection))
    result.push_back(node.attributeNS(DRAW, "name"));
  return result;
}
void lifecycle(const Text &input, const Text &output, const Text &artifacts) {
  Presentation p;
  p.load(input);
  auto original_parts = read_document_archive(input);
  auto original = source(p);
  auto image = p.scene(0).shapes.at(find(p, "Y")).image;
  // Keep a real redo branch throughout draft preparation, encoding and cancel.
  p.move_shape(0, find(p, "A"), {5, 7});
  p.undo();
  assert(!p.modified() && p.can_redo());
  auto before = Snapshot(p);
  std::vector<NativeObjectRef> refs{ref(p, "Y"), ref(p, "X")};
  {
    auto cancelled = p.prepare_cut_objects(0, refs);
    before.same(p);
    auto transferred =
        decode_stage_fragment(encode_stage_fragment(cancelled.fragment()));
    assert(selected_names(transferred) == std::vector<Text>({"Y", "X"}));
    before.same(p);
  }
  before.same(p);
  auto cut = p.prepare_cut_objects(0, refs);
  if (!artifacts.empty()) {
    auto encoded = encode_stage_fragment(cut.fragment());
    std::ofstream raw((artifacts + "/selected.cstage").toStdString(),
                      std::ios::binary);
    raw.write(encoded.data(), encoded.size());
    assert(raw.good());
    OdfPackage selected(Kind::Stage);
    selected.load_bytes(cut.fragment().archive);
    selected.save(artifacts + "/selected.odp");
  }
  auto next = p.commit_cut_objects(std::move(cut));
  assert(p.modified() && p.can_undo() && !p.can_redo());
  assert(p.revision() == before.revision + 1);
  order(p, {"A", "B", "N1", "N2", "C"});
  auto changed = source(p);
  auto page = p.scene(0).source;
  assert(page.attributeNS(DRAW, "nav-order") == "a b n1 n2 c");
  auto groups =
      p.scene(0).source.ownerDocument().elementsByTagNameNS(DRAW, "g");
  assert(groups.size() == 2 && groups[0].attributeNS(DRAW, "name") == "Outer" &&
         groups[1].attributeNS(DRAW, "name") == "Inner");
  parts_unchanged(p, original_parts);
  // Only the second slide's original rectangle remains there.
  assert(p.count() == 2 && p.scene(1).shapes.size() == 1);
  auto undo = p.undo();
  assert(undo && undo->objects == refs && source(p) == original &&
         !p.modified());
  assert(p.scene(0).shapes.at(find(p, "Y")).image == image);
  parts_unchanged(p, original_parts);
  assert(p.redo() == next && source(p) == changed && p.modified());
  p.save(output);
  assert(!p.modified());
  Presentation reopened;
  reopened.load(output);
  order(reopened, {"A", "B", "N1", "N2", "C"});
  assert(source(reopened) == changed);
  parts_unchanged(reopened, original_parts);
  // Moved drafts have no accessible payload or commit token.
  p.load(input);
  auto draft = p.prepare_cut_objects(0, {ref(p, "A"), ref(p, "B")});
  auto moved = std::move(draft);
  auto stable = Snapshot(p);
  rejects([&] { (void)draft.fragment(); });
  rejects([&] { p.commit_cut_objects(std::move(draft)); });
  stable.same(p);
  p.commit_cut_objects(std::move(moved));
  order(p, {"X", "Y", "N1", "N2", "C"});
  stable = Snapshot(p);
  rejects([&] { p.commit_cut_objects(std::move(moved)); });
  stable.same(p);
}
void groups_and_empty_slide(const Text &input) {
  Presentation p;
  p.load(input);
  auto original = source(p);
  auto outer = ref(p, "X", 1);
  auto cut = p.prepare_cut_objects(0, {ref(p, "X"), outer, ref(p, "Y"), outer});
  assert(selected_names(cut.fragment()) == std::vector<Text>({"Outer"}));
  p.commit_cut_objects(std::move(cut));
  order(p, {"A", "B", "C"});
  auto previous = p.undo();
  assert(previous &&
         previous->objects == std::vector<NativeObjectRef>({outer}) &&
         source(p) == original && !p.modified());
  std::vector<NativeObjectRef> all{ref(p, "C"), outer, ref(p, "A"),
                                   ref(p, "B")};
  auto everything = p.prepare_cut_objects(0, all);
  assert(selected_names(everything.fragment()) ==
         std::vector<Text>({"C", "Outer", "A", "B"}));
  auto empty = p.commit_cut_objects(std::move(everything));
  assert(p.scene(0).shapes.empty() && empty.objects.empty() &&
         empty.shape == -1);
  assert(p.count() == 2 && p.scene(1).shapes.size() == 1);
  assert(p.print_scenes().size() == 2);
  previous = p.undo();
  assert(previous && previous->objects == all && source(p) == original &&
         !p.modified());
  assert(p.redo() == empty && p.scene(0).shapes.empty());
}
void stale(const Text &input, const Text &save_as) {
  Presentation p, other;
  p.load(input);
  other.load(input);
  auto draft = p.prepare_cut_objects(0, {ref(p, "X", 1)});
  auto stable = Snapshot(p), other_stable = Snapshot(other);
  rejects([&] { other.commit_cut_objects(std::move(draft)); });
  stable.same(p);
  other_stable.same(other);
  // Default model copies share archive/image storage and the document token,
  // but must never accept another model instance's delayed cut transaction.
  auto copied = p;
  auto copied_draft = p.prepare_cut_objects(0, {ref(p, "X", 1)});
  auto copied_stable = Snapshot(copied);
  rejects([&] { copied.commit_cut_objects(std::move(copied_draft)); });
  stable.same(p);
  copied_stable.same(copied);
  for (int mode = 0; mode < 4; ++mode) {
    p.load(input);
    auto pending = p.prepare_cut_objects(0, {ref(p, "X", 1)});
    if (mode == 0)
      p.move_shape(0, find(p, "A"), {1, 2});
    else if (mode == 1) {
      p.move_shape(0, find(p, "A"), {1, 2});
      p.undo(); // Matching bytes do not make an old revision safe.
    } else if (mode == 2)
      p.load(input); // Matching file still has a distinct document identity.
    else
      p.begin_objects_move(0, {ref(p, "A")}, {});
    auto after = Snapshot(p);
    rejects([&] { p.commit_cut_objects(std::move(pending)); });
    after.same(p);
    if (mode == 3) {
      assert(p.objects_dragging());
      rejects([&] { p.prepare_cut_objects(0, {ref(p, "A")}); });
      assert(p.objects_dragging());
      after.same(p);
      p.finish_handle_drag(true);
    }
  }
  // Save As without a source edit is compatible; undo returns to that saved
  // state rather than an obsolete saved-state marker in a copied history.
  p.load(input);
  p.move_shape(0, find(p, "A"), {1, 2});
  auto pending = p.prepare_cut_objects(0, {ref(p, "X", 1)});
  p.save(save_as);
  assert(!p.modified());
  p.commit_cut_objects(std::move(pending));
  assert(p.modified());
  p.undo();
  assert(!p.modified());
}
void guards(const Text &input) {
  Presentation p;
  p.load(input);
  auto stable = Snapshot(p);
  rejects([&] { p.prepare_cut_objects(0, {}); });
  rejects([&] { p.prepare_cut_objects(0, {{10000, 0}}); });
  rejects([&] { p.prepare_cut_objects(-1, {ref(p, "A")}); });
  stable.same(p);
  auto package = fixture();
  auto page = children(package.body(), DRAW, "page")[0];
  auto set = package.xml.createElementNS(DRAW, "draw:layer-set");
  auto layer = package.xml.createElementNS(DRAW, "draw:layer");
  layer.setAttributeNS(DRAW, "draw:name", "Locked");
  layer.setAttributeNS(DRAW, "draw:protected", "true");
  layer.setAttributeNS(DRAW, "draw:display", "printer");
  set.appendChild(layer);
  page.insertBefore(set, page.firstChild());
  auto line = package.xml.elementsByTagNameNS(DRAW, "line")[0];
  line.setAttributeNS(DRAW, "draw:layer", "Locked");
  package.save(input);
  p.load(input);
  stable = Snapshot(p);
  // A protected print-only descendant prohibits cutting its selected group.
  assert(!p.copy_objects(0, {ref(p, "X", 1)}).archive.empty());
  rejects([&] { p.prepare_cut_objects(0, {ref(p, "X", 1)}); });
  rejects([&] { p.prepare_cut_objects(0, {ref(p, "A"), ref(p, "X", 1)}); });
  stable.same(p);
  package = fixture();
  auto rect = package.xml.elementsByTagNameNS(DRAW, "rect")[1];
  auto link = package.xml.createElementNS(TEXT, "text:a");
  link.setAttributeNS(XLINK, "xlink:href", "#a");
  set_plain_text(link, "link");
  children(rect, TEXT, "p")[0].appendChild(link);
  package.save(input);
  p.load(input);
  stable = Snapshot(p);
  assert(!p.copy_objects(0, {ref(p, "A")}).archive.empty());
  rejects([&] { p.prepare_cut_objects(0, {ref(p, "A")}); });
  stable.same(p);
  // A screen-valid document may still have an invalid print-only object.
  // Preparing another object's cut must validate that second target too.
  package = fixture();
  page = children(package.body(), DRAW, "page")[0];
  set = package.xml.createElementNS(DRAW, "draw:layer-set");
  layer = package.xml.createElementNS(DRAW, "draw:layer");
  layer.setAttributeNS(DRAW, "draw:name", "Printer");
  layer.setAttributeNS(DRAW, "draw:display", "printer");
  set.appendChild(layer);
  page.insertBefore(set, page.firstChild());
  for (auto rect : package.xml.elementsByTagNameNS(DRAW, "rect"))
    if (rect.attributeNS(DRAW, "name") == "N2") {
      rect.setAttributeNS(DRAW, "draw:layer", "Printer");
      rect.setAttributeNS(SVG, "svg:width", "nanpt");
    }
  package.save(input);
  p.load(input);
  stable = Snapshot(p);
  rejects([&] { p.prepare_cut_objects(0, {ref(p, "A")}); });
  stable.same(p);
  // Signed/read-only source can be copied, but never prepared for deletion.
  package = fixture();
  package.set_entry("META-INF/documentsignatures.xml", "signature", "text/xml");
  package.save(input);
  p.load(input);
  stable = Snapshot(p);
  assert(p.read_only() && !p.copy_objects(0, {ref(p, "A")}).archive.empty());
  rejects([&] { p.prepare_cut_objects(0, {ref(p, "A")}); });
  stable.same(p);
}
} // namespace
int main(int argc, char **argv) {
  TempDir dir;
  auto input = dir.path() + "/source.odp", output = dir.path() + "/cut.odp";
  fixture().save(input);
  Text artifacts;
  if (argc == 3 && Text(argv[1]) == "--documents") {
    artifacts = argv[2];
    std::filesystem::create_directories(artifacts.toStdString());
    fixture().save(artifacts + "/source.odp");
  }
  lifecycle(input, output, artifacts);
  if (!artifacts.empty())
    std::filesystem::copy_file(
        output.toStdString(), (artifacts + "/cut.odp").toStdString(),
        std::filesystem::copy_options::overwrite_existing);
  groups_and_empty_slide(input);
  stale(input, output);
  guards(input);
  std::cout
      << "PASS prepared object cut, cancellation/redo preservation, exact "
         "source/archive/selection history, stale/foreign/moved drafts, "
         "Save As, protected descendants, links and read-only guards\n";
}
