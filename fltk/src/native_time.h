// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_types.h"
struct Date {
  std::chrono::year_month_day day;
  bool valid = false;
  Date() = default;
  Date(int y, int m, int d)
      : day(std::chrono::year(y), std::chrono::month(m), std::chrono::day(d)),
        valid(day.ok()) {}
  Date addDays(Int64 n) const {
    Date out;
    out.day = std::chrono::year_month_day(std::chrono::sys_days(day) +
                                          std::chrono::days(n));
    out.valid = valid && out.day.ok();
    return out;
  }
  bool isValid() const { return valid; }
  static Date fromString(const Text &s, TextOption) {
    auto b = s.toStdString();
    std::smatch m;
    static const std::regex pattern("([0-9]{4})-([0-9]{2})-([0-9]{2})");
    if (!std::regex_match(b, m, pattern))
      return {};
    return Date(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]));
  }
};
struct Time {
  Int64 ms = 0;
  Time(int h, int m) : ms((h * 60 + m) * 60000) {}
  Time addMSecs(Int64 v) const {
    Time t = *this;
    t.ms = (ms + v) % 86400000;
    return t;
  }
};
struct DateTime {
  Date day;
  Time time{0, 0};
  bool valid = false;
  DateTime() = default;
  DateTime(Date d, Time t, TextOption) : day(d), time(t), valid(d.valid) {}
  bool isValid() const { return valid; }
  static DateTime fromString(const Text &s, TextOption) {
    if (s.size() > 128)
      return {};
    auto b = s.toStdString();
    std::smatch m;
    static const std::regex pattern(
        "([0-9]{4}-[0-9]{2}-[0-9]{2})T([0-9]{2}):([0-9]{2}):([0-"
        "9]{2})(\\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})?");
    if (!std::regex_match(b, m, pattern))
      return {};
    DateTime out;
    out.day = Date::fromString(Text(m[1].str()), ISODate);
    out.valid = out.day.valid && std::stoi(m[2]) < 24 && std::stoi(m[3]) < 60 &&
                std::stoi(m[4]) < 60;
    if (m[6].matched && m[6].str() != "Z") {
      auto zone = m[6].str();
      out.valid = out.valid && std::stoi(zone.substr(1, 2)) < 24 &&
                  std::stoi(zone.substr(4, 2)) < 60;
    }
    out.time.ms =
        ((std::stoi(m[2]) * 60 + std::stoi(m[3])) * 60 + std::stoi(m[4])) *
        1000LL;
    if (m[5].matched) {
      auto fraction = m[5].str().substr(1, 3);
      fraction.append(3 - fraction.size(), '0');
      out.time.ms += std::stoi(fraction);
    }
    return out;
  }
  Text toString(TextOption) const {
    if (!valid)
      return {};
    char b[64];
    snprintf(b, sizeof b, "%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ",
             int(day.day.year()), unsigned(day.day.month()),
             unsigned(day.day.day()), (long long)(time.ms / 3600000),
             (long long)(time.ms / 60000 % 60),
             (long long)(time.ms / 1000 % 60), (long long)(time.ms % 1000));
    return Text(b);
  }
};
