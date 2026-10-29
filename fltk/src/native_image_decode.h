// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
#include <FL/Fl_RGB_Image.H>
namespace DomeOffice {
std::unique_ptr<Fl_RGB_Image> decode_image(const Bytes &, int width,
                                           int height);
bool image_is_vector(const Bytes &);
} // namespace DomeOffice
