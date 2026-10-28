// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "odf_package.h"
namespace DomeOffice {
OdfPackage import_docx(const Text& path);
OdfPackage import_pptx(const Text& path);
}
