// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
#include <array>
#include <tuple>
namespace DomeOffice {
using NativeStageStyleKey = std::tuple<Text, Text, Text>;
// Keys identify kind/family/name. Automatic styles and font declarations retain
// their content.xml/styles.xml scope; common/default definitions follow the
// native document cascade. A generic string replacement cannot model this.
struct NativeStageStyleCatalog {
  using Key = NativeStageStyleKey;
  struct Definition {
    XmlElement node;
    Key key;
    int source;
    Text group;
  };
  std::array<XmlDocument, 2> documents;
  std::array<std::map<Key, size_t>, 2> automatic, fonts;
  std::map<Key, size_t> common;
  std::map<Text, size_t> defaults;
  std::vector<Definition> definitions;
  explicit NativeStageStyleCatalog(const OdfPackage &);
  static std::optional<Key> key(XmlElement);
  size_t resolve(const Key &, int source, bool common_only = false) const;

private:
  void index(int);
};
struct NativeStageStyleReference {
  XmlNode::Attribute attribute;
  std::vector<NativeStageStyleKey> keys;
  bool common_only = false;
};
Text native_stage_text_family(XmlElement);
Text native_stage_table_family(XmlElement);
std::vector<NativeStageStyleReference>
native_stage_style_references(XmlElement, const Text &context_family = {},
                              const Text &context_kind = {},
                              bool common_context = false);
} // namespace DomeOffice
