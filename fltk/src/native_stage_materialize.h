// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_merge.h"
namespace DomeOffice {
// Point-of-use native formatting on a detached receiving draft. Existing target
// definitions/defaults/objects are untouched. Original imported style graphs
// and unknown properties remain; native values are isolated in fresh styles.
// Default tabs have finite frame coverage. Table size values capture the source
// frame's resolved fixed widths; arbitrary subsequent resizing/next-style and
// unsupported ODF property semantics need their own interoperability work.
void materialize_stage_import_contexts(NativeStageMergeDraft &);
} // namespace DomeOffice
