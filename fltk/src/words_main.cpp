// SPDX-License-Identifier: GPL-2.0-or-later
#include "odf_package.h"
int runOffice(DomeOffice::Kind, int, char**);
int main(int argc, char** argv) { return runOffice(DomeOffice::Kind::Words, argc, argv); }
