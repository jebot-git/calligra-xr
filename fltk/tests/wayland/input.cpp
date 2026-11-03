// SPDX-License-Identifier: GPL-2.0-or-later
// Sends input only to the explicitly named, private Cage Wayland socket.
#include "virtual-keyboard-client.h"
#include "virtual-pointer-client.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
namespace {
wl_seat *seat;
zwp_virtual_keyboard_manager_v1 *key_manager;
zwlr_virtual_pointer_manager_v1 *pointer_manager;
void global(void *, wl_registry *registry, uint32_t name, const char *interface,
            uint32_t) {
  if (!std::strcmp(interface, "wl_seat"))
    seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, name, &wl_seat_interface, 1));
  if (!std::strcmp(interface, "zwp_virtual_keyboard_manager_v1"))
    key_manager =
        static_cast<zwp_virtual_keyboard_manager_v1 *>(wl_registry_bind(
            registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
  if (!std::strcmp(interface, "zwlr_virtual_pointer_manager_v1"))
    pointer_manager =
        static_cast<zwlr_virtual_pointer_manager_v1 *>(wl_registry_bind(
            registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1));
}
void removed(void *, wl_registry *, uint32_t) {}
} // namespace
int main(int argc, char **argv) try {
  if (argc != 2)
    throw std::runtime_error("Pass the private host Wayland socket explicitly");
  wl_display *display = wl_display_connect(argv[1]);
  if (!display)
    throw std::runtime_error("Cannot connect private host");
  auto *registry = wl_display_get_registry(display);
  const wl_registry_listener listener{global, removed};
  wl_registry_add_listener(registry, &listener, nullptr);
  if (wl_display_roundtrip(display) < 0 || !seat || !key_manager ||
      !pointer_manager)
    throw std::runtime_error("Private host input globals missing");
  auto *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_rule_names names{};
  names.layout = "us";
  auto *map =
      xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!map)
    throw std::runtime_error("Cannot create fixture keymap");
  auto *state = xkb_state_new(map);
  auto *keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
      key_manager, seat);
  auto *pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
      pointer_manager, seat);
  char *text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
  size_t size = std::strlen(text) + 1;
  int fd = memfd_create("calligra-test-keymap", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size) < 0)
    throw std::runtime_error("Cannot allocate fixture keymap");
  void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED)
    throw std::runtime_error("Cannot map fixture keymap");
  std::memcpy(data, text, size);
  free(text);
  zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                 size);
  munmap(data, size);
  close(fd);
  if (wl_display_roundtrip(display) < 0)
    throw std::runtime_error("Private host rejected keymap");
  std::cout << "ready" << std::endl;
  std::string line;
  while (std::getline(std::cin, line)) {
    auto action = nlohmann::json::parse(line);
    uint32_t time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    if (action.contains("key")) {
      uint32_t key = action.at("key"), pressed = action.at("state");
      zwp_virtual_keyboard_v1_key(keyboard, time, key, pressed);
      xkb_state_update_key(state, key + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
      zwp_virtual_keyboard_v1_modifiers(
          keyboard, xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
          xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
          xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED),
          xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE));
    } else if (action.contains("move")) {
      auto p = action.at("move"), extent = action.at("extent");
      zwlr_virtual_pointer_v1_motion_absolute(pointer, time, p.at(0), p.at(1),
                                              extent.at(0), extent.at(1));
      zwlr_virtual_pointer_v1_frame(pointer);
    } else if (action.contains("scroll")) {
      auto steps = action.at("scroll");
      zwlr_virtual_pointer_v1_axis_source(pointer,
                                          WL_POINTER_AXIS_SOURCE_WHEEL);
      for (int i = 0; i < 2; ++i) {
        int step = steps.at(i);
        if (step)
          zwlr_virtual_pointer_v1_axis_discrete(
              pointer, time,
              i == 0 ? WL_POINTER_AXIS_HORIZONTAL_SCROLL
                     : WL_POINTER_AXIS_VERTICAL_SCROLL,
              wl_fixed_from_double(step * 10.), step);
      }
      zwlr_virtual_pointer_v1_frame(pointer);
    } else if (action.contains("button")) {
      zwlr_virtual_pointer_v1_button(pointer, time, action.at("button"),
                                     action.at("state"));
      zwlr_virtual_pointer_v1_frame(pointer);
    } else
      throw std::runtime_error("Unknown fixture action");
    if (wl_display_roundtrip(display) < 0)
      throw std::runtime_error("Private input connection failed");
    std::cout << "ok" << std::endl;
  }
  zwp_virtual_keyboard_v1_destroy(keyboard);
  zwlr_virtual_pointer_v1_destroy(pointer);
  xkb_state_unref(state);
  xkb_keymap_unref(map);
  xkb_context_unref(context);
  zwp_virtual_keyboard_manager_v1_destroy(key_manager);
  zwlr_virtual_pointer_manager_v1_destroy(pointer_manager);
  wl_seat_destroy(seat);
  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
