// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include "workbook.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
void version(const Text &path, const Text &want) {
  auto parts = read_document_archive(path);
  for (auto name : {"content.xml", "styles.xml"}) {
    auto doc = read_document_xml(parts.value(name));
    assert(doc.documentElement().attributeNS(OFFICE, "version") == want);
  }
  assert(read_document_xml(parts.value("META-INF/manifest.xml"))
             .documentElement()
             .attributeNS(MANIFEST, "version") == "1.2");
}
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  Text directory =
      argc == 3 && Text(argv[1]) == "--documents" ? Text(argv[2]) : temp.path();
  std::filesystem::create_directories(directory.toStdString());
  Text w = directory + "/new-words.odt", s = directory + "/new-sheets.ods",
       p = directory + "/new-stage.odp";
  TextDocument words;
  words.set_text("ODF 1.2 native Words Český Ω");
  words.save(w);
  version(w, "1.2");
  Workbook sheets;
  sheets.set(0, 1, 1, "=SUM({1;2;3})");
  sheets.save(s);
  version(s, "1.2");
  Presentation stage;
  stage.set_box_text(0, 0, "ODF 1.2 native Stage");
  stage.save(p);
  version(p, "1.2");
  // A required-version repair is confined to serialized manifest bytes.
  OdfPackage legacy(Kind::Words);
  auto old_manifest = legacy.entry("META-INF/manifest.xml");
  auto missing_version = read_document_xml(old_manifest);
  missing_version.documentElement().removeAttributeNS(MANIFEST, "version");
  auto raw = missing_version.toByteArray();
  legacy.replay_part_changes(
      {{"META-INF/manifest.xml", true, true, 0, old_manifest.size(), raw.size(),
        old_manifest, raw}},
      true);
  auto legacy_path = directory + "/manifest-repaired.odt";
  legacy.save(legacy_path);
  version(legacy_path, "1.2");
  assert(legacy.entry("META-INF/manifest.xml") == raw);
  // Editing newer sources retains their declared document versions.
  for (auto newer : {Text("1.3"), Text("1.4")})
    for (auto pair : {std::pair{Kind::Words, w}, std::pair{Kind::Sheets, s},
                      std::pair{Kind::Stage, p}}) {
      OdfPackage package(pair.first);
      package.load(pair.second);
      package.xml.documentElement().setAttributeNS(OFFICE, "office:version",
                                                   newer);
      auto styles = read_document_xml(package.entry("styles.xml"));
      styles.documentElement().setAttributeNS(OFFICE, "office:version", newer);
      package.set_entry("styles.xml", styles.toByteArray(), "text/xml");
      auto source = directory + "/source-" + newer + suffix(pair.first) + "." +
                    suffix(pair.first),
           saved = directory + "/saved-" + newer + suffix(pair.first) + "." +
                   suffix(pair.first);
      package.save(source);
      if (pair.first == Kind::Words) {
        TextDocument d;
        d.load(source);
        d.replace(d.text().size(), 0, " preserved");
        d.save(saved);
      } else if (pair.first == Kind::Sheets) {
        Workbook b;
        b.load(source);
        b.set(0, 2, 1, "=7+5");
        b.save(saved);
      } else {
        Presentation d;
        d.load(source);
        d.move_shape(0, 0, {1, 2});
        d.save(saved);
      }
      version(saved, newer);
    }
  std::cout << "PASS new ODT/ODS/ODP default to1.2; edits retain1.3/1.4 source "
               "versions\n";
}
