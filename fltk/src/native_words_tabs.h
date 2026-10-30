// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_text_layout.h"
namespace DomeOffice {
struct WordsTabStopSpec {
  // STYLE attributes keep their original lexical forms until explicitly edited.
  std::map<Text, Text> properties;
  XmlElement source;
  enum LeaderScope {
    Common,
    ContentAutomatic,
    MasterAutomatic
  } leader_scope = Common;
};
struct WordsTabSet {
  std::vector<WordsTabStopSpec> stops;
  XmlElement source;
};
struct WordsTabsInfo {
  std::optional<WordsTabSet> stops; // Missing means a mixed selection.
  double default_distance = 72 * 1.25 / 2.54;
  bool relative = true, origin_editable = false;
};
struct WordsTabsFormat {
  std::optional<WordsTabSet>
      stops;            // Empty list explicitly clears inherited stops.
  bool inherit = false; // Remove only the paragraph's direct tab override.
  std::optional<double>
      default_distance;         // Document paragraph default, in points.
  std::optional<bool> relative; // Document default, ODF 1.4 only.
};
double words_tab_position(const WordsTabStopSpec &);
WordsTabStopSpec words_new_tab_stop(double points);
void validate_words_tabs(const WordsTabsFormat &);
WordsTabsInfo read_words_tabs(const OdfPackage &, size_t begin, size_t end);
bool format_words_tabs(OdfPackage &, size_t begin, size_t end,
                       const WordsTabsFormat &);
} // namespace DomeOffice
