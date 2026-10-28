#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build=${CALLIGRA_BUILD_DIR:-$root/build-native}
extra=()
if [[ -n ${FLTK_PREFIX:-} ]]; then
  extra+=("-DFLTK_DIR=$FLTK_PREFIX/share/fltk")
fi
cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -march=native' \
  -DCMAKE_INSTALL_PREFIX=/opt/calligra-fltk \
  -DCALLIGRA_BUILD_FLTK=ON -DCALLIGRA_BUILD_QT_FRONTENDS=OFF "${extra[@]}" "$@"
cmake --build "$build" --parallel "${BUILD_JOBS:-4}"
