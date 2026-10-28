// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
Bytes content(const Presentation &p) {
  return p.scene(0).editable_sources.at(0).ownerDocument().toByteArray();
}
void position(std::optional<PresentationPosition> actual, int slide, int box) {
  assert(actual && (*actual == PresentationPosition{slide, box}));
}
OdfPackage fixture() {
  OdfPackage p(Kind::Stage);
  p.xml = read_document_xml(R"XML(<office:document-content
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:extra="urn:test:retained"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" office:version="1.3">
 <office:automatic-styles><style:style style:name="Strong" style:family="text"/></office:automatic-styles>
 <office:body><office:presentation><draw:page draw:name="Styled" draw:master-page-name="Default">
 <draw:g draw:transform="translate(5pt 6pt)"><draw:frame svg:width="200pt" svg:height="60pt">
 <draw:text-box><text:p><text:span text:style-name="Strong">Bold</text:span><text:s/>
 <text:a xlink:href="https://example.org/">link</text:a><text:s text:c="2"/>tail</text:p></draw:text-box></draw:frame></draw:g>
 <draw:frame svg:width="200pt" svg:height="60pt"><draw:text-box><text:list><text:list-item><text:p>Nested one</text:p></text:list-item><text:list-item><text:p>Nested two</text:p></text:list-item></text:list></draw:text-box></draw:frame>
 <draw:frame svg:width="20pt" svg:height="20pt"><draw:image xlink:href="Pictures/retained.bin"/></draw:frame>
 <extra:animation extra:opaque="kept"><extra:key>original</extra:key></extra:animation>
 </draw:page><draw:page draw:name="Second" draw:master-page-name="Default">
 <draw:custom-shape svg:width="100pt" svg:height="100pt"><draw:enhanced-geometry svg:viewBox="0 0 100 100" draw:enhanced-path="M 0 0 L 100 0 100 100 Z N" draw:modifiers="20" draw:mirror-horizontal="true"><draw:equation draw:name="kept" draw:formula="$0*2"/></draw:enhanced-geometry><text:p>Second text</text:p></draw:custom-shape>
 </draw:page></office:presentation></office:body></office:document-content>)XML");
  p.set_entry("Pictures/retained.bin", "opaque image payload",
              "application/octet-stream");
  p.set_entry("extra/retained.xml", "<unknown><value>kept</value></unknown>",
              "text/xml");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  TempDir directory;
  auto source = directory.path() + "/source.odp",
       output = directory.path() + "/output.odp";
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--external") {
    Presentation p;
    p.load(Text::fromUtf8(argv[2]));
    const auto before = content(p);
    const auto input = read_document_archive(Text::fromUtf8(argv[2]));
    p.set_box_text(0, 0, "Independent native history — Český 😀");
    p.add_slide("Temporary");
    p.remove_slide(0);
    position(p.undo(), 0, 0);
    position(p.undo(), 0, 0);
    position(p.undo(), 0, 0);
    assert(content(p) == before && !p.modified());
    p.save(output);
    const auto saved = read_document_archive(output);
    for (auto it = input.cbegin(); it != input.cend(); ++it)
      if (it.key() != "content.xml")
        assert(saved.value(it.key()) == it.value());
    assert(saved.value("content.xml") == before);
    std::cout
        << "PASS independent ODF history, exact XML and archive preservation\n";
    return 0;
  }
  auto original = fixture();
  original.save(source);
  if (argc == 3 && Text::fromUtf8(argv[1]) == "--fixture") {
    assert(File::copy(source, Text::fromUtf8(argv[2])));
    return 0;
  }
  Presentation p;
  assert(!p.modified() && !p.can_undo() && !p.can_redo() && !p.undo() &&
         !p.redo());
  rejects([&] { p.remove_slide(0); });
  p.load(source);
  const auto baseline = content(p);
  const auto initial_revision = p.revision();
  assert(!p.modified() && !p.can_undo());
  p.set_box_text(0, 0, "Český 😀\nAdded paragraph");
  const auto text_edit = content(p);
  assert(p.modified() && p.revision() > initial_revision && p.can_undo());
  p.add_slide("Inserted", {0, 1});
  const auto slide_edit = content(p);
  assert(p.count() == 3);
  p.set_box_text(2, 1, "Third slide body");
  const auto body_edit = content(p);
  p.remove_slide(0, 1);
  const auto removed = content(p);
  assert(p.count() == 2 && p.slide(1).boxes[1].text == "Third slide body");
  position(p.undo(), 0, 1);
  assert(content(p) == body_edit && p.count() == 3);
  position(p.undo(), 2, 1);
  assert(content(p) == slide_edit);
  position(p.undo(), 0, 1);
  assert(content(p) == text_edit && p.count() == 2);
  position(p.undo(), 0, 0);
  assert(content(p) == baseline && !p.modified());
  assert(!p.undo());
  position(p.redo(), 0, 0);
  assert(content(p) == text_edit);
  position(p.redo(), 2, 0);
  assert(content(p) == slide_edit);
  position(p.redo(), 2, 1);
  assert(content(p) == body_edit);
  position(p.redo(), 0, 0);
  assert(content(p) == removed && !p.can_redo());
  // The current saved point remains identifiable across both directions.
  p.save(output);
  assert(!p.modified());
  position(p.undo(), 0, 1);
  assert(p.modified());
  position(p.redo(), 0, 0);
  assert(!p.modified());
  position(p.undo(), 0, 1);
  p.set_box_text(0, 0, "Branch");
  assert(p.modified() && !p.can_redo());
  position(p.undo(), 0, 0);
  assert(content(p) == body_edit && p.modified());
  p.save(output);
  const auto saved = read_document_archive(output),
             input = read_document_archive(source);
  for (auto entry : {"styles.xml", "Pictures/retained.bin",
                     "extra/retained.xml", "META-INF/manifest.xml"})
    assert(saved.value(entry) == input.value(entry));
  assert(read_document_archive(source).value("content.xml") ==
         original.xml.toByteArray());
  std::cout << "PASS cross-frame/slide undo, exact styled XML, opaque entries, "
               "save points and branching\n";

  p.load(source);
  p.set_box_text(0, 0, "A", true);
  p.set_box_text(0, 0, "AB", true);
  position(p.undo(), 0, 0);
  assert(content(p) == baseline && !p.modified());
  position(p.redo(), 0, 0);
  assert(p.slide(0).boxes[0].text == "AB");
  p.set_box_text(0, 0, "ABC",
                 true); // Undo/redo ends the previous typing group.
  position(p.undo(), 0, 0);
  assert(p.slide(0).boxes[0].text == "AB");
  p.set_box_text(0, 0, "AB"); // A no-op does not discard redo.
  assert(p.can_redo());
  position(p.redo(), 0, 0);
  assert(p.slide(0).boxes[0].text == "ABC");
  p.save(output);
  p.set_box_text(0, 0, "ABCD", true);
  position(p.undo(), 0, 0);
  assert(p.slide(0).boxes[0].text == "ABC" && !p.modified());
  p.set_box_text(0, 0, "Different frame", true);
  p.set_box_text(1, 0, "Different slide", true);
  position(p.undo(), 1, 0);
  assert(p.slide(1).boxes[0].text == "Second text");
  position(p.undo(), 0, 0);
  assert(p.slide(0).boxes[0].text == "ABC");
  p.load(source);
  Presentation canceled;
  const auto canceled_baseline = content(canceled);
  canceled.set_box_text(0, 0, "Temporary", true);
  canceled.set_box_text(0, 0, "Title", true);
  assert(!canceled.modified() && !canceled.can_undo() &&
         content(canceled) == canceled_baseline);
  std::cout << "PASS typing coalescence, no-op/redo, group boundaries and "
               "canceled edits\n";

  p.set_box_text(0, 0, "Retained edit");
  const auto before_failure = content(p);
  const auto revision = p.revision();
  for (auto action : std::vector<std::function<void()>>{
           [&] {
             p.set_box_text(0, 1, "Changed first\nChanged second\nNew third");
           },
           [&] { p.set_box_text(0, 2, "Image cannot become text"); },
           [&] { p.set_box_text(9, 0, "Invalid slide"); },
           [&] { p.set_box_text(0, 9, "Invalid frame"); },
           [&] { p.remove_slide(-1); }, [&] { p.remove_slide(99); },
           [&] { p.load(directory.path() + "/absent.odp"); },
           [&] { p.save(directory.path() + "/absent/subdir.odp"); }}) {
    rejects(action);
    assert(content(p) == before_failure && p.revision() == revision &&
           p.can_undo());
  }
  position(p.undo(), 0, 0);
  assert(content(p) == baseline);
  p.load(source);
  assert(!p.can_undo() && !p.can_redo() && !p.modified());
  original.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                     "text/xml");
  original.save(output);
  p.load(output);
  assert(p.read_only());
  rejects([&] { p.undo(); });
  rejects([&] { p.redo(); });
  rejects([&] { p.add_slide(); });
  rejects([&] { p.remove_slide(0); });
  rejects([&] { p.set_box_text(0, 0, "Forbidden"); });
  assert(!p.can_undo() && !p.modified());
  std::cout << "PASS transactional rejection, history reset and signature "
               "read-only protection\n";

  p.load(source);
  for (int i = 1; i <= 110; ++i)
    p.set_box_text(0, 0, Text::number(i));
  int undos = 0;
  while (p.undo())
    ++undos;
  assert(undos == 100 && p.slide(0).boxes[0].text == "10" && p.modified());
  int redos = 0;
  while (p.redo())
    ++redos;
  assert(redos == 100 && p.slide(0).boxes[0].text == "110");
  // The byte budget must discard old deltas even below the entry-count cap.
  p.load(source);
  for (int i = 0; i < 11; ++i)
    p.set_box_text(0, 0, Text(std::u32string(500000, char32_t(0x1f600 + i))));
  undos = 0;
  while (p.undo())
    ++undos;
  assert(undos > 0 && undos < 11 && p.modified());
  std::cout << "PASS bounded 100-command / 32 MiB delta history (" << undos
            << " large undos)\n";
}
