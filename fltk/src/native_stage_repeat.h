// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_fragment.h"
namespace DomeOffice {
// Detached canonical selection with explicit row/cell instances. New local
// identities/references are typed; outer references still reach the original
// first instance. Embedded documents keep their own unmodified namespaces.
// Existing formula-bearing text tables retain the importer's explicit guard.
NativeStageFragment
materialize_stage_repeated_cells(const Bytes &native_fragment);
} // namespace DomeOffice
