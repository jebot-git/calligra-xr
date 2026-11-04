// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_clipboard_process.h"
#include <array>
#include <algorithm>
#include <utility>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

namespace DomeOffice {
namespace {
using Clock = std::chrono::steady_clock;
[[noreturn]] void fail(const std::string &s) { throw std::runtime_error(s); }
struct Fd {
  int value = -1;
  Fd() = default;
  explicit Fd(int n) : value(n) {}
  ~Fd() {
    if (value >= 0)
      close(value);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  Fd(Fd &&other) noexcept : value(std::exchange(other.value, -1)) {}
  Fd &operator=(Fd &&other) noexcept {
    if (value >= 0)
      close(value);
    value = std::exchange(other.value, -1);
    return *this;
  }
};
struct Pipe {
  Fd read, write;
  Pipe() {
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) < 0)
      fail("Cannot create clipboard process pipe");
    read = Fd(fd[0]);
    write = Fd(fd[1]);
    if (fcntl(read.value, F_SETFL, O_NONBLOCK) < 0)
      fail("Cannot prepare clipboard process pipe");
  }
};
bool drain(Fd &fd, std::vector<uint8_t> &out, size_t limit) {
  if (fd.value < 0)
    return true;
  std::array<uint8_t, 65536> data;
  // Bounded work per UI tick, even when a peer continuously writes.
  for (int i = 0; i < 16; ++i) {
    auto n = read(fd.value, data.data(), data.size());
    if (!n) {
      fd = Fd();
      return true;
    }
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && errno == EAGAIN)
      return false;
    if (n < 0)
      fail("Cannot read clipboard process result");
    if (size_t(n) > limit - out.size())
      fail("Clipboard process result exceeds the limit");
    out.insert(out.end(), data.begin(), data.begin() + n);
  }
  return false;
}
} // namespace

struct NativeClipboardProcess::Impl {
  std::string helper;
  pid_t pending = -1;
  bool offering = false, reaped = false;
  int status = 0;
  Fd output, errors;
  std::vector<uint8_t> bytes, diagnostics;
  std::vector<pid_t> owners;
  Clock::time_point deadline;
  explicit Impl(std::string h) : helper(std::move(h)) {}
  void reap_owners() {
    std::erase_if(owners, [](pid_t p) {
      int s;
      auto n = waitpid(p, &s, WNOHANG);
      return n == p || (n < 0 && errno == ECHILD);
    });
  }
  void cancel() {
    if (pending > 0 && !reaped) {
      kill(pending, SIGKILL);
      while (waitpid(pending, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
    pending = -1;
    reaped = false;
    output = Fd();
    errors = Fd();
    bytes.clear();
    diagnostics.clear();
    reap_owners();
  }
  void start(std::vector<std::string> args, std::vector<uint8_t> input,
             bool offer) {
    cancel();
    Fd source(memfd_create("calligra-clipboard", MFD_CLOEXEC));
    if (source.value < 0)
      fail("Cannot prepare clipboard input");
    size_t at = 0;
    while (at < input.size()) {
      auto n = write(source.value, input.data() + at, input.size() - at);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        fail("Cannot prepare clipboard input");
      at += size_t(n);
    }
    if (lseek(source.value, 0, SEEK_SET) < 0)
      fail("Cannot rewind clipboard input");
    Pipe out, err;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int ec = posix_spawn_file_actions_init(&actions);
    if (ec)
      fail("Cannot prepare clipboard child process");
    struct Actions {
      posix_spawn_file_actions_t *p;
      ~Actions() { posix_spawn_file_actions_destroy(p); }
    } cleanup{&actions};
    ec = posix_spawnattr_init(&attributes);
    if (ec)
      fail("Cannot prepare clipboard child attributes");
    struct Attributes {
      posix_spawnattr_t *p;
      ~Attributes() { posix_spawnattr_destroy(p); }
    } cleanup_attr{&attributes};
    auto check = [](int result) {
      if (result)
        fail("Cannot configure clipboard child process");
    };
    check(
        posix_spawn_file_actions_adddup2(&actions, source.value, STDIN_FILENO));
    check(posix_spawn_file_actions_adddup2(&actions, out.write.value,
                                           STDOUT_FILENO));
    check(posix_spawn_file_actions_adddup2(&actions, err.write.value,
                                           STDERR_FILENO));
    sigset_t mask;
    sigemptyset(&mask);
    check(posix_spawnattr_setsigmask(&attributes, &mask));
    check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK));
    args.insert(args.begin(), helper);
    std::vector<char *> argv;
    for (auto &a : args)
      argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t child = -1;
    ec = posix_spawn(&child, helper.c_str(), &actions, &attributes, argv.data(),
                     environ);
    if (ec)
      fail("Cannot start native clipboard helper: " +
           std::string(std::strerror(ec)));
    pending = child;
    offering = offer;
    reaped = false;
    status = 0;
    output = std::move(out.read);
    errors = std::move(err.read);
    deadline = Clock::now() + std::chrono::seconds(45);
  }
  std::optional<Result> poll() {
    reap_owners();
    if (pending < 0)
      return {};
    try {
      bool done = drain(output, bytes, clipboard_wire_limit);
      bool errors_done = drain(errors, diagnostics, 16384);
      if (offering &&
          bytes == std::vector<uint8_t>{'R', 'E', 'A', 'D', 'Y', '\n'}) {
        owners.push_back(pending);
        pending = -1;
        output = Fd();
        errors = Fd();
        bytes.clear();
        diagnostics.clear();
        return Result{true, {}, {}};
      }
      if (!reaped) {
        auto n = waitpid(pending, &status, WNOHANG);
        if (n == pending)
          reaped = true;
        else if (n < 0 && errno != EINTR)
          fail("Cannot collect clipboard child process");
      }
      if (reaped && done && errors_done) {
        bool success =
            WIFEXITED(status) && WEXITSTATUS(status) == 0 && !offering;
        if (success) {
          auto formats = decode_clipboard_bundle(bytes);
          cancel();
          return Result{true, std::move(formats), {}};
        }
        std::string error(diagnostics.begin(), diagnostics.end());
        if (error.empty())
          error = WIFEXITED(status) && WEXITSTATUS(status) == 3
                      ? "The clipboard has no supported objects or text"
                      : "Native clipboard transfer failed";
        cancel();
        return Result{false, {}, std::move(error)};
      }
      if (Clock::now() > deadline)
        fail("Native clipboard transfer timed out");
    } catch (const std::exception &e) {
      auto error = std::string(e.what());
      cancel();
      return Result{false, {}, std::move(error)};
    }
    return {};
  }
};
NativeClipboardProcess::NativeClipboardProcess(std::string h)
    : impl_(std::make_unique<Impl>(std::move(h))) {}
NativeClipboardProcess::~NativeClipboardProcess() { impl_->cancel(); }
void NativeClipboardProcess::offer(const NativeClipboardBundle &bundle) {
  auto bytes = encode_clipboard_bundle(bundle);
  impl_->start({"--offer"}, std::move(bytes), true);
}
void NativeClipboardProcess::receive(
    const std::vector<std::string> &preferred) {
  if (preferred.empty() || preferred.size() > clipboard_format_limit)
    fail("Invalid clipboard format preference");
  std::vector<std::string> args{"--receive-preferred"};
  for (const auto &s : preferred) {
    if (!native_clipboard_mime(s))
      fail("Invalid clipboard format preference");
    args.push_back(s);
  }
  impl_->start(std::move(args), {}, false);
}
std::optional<NativeClipboardProcess::Result> NativeClipboardProcess::poll() {
  return impl_->poll();
}
void NativeClipboardProcess::cancel() { impl_->cancel(); }
bool NativeClipboardProcess::busy() const { return impl_->pending > 0; }
std::string native_clipboard_helper() {
  std::error_code ec;
  auto executable = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec)
    fail("Cannot locate the native clipboard helper");
  auto directory = executable.parent_path();
  for (const auto &p :
       {directory / "calligra-wayland-clipboard",
        directory / "../libexec/calligra-fltk/calligra-wayland-clipboard"})
    if (access(p.c_str(), X_OK) == 0)
      return p.lexically_normal().string();
  fail("The native clipboard helper is not installed beside this build");
}
} // namespace DomeOffice
