# SPDX-License-Identifier: GPL-2.0-or-later
pkg_check_modules(CLIPBOARD_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
find_program(CLIPBOARD_WAYLAND_SCANNER wayland-scanner REQUIRED)
set(clipboard_protocol "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-data-control-v1.xml")
add_custom_command(OUTPUT ext-data-control-client.h ext-data-control-protocol.c
  COMMAND "${CLIPBOARD_WAYLAND_SCANNER}" client-header "${clipboard_protocol}" ext-data-control-client.h
  COMMAND "${CLIPBOARD_WAYLAND_SCANNER}" private-code "${clipboard_protocol}" ext-data-control-protocol.c
  DEPENDS "${clipboard_protocol}")
add_library(calligra-native-clipboard-bundle STATIC src/native_clipboard_bundle.cpp)
target_include_directories(calligra-native-clipboard-bundle PUBLIC src)
target_compile_options(calligra-native-clipboard-bundle PRIVATE -Wall -Wextra -Wpedantic)
add_executable(calligra-wayland-clipboard src/native_wayland_clipboard.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-client.h"
  "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-protocol.c")
target_include_directories(calligra-wayland-clipboard PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(calligra-wayland-clipboard PRIVATE calligra-native-clipboard-bundle PkgConfig::CLIPBOARD_WAYLAND)
target_compile_options(calligra-wayland-clipboard PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS calligra-wayland-clipboard RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/calligra-fltk)
install(FILES protocols/ext-data-control-v1.xml protocols/README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/calligra-fltk/protocols)
if(BUILD_TESTING)
  add_executable(calligra-native-clipboard-bundle-test tests/native_clipboard_bundle_test.cpp)
  target_link_libraries(calligra-native-clipboard-bundle-test PRIVATE calligra-native-clipboard-bundle)
  target_compile_options(calligra-native-clipboard-bundle-test PRIVATE -UNDEBUG)
  add_test(NAME native-clipboard-bundle COMMAND calligra-native-clipboard-bundle-test)
  add_executable(calligra-wayland-clipboard-peer tests/wayland/clipboard_peer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-client.h"
    "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-protocol.c")
  target_include_directories(calligra-wayland-clipboard-peer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(calligra-wayland-clipboard-peer PRIVATE PkgConfig::CLIPBOARD_WAYLAND)
  target_compile_options(calligra-wayland-clipboard-peer PRIVATE -UNDEBUG)
endif()
