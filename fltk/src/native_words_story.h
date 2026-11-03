// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_page_stories.h"
namespace DomeOffice {
struct WordsStoryAddress {
  Text master;
  TextPageStory::Kind kind = TextPageStory::Header;
  TextPageStory::Variant variant = TextPageStory::Default;
  enum Region { Direct, Left, Center, Right } region = Direct;
};
// An isolated source-scoped rich-text projection. Header/footer automatic
// styles come from styles.xml, independently of same-named body definitions.
// Merge writes only the addressed story and explicitly edited style groups;
// body content and unrelated archive parts remain owned by the parent.
class WordsStoryEdit {
public:
  WordsStoryEdit(const OdfPackage &, WordsStoryAddress);
  OdfPackage document() const;
  TextFlow editing_flow(const OdfPackage &) const;
  OdfPackage finish(const OdfPackage &, bool displayed) const;
  bool displayed() const;
  const WordsStoryAddress &address() const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};
} // namespace DomeOffice
