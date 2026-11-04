// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_timing.h"
#include <libxml/tree.h>
namespace DomeOffice {
std::vector<NativeStageTimingReference> native_stage_timing_references(
    const Text &value, const std::function<bool(const Text &)> &selected,
    NativeStageTimingBudget *shared_budget) {
  std::vector<NativeStageTimingReference> result;
  NativeStageTimingBudget local_budget;
  auto &budget = shared_budget ? *shared_budget : local_budget;
  if (budget.examined > 1000000 || budget.terms > 100000)
    throw std::runtime_error("Invalid clipboard timing traversal budget");
  size_t offset = 0;
  for (const auto &raw : value.split(';')) {
    if (++budget.terms > 100000)
      throw std::runtime_error("Clipboard timing list exceeds the limit");
    auto term = raw.trimmed();
    if (!term.startsWith("wallclock(") && !term.startsWith("accessKey(")) {
      bool reference_syntax = false, found = false;
      auto dot = term.lastIndexOf('.');
      while (dot > 0) {
        // Bound the sum of prefix scans, not only the number of separators.
        if (size_t(dot) > 1000000 - budget.examined)
          throw std::runtime_error(
              "Clipboard timing identifier traversal exceeds the limit");
        budget.examined += size_t(dot);
        // A fractional clock offset is not an event separator. In particular,
        // event-0.5s must not bind to a coincidentally named event-0 object.
        auto event_start = term.mid(dot + 1, 1).toUtf8();
        if (event_start.empty() ||
            xmlValidateNCName(
                reinterpret_cast<const xmlChar *>(event_start.c_str()), 0) !=
                0) {
          dot = term.left(dot).lastIndexOf('.');
          continue;
        }
        auto prefix = term.left(dot);
        Text identifier;
        bool escaped = false;
        for (auto ch : prefix) {
          if (!escaped && ch == U'\\') {
            escaped = true;
            continue;
          }
          identifier.push_back(ch);
          escaped = false;
        }
        auto utf8 = identifier.toUtf8();
        if (!escaped && !identifier.empty() &&
            xmlValidateNCName(reinterpret_cast<const xmlChar *>(utf8.c_str()),
                              0) == 0) {
          reference_syntax = true;
          if (selected(identifier)) {
            result.push_back(
                {offset + size_t(raw.indexOf(term)), size_t(dot), identifier});
            found = true;
            break;
          }
        }
        dot = term.left(dot).lastIndexOf('.');
      }
      if (reference_syntax && !found)
        throw std::runtime_error(
            "A copied timing expression references an unselected object");
    }
    offset += raw.size() + 1;
  }
  return result;
}
} // namespace DomeOffice
