// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_cut.h"
#include "native_stage_removal.h"
namespace DomeOffice {
struct NativeStageCut::State {
  const Presentation *owner;
  std::shared_ptr<const char> identity;
  uint64_t revision;
  NativeStageFragment fragment;
  NativeStageRemoval removal;
};
NativeStageCut::NativeStageCut(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
NativeStageCut::~NativeStageCut() = default;
NativeStageCut::NativeStageCut(NativeStageCut &&) noexcept = default;
NativeStageCut &NativeStageCut::operator=(NativeStageCut &&) noexcept = default;
const NativeStageFragment &NativeStageCut::fragment() const {
  if (!state_)
    throw std::runtime_error("This cut draft has already been consumed");
  return state_->fragment;
}
NativeStageCut Presentation::prepare_cut_objects(
    int slide, const std::vector<NativeObjectRef> &selection) const {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  if (drag_)
    throw std::runtime_error(
        "Finish the current object gesture before cutting");
  const auto &model = scene(slide);
  auto removal = prepare_stage_removal(package_, model, slide, selection);
  auto fragment = native_stage_fragment(package_, model, selection);
  // Validate both targets before publishing anything to the clipboard. Commit
  // repeats the ordinary structural transaction validation after its stale
  // guard; a failed clipboard publication never consumes source or redo.
  removal.package.validate_archive_limits();
  auto reader = std::make_shared<OdfTextReader>(removal.package);
  (void)read_presentation_scenes(removal.package, reader);
  (void)read_presentation_scenes(removal.package, reader, SceneTarget::Print);
  return NativeStageCut(std::make_unique<NativeStageCut::State>(
      NativeStageCut::State{this, document_identity_, revision_,
                            std::move(fragment), std::move(removal)}));
}
PresentationPosition Presentation::commit_cut_objects(NativeStageCut cut) {
  if (read_only())
    throw std::runtime_error("This presentation is read-only");
  if (!cut.state_ || drag_ || cut.state_->owner != this ||
      cut.state_->identity != document_identity_ ||
      cut.state_->revision != revision_)
    throw std::runtime_error(
        "The presentation changed while the cut was pending");
  auto &removal = cut.state_->removal;
  auto result = commit_object_structure(
      std::move(removal.package), removal.selected, removal.previous, true);
  end_text_edit();
  return result;
}
} // namespace DomeOffice
