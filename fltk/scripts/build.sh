#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
prefix=${FLTK_PREFIX:-/opt/fltk-vr/toolkit}
backends=${CALLIGRA_BACKEND_PREFIX:-}
search="$prefix${backends:+;$backends}"
cmake -S "$root" -B "$root/build-fltk" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -march=native' \
  -DCMAKE_PREFIX_PATH="$search" -DCMAKE_INSTALL_PREFIX=/opt/calligra-fltk \
  -DCALLIGRA_BUILD_FLTK=ON -DCALLIGRA_BUILD_QT_FRONTENDS=OFF "$@"
cmake --build "$root/build-fltk" --parallel "${BUILD_JOBS:-4}"
