// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_story.h"
#include "native_words_page.h"
#include "native_words_format.h"
namespace DomeOffice {
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
void walk(XmlElement root, const std::function<void(XmlElement)> &fn) {
  fn(root);
  for (auto node = root.firstChild(); !node.isNull(); node = node.nextSibling())
    if (node.isElement())
      walk(node, fn);
}
bool same(XmlNode a, XmlNode b) {
  if (a.isNull() || b.isNull())
    return a.isNull() == b.isNull();
  if (a.isElement() != b.isElement() || a.namespaceURI() != b.namespaceURI() ||
      a.localName() != b.localName())
    return false;
  if (!a.isElement())
    return a.nodeValue() == b.nodeValue();
  auto attributes = [](XmlElement node) {
    std::map<std::pair<Text, Text>, Text> result;
    for (const auto &a : node.attributes())
      result[{a.uri, a.name.mid(a.name.indexOf(':') + 1)}] = a.value;
    return result;
  };
  if (attributes(a) != attributes(b))
    return false;
  auto ac = a.firstChild(), bc = b.firstChild();
  while (!ac.isNull() && !bc.isNull()) {
    if (!same(ac, bc))
      return false;
    ac = ac.nextSibling();
    bc = bc.nextSibling();
  }
  return ac.isNull() && bc.isNull();
}
size_t rank(const TextList &order, const Text &name) {
  return size_t(std::find(order.begin(), order.end(), name) - order.begin());
}
Text story_tag(const WordsStoryAddress &address) {
  if ((address.kind != TextPageStory::Header &&
       address.kind != TextPageStory::Footer) ||
      address.variant < TextPageStory::Default ||
      address.variant > TextPageStory::Left ||
      address.region < WordsStoryAddress::Direct ||
      address.region > WordsStoryAddress::Right)
    fail("Invalid header/footer address");
  return Text(address.kind == TextPageStory::Header ? "header" : "footer") +
         (address.variant == TextPageStory::First  ? "-first"
          : address.variant == TextPageStory::Left ? "-left"
                                                   : "");
}
Text region_tag(const WordsStoryAddress &address) {
  return address.region == WordsStoryAddress::Left     ? "region-left"
         : address.region == WordsStoryAddress::Center ? "region-center"
         : address.region == WordsStoryAddress::Right  ? "region-right"
                                                       : "";
}
bool region(XmlNode node) {
  return node.namespaceURI() == STYLE &&
         TextList{"region-left", "region-center", "region-right"}.contains(
             node.localName());
}
XmlElement master(const XmlDocument &styles, const Text &name) {
  for (auto node : styles.elementsByTagNameNS(STYLE, "master-page"))
    if (node.attributeNS(STYLE, "name") == name)
      return node;
  fail("The selected page style no longer exists");
}
OdfPackage clone(const OdfPackage &p) {
  auto copy = p;
  copy.xml = read_document_xml(p.xml.toByteArray());
  return copy;
}
void replace_group(XmlDocument &to, const char *name, XmlElement source) {
  auto root = to.documentElement();
  auto old = child(root, OFFICE, name);
  if (same(old, source))
    return;
  if (!source.isNull()) {
    auto copy = to.importNode(source, true);
    auto before = old;
    if (before.isNull()) {
      const TextList order{"font-face-decls", "styles", "automatic-styles",
                           "master-styles", "body"};
      for (auto node : children(root, OFFICE))
        if (rank(order, node.localName()) > rank(order, name)) {
          before = node;
          break;
        }
    }
    root.insertBefore(copy, before);
  }
  if (!old.isNull())
    root.removeChild(old);
}
// Names created while the body is projected away must not collide on merge.
// Existing names remain stable; new identity references move with their source.
void reserve_ids(XmlElement edited, XmlElement original,
                 const OdfPackage &package) {
  std::set<Text> existing, used;
  walk(original, [&](XmlElement n) {
    if (n.hasAttributeNS(XML, "id"))
      existing.insert(n.attributeNS(XML, "id"));
  });
  auto reserve = [&](XmlElement n) {
    if (n.hasAttributeNS(XML, "id"))
      used.insert(n.attributeNS(XML, "id"));
  };
  walk(package.xml.documentElement(), reserve);
  walk(read_document_xml(package.entry("styles.xml")).documentElement(),
       reserve);
  std::set<Text> all = used;
  walk(edited, [&](XmlElement n) {
    if (n.hasAttributeNS(XML, "id"))
      all.insert(n.attributeNS(XML, "id"));
  });
  std::map<Text, Text> renamed;
  size_t counter = 1;
  walk(edited, [&](XmlElement n) {
    auto id = n.attributeNS(XML, "id");
    if (id.empty() || existing.contains(id) || !used.contains(id))
      return;
    if (renamed.contains(id))
      fail("Duplicate generated header/footer identity");
    Text name;
    do {
      name = "WordsStory" + Text::number(counter++);
    } while (all.contains(name));
    all.insert(name);
    renamed[id] = name;
    n.setAttributeNS(XML, "xml:id", name);
  });
  walk(edited, [&](XmlElement n) {
    for (auto a : n.attributes()) {
      auto local = a.name.mid(a.name.indexOf(':') + 1);
      bool fragment =
          a.uri == XLINK && local == "href" && a.value.startsWith('#');
      bool reference =
          fragment ||
          (a.uri == TEXT &&
           (local == "continue-list" || local == "ref-name")) ||
          (a.uri == DRAW && (local == "start-shape" || local == "end-shape"));
      auto key = fragment ? a.value.mid(1) : a.value;
      if (reference && renamed.contains(key))
        n.setAttributeNS(a.uri, a.name,
                         (fragment ? Text("#") : Text{}) + renamed.at(key));
    }
  });
  std::set<Text> old_tables, tables;
  auto collect_tables = [&](XmlElement root, std::set<Text> &names) {
    walk(root, [&](XmlElement n) {
      if (n.namespaceURI() == TABLE && n.localName() == "table")
        names.insert(n.attributeNS(TABLE, "name"));
    });
  };
  collect_tables(original, old_tables);
  collect_tables(package.xml.documentElement(), tables);
  collect_tables(
      read_document_xml(package.entry("styles.xml")).documentElement(), tables);
  auto reserved_tables = tables;
  collect_tables(edited, reserved_tables);
  walk(edited, [&](XmlElement n) {
    if (n.namespaceURI() != TABLE || n.localName() != "table")
      return;
    auto name = n.attributeNS(TABLE, "name");
    if (name.empty() || old_tables.contains(name) || !tables.contains(name))
      return;
    walk(n, [&](XmlElement cell) {
      if (cell.hasAttributeNS(TABLE, "formula"))
        fail("New header/footer table formulas require reference adjustment "
             "before merging");
    });
    Text next;
    do {
      next = "WordsStoryTable" + Text::number(counter++);
    } while (reserved_tables.contains(next));
    reserved_tables.insert(next);
    n.setAttributeNS(TABLE, "table:name", next);
  });
}
} // namespace
struct WordsStoryEdit::Impl {
  OdfPackage original, seed, projection;
  WordsStoryAddress address;
  TextPageStyle editing_page;
  bool displayed = true;
  Impl(const OdfPackage &p, WordsStoryAddress where)
      : original(clone(p)), seed(clone(p)), projection(clone(p)),
        address(std::move(where)) {
    if (p.kind() != Kind::Words || p.read_only())
      fail("This document cannot be edited");
    auto tag = story_tag(address);
    (void)words_page_style(seed, address.master);
    if (address.master.empty()) {
      format_words_page(seed, {}, {}, true);
      address.master = read_text_flow(seed).first_master;
    }
    auto styles = read_document_xml(seed.entry("styles.xml"));
    auto page = master(styles, address.master);
    auto source = child(page, STYLE, tag);
    auto display = source.attributeNS(STYLE, "display", "true");
    displayed = source.isNull() || (display != "false" && display != "0");
    auto container = address.region == WordsStoryAddress::Direct
                         ? source
                         : child(source, STYLE, region_tag(address));
    auto probe = clone(seed);
    auto probe_styles = read_document_xml(seed.entry("styles.xml"));
    if (child(master(probe_styles, address.master), STYLE, tag).isNull())
      master(probe_styles, address.master)
          .appendChild(probe_styles.createElementNS(STYLE, "style:" + tag));
    probe.set_entry("styles.xml", probe_styles.toByteArray(), "text/xml");
    editing_page = read_text_flow(probe).masters.at(address.master);
    const auto &stories = address.kind == TextPageStory::Header
                              ? editing_page.stories->headers
                              : editing_page.stories->footers;
    auto story = stories[address.variant];
    editing_page.left +=
        story->margins[3] + story->box.padding[3] + story->box.borders[3].width;
    editing_page.right +=
        story->margins[1] + story->box.padding[1] + story->box.borders[1].width;
    editing_page.stories.reset();
    if (address.region == WordsStoryAddress::Left && container.isNull()) {
      bool regional = false;
      for (auto n : children(source, STYLE))
        regional = regional || region(n);
      if (!regional)
        container = source;
    }
    if (address.region == WordsStoryAddress::Direct)
      for (auto n : children(source, STYLE))
        if (region(n))
          fail("Choose the left, center or right region of this header/footer");
    replace_group(projection.xml, "automatic-styles",
                  child(styles.documentElement(), OFFICE, "automatic-styles"));
    replace_group(projection.xml, "font-face-decls",
                  child(styles.documentElement(), OFFICE, "font-face-decls"));
    auto body = projection.body();
    while (!body.firstChild().isNull())
      body.removeChild(body.firstChild());
    for (auto node = container.firstChild(); !node.isNull();
         node = node.nextSibling())
      if (address.region != WordsStoryAddress::Direct || !region(node))
        body.appendChild(projection.xml.importNode(node, true));
    walk(styles.documentElement(), [&](XmlElement n) {
      if (n.namespaceURI() == STYLE &&
          TextList{"header", "header-first", "header-left", "footer",
                   "footer-first", "footer-left"}
              .contains(n.localName()))
        n.setAttributeNS(STYLE, "style:display", "false");
    });
    projection.set_entry("styles.xml", styles.toByteArray(), "text/xml");
    (void)read_text_flow(projection);
  }
};
WordsStoryEdit::WordsStoryEdit(const OdfPackage &p, WordsStoryAddress address)
    : impl_(std::make_shared<Impl>(p, std::move(address))) {}
OdfPackage WordsStoryEdit::document() const { return clone(impl_->projection); }
TextFlow WordsStoryEdit::editing_flow(const OdfPackage &package) const {
  auto flow = read_text_flow(package);
  for (auto &[name, page] : flow.masters) {
    page = impl_->editing_page;
    page.name = name;
    page.next = name;
  }
  auto align = [&](TextParagraph &p) {
    if (impl_->address.region != WordsStoryAddress::Direct)
      p.alignment = impl_->address.region == WordsStoryAddress::Left
                        ? TextParagraph::Left
                    : impl_->address.region == WordsStoryAddress::Center
                        ? TextParagraph::Center
                        : TextParagraph::Right;
  };
  for (auto &p : flow.paragraphs)
    align(p);
  for (auto &b : flow.blocks)
    if (b.kind == TextBlock::Paragraph && b.formatted) {
      auto p = std::make_shared<TextParagraph>(*b.formatted);
      align(*p);
      b.formatted = std::move(p);
    }
  return flow;
}
bool WordsStoryEdit::displayed() const { return impl_->displayed; }
const WordsStoryAddress &WordsStoryEdit::address() const {
  return impl_->address;
}
OdfPackage WordsStoryEdit::finish(const OdfPackage &edited,
                                  bool displayed) const {
  const auto &i = *impl_;
  if (edited.kind() != Kind::Words || edited.read_only())
    fail("Invalid header/footer editing document");
  (void)read_text_flow(edited);
  for (const auto &part : edited.part_changes_from(i.projection))
    if (part.name != "styles.xml" && part.name != "META-INF/manifest.xml")
      fail("Header/footer editing cannot replace unrelated archive parts");
  auto before = read_document_xml(i.projection.entry("styles.xml"));
  auto external = read_document_xml(edited.entry("styles.xml"));
  // Page geometry and other stories are not editable through this projection.
  for (const auto &name : {"automatic-styles", "master-styles"})
    if (!same(child(before.documentElement(), OFFICE, name),
              child(external.documentElement(), OFFICE, name)))
      fail("Page layout must be edited outside the header/footer text editor");
  auto result = clone(i.seed);
  auto styles = read_document_xml(result.entry("styles.xml"));
  // A named style used by the projected-away body is still in use.
  for (auto node : children(child(before.documentElement(), OFFICE, "styles"),
                            STYLE, "style")) {
    const auto name = node.attributeNS(STYLE, "name"),
               family = node.attributeNS(STYLE, "family");
    bool retained = false;
    for (auto next :
         children(child(external.documentElement(), OFFICE, "styles"), STYLE,
                  "style"))
      retained = retained || (next.attributeNS(STYLE, "name") == name &&
                              next.attributeNS(STYLE, "family") == family);
    if (!retained) {
      auto check = clone(i.seed);
      if (family == "paragraph")
        delete_words_paragraph_style(check, name);
      else if (family == "text")
        delete_words_character_style(check, name);
      else
        fail("Delete shared styles outside the header/footer editor");
    }
  }
  for (const auto &name : {"styles", "font-face-decls"})
    if (!same(child(before.documentElement(), OFFICE, name),
              child(external.documentElement(), OFFICE, name)))
      replace_group(styles, name,
                    child(external.documentElement(), OFFICE, name));
  // Direct formatting and new tables/list styles live in the projected content
  // automatic group; in the actual document they belong to styles.xml.
  replace_group(
      styles, "automatic-styles",
      child(edited.xml.documentElement(), OFFICE, "automatic-styles"));
  if (!same(
          child(i.projection.xml.documentElement(), OFFICE, "font-face-decls"),
          child(edited.xml.documentElement(), OFFICE, "font-face-decls")))
    fail("Font declarations must be edited outside the header/footer text "
         "editor");
  auto page = master(styles, i.address.master);
  auto source = child(page, STYLE, story_tag(i.address));
  auto old_styles = read_document_xml(i.seed.entry("styles.xml"));
  auto old_source =
      child(master(old_styles, i.address.master), STYLE, story_tag(i.address));
  auto old_container = i.address.region == WordsStoryAddress::Direct
                           ? old_source
                           : child(old_source, STYLE, region_tag(i.address));
  if (i.address.region == WordsStoryAddress::Left && old_container.isNull()) {
    bool regional = false;
    for (auto n : children(old_source, STYLE))
      regional = regional || region(n);
    if (!regional)
      old_container = old_source;
  }
  auto replacement = styles.importNode(edited.body(), true).toElement();
  reserve_ids(replacement, old_container, i.seed);
  if (source.isNull()) {
    if (i.address.variant != TextPageStory::Default) {
      const Text tag =
          i.address.kind == TextPageStory::Header ? "header" : "footer";
      if (child(page, STYLE, tag).isNull()) {
        auto base = styles.createElementNS(STYLE, "style:" + tag);
        base.setAttributeNS(STYLE, "style:display", "false");
        XmlNode before_footer;
        for (auto n : children(page, STYLE))
          if (n.localName().startsWith("footer")) {
            before_footer = n;
            break;
          }
        page.insertBefore(base, before_footer);
      }
    }
    source = styles.createElementNS(STYLE, "style:" + story_tag(i.address));
    // Preserve the schema's default, left, first ordering, headers before
    // footers.
    const TextList order{"header", "header-left", "header-first",
                         "footer", "footer-left", "footer-first"};
    XmlNode next;
    for (auto node : children(page, STYLE))
      if (rank(order, node.localName()) > rank(order, story_tag(i.address))) {
        next = node;
        break;
      }
    page.insertBefore(source, next);
  }
  if (displayed != i.displayed || old_source.isNull())
    source.setAttributeNS(STYLE, "style:display", displayed ? "true" : "false");
  auto target = source;
  if (i.address.region != WordsStoryAddress::Direct) {
    bool has_direct = false;
    for (auto n = source.firstChild(); !n.isNull(); n = n.nextSibling())
      has_direct = has_direct || (n.isElement() && !region(n));
    // Converting direct content to regions retains it as the left region.
    if (has_direct) {
      auto left = child(source, STYLE, "region-left");
      if (!left.isNull())
        fail("Mixed direct and regional header/footer content requires repair");
      left = styles.createElementNS(STYLE, "style:region-left");
      for (auto n = source.firstChild(); !n.isNull();) {
        auto next = n.nextSibling();
        if (!region(n))
          left.appendChild(n);
        n = next;
      }
      source.insertBefore(left, source.firstChild());
    }
    target = child(source, STYLE, region_tag(i.address));
    if (target.isNull()) {
      target = styles.createElementNS(STYLE, "style:" + region_tag(i.address));
      const TextList order{"region-left", "region-center", "region-right"};
      XmlNode next;
      for (auto node : children(source, STYLE))
        if (rank(order, node.localName()) >
            rank(order, region_tag(i.address))) {
          next = node;
          break;
        }
      source.insertBefore(target, next);
    }
  }
  // Preserve the selected story/region's attributes and all other regions.
  XmlNode first_region;
  for (auto node = target.firstChild(); !node.isNull();) {
    auto next = node.nextSibling();
    if (i.address.region == WordsStoryAddress::Direct && region(node)) {
      if (first_region.isNull())
        first_region = node;
    } else
      target.removeChild(node);
    node = next;
  }
  for (auto node = replacement.firstChild(); !node.isNull();) {
    auto next = node.nextSibling();
    target.insertBefore(node, first_region);
    node = next;
  }
  if (i.address.variant == TextPageStory::First) {
    auto version =
        result.xml.documentElement().attributeNS(OFFICE, "version", "1.3");
    if (TextList{"1.0", "1.1", "1.2"}.contains(version)) {
      result.xml.documentElement().setAttributeNS(OFFICE, "office:version",
                                                  "1.3");
      styles.documentElement().setAttributeNS(OFFICE, "office:version", "1.3");
    }
  }
  if (same(styles.documentElement(), old_styles.documentElement()) &&
      i.seed.entry("styles.xml") == i.original.entry("styles.xml"))
    return clone(i.original);
  result.set_entry("styles.xml", styles.toByteArray(), "text/xml");
  (void)read_text_flow(result);
  return result;
}
} // namespace DomeOffice
