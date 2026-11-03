// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_selection.h"
#include <memory>
namespace DomeOffice {
enum class NativeLayerScope { Document, Slide, Master };
struct NativeLayerInfo {
  Text name, title, description;
  SceneLayer properties;
  size_t uses = 0;
};
struct NativeLayerPatch {
  std::optional<Text> name, title, description;
  std::optional<SceneLayer::Display> display;
  std::optional<bool> protected_objects;
};
// A detached draft. Apply publishes all source/archive changes in one history
// transaction; cancelling or a rejected draft command cannot mutate the
// document.
class NativeStageLayerEdit {
public:
  ~NativeStageLayerEdit();
  NativeStageLayerEdit(NativeStageLayerEdit &&) noexcept;
  NativeStageLayerEdit &operator=(NativeStageLayerEdit &&) noexcept;
  NativeStageLayerEdit(const NativeStageLayerEdit &) = delete;
  NativeStageLayerEdit &operator=(const NativeStageLayerEdit &) = delete;
  bool read_only() const;
  bool available(NativeLayerScope) const;
  bool own_set(NativeLayerScope) const;
  std::vector<NativeLayerInfo> layers(NativeLayerScope) const;
  std::vector<Text> missing_layers(NativeLayerScope) const;
  Text new_name(NativeLayerScope) const;
  void make_local(NativeLayerScope, bool copy_global = true);
  void inherit(NativeLayerScope);
  void create(NativeLayerScope, const Text &, bool repair_missing = false);
  void update(NativeLayerScope, const Text &, const NativeLayerPatch &);
  void remove(NativeLayerScope, const Text &);
  void validate() const;
  bool has_selection() const;
  std::vector<std::optional<Text>> selected_layers() const;
  void assign(std::optional<Text>);

private:
  friend class Presentation;
  struct State;
  std::shared_ptr<const char> identity_;
  std::unique_ptr<State> state_;
  int slide_;
  uint64_t revision_;
  std::vector<NativeObjectRef> original_refs_;
  NativeStageLayerEdit(const OdfPackage &, int, uint64_t,
                       const std::vector<NativeObjectRef> &,
                       const std::vector<XmlElement> &,
                       std::shared_ptr<const char>);
  template <class F> void change(F);
};
} // namespace DomeOffice
