// SPDX-License-Identifier: GPL-2.0-or-later
// Independent, deliberately uncooperative peer, supervised by the private
// Wayland acceptance test. No production clipboard implementation is reused.
#include "ext-data-control-client.h"
#include <wayland-client.h>
#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
struct Peer {
  wl_display *display;
  wl_registry *registry;
  wl_seat *seat = nullptr;
  ext_data_control_manager_v1 *manager = nullptr;
  ext_data_control_device_v1 *device = nullptr;
  ext_data_control_source_v1 *source = nullptr;
  ext_data_control_offer_v1 *offer = nullptr;
  std::vector<ext_data_control_offer_v1 *> offers;
  std::vector<int> held;
  std::string mode;
  bool cancelled = false;
  ~Peer() {
    for (int fd : held)
      close(fd);
    for (auto *o : offers)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(o));
    if (source)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(source));
    if (device)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(device));
    if (manager)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(manager));
    if (seat)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(seat));
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
  }
  Peer() {
    display = wl_display_connect(nullptr);
    assert(display);
    registry = wl_display_get_registry(display);
    static const wl_registry_listener registry_listener{
        [](void *data, wl_registry *registry, uint32_t id, const char *name,
           uint32_t) {
          auto &p = *static_cast<Peer *>(data);
          if (!strcmp(name, "wl_seat") && !p.seat)
            p.seat = static_cast<wl_seat *>(
                wl_registry_bind(registry, id, &wl_seat_interface, 1));
          if (!strcmp(name, "ext_data_control_manager_v1"))
            p.manager =
                static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(
                    registry, id, &ext_data_control_manager_v1_interface, 1));
        },
        [](void *, wl_registry *, uint32_t) {}};
    wl_registry_add_listener(registry, &registry_listener, this);
    assert(wl_display_roundtrip(display) >= 0 && seat && manager);
    device = ext_data_control_manager_v1_get_data_device(manager, seat);
    static const ext_data_control_device_v1_listener device_listener{
        [](void *data, ext_data_control_device_v1 *,
           ext_data_control_offer_v1 *offer) {
          auto &p = *static_cast<Peer *>(data);
          p.offers.push_back(offer);
          static const ext_data_control_offer_v1_listener listener{
              [](void *, ext_data_control_offer_v1 *, const char *) {}};
          ext_data_control_offer_v1_add_listener(offer, &listener, &p);
        },
        [](void *data, ext_data_control_device_v1 *,
           ext_data_control_offer_v1 *offer) {
          static_cast<Peer *>(data)->offer = offer;
        },
        [](void *data, ext_data_control_device_v1 *) {
          static_cast<Peer *>(data)->cancelled = true;
        },
        [](void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *) {
        }};
    ext_data_control_device_v1_add_listener(device, &device_listener, this);
    assert(wl_display_roundtrip(display) >= 0);
  }
  void own() {
    source = ext_data_control_manager_v1_create_data_source(manager);
    static const ext_data_control_source_v1_listener listener{
        [](void *data, ext_data_control_source_v1 *, const char *, int fd) {
          auto &p = *static_cast<Peer *>(data);
          if (p.mode == "stall-source") {
            p.held.push_back(fd);
            return;
          }
          auto flags = fcntl(fd, F_GETFL);
          assert(flags >= 0);
          assert(fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0);
          std::array<char, 65536> chunk{};
          size_t left = 64 * 1024 * 1024 + 1;
          while (left) {
            auto n = write(fd, chunk.data(), std::min(left, chunk.size()));
            if (n < 0 && errno == EINTR)
              continue;
            if (n <= 0)
              break;
            left -= size_t(n);
          }
          close(fd);
        },
        [](void *data, ext_data_control_source_v1 *) {
          static_cast<Peer *>(data)->cancelled = true;
        }};
    ext_data_control_source_v1_add_listener(source, &listener, this);
    ext_data_control_source_v1_offer(source,
                                     "application/x-calligra-clipboard-test");
    ext_data_control_device_v1_set_selection(device, source);
    assert(wl_display_roundtrip(display) >= 0);
  }
  void hold_receive(const char *type) {
    assert(offer);
    int pipe_fds[2];
    assert(pipe2(pipe_fds, O_CLOEXEC) == 0);
    held.push_back(pipe_fds[0]);
    ext_data_control_offer_v1_receive(offer, type, pipe_fds[1]);
    close(pipe_fds[1]);
    assert(wl_display_roundtrip(display) >= 0);
  }
};
} // namespace
int main(int argc, char **argv) {
  std::signal(SIGPIPE, SIG_IGN);
  assert(argc >= 2);
  Peer peer;
  peer.mode = argv[1];
  if (peer.mode == "hold-receive") {
    assert(argc == 3);
    peer.hold_receive(argv[2]);
  } else {
    assert(peer.mode == "stall-source" || peer.mode == "oversize-source");
    peer.own();
  }
  std::cout << "READY\n" << std::flush;
  while (!peer.cancelled && wl_display_dispatch(peer.display) >= 0) {
  }
}
