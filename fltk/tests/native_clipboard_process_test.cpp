// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_clipboard_process.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <thread>
#include <unistd.h>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  assert(caught);
}
NativeClipboardProcess::Result finish(NativeClipboardProcess &p) {
  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < until) {
    if (auto r = p.poll())
      return std::move(*r);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::abort();
}
int helper(int argc, char **argv) {
  std::string mode = argv[1];
  if (mode == "--offer") {
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(std::cin)), {});
    auto bundle = decode_clipboard_bundle(bytes);
    if (bundle[0].mime == "application/x-error") {
      std::cerr << "Publication failed";
      return 1;
    }
    std::cout << "READY\n" << std::flush;
    return 0;
  }
  assert(mode == "--receive-preferred" && argc > 2);
  std::string type = argv[2];
  if (type == "application/x-none")
    return 3;
  if (type == "application/x-slow") {
    sleep(30);
    return 0;
  }
  if (type == "application/x-malformed") {
    std::cout << "broken";
    return 0;
  }
  assert(type == "application/x-binary");
  auto result = encode_clipboard_bundle({{type, {0, 1, 2, 255, 0}}});
  std::cout.write(reinterpret_cast<const char *>(result.data()), result.size());
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1)
    return helper(argc, argv);
  auto self = std::filesystem::read_symlink("/proc/self/exe").string();
  NativeClipboardProcess p(self);
  assert(!p.busy() && !p.poll());
  p.offer({{"application/x-good", {0, 1, 2}}});
  assert(p.busy());
  assert(finish(p).success && !p.busy());
  p.offer({{"application/x-error", {}}});
  auto error = finish(p);
  assert(!error.success &&
         error.error.find("Publication failed") != std::string::npos);
  p.receive({"application/x-binary", "text/plain"});
  auto r = finish(p);
  assert(r.success &&
         r.formats == NativeClipboardBundle(
                          {{"application/x-binary", {0, 1, 2, 255, 0}}}));
  p.receive({"application/x-none"});
  assert(!finish(p).success);
  p.receive({"application/x-malformed"});
  assert(!finish(p).success);
  p.receive({"application/x-slow"});
  p.cancel();
  assert(!p.busy() && !p.poll());
  p.receive({"application/x-slow"});
  p.receive({"application/x-binary"});
  assert(finish(p).success);
  rejects([&] { p.receive({}); });
  rejects([&] { p.receive({"not-a-mime"}); });
  NativeClipboardProcess missing("/no-such-directory/calligra-clipboard");
  rejects([&] { missing.receive({"text/plain"}); });
  assert(!missing.busy());
  std::cout << "PASS asynchronous offer acknowledgement, exact binary receive, "
               "failure, cancellation and replacement\n";
}
