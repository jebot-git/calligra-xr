// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text XML = "http://www.w3.org/XML/1998/namespace";
template <class F> void rejects(F call) {
  bool failed = false;
  try {
    call();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  p.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:xlink="http://www.w3.org/1999/xlink" office:version="1.3"><office:automatic-styles><style:style style:name="Direct" style:family="paragraph" style:parent-style-name="Body"><style:paragraph-properties fo:margin-left="22pt"/></style:style><style:style style:name="WordsNamed1" style:family="text"><style:text-properties fo:font-weight="700"/></style:style><style:style style:name="Shadow" style:family="paragraph" style:parent-style-name="Body"><style:text-properties fo:font-size="3pt"/></style:style></office:automatic-styles><office:body><office:text><text:p xml:id="first" text:style-name="Direct">Alpha <text:span text:style-name="Emphasis">Český</text:span> <text:a xlink:href="https://example.invalid/" xlink:type="simple">日本語</text:a></text:p><text:p text:style-name="Body">Beta</text:p><table:table table:name="Cells"><table:table-column/><table:table-row><table:table-cell office:value-type="string"><text:p text:style-name="Body">Cell</text:p></table:table-cell></table:table-row></table:table><text:p text:style-name="Shadow">Shadow paragraph</text:p></office:text></office:body></office:document-content>)XML");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:ex="urn:example:opaque" office:version="1.3"><office:styles><style:style style:name="Body" style:family="paragraph" style:default-outline-level="" style:class="text"><style:paragraph-properties fo:margin-left="2pt"/><style:text-properties fo:font-family="Noto Sans" fo:font-size="12pt" style:text-underline-style="wave" style:text-underline-color="#0077aa"/></style:style><style:style style:name="Body" style:family="text"><style:text-properties fo:font-weight="100"/></style:style><style:style style:name="Fancy" style:display-name="Fancy 日本語" style:family="paragraph" style:parent-style-name="Body" style:next-style-name="Body"><style:text-properties fo:font-size="150%"/></style:style><style:style style:name="Heading" style:display-name="Heading 2" style:family="paragraph" style:parent-style-name="Body" style:next-style-name="Body" style:default-outline-level="2"><style:text-properties fo:font-size="22pt" fo:font-weight="700"/></style:style><style:style style:name="Emphasis" style:family="text"><style:text-properties fo:font-style="italic"/></style:style><style:style style:name="Shadow" style:family="paragraph" style:parent-style-name="Body" style:next-style-name="Body"><style:text-properties fo:font-size="24pt"/></style:style></office:styles></office:document-styles>)XML",
      "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
struct Snapshot {
  Bytes content, styles, manifest;
  bool operator==(const Snapshot &) const = default;
};
} // namespace
int main(int argc, char **argv) {
  TempDir temporary;
  std::filesystem::path dir = temporary.path().toStdString();
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    dir = argv[2];
    if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir))
      throw std::runtime_error("Choose an empty artifact directory");
    std::filesystem::create_directories(dir);
  }
  auto source = fixture();
  const auto input = Text((dir / "input.odt").string());
  source.save(input);
  TextDocument book;
  book.load(input);
  const auto original = book.text();
  const TextSelection caret{2, 2};
  auto snapshot = [&](const char *name) {
    auto path = Text((dir / (std::string(name) + ".odt")).string());
    book.save(path);
    auto archive = read_document_archive(path);
    assert(archive.value("Extras/opaque.bin") ==
           source.entry("Extras/opaque.bin"));
    return Snapshot{archive.value("content.xml"), archive.value("styles.xml"),
                    archive.value("META-INF/manifest.xml")};
  };
  auto original_parts = snapshot("original");
  auto styles = words_paragraph_styles(book.style_snapshot());
  assert(styles.size() == 4);
  assert(words_current_paragraph_style(book.style_snapshot(), 0, 0) == "Body");
  auto shadow = size_t(original.indexOf("Shadow paragraph"));
  assert(words_current_paragraph_style(book.style_snapshot(), shadow, shadow) ==
         "Body");
  auto work = book.style_snapshot();
  WordsParagraphStyleChange create;
  create.label = "Notice 日本語 & teal";
  create.parent = "Fancy";
  create.next = ":self";
  create.character.weight = 700;
  create.paragraph.before = 6;
  auto name = define_words_paragraph_style(work, create);
  assert(name == "WordsNamed2");
  auto info = words_paragraph_style_format(work, name);
  assert(info.character.size == 18 && info.character.weight == 700 &&
         info.character.underline == true);
  assert(info.paragraph.before == 6);
  assert(book.commit_styles(work, caret, book.revision()));
  assert(book.modified() && book.text() == original);
  auto created = snapshot("created");
  assert(created.content == original_parts.content &&
         created.styles != original_parts.styles);
  assert(book.undo() == caret && snapshot("created-undone") == original_parts);
  assert(book.redo() == caret && snapshot("created-redone") == created);
  work = book.style_snapshot();
  WordsParagraphStyleChange noop;
  noop.name = name;
  define_words_paragraph_style(work, noop);
  auto revision = book.revision();
  assert(!book.commit_styles(work, caret, revision));
  assert(book.revision() == revision && !book.modified());
  // Global style edits leave body XML alone and preserve the exact original
  // package parts through saved-state transitions and combined undo/redo.
  work = book.style_snapshot();
  WordsParagraphStyleChange update;
  update.name = "Body";
  update.label = "Body renamed";
  update.character.size = 16;
  define_words_paragraph_style(work, update);
  assert(book.commit_styles(work, caret, book.revision()));
  auto edited = snapshot("edited");
  assert(edited.content == created.content);
  assert(edited.styles.contains("style:class=\"text\""));
  assert(edited.styles.contains("150%") &&
         edited.styles.contains("text-underline-style=\"wave\""));
  assert(book.format_info({0, 1}).character.size == 16);
  assert(book.undo() == caret && snapshot("edited-undone") == created);
  assert(book.redo() == caret && snapshot("edited-redone") == edited);
  // Applying named styles drops paragraph-direct overrides, keeps styled
  // inline spans/links and IDs, and changes heading roles only as specified.
  work = book.style_snapshot();
  assert(book.commit_styles(work, caret, book.revision(), Text("Heading")));
  auto heading = snapshot("heading");
  auto pkg = book.style_snapshot();
  auto h = children(pkg.body(), TEXT, "h");
  assert(h.size() == 1 && h[0].attributeNS(TEXT, "outline-level") == "2");
  assert(h[0].attributeNS(XML, "id") == "first");
  assert(pkg.xml.elementsByTagNameNS(TEXT, "a")[0].attributeNS(XLINK, "href") ==
         "https://example.invalid/");
  assert(book.format_info({0, 1}).paragraph.left == 2);
  assert(book.format_info({6, 11}).character.italic == true);
  assert(!book.commit_styles(book.style_snapshot(), caret, book.revision(),
                             Text("Heading")));
  assert(book.commit_styles(book.style_snapshot(), caret, book.revision(),
                            Text{}));
  assert(children(book.style_snapshot().body(), TEXT, "h").empty());
  snapshot("body");
  assert(book.undo() == caret && snapshot("body-undone") == heading);
  assert(book.undo() == caret && snapshot("heading-undone") == edited);
  // Content automatic styles may share an identifier with a common style.
  // Use an unambiguous parent reference rather than applying the wrong style.
  assert(book.commit_styles(book.style_snapshot(), {shadow, shadow},
                            book.revision(), Text("Shadow")));
  assert(book.format_info({shadow, shadow + 1}).character.size == 24);
  assert(words_current_paragraph_style(book.style_snapshot(), shadow, shadow) ==
         "Shadow");
  snapshot("shadow-common");
  // A next-style assignment must update the new paragraph's heading role.
  book.load(input);
  assert(book.commit_styles(book.style_snapshot(), caret, book.revision(),
                            Text("Heading")));
  auto first_end = size_t(book.text().indexOf('\n'));
  book.replace(first_end, 0, "\n");
  pkg = book.style_snapshot();
  assert(children(pkg.body(), TEXT, "h").size() == 1);
  assert(children(pkg.body(), TEXT, "p")[0].attributeNS(TEXT, "style-name") ==
         "Body");
  snapshot("heading-next-body");
  book.load(input);
  work = book.style_snapshot();
  update = {};
  update.name = "Heading";
  update.next = ":self";
  define_words_paragraph_style(work, update);
  assert(book.commit_styles(work, caret, book.revision(), Text("Heading")));
  first_end = size_t(book.text().indexOf('\n'));
  book.replace(first_end, 0, "\n");
  assert(children(book.style_snapshot().body(), TEXT, "h").size() == 2);
  snapshot("heading-next-self");
  // Cycles, used deletion, stale sessions and failed compound changes are
  // atomic; cancellation restores styles/manifest as well as body XML.
  book.load(input);
  work = book.style_snapshot();
  auto pristine_styles = work.entry("styles.xml");
  update = {};
  update.name = "Body";
  update.parent = "Fancy";
  rejects([&] { define_words_paragraph_style(work, update); });
  assert(work.entry("styles.xml") == pristine_styles);
  rejects([&] { delete_words_paragraph_style(work, "Body"); });
  work = book.style_snapshot();
  name = define_words_paragraph_style(work, create);
  assert(book.commit_styles(work, caret, book.revision()));
  auto unused = snapshot("unused-created");
  work = book.style_snapshot();
  assert(delete_words_paragraph_style(work, name));
  assert(book.commit_styles(work, caret, book.revision()));
  snapshot("unused-deleted");
  assert(book.undo() == caret && snapshot("unused-delete-undone") == unused);
  book.load(input);
  work = book.style_snapshot();
  revision = book.revision();
  book.replace(0, 0, "x");
  rejects([&] { book.commit_styles(work, caret, revision, Text("Body")); });
  book.undo();
  assert(snapshot("stale-rejected") == original_parts);
  book.load(input);
  book.begin_edit(caret);
  work = book.style_snapshot();
  name = define_words_paragraph_style(work, create);
  assert(book.commit_styles(work, caret, book.revision(), name));
  work = book.style_snapshot();
  set_plain_text(children(work.body(), TEXT)[0], "lost");
  rejects([&] { book.commit_styles(work, caret, book.revision()); });
  assert(book.end_edit() == caret);
  assert(!book.modified() && snapshot("compound-rejected") == original_parts);
  // Empty and absent ZIP parts differ; replay failures cannot partially
  // update a manifest, and large parts retain compact byte-delta history.
  auto a = fixture(), b = a;
  b.set_entry("Extras/empty.bin", {}, "application/octet-stream");
  auto changes = b.part_changes_from(a);
  assert(changes.size() == 2);
  auto manifest = a.entry("META-INF/manifest.xml");
  auto broken = changes;
  broken.back().before_size += 1;
  rejects([&] { a.replay_part_changes(broken, true); });
  assert(!a.entry_data("Extras/empty.bin") &&
         a.entry("META-INF/manifest.xml") == manifest);
  a.replay_part_changes(changes, true);
  assert(a.entry_data("Extras/empty.bin") &&
         a.entry("Extras/empty.bin").empty());
  a.replay_part_changes(changes, false);
  assert(!a.entry_data("Extras/empty.bin") &&
         a.entry("META-INF/manifest.xml") == manifest);
  a.set_entry("Extras/large.bin", Bytes(std::string(2 * 1024 * 1024, 'x')),
              "application/octet-stream");
  b = a;
  auto data = b.entry("Extras/large.bin");
  data[data.size() / 2] = 'y';
  b.set_entry("Extras/large.bin", data, "application/octet-stream");
  changes = b.part_changes_from(a);
  assert(changes.size() == 1 &&
         changes[0].before.size() + changes[0].after.size() == 2);
  a.replay_part_changes(changes, true);
  assert(a.entry("Extras/large.bin") == data);
  a.replay_part_changes(changes, false);
  assert(a.entry("Extras/large.bin") != data);
  // Optional styles.xml can be absent. Creation supplies a version-matched
  // part and manifest entry; undo must restore absence, not an empty file.
  OdfPackage blank(Kind::Words);
  auto blank_p = blank.xml.createElementNS(TEXT, "text:p");
  set_plain_text(blank_p, "Blank document");
  blank.body().appendChild(blank_p);
  const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
  auto old_styles = blank.entry("styles.xml"),
       old_manifest = blank.entry("META-INF/manifest.xml");
  auto manifest_xml = read_document_xml(old_manifest);
  for (auto entry :
       children(manifest_xml.documentElement(), MANIFEST, "file-entry"))
    if (entry.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest_xml.documentElement().removeChild(entry);
  auto no_styles_manifest = manifest_xml.toByteArray();
  std::vector<ArchivePartChange> without_styles{
      {"styles.xml", true, false, 0, old_styles.size(), 0, old_styles, {}},
      {"META-INF/manifest.xml", true, true, 0, old_manifest.size(),
       no_styles_manifest.size(), old_manifest, no_styles_manifest}};
  blank.replay_part_changes(without_styles, true);
  assert(!blank.entry_data("styles.xml"));
  auto missing_input = Text((dir / "no-styles-original.odt").string());
  blank.save(missing_input);
  TextDocument missing;
  missing.load(missing_input);
  work = missing.style_snapshot();
  create = {};
  create.label = "Draft";
  create.character.family = "Noto Sans";
  create.character.size = 14;
  name = define_words_paragraph_style(work, create);
  assert(missing.commit_styles(work, {0, 0}, missing.revision(), name));
  auto restored_styles = missing.style_snapshot().entry("styles.xml");
  assert(!restored_styles.empty() &&
         missing.format_info({0, 1}).character.size == 14);
  missing.save(Text((dir / "no-styles-created.odt").string()));
  assert(missing.undo());
  assert(!missing.style_snapshot().entry_data("styles.xml"));
  assert(missing.style_snapshot().entry("META-INF/manifest.xml") ==
         no_styles_manifest);
  missing.save(Text((dir / "no-styles-undone.odt").string()));
  assert(missing.redo());
  assert(missing.style_snapshot().entry("styles.xml") == restored_styles);
  missing.save(Text((dir / "no-styles-redone.odt").string()));
  // Empty office:text is valid. Assignment and typing can create its first
  // source paragraph; undo must restore the original empty body exactly.
  OdfPackage empty_document(Kind::Words);
  auto empty_path = Text((dir / "empty-original.odt").string());
  empty_document.save(empty_path);
  TextDocument empty;
  empty.load(empty_path);
  auto empty_xml = empty.style_snapshot().xml.toByteArray();
  work = empty.style_snapshot();
  create = {};
  create.label = "Empty body style";
  name = define_words_paragraph_style(work, create);
  assert(empty.commit_styles(work, {0, 0}, empty.revision(), name));
  assert(children(empty.style_snapshot().body(), TEXT, "p").size() == 1);
  empty.save(Text((dir / "empty-styled.odt").string()));
  assert(empty.undo());
  assert(empty.style_snapshot().xml.toByteArray() == empty_xml);
  empty.save(Text((dir / "empty-undone.odt").string()));
  assert(empty.redo());
  assert(children(empty.style_snapshot().body(), TEXT, "p").size() == 1);
  assert(empty.undo());
  assert(empty.format_info({0, 0}).character.size == 12);
  WordsCharacterFormat empty_font;
  empty_font.weight = 700;
  assert(empty.format_characters({0, 0}, empty_font));
  assert(empty.format_info({0, 0}).character.weight == 700);
  empty.save(Text((dir / "empty-character-default.odt").string()));
  assert(empty.undo());
  assert(empty.style_snapshot().xml.toByteArray() == empty_xml);
  WordsParagraphFormat empty_paragraph;
  empty_paragraph.alignment = "center";
  assert(empty.format_paragraphs({0, 0}, empty_paragraph));
  assert(empty.format_info({0, 0}).paragraph.alignment == "center");
  empty.save(Text((dir / "empty-paragraph-default.odt").string()));
  assert(empty.undo());
  assert(empty.style_snapshot().xml.toByteArray() == empty_xml);
  empty.replace(0, 0, "x");
  assert(empty.text() == "x");
  assert(empty.undo() && empty.text().empty());
  assert(empty.style_snapshot().xml.toByteArray() == empty_xml);
  std::cout << "PASS named paragraph styles, role/next-style application and "
               "atomic archive-part history\n";
}
