// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_fragment.h"
namespace DomeOffice {
// Prepare an isolated import source with names/resources reserved against the
// entire receiving package. This does not merge defaults or publish a paste.
// The input is the bounded private selection representation, not a whole source
// presentation supplied by the caller as an unchecked fragment. Variable/field
// names and formulas retain their original local data; global field/section
// dependency isolation is still required before merging into a destination.
NativeStageFragment remap_stage_fragment_for_destination(const OdfPackage &,
                                                         const Bytes &);
} // namespace DomeOffice
