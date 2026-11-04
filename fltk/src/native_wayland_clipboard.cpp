// SPDX-License-Identifier: GPL-2.0-or-later
// Foreground clipboard owner/receiver. The caller retains the process only
// until READY, then the owner survives its window and exits on replacement.
#include "native_clipboard_bundle.h"
#include "ext-data-control-client.h"
#include <wayland-client.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace {
using namespace DomeOffice;
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr auto transfer_timeout = std::chrono::seconds(5);
constexpr size_t transfer_limit = 16;
struct Fd {
  int value = -1;
  explicit Fd(int fd = -1) : value(fd) {}
  Fd(Fd &&other) noexcept : value(std::exchange(other.value, -1)) {}
  Fd &operator=(Fd &&other) noexcept {
    if (value >= 0)
      close(value);
    value = std::exchange(other.value, -1);
    return *this;
  }
  ~Fd() {
    if (value >= 0)
      close(value);
  }
};
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
int milliseconds(Deadline end) {
  auto left =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now())
          .count();
  return int(std::clamp<int64_t>(left, 0, 5000));
}
void nonblocking(int fd) {
  auto flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    fail("Cannot prepare clipboard file descriptor");
}
short wait_fd(int fd, short events, Deadline end) {
  for (;;) {
    if (Clock::now() >= end)
      fail("Clipboard transfer timed out");
    pollfd p{fd, events, 0};
    auto result = poll(&p, 1, milliseconds(end));
    if (result < 0 && errno == EINTR)
      continue;
    if (result < 0 || (p.revents & POLLNVAL))
      fail("Clipboard file descriptor failed");
    if (result)
      return p.revents;
  }
}
std::vector<uint8_t> read_input() {
  nonblocking(STDIN_FILENO);
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 65536> buffer;
  auto end = Clock::now() + transfer_timeout;
  for (;;) {
    wait_fd(STDIN_FILENO, POLLIN, end);
    auto n = read(STDIN_FILENO, buffer.data(), buffer.size());
    if (n == 0)
      return bytes;
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n < 0)
      fail("Cannot read clipboard bundle");
    if (size_t(n) > clipboard_wire_limit - bytes.size())
      fail("Clipboard bundle exceeds the transfer limit");
    bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + n);
  }
}
void output(std::span<const uint8_t> data) {
  nonblocking(STDOUT_FILENO);
  auto end = Clock::now() + transfer_timeout;
  while (!data.empty()) {
    wait_fd(STDOUT_FILENO, POLLOUT, end);
    auto n = write(STDOUT_FILENO, data.data(), data.size());
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n <= 0)
      fail("Cannot write clipboard result");
    data = data.subspan(size_t(n));
  }
}
void output(std::string_view text) {
  output(
      std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}

struct Client {
  struct Seat {
    Client *client;
    uint32_t global;
    wl_seat *proxy;
    std::string name;
    bool alive = true;
  };
  struct Offer {
    Client *client;
    ext_data_control_offer_v1 *proxy;
    std::vector<std::string> types;
  };
  struct Transfer {
    Fd fd;
    size_t format, offset = 0;
    Deadline end;
  };
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  ext_data_control_manager_v1 *manager = nullptr;
  ext_data_control_device_v1 *device = nullptr;
  ext_data_control_source_v1 *source = nullptr;
  wl_callback *sync = nullptr;
  std::vector<std::unique_ptr<Seat>> seats;
  std::map<ext_data_control_offer_v1 *, std::unique_ptr<Offer>> offers;
  Offer *selection = nullptr;
  Seat *seat = nullptr;
  NativeClipboardBundle bundle;
  std::vector<Transfer> transfers;
  std::exception_ptr error;
  bool finished = false, cancelled = false;
  ~Client() {
    // The connection is closing; destroy local proxies without marshalling
    // destructor requests into an already failed or congested connection.
    if (sync)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(sync));
    for (auto &[proxy, offer] : offers)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(proxy));
    if (source)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(source));
    if (device)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(device));
    if (manager)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(manager));
    for (auto &s : seats)
      wl_proxy_destroy(reinterpret_cast<wl_proxy *>(s->proxy));
    if (registry)
      wl_registry_destroy(registry);
    if (display)
      wl_display_disconnect(display);
  }
  template <class F> void callback(F action) noexcept {
    try {
      action();
    } catch (...) {
      if (!error)
        error = std::current_exception();
    }
  }
  void check() {
    if (error)
      std::rethrow_exception(error);
    if (finished)
      fail("Wayland clipboard device is no longer available");
    if (wl_display_get_error(display))
      fail("Wayland clipboard connection failed");
  }
  void drop(Offer *offer) {
    if (!offer)
      return;
    auto proxy = offer->proxy;
    if (selection == offer)
      selection = nullptr;
    ext_data_control_offer_v1_destroy(proxy);
    offers.erase(proxy);
  }
  void dispatch(Deadline end, int input = -1) {
    check();
    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) < 0)
        fail("Wayland clipboard connection failed");
      check();
    }
    auto flushed = wl_display_flush(display);
    if (flushed < 0 && errno != EAGAIN) {
      wl_display_cancel_read(display);
      fail("Cannot flush Wayland clipboard requests");
    }
    std::vector<pollfd> fds{{wl_display_get_fd(display),
                             short(POLLIN | (flushed < 0 ? POLLOUT : 0)), 0}};
    if (input >= 0)
      fds.push_back({input, POLLIN, 0});
    auto earliest = end;
    for (auto &t : transfers) {
      earliest = std::min(earliest, t.end);
      fds.push_back({t.fd.value, POLLOUT, 0});
    }
    auto result = poll(fds.data(), fds.size(), milliseconds(earliest));
    auto saved_errno = errno;
    if (result > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(display) < 0)
        fail("Wayland clipboard connection failed");
    } else
      wl_display_cancel_read(display);
    if (result < 0 && saved_errno != EINTR)
      fail("Wayland clipboard polling failed");
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
      fail("Wayland clipboard connection closed");
    // Service transfers before callbacks can append another one. A client
    // which stops reading cannot block a later, independent paste request.
    size_t first = input >= 0 ? 2 : 1;
    for (size_t i = transfers.size(); i-- > 0;) {
      auto &t = transfers[i];
      bool remove = Clock::now() >= t.end ||
                    (fds[first + i].revents & (POLLERR | POLLHUP | POLLNVAL));
      if (!remove && (fds[first + i].revents & POLLOUT)) {
        const auto &data = bundle[t.format].data;
        auto n =
            write(t.fd.value, data.data() + t.offset, data.size() - t.offset);
        if (n > 0)
          t.offset += size_t(n);
        else if (n < 0 && errno != EAGAIN && errno != EINTR)
          remove = true;
        remove = remove || t.offset == data.size();
      }
      if (remove)
        transfers.erase(transfers.begin() + i);
    }
    if (wl_display_dispatch_pending(display) < 0)
      fail("Wayland clipboard connection failed");
    check();
  }
  void roundtrip() {
    sync = wl_display_sync(display);
    if (!sync)
      fail("Cannot synchronize Wayland clipboard");
    static const wl_callback_listener listener{
        [](void *data, wl_callback *callback, uint32_t) {
          auto &c = *static_cast<Client *>(data);
          wl_callback_destroy(callback);
          c.sync = nullptr;
        }};
    wl_callback_add_listener(sync, &listener, this);
    auto end = Clock::now() + transfer_timeout;
    while (sync) {
      if (Clock::now() >= end)
        fail("Wayland clipboard synchronization timed out");
      dispatch(end);
    }
  }
  void connect(const std::string &seat_name) {
    display = wl_display_connect(nullptr);
    if (!display)
      fail("Cannot connect to native Wayland clipboard");
    registry = wl_display_get_registry(display);
    static const wl_registry_listener registry_listener{
        [](void *data, wl_registry *r, uint32_t id, const char *name,
           uint32_t version) {
          auto &c = *static_cast<Client *>(data);
          c.callback([&] {
            if (!std::strcmp(name,
                             ext_data_control_manager_v1_interface.name) &&
                !c.manager)
              c.manager =
                  static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(
                      r, id, &ext_data_control_manager_v1_interface, 1));
            else if (!std::strcmp(name, wl_seat_interface.name)) {
              if (c.seats.size() >= 32)
                fail("Too many Wayland seats");
              auto seat = std::make_unique<Seat>();
              seat->client = &c;
              seat->global = id;
              seat->proxy = static_cast<wl_seat *>(wl_registry_bind(
                  r, id, &wl_seat_interface, std::min(version, 7u)));
              static const wl_seat_listener listener{
                  [](void *, wl_seat *, uint32_t) {},
                  [](void *data, wl_seat *, const char *name) {
                    auto &s = *static_cast<Seat *>(data);
                    s.client->callback([&] { s.name = name; });
                  }};
              c.seats.push_back(std::move(seat));
              wl_seat_add_listener(c.seats.back()->proxy, &listener,
                                   c.seats.back().get());
            }
          });
        },
        [](void *data, wl_registry *, uint32_t id) {
          auto &c = *static_cast<Client *>(data);
          for (auto &s : c.seats)
            if (s->global == id) {
              s->alive = false;
              if (c.seat == s.get())
                c.finished = true;
            }
        }};
    wl_registry_add_listener(registry, &registry_listener, this);
    roundtrip();
    if (!manager)
      fail("Compositor does not expose ext-data-control-v1; rich clipboard is "
           "unavailable");
    roundtrip(); // wl_seat names arrive after registry binding.
    for (auto &s : seats)
      if (s->alive && (seat_name.empty() || s->name == seat_name)) {
        if (seat)
          fail("Multiple Wayland seats; select the clipboard seat explicitly");
        seat = s.get();
      }
    if (!seat)
      fail("Requested Wayland clipboard seat is unavailable");
    device = ext_data_control_manager_v1_get_data_device(manager, seat->proxy);
    static const ext_data_control_device_v1_listener device_listener{
        [](void *data, ext_data_control_device_v1 *,
           ext_data_control_offer_v1 *proxy) {
          auto &c = *static_cast<Client *>(data);
          c.callback([&] {
            if (c.offers.size() >= 64)
              fail("Too many clipboard offers");
            auto offer = std::make_unique<Offer>();
            offer->client = &c;
            offer->proxy = proxy;
            static const ext_data_control_offer_v1_listener listener{
                [](void *data, ext_data_control_offer_v1 *, const char *type) {
                  auto &o = *static_cast<Offer *>(data);
                  o.client->callback([&] {
                    if (o.types.size() >= 256 ||
                        std::strlen(type) > clipboard_mime_limit)
                      fail("Clipboard MIME inventory exceeds its limit");
                    o.types.emplace_back(type);
                  });
                }};
            c.offers.emplace(proxy, std::move(offer));
            ext_data_control_offer_v1_add_listener(proxy, &listener,
                                                   c.offers.at(proxy).get());
          });
        },
        [](void *data, ext_data_control_device_v1 *,
           ext_data_control_offer_v1 *proxy) {
          auto &c = *static_cast<Client *>(data);
          c.callback([&] {
            auto old = c.selection;
            if (old && old->proxy != proxy)
              c.drop(old);
            c.selection = proxy ? c.offers.at(proxy).get() : nullptr;
          });
        },
        [](void *data, ext_data_control_device_v1 *) {
          static_cast<Client *>(data)->finished = true;
        },
        [](void *data, ext_data_control_device_v1 *,
           ext_data_control_offer_v1 *proxy) {
          // This helper never reads or changes the user's primary selection.
          auto &c = *static_cast<Client *>(data);
          c.callback([&] {
            if (proxy && (!c.selection || c.selection->proxy != proxy))
              c.drop(c.offers.at(proxy).get());
          });
        }};
    ext_data_control_device_v1_add_listener(device, &device_listener, this);
    roundtrip();
  }
  void own() {
    source = ext_data_control_manager_v1_create_data_source(manager);
    static const ext_data_control_source_v1_listener listener{
        [](void *data, ext_data_control_source_v1 *, const char *type, int fd) {
          Fd owned(fd);
          auto &c = *static_cast<Client *>(data);
          c.callback([&] {
            auto format =
                std::find_if(c.bundle.begin(), c.bundle.end(),
                             [&](const auto &f) { return f.mime == type; });
            if (format == c.bundle.end() ||
                c.transfers.size() >= transfer_limit || format->data.empty())
              return;
            nonblocking(fd);
            c.transfers.push_back({std::move(owned),
                                   size_t(format - c.bundle.begin()), 0,
                                   Clock::now() + transfer_timeout});
          });
        },
        [](void *data, ext_data_control_source_v1 *) {
          static_cast<Client *>(data)->cancelled = true;
        }};
    ext_data_control_source_v1_add_listener(source, &listener, this);
    for (const auto &format : bundle)
      ext_data_control_source_v1_offer(source, format.mime.c_str());
    ext_data_control_device_v1_set_selection(device, source);
    roundtrip();
    if (cancelled)
      fail("Clipboard ownership was replaced before acknowledgement");
    output("READY\n");
    close(STDOUT_FILENO);
    while (!cancelled || !transfers.empty())
      dispatch(Clock::now() + transfer_timeout);
  }
  int receive(const std::string &type, bool framed = false) {
    if (!selection ||
        std::find(selection->types.begin(), selection->types.end(), type) ==
            selection->types.end())
      return 3;
    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0)
      fail("Cannot create clipboard transfer pipe");
    Fd read_end(pipe_fds[0]), write_end(pipe_fds[1]);
    ext_data_control_offer_v1_receive(selection->proxy, type.c_str(),
                                      write_end.value);
    write_end = Fd();
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 65536> buffer;
    auto end = Clock::now() + transfer_timeout;
    for (;;) {
      if (Clock::now() >= end)
        fail("Clipboard source transfer timed out");
      dispatch(end, read_end.value);
      for (;;) {
        if (Clock::now() >= end)
          fail("Clipboard source transfer timed out");
        auto n = read(read_end.value, buffer.data(), buffer.size());
        if (!n) {
          if (framed)
            output(encode_clipboard_bundle({{type, std::move(bytes)}}));
          else
            output(bytes);
          return 0;
        }
        if (n < 0 && errno == EINTR)
          continue;
        if (n < 0 && errno == EAGAIN)
          break;
        if (n < 0)
          fail("Cannot read clipboard source");
        if (size_t(n) > clipboard_payload_limit - bytes.size())
          fail("Clipboard source exceeds the transfer limit");
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + n);
      }
    }
  }
};
} // namespace

int main(int argc, char **argv) {
  // A receiver can close its pipe at any time. That cancels its transfer,
  // rather than terminating the owner and invalidating all other formats.
  std::signal(SIGPIPE, SIG_IGN);
  try {
    std::string mode, type, seat;
    std::vector<std::string> preferred;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::cout << "Native Wayland clipboard: --offer | --types | --receive "
                   "MIME | --receive-preferred MIME... [--seat NAME]\n"
                   "Offer reads a bounded binary bundle, prints READY and owns "
                   "it until replaced.\n"
                   "Receive prints raw bytes only after a complete bounded "
                   "transfer; absent MIME exits 3.\n";
      return 0;
    }
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--seat" && i + 1 < argc && seat.empty())
        seat = argv[++i];
      else if (arg == "--receive" && mode.empty() && i + 1 < argc) {
        mode = arg;
        type = argv[++i];
      } else if (arg == "--receive-preferred" && mode.empty())
        mode = arg;
      else if (mode == "--receive-preferred" && native_clipboard_mime(arg) &&
               preferred.size() < clipboard_format_limit)
        preferred.push_back(arg);
      else if ((arg == "--offer" || arg == "--types") && mode.empty())
        mode = arg;
      else
        fail("Invalid native clipboard arguments; use --help");
    }
    if (mode.empty() || (mode == "--receive" && !native_clipboard_mime(type)) ||
        (mode == "--receive-preferred" && preferred.empty()))
      fail("Invalid native clipboard operation");
    Client client;
    // Validate before touching the compositor or replacing its selection.
    if (mode == "--offer")
      client.bundle = decode_clipboard_bundle(read_input());
    client.connect(seat);
    if (mode == "--offer")
      client.own();
    else if (mode == "--receive")
      return client.receive(type);
    else if (mode == "--receive-preferred") {
      if (client.selection)
        for (const auto &mime : preferred)
          if (std::find(client.selection->types.begin(),
                        client.selection->types.end(),
                        mime) != client.selection->types.end())
            return client.receive(mime, true);
      return 3;
    } else if (client.selection) {
      std::string types;
      for (const auto &mime : client.selection->types)
        if (native_clipboard_mime(mime))
          types += mime + '\n';
      output(types);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native clipboard: " << error.what() << '\n';
    return 1;
  }
}
