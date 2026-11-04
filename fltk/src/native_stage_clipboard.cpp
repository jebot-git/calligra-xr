// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_stage_clipboard.h"
#include "native_stage_identity.h"
namespace DomeOffice {
namespace {
const std::string odp = "application/vnd.oasis.opendocument.presentation";
const std::string utf8 = "text/plain;charset=utf-8", plain = "text/plain";
std::vector<uint8_t> binary(const Bytes &b) {
  return {reinterpret_cast<const uint8_t *>(b.data()),
          reinterpret_cast<const uint8_t *>(b.data()) + b.size()};
}
Bytes fragment_from_odp(const Bytes &archive) {
  OdfPackage package(Kind::Stage);
  package.load_bytes(archive);
  auto pages = children(package.body(), DRAW, "page");
  if (pages.size() != 1)
    throw std::runtime_error("An object clipboard requires exactly one slide");
  std::vector<NativeStageObjectPath> paths;
  uint32_t index = 0;
  for (auto c = pages[0].firstChild(); !c.isNull(); c = c.nextSibling())
    if (c.isElement()) {
      if (native_stage_object_identity(c.toElement()))
        paths.push_back({index});
      ++index;
    }
  return encode_stage_fragment({archive, {}, std::move(paths)});
}
} // namespace
std::vector<std::string> stage_clipboard_preferences() {
  return {native_stage_fragment_mime, odp, utf8, plain};
}
NativeClipboardBundle
stage_clipboard_formats(const NativeStageFragment &fragment) {
  // Canonicalize untrusted metadata once and derive every fallback from it.
  auto wire = encode_stage_fragment(fragment);
  auto canonical = decode_stage_fragment(wire);
  NativeClipboardBundle formats{{native_stage_fragment_mime, binary(wire)},
                                {odp, binary(canonical.archive)},
                                {utf8, binary(canonical.text.toUtf8())},
                                {plain, binary(canonical.text.toUtf8())}};
  (void)encode_clipboard_bundle(formats);
  return formats;
}
Bytes stage_clipboard_fragment(const NativeClipboardFormat &format) {
  if (format.data.size() > clipboard_payload_limit)
    throw std::runtime_error("Clipboard data exceeds the transfer limit");
  Bytes bytes(reinterpret_cast<const char *>(format.data.data()),
              format.data.size());
  if (format.mime == native_stage_fragment_mime)
    return encode_stage_fragment(decode_stage_fragment(bytes));
  if (format.mime == odp)
    return fragment_from_odp(bytes);
  if (format.mime != utf8 && format.mime != plain)
    throw std::runtime_error("Unsupported object clipboard format");
  auto text = Text::fromUtf8(bytes);
  if (text.toUtf8() != bytes || text.contains(Char(0)))
    throw std::runtime_error("Clipboard text must be valid UTF-8 without NUL");
  if (text.empty())
    return {};
  text.replace("\r\n", "\n");
  text.replace("\r", "\n");
  for (auto c : text)
    if ((c < 0x20 && c != '\n' && c != '\t') || c == 0xfffe || c == 0xffff)
      throw std::runtime_error(
          "Clipboard text contains an invalid XML character");
  // Apply the existing native frame/paragraph limits before constructing XML.
  // A small newline/tab payload can otherwise expand into millions of nodes.
  if (text.size() > 4000000)
    throw std::runtime_error("Text frame exceeds native layout text limit");
  size_t paragraphs = 1, length = 0, nodes = 4;
  bool run = false, spaces = false;
  for (auto c : text) {
    if (c == '\n') {
      if (++paragraphs > 100000)
        throw std::runtime_error("Too many text paragraphs");
      nodes += 1 + size_t(run);
      run = spaces = false;
      length = 0;
    } else {
      if (++length > 1000000)
        throw std::runtime_error("Paragraph exceeds layout text limit");
      if (c == ' ' || c == '\t') {
        nodes += size_t(run) + size_t(c != ' ' || !spaces);
        run = false;
        spaces = c == ' ';
      } else {
        run = true;
        spaces = false;
      }
    }
    if (nodes + size_t(run) > 1000000)
      throw std::runtime_error(
          "Clipboard text XML exceeds the native node limit");
  }
  OdfPackage package(Kind::Stage);
  auto doc = package.xml;
  auto page = doc.createElementNS(DRAW, "draw:page");
  page.setAttributeNS(DRAW, "draw:master-page-name", "Default");
  package.body().appendChild(page);
  auto frame = doc.createElementNS(DRAW, "draw:frame");
  page.appendChild(frame);
  frame.setAttributeNS(DRAW, "draw:name", "Pasted text");
  frame.setAttributeNS(SVG, "svg:x", "20pt");
  frame.setAttributeNS(SVG, "svg:y", "20pt");
  frame.setAttributeNS(SVG, "svg:width", "360pt");
  frame.setAttributeNS(SVG, "svg:height", "160pt");
  auto box = doc.createElementNS(DRAW, "draw:text-box");
  frame.appendChild(box);
  for (const auto &line : text.split('\n')) {
    auto p = doc.createElementNS(TEXT, "text:p");
    box.appendChild(p);
    set_plain_text(p, line);
  }
  return fragment_from_odp(package.archive_bytes());
}
} // namespace DomeOffice
