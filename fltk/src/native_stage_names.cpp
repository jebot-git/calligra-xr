// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_names.h"
#include <functional>
namespace DomeOffice {
namespace {
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
void walk(XmlElement root, const std::function<void(XmlElement)> &visit) {
  size_t count = 0;
  auto next = [&](auto &&self, XmlElement n, int depth) -> void {
    if (++count > 1000000 || depth > 256)
      fail("Clipboard name inventory exceeds the limit");
    if (n.isElement())
      visit(n);
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      self(self, c, depth + 1);
  };
  next(next, root, 0);
}
} // namespace
struct NativeStageNames::State {
  std::set<Text> reserved;
  size_t counter = 0;
  void scan(const Text &value) {
    // Generated names have one ASCII stem. Reserve occurrences inside
    // unresolved URI/timing/vendor references too; adding a definition must not
    // heal one.
    int at = 0;
    while ((at = value.indexOf("NativePaste", at)) >= 0) {
      auto end = size_t(at) + 11;
      while (end < value.size() && value[end] >= U'0' && value[end] <= U'9')
        ++end;
      if (end > size_t(at) + 11) {
        if (reserved.size() >= 1000000)
          fail("Clipboard destination name inventory exceeds the limit");
        reserved.insert(value.mid(at, int(end) - at));
      }
      at = int(end);
    }
  }
  void reserve(const OdfPackage &package) {
    auto collect = [&](XmlElement node) {
      for (const auto &a : node.attributes()) {
        scan(a.value);
        std::optional<Text> decoded;
        try {
          decoded = Url::fromPercentEncoding(a.value.toUtf8());
        } catch (
            const std::exception &) { /* An opaque value need not be a URI. */
        }
        if (decoded)
          scan(*decoded);
      }
    };
    walk(package.xml.documentElement(), collect);
    if (!package.entry("styles.xml").empty())
      walk(read_document_xml(package.entry("styles.xml")).documentElement(),
           collect);
    if (!package.entry("META-INF/manifest.xml").empty())
      walk(read_document_xml(package.entry("META-INF/manifest.xml"))
               .documentElement(),
           collect);
    for (const auto &path : package.entry_names()) {
      scan(path);
      const auto bytes = package.entry_data(path);
      if (!bytes ||
          TextList{"content.xml", "styles.xml", "META-INF/manifest.xml"}
              .contains(path))
        continue;
      // Include opaque parts conservatively, and decode valid XML resources so
      // percent escapes/entities in nested unresolved URIs reserve names too.
      for (size_t at = 0;
           (at = bytes->find("NativePaste", at)) != Bytes::npos;) {
        size_t end = at + 11;
        while (end < bytes->size() && (*bytes)[end] >= '0' &&
               (*bytes)[end] <= '9')
          ++end;
        scan(Text::fromUtf8(bytes->substr(at, end - at)));
        at = end;
      }
      auto first = bytes->find_first_not_of(" \r\n\t");
      if (first != Bytes::npos && (*bytes)[first] == '<') {
        std::optional<XmlDocument> parsed;
        try {
          parsed = read_document_xml(*bytes);
        } catch (const std::exception
                     &) { /* Opaque, invalid XML has no parsed references. */
        }
        if (parsed)
          walk(parsed->documentElement(), collect);
      }
    }
  }
  Text take() {
    for (;;) {
      if (++counter > 2000001)
        fail("Clipboard imported name count exceeds the limit");
      auto name = "NativePaste" + Text::number(counter);
      if (reserved.insert(name).second)
        return name;
    }
  }
};
NativeStageNames::NativeStageNames(const OdfPackage &p)
    : state_(std::make_unique<State>()) {
  state_->reserve(p);
}
NativeStageNames::~NativeStageNames() = default;
void NativeStageNames::reserve(const OdfPackage &p) { state_->reserve(p); }
Text NativeStageNames::take() { return state_->take(); }
} // namespace DomeOffice
