// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_environment.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <sys/utsname.h>
namespace DomeOffice {
namespace {
std::optional<uint64_t> kib_field(const char *path, const char *field) {
  std::ifstream file(path);
  std::string line;
  size_t bytes = 0;
  while (std::getline(file, line)) {
    bytes += line.size();
    if (bytes > 65536)
      return {};
    std::istringstream data(line);
    std::string key, unit;
    uint64_t value;
    if (!(data >> key) || key != field)
      continue;
    if (!(data >> value >> unit) || unit != "kB" || value > UINT64_MAX / 1024)
      return {};
    return value * 1024;
  }
  return {};
}
NativeValue memory_sum(const char *left, const char *right) {
  auto a = kib_field("/proc/meminfo", left),
       b = kib_field("/proc/meminfo", right);
  if (!a || !b || *a > UINT64_MAX - *b)
    return NativeValue::error("#VALUE!", true);
  return NativeValue::numeric(double(*a + *b));
}
} // namespace
NativeValue native_host_information(const Text &category) {
  using V = NativeValue;
  if (category == "directory") {
    std::error_code error;
    auto path = std::filesystem::current_path(error);
    if (error)
      return V::error("#VALUE!", true);
    auto value = path.string();
    if (!value.ends_with('/'))
      value += '/';
    return V(Text(value));
  }
  if (category == "system" || category == "osversion") {
    struct utsname os;
    if (uname(&os) != 0)
      return V::error("#VALUE!", true);
    return V(category == "system" ? Text(os.sysname)
                                  : Text(os.sysname) + " " + Text(os.release) +
                                        " (" + Text(os.machine) + ")");
  }
  if (category == "release")
    return V(Text(CALLIGRA_NATIVE_VERSION));
  if (category == "totmem")
    return memory_sum("MemTotal:", "SwapTotal:");
  if (category == "memavail")
    return memory_sum("MemAvailable:", "SwapFree:");
  if (category == "memused") {
    // Resident anonymous process data (heap/stack/BSS), as measured by Linux.
    // This excludes shared executable/file-backed pages. OS values are
    // estimates and are not promises that a future allocation will succeed.
    auto bytes = kib_field("/proc/self/status", "RssAnon:");
    return bytes ? V::numeric(double(*bytes)) : V::error("#VALUE!", true);
  }
  return V::error("#VALUE!");
}
} // namespace DomeOffice
