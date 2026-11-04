// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_stage_fragment.h"
#include "native_clipboard_bundle.h"
namespace DomeOffice {
std::vector<std::string> stage_clipboard_preferences();
NativeClipboardBundle stage_clipboard_formats(const NativeStageFragment &);
// Validate exactly the received representation. Malformed rich data is an
// error, never silently replaced by its less expressive plain-text fallback.
Bytes stage_clipboard_fragment(const NativeClipboardFormat &);
} // namespace DomeOffice
