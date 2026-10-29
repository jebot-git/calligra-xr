// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_documents.h"
#include <cassert>
#include <iostream>
#include <filesystem>
using namespace DomeOffice;
namespace {
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
  assert(p.xml.setContent(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:automatic-styles>
<style:style style:name="First" style:family="paragraph" style:master-page-name="Default"><style:paragraph-properties fo:break-before="page"/><style:map style:condition="table()" style:apply-style-name="Body"/></style:style>
<style:style style:name="Body" style:family="paragraph"/><style:style style:name="Bold" style:family="text"><style:text-properties fo:font-weight="bold"/></style:style>
</office:automatic-styles><office:body><office:text><text:p text:style-name="First" xml:id="first">Start <text:span text:style-name="Bold">Český 日本語</text:span> tail</text:p><text:section text:name="Section"><text:p>Section one</text:p><text:p>Section two</text:p></text:section><table:table table:name="Cells"><table:table-column table:number-columns-repeated="2"/><table:table-row><table:table-cell office:value-type="string"><text:p>Cell one</text:p></table:table-cell><table:table-cell office:value-type="string"><text:p>Cell two</text:p></table:table-cell></table:table-row></table:table><text:list><text:list-item><text:p>List item</text:p></text:list-item></text:list></office:text></office:body></office:document-content>)XML"));
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  Text dir = temp.path();
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    dir = Text(argv[2]);
    auto path = std::filesystem::path(dir.toUtf8().toStdString());
    if (std::filesystem::exists(path) && !std::filesystem::is_empty(path))
      throw std::runtime_error("Choose a new or empty artifact directory");
    assert(Dir().mkpath(dir));
  }
  auto input = dir + "/input.odt";
  fixture().save(input);
  TextDocument book;
  book.load(input);
  auto original = book.text();
  auto snapshot = [&](const char *name) {
    auto path = dir + "/" + name + ".odt";
    book.save(path);
    OdfPackage p(Kind::Words);
    p.load(path);
    assert(p.entry("Extras/opaque.bin") == Bytes("\0keep\xff", 6));
    return p.xml.toByteArray();
  };
  auto original_xml = snapshot("original");
  TextSelection selected{14, 6}, result{9, 9};
  // A selection replacement is one command, even when the widget removes the
  // selection and inserts Unicode text in separate callbacks.
  book.begin_edit(selected);
  book.replace(6, 8, "");
  assert(book.modified() && !book.can_undo());
  book.replace(6, 0, "世界!");
  assert(!book.end_edit(result) && !book.editing());
  auto replaced = original.left(6) + "世界!" + original.mid(14);
  assert(book.text() == replaced && book.can_undo());
  auto replaced_xml = snapshot("replaced");
  assert(book.undo() == selected && book.text() == original && book.modified());
  assert(!book.can_undo() && book.can_redo());
  assert(book.redo() == result && book.text() == replaced && !book.modified());
  assert(snapshot("redone") == replaced_xml);
  assert(book.undo() == selected);
  // Retain both the saved-state identity and the existing redo branch when a
  // later callback fails after an earlier deletion succeeded.
  assert(snapshot("before-rejection") == original_xml);
  book.begin_edit(selected);
  book.replace(6, 8, "");
  assert(book.text() != original && book.modified());
  rejects([&] { book.replace(6, 0, "\n"); });
  assert(book.edit_cancelled() && book.text() == original && !book.modified());
  rejects([&] { book.replace(6, 0, "x"); });
  assert(book.end_edit() == selected && !book.editing());
  assert(book.can_redo() && !book.can_undo());
  assert(snapshot("rejected-late") == original_xml);
  assert(book.redo() == result && book.text() == replaced && book.modified());
  assert(book.undo() == selected && !book.modified());
  // A rejected first callback must also suppress the insertion callback.
  TextSelection all{original.size(), 0};
  book.begin_edit(all);
  rejects([&] { book.replace(0, original.size(), ""); });
  rejects([&] { book.replace(0, 0, "x"); });
  assert(book.end_edit() == all && book.text() == original);
  assert(!book.modified() && book.can_redo() && !book.can_undo());
  assert(snapshot("rejected-first") == original_xml);
  // Cancellation after several accepted callbacks restores styles and source
  // structure, not only the visible string. No cancelled edit consumes redo.
  book.begin_edit(selected);
  book.replace(6, 2, "");
  book.replace(6, 0, "other");
  book.cancel_edit();
  book.cancel_edit();
  assert(book.end_edit() == selected && !book.modified());
  assert(snapshot("cancelled") == original_xml && book.can_redo());
  // Validate the final widget selection before committing anything.
  book.begin_edit(selected);
  book.replace(6, 1, "x");
  rejects([&] { book.end_edit(TextSelection{9999, 9999}); });
  assert(book.edit_cancelled() && book.text() == original);
  assert(book.end_edit() == selected && !book.modified() && book.can_redo());
  // Empty events neither create history nor invalidate redo.
  book.begin_edit(selected);
  assert(!book.end_edit() && !book.modified() && book.can_redo());
  rejects([&] { book.begin_edit({9999, 9999}); });
  assert(!book.editing());
  // A failed bulk operation or load cancels the entire pending event too.
  book.begin_edit(selected);
  book.replace(6, 1, "x");
  rejects([&] { book.set_text("Cannot flatten a structured document"); });
  assert(book.end_edit() == selected && book.text() == original);
  book.begin_edit(selected);
  book.replace(6, 1, "x");
  rejects([&] { book.load(dir + "/missing.odt"); });
  assert(book.end_edit() == selected && !book.modified() && book.can_redo());
  assert(snapshot("rejected-load") == original_xml);
  // Saving and history traversal are boundaries. Saving commits one group;
  // undo never records an intermediate deletion as a separate command.
  book.begin_edit(selected);
  book.replace(6, 8, "");
  book.replace(6, 0, "世界!");
  assert(snapshot("saved-pending") == replaced_xml);
  assert(!book.editing() && !book.modified());
  assert(book.undo() == selected && !book.can_undo());
  assert(book.redo() == result && !book.modified());
  book.begin_edit(result);
  book.replace(9, 0, "x");
  assert(book.undo() == result && book.text() == replaced && !book.modified());
  assert(book.can_redo());
  assert(snapshot("undo-pending") == replaced_xml);
  // A successful load replaces the document and closes an unfinished event.
  book.begin_edit(result);
  book.replace(9, 0, "x");
  book.load(input);
  assert(!book.editing() && !book.modified() && !book.can_undo() &&
         !book.can_redo());
  assert(snapshot("reloaded") == original_xml);
  auto signed_source = fixture();
  signed_source.set_entry("META-INF/documentsignatures.xml",
                          "<document-signatures/>", "text/xml");
  signed_source.save(dir + "/signed.odt");
  book.load(dir + "/signed.odt");
  assert(book.read_only());
  book.begin_edit(selected);
  rejects([&] { book.replace(6, 1, "x"); });
  assert(book.end_edit() == selected && book.text() == original);
  assert(!book.modified() && !book.can_undo());
  std::cout << "PASS Words compound edit transactions, selection/history, "
               "rollback and save boundaries\n";
}
