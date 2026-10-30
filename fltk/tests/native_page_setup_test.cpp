// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_words_page.h"
#include "text_documents.h"
#include <cassert>
#include <iostream>
using namespace DomeOffice;
namespace {
const Text STYLE = "urn:oasis:names:tc:opendocument:xmlns:style:1.0";
const Text FO = "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
const Text MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";
void near(double a, double b) { assert(std::abs(a - b) < 1e-7); }
template <class F> void rejects(F call) {
  bool failed = false;
  try {
    call();
  } catch (const std::exception &) {
    failed = true;
  }
  assert(failed);
}
struct Snapshot {
  Bytes content, styles, manifest;
  bool operator==(const Snapshot &) const = default;
};
Snapshot state(const OdfPackage &p) {
  return {p.xml.toByteArray(), p.entry("styles.xml"),
          p.entry("META-INF/manifest.xml")};
}
OdfPackage fixture() {
  OdfPackage p(Kind::Words);
  p.xml = read_document_xml(
      R"XML(<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" office:version="1.3"><office:automatic-styles><style:style style:name="First" style:family="paragraph" style:master-page-name="A"/></office:automatic-styles><office:body><office:text><text:p text:style-name="First" xml:id="first">Paper geometry</text:p></office:text></office:body></office:document-content>)XML");
  p.set_entry(
      "styles.xml",
      R"XML(<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3"><office:styles/><office:automatic-styles><style:page-layout style:name="Shared"><style:page-layout-properties fo:page-width="180mm" fo:page-height="250mm" fo:margin="10mm" fo:margin-left="12mm" fo:background-color="#f7f2ed" style:paper-tray-name="default"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="A" style:display-name="First pages" style:next-style-name="B" style:page-layout-name="Shared"><style:header><text:p>Keep this header</text:p></style:header></style:master-page><style:master-page style:name="B" style:display-name="Following pages" style:page-layout-name="Shared"/></office:master-styles></office:document-styles>)XML",
      "text/xml");
  p.set_entry("Extras/opaque.bin", Bytes("\0keep\xff", 6),
              "application/octet-stream");
  return p;
}
} // namespace
int main(int argc, char **argv) {
  TempDir temp;
  std::filesystem::path dir = temp.path().toStdString();
  if (argc == 3 && Text(argv[1]) == "--artifacts") {
    dir = argv[2];
    if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir))
      throw std::runtime_error("Choose an empty artifact directory");
    std::filesystem::create_directories(dir);
  }
  constexpr double mm = 72 / 25.4;
  auto p = fixture();
  auto path = Text((dir / "original.odt").string());
  p.save(path);
  auto original = state(p);
  auto styles = words_page_styles(p);
  assert(styles.size() == 2 && styles[0].name == "A" &&
         styles[0].label == "First pages");
  near(styles[0].geometry.width, 180 * mm);
  near(styles[0].geometry.left, 12 * mm);
  TextDocument d;
  d.load(path);
  TextSelection selection{3, 7};
  auto initial_revision = d.revision();
  auto save = [&](const char *name) {
    auto target = Text((dir / (std::string(name) + ".odt")).string());
    d.save(target);
    OdfPackage read(Kind::Words);
    read.load(target);
    assert(read.entry("Extras/opaque.bin") == p.entry("Extras/opaque.bin"));
    return read;
  };
  WordsPageFormat change;
  change.width = 210 * mm;
  change.height = 148 * mm;
  change.left = 18 * mm;
  change.top = 15 * mm;
  auto working = d.style_snapshot();
  assert(format_words_page(working, "A", change));
  assert(d.commit_styles(working, selection, initial_revision));
  auto modified = state(d.style_snapshot());
  assert(modified.content == original.content &&
         modified.styles != original.styles);
  auto applied = save("shared-layout-edited");
  auto a = words_page_style(applied, "A").geometry,
       b = words_page_style(applied, "B").geometry;
  near(a.width, 210 * mm);
  near(a.height, 148 * mm);
  near(a.left, 18 * mm);
  near(a.right, 10 * mm);
  near(a.top, 15 * mm);
  assert(a.next == "B");
  near(b.width, 180 * mm);
  near(b.height, 250 * mm);
  near(b.left, 12 * mm);
  assert(a.background == b.background);
  auto flow = read_text_flow(applied);
  flow.paragraphs[0].text = Text(4000, U'W');
  flow.paragraphs[0].spans.clear();
  auto pages = paginate_text(flow, [](const Text &text, const TextFont &font) {
    return TextMetrics{double(text.size()) * font.size * .5, font.size * .8,
                       font.size * .2};
  });
  assert(pages.pages.size() >= 2 && pages.pages[0].style.name == "A" &&
         pages.pages[1].style.name == "B");
  near(pages.pages[0].style.width, 210 * mm);
  near(pages.pages[0].style.height, 148 * mm);
  near(pages.pages[1].style.width, 180 * mm);
  near(pages.pages[1].style.height, 250 * mm);
  auto external = read_document_xml(applied.entry("styles.xml"));
  auto layouts = external.elementsByTagNameNS(STYLE, "page-layout");
  assert(layouts.size() == 2);
  auto masters = external.elementsByTagNameNS(STYLE, "master-page");
  assert(masters[0].attributeNS(STYLE, "page-layout-name") !=
         masters[1].attributeNS(STYLE, "page-layout-name"));
  assert(masters[1].attributeNS(STYLE, "page-layout-name") == "Shared");
  assert(plain_text(child(masters[0], STYLE, "header")) == "Keep this header");
  auto props = child(layouts[1], STYLE, "page-layout-properties");
  assert(props.attributeNS(STYLE, "print-orientation") == "landscape" &&
         props.attributeNS(STYLE, "paper-tray-name") == "default");
  assert(props.attributeNS(FO, "margin") == "10mm");
  assert(d.undo() == selection && state(d.style_snapshot()) == original);
  save("page-undone");
  assert(d.redo() == selection && state(d.style_snapshot()) == modified);
  save("page-redone");
  working = d.style_snapshot();
  auto stable = state(working);
  assert(!format_words_page(working, "A", {}));
  assert(!format_words_page(working, "A", change));
  assert(state(working) == stable);
  assert(!d.commit_styles(working, selection, d.revision()));
  // Editing the now-private layout updates it in place without orphan growth.
  for (size_t i = 0; i < 64; ++i) {
    WordsPageFormat margin;
    margin.bottom = (i % 2 ? 11 : 12) * mm;
    assert(format_words_page(working, "A", margin));
  }
  assert(read_document_xml(working.entry("styles.xml"))
             .elementsByTagNameNS(STYLE, "page-layout")
             .size() == 2);
  assert(d.commit_styles(working, selection, d.revision()));
  save("private-layout-repeated");
  assert(d.undo() && state(d.style_snapshot()) == modified);
  // Invalid geometry, stale sessions and failed grouped edits are atomic.
  for (double value :
       {-1.0, 0.0, 10001.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    working = d.style_snapshot();
    WordsPageFormat bad;
    bad.width = value;
    rejects([&] { format_words_page(working, "A", bad); });
    assert(state(working) == modified);
  }
  working = d.style_snapshot();
  WordsPageFormat bad;
  bad.left = 1000;
  rejects([&] { format_words_page(working, "A", bad); });
  assert(state(working) == modified);
  rejects([&] { format_words_page(working, "missing", change); });
  assert(state(working) == modified);
  rejects([&] { d.commit_styles(working, selection, d.revision() - 1); });
  assert(state(d.style_snapshot()) == modified);
  d.begin_edit(selection);
  working = d.style_snapshot();
  WordsPageFormat margin;
  margin.bottom = 22 * mm;
  assert(format_words_page(working, "A", margin));
  assert(d.commit_styles(working, selection, d.revision()));
  d.cancel_edit();
  assert(d.end_edit() == selection && state(d.style_snapshot()) == modified);
  // Missing layout definitions are made explicit; missing styles parts are
  // created with the document version and undone back to absence exactly.
  OdfPackage blank(Kind::Words);
  auto old_styles = blank.entry("styles.xml"),
       old_manifest = blank.entry("META-INF/manifest.xml");
  auto manifest = read_document_xml(old_manifest);
  for (auto entry :
       children(manifest.documentElement(), MANIFEST, "file-entry"))
    if (entry.attributeNS(MANIFEST, "full-path") == "styles.xml")
      manifest.documentElement().removeChild(entry);
  auto no_manifest = manifest.toByteArray();
  blank.replay_part_changes(
      {{"styles.xml", true, false, 0, old_styles.size(), 0, old_styles, {}},
       {"META-INF/manifest.xml", true, true, 0, old_manifest.size(),
        no_manifest.size(), old_manifest, no_manifest}},
      true);
  auto missing_path = Text((dir / "missing-styles-original.odt").string());
  blank.save(missing_path);
  TextDocument empty;
  empty.load(missing_path);
  auto missing = state(empty.style_snapshot());
  auto defaults = words_page_styles(blank);
  assert(defaults.size() == 1 && defaults[0].name.empty());
  near(defaults[0].geometry.left, 20 * mm);
  working = empty.style_snapshot();
  margin = {};
  margin.left = 14 * mm;
  assert(format_words_page(working, {}, margin));
  assert(empty.commit_styles(working, {0, 0}, empty.revision()));
  auto created = empty.style_snapshot();
  assert(created.entry_data("styles.xml"));
  auto created_styles = words_page_styles(created);
  assert(created_styles.size() == 1 &&
         created_styles[0].name.startsWith("WordsPage"));
  near(created_styles[0].geometry.left, 14 * mm);
  near(created_styles[0].geometry.right, 20 * mm);
  empty.save(Text((dir / "missing-styles-created.odt").string()));
  assert(empty.undo() && state(empty.style_snapshot()) == missing &&
         !empty.style_snapshot().entry_data("styles.xml"));
  empty.save(Text((dir / "missing-styles-undone.odt").string()));
  assert(empty.redo());
  empty.save(Text((dir / "missing-styles-redone.odt").string()));
  // A master referring to a missing layout gets its own explicit native
  // defaults.
  working = p;
  auto doc = read_document_xml(working.entry("styles.xml"));
  auto group = child(doc.documentElement(), OFFICE, "automatic-styles");
  group.removeChild(child(group, STYLE, "page-layout"));
  working.set_entry("styles.xml", doc.toByteArray(), "text/xml");
  assert(format_words_page(working, "B", margin));
  near(words_page_style(working, "A").geometry.left, 20 * mm);
  near(words_page_style(working, "B").geometry.left, 14 * mm);
  working.save(Text((dir / "missing-layout-created.odt").string()));
  // Signature protection and application kind checks cover the standalone API.
  auto signed_path = Text((dir / "signed.odt").string());
  auto signed_source = p;
  signed_source.set_entry("META-INF/documentsignatures.xml", "<signatures/>",
                          "text/xml");
  signed_source.save(signed_path);
  OdfPackage signed_doc(Kind::Words);
  signed_doc.load(signed_path);
  assert(signed_doc.read_only());
  rejects([&] { format_words_page(signed_doc, "A", change); });
  OdfPackage sheet(Kind::Sheets);
  rejects([&] { format_words_page(sheet, {}, change); });
  std::cout << "PASS native Words page setup, shared-layout isolation, atomic "
               "ZIP history, missing parts and geometry guards\n";
}
