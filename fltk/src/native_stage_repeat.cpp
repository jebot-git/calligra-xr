// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_repeat.h"
#include "native_stage_names.h"
#include "native_stage_identity.h"
#include "native_stage_timing.h"
#include "native_document_scope.h"
#include <functional>
namespace DomeOffice {
namespace {
const Text XML = "http://www.w3.org/XML/1998/namespace";
const Text SMIL = "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
[[noreturn]] void fail(const char *s) { throw std::runtime_error(s); }
Text local(const XmlNode::Attribute &a) {
  return a.name.mid(a.name.indexOf(':') + 1);
}
void walk(XmlElement root, const std::function<void(XmlElement)> &visit) {
  size_t count = 0;
  auto next = [&](auto &&self, XmlElement n, int depth) -> void {
    if (++count > 1000000 || depth > 256)
      fail("Repeated cell identity traversal exceeds the limit");
    if (n != root && native_embedded_document_boundary(n) &&
        n.namespaceURI() == OFFICE)
      return;
    visit(n);
    if (native_embedded_document_boundary(n))
      return;
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        self(self, c, depth + 1);
  };
  next(next, root, 0);
}
using Targets = std::map<Text, Text>;
XmlElement clone(XmlElement original, NativeStageNames &names) {
  // Inline opaque documents have an independent reference grammar. Until
  // scoped cloning supports their XML identities, never duplicate those IDs
  // into an invalid outer XML document. External shared object parts are not
  // duplicated and retain their original bytes.
  size_t inspected = 0;
  auto opaque_ids = [&](auto &&self, XmlElement n, bool opaque,
                        int depth) -> void {
    if (++inspected > 1000000 || depth > 256)
      fail("Repeated embedded identity inspection exceeds the limit");
    if (opaque && n.hasAttributeNS(XML, "id"))
      fail("Repeated inline embedded XML identities need scoped cloning");
    opaque = opaque || native_embedded_document_boundary(n);
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        self(self, c, opaque, depth + 1);
  };
  opaque_ids(opaque_ids, original, false, 0);
  auto result = original.cloneNode(true);
  Targets ids, objects, anchors, sections, tables;
  auto identity = [&](Targets &map, const Text &value) -> Text {
    auto [at, inserted] = map.emplace(value, Text());
    if (inserted)
      at->second = names.take();
    return at->second;
  };
  walk(result, [&](XmlElement n) {
    for (const auto &a : n.attributes()) {
      const auto key = local(a);
      Targets *map = nullptr;
      if ((a.uri == XML ||
           (a.uri == DRAW && native_stage_object_identity(n))) &&
          key == "id")
        map = &ids;
      else if (a.uri == DRAW && key == "name" &&
               native_stage_object_identity(n))
        map = &objects;
      else if (a.uri == TEXT && key == "name" && native_stage_text_anchor(n))
        map = &anchors;
      else if (a.uri == TEXT && key == "name" && native_stage_section_target(n))
        map = &sections;
      else if (a.uri == TABLE && key == "name" && n.localName() == "table")
        map = &tables;
      if (map)
        n.setAttributeNS(a.uri, a.name, identity(*map, a.value));
    }
  });
  auto any = [&](const Text &old) -> std::optional<Text> {
    if (auto i = ids.find(old); i != ids.end())
      return i->second;
    std::optional<Text> out;
    for (const auto *map : {&objects, &anchors, &sections, &tables})
      if (auto i = map->find(old); i != map->end()) {
        if (out && *out != i->second)
          fail("Ambiguous repeated cell named reference");
        out = i->second;
      }
    return out;
  };
  NativeStageTimingBudget timing_budget;
  walk(result, [&](XmlElement n) {
    for (const auto &a : n.attributes()) {
      const auto key = local(a);
      std::optional<Text> value;
      const Targets *map = nullptr;
      if (a.uri == DRAW &&
          TextList{"start-shape", "end-shape", "caption-id"}.contains(key))
        map = &ids;
      else if (a.uri == DRAW &&
               TextList{"copy-of", "chain-next-name"}.contains(key)) {
        map = objects.contains(a.value) ? &objects : &ids;
      } else if (a.uri == TEXT && key == "continue-list")
        map = &ids;
      else if (a.uri == TEXT && key == "section-name" &&
               native_stage_local_section_source(n))
        map = &sections;
      else if (a.uri == SMIL && key == "targetElement")
        map = &ids;
      else if (a.uri == TEXT && key == "ref-name")
        value = any(a.value);
      else if (a.uri == XLINK && key == "href" && a.value.startsWith('#')) {
        if (auto target =
                any(Url::fromPercentEncoding(a.value.mid(1).toUtf8())))
          value = "#" + *target;
      } else if (a.uri == SMIL && (key == "begin" || key == "end")) {
        auto refs = native_stage_timing_references(
            a.value, [&](const Text &id) { return ids.contains(id); },
            &timing_budget);
        Text out;
        size_t at = 0;
        for (const auto &ref : refs) {
          out += a.value.mid(int(at), int(ref.begin - at));
          out += ids.at(ref.id);
          at = ref.begin + ref.size;
        }
        value = out + a.value.mid(int(at));
      } else if (a.uri == DRAW && key == "nav-order") {
        TextList out;
        std::istringstream stream(a.value.toStdString());
        for (std::string token; stream >> token;) {
          if (out.size() >= 100000)
            fail("Repeated cell navigation exceeds the limit");
          out.push_back(ids.contains(Text(token)) ? ids.at(Text(token))
                                                  : Text(token));
        }
        value = out.join(' ');
      }
      if (map)
        if (auto at = map->find(a.value); at != map->end())
          value = at->second;
      if (value)
        n.setAttributeNS(a.uri, a.name, *value);
    }
  });
  return result;
}
struct Expansion {
  NativeStageNames names;
  size_t elements = 0, instances = 0, created_nodes = 0, created_bytes = 0;
  std::pair<size_t, size_t> cost(XmlElement root) {
    size_t nodes = 0, bytes = 0;
    auto scan = [&](auto &&self, XmlElement n, int depth) -> void {
      if (++nodes > 1000000 || depth > 256)
        fail("Repeated source subtree exceeds the limit");
      size_t value = 256 + n.nodeValue().size() * 4;
      for (const auto &a : n.attributes())
        value += 512 + (a.name.size() + a.uri.size() + a.value.size()) * 4;
      if (value > 128 * 1024 * 1024 - bytes)
        fail("Repeated source subtree exceeds the byte limit");
      bytes += value;
      for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
        self(self, c, depth + 1);
    };
    scan(scan, root, 0);
    return {nodes, bytes};
  }
  explicit Expansion(const OdfPackage &p) : names(p) {}
  void expand(XmlElement n, int depth = 0) {
    if (++elements > 1000000 || depth > 256)
      fail("Repeated text table expansion exceeds the limit");
    if (native_embedded_document_boundary(n))
      return;
    Text attribute;
    if (n.namespaceURI() == TABLE && n.localName() == "table-row")
      attribute = "number-rows-repeated";
    if (n.namespaceURI() == TABLE && (n.localName() == "table-cell" ||
                                      n.localName() == "covered-table-cell"))
      attribute = "number-columns-repeated";
    if (!attribute.empty() && n.hasAttributeNS(TABLE, attribute)) {
      bool ok = false;
      auto count = n.attributeNS(TABLE, attribute).toLongLong(&ok);
      if (!ok || count < 1 || count > 100000 ||
          size_t(count) > 100000 - instances)
        fail("Repeated text table instances exceed the limit");
      instances += size_t(count);
      n.removeAttributeNS(TABLE, attribute);
      const auto [nodes, bytes] = cost(n);
      const size_t copies = size_t(count - 1);
      if (copies > (1000000 - created_nodes) / nodes ||
          copies > (128 * 1024 * 1024 - created_bytes) / bytes)
        fail("Repeated table clone allocation exceeds the limit");
      created_nodes += copies * nodes;
      created_bytes += copies * bytes;
      auto after = n;
      for (long long i = 1; i < count; ++i) {
        auto copy = clone(n, names);
        n.parentNode().insertAfter(copy, after);
        after = copy;
      }
    }
    // Visit each inserted sibling from its parent's iteration, so nested tables
    // are expanded in every explicit row/cell instance exactly once.
    for (auto c = n.firstChild(); !c.isNull(); c = c.nextSibling())
      if (c.isElement())
        expand(c, depth + 1);
  }
};
} // namespace
NativeStageFragment materialize_stage_repeated_cells(const Bytes &wire) {
  auto fragment = decode_stage_fragment(wire);
  OdfPackage p(Kind::Stage);
  p.load_bytes(fragment.archive);
  Expansion(p).expand(p.body());
  p.validate_archive_limits();
  fragment.archive = p.archive_bytes();
  // Canonicalization rechecks selected paths, local dependencies and derives
  // the expanded text fallback; element ranks above cell content stay stable.
  return decode_stage_fragment(encode_stage_fragment(fragment));
}
} // namespace DomeOffice
