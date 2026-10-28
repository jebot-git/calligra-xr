# Native engine validation — 2026-10-28

The Qt-free Release build passes the three original document CTests with their
assertions retained, plus seven native calculation/matrix/text/scene regression suites. Actual
Xvfb/D-Bus keyboard, menu, edit/save and Office-to-ODF conversion checks pass
for Words, Sheets and Stage using system FLTK. Link checks show no Qt libraries.

The historical Qt-based Release build also passed the original tests before
porting. Native tests add range aggregation, mathematical/text/financial
functions, typed arrays, scalar intersection, lazy branches, error propagation,
cycles, matrix-block cached results and failed-load rollback. The upstream FODS
matrix corpus contributes 85 checked result cells from four fixtures. This
caught and corrected logical-array coercion in SUMPRODUCT. The keyboard flow
also enters SUMPRODUCT with an array constant, checks undo/redo, and independently
reads its saved OpenFormula and numeric cache. Unsupported functions
remain an explicit limitation; tests do not establish the original complete
formula catalogue. Additional installed/Wayland results are recorded in the
local native-engine artifacts and installation receipt.

The XLSX array-import regression now covers numeric/Boolean/Unicode/error
results, single-cell array evaluation, dependencies, missing and stale result
caches, fixed ODF spans and typed caches on save, unchanged unsupported caches,
edit protection and failed-load rollback. It rejects missing/reversed/out-of-
bounds/overlapping ranges, conflicting formulas and excessive matrix expansion.
Transitional and Strict XLSX dialects pass. An independent openpyxl producer
writes array formulas without calculated caches; the native engine calculates,
edits and round-trips them. The keyboard flow imports an XLSX matrix workbook,
edits an input, checks dependent recalculation, saves ODS and independently
inspects the declared matrix spans and each expected result cache. This does
not establish dynamic-array, data-table or complete XLSX support.

The native text-layout suite checks physical page dimensions/margins, common
and automatic style inheritance, percentage font sizes, styled inline runs,
master continuation, explicit page breaks, keep-together/keep-with-next,
widows/orphans, alignment, blank/hard-broken lines, Unicode combining/emoji
boundaries, invalid geometry/metrics/styles and bounded resources. A 20,000-line
paragraph produces 5,000 pages without wrapping the remaining text again on
each page. An independent odfpy producer checks import/edit/save of a styled
two-page document. The keyboard test switches between Pages/Edit, navigates
pages, edits and reflows paragraphs, then independently reads saved ODT text
and verifies the original styles.xml bytes. Rendered captures are reviewed in
addition to checking page counts; green model tests alone do not prove display
fidelity. Full typography, shape/table layout and printing remain gaps.

The native shaping suite checks ligatures, kerning, combining clusters, Arabic
joining, Devanagari shaping, mixed Hebrew/Latin/numeric visual order, CJK font
fallback, declared bold/italic faces, numeric weights, quoted family lists,
RTL indentation/alignment, space justification and soft-line paragraph
direction. It checks bitmap/outline rasterization, font/library lifetime,
independent shaping and raster sizes, missing glyphs and invalid inputs. The
multilingual ODT fixture round-trips text and unchanged style bytes. Nine
Release and ASan/UBSan CTests pass, including leak detection in the core suites.
Fontconfig substitution uses an explicit private context rather than its
global-default API; no leak suppression is used by those core tests.

`shaping_reference.py` independently invokes `hb-shape` on six Latin, Arabic,
Devanagari, Hebrew and combining-text samples. Glyph IDs, source clusters,
advances and both offsets match. The test uses system reference fonts and
records their actual paths through the native dump. The keyboard test opens
and captures the seven-line multilingual document, reports zero missing glyphs,
then closes it with unchanged source bytes. Native Wayland captures show the
joined scripts and distinct styled faces; actual pixel review supplements
metadata assertions. GUI sanitizer runs exclude third-party toolkit leak
reporting while retaining address/undefined checks. These tests do not establish
all-script, all-font, full rich editing or full office-suite parity.

The native scene suite checks referenced landscape/portrait layouts, master
backgrounds and visibility, automatic-style scoping, inherited opacity,
concave polygons, ellipse geometry, stable paint order, transform-list order,
inverse hit tests and edits to nested frames whose source order differs from
paint order. It checks malformed transforms, cyclic styles, failed-load
rollback and unchanged saved styles. `scene_producer.py` independently creates
an ODP using odfpy and checks its master, grouped frame geometry, selection,
editing and round trip. The keyboard test selects both plain and transformed
nested frames, edits/saves the nested frame and independently inspects the
saved XML and original style bytes. Rendered captures supplement model checks.
`scene_pixels.py` independently samples the framebuffer for master paint and
background, solid fill, alpha compositing/stacking, a concave polygon/notch and
a transformed image. The ten Release and ASan/UBSan document suites pass.
These checks cover the documented basic scene subset, not full Stage fidelity.

The following earlier milestone results describe the old baseline and are
retained for provenance; references there to upstream calculation reuse or
QtCore/XML no longer describe the default build.

---

# Validation — 2026-10-25

Upstream baseline: `edb7aee19cf4c38b55389149ea052982d7a6d41e`.
Release build: GCC 16, C++20, `-O3 -DNDEBUG -march=native`, shared patched FLTK
1.4.5, Qt 6.11.2 headless libraries and KF 6.30. Development RPMs were signature
verified and extracted privately; no Qt graphical packages were installed into
the system for this build.

- The document CTest target passed real formula/dependency/cross-sheet cases,
  ODT/ODS/ODP save/reload, Unicode and whitespace, repeated-cell edits, retained
  spans and binary package entries, signed-document protection and XML rejection.
- The private Xvfb/D-Bus test edited and saved Words, Sheets and Stage through
  native keyboard input, inspected saved archives independently, checked menu
  export, exercised Sheets undo/redo and Stage presentation navigation, and
  closed all three with exit status zero.
- Separate native Wayland captures of all three applications reported
  `Mesa Intel(R) Arc(tm) A770 Graphics (DG2)`, GPU widgets, and universal-menu
  attachment. Screenshots were visually reviewed. The Xvfb test uses llvmpipe
  and is not counted as hardware-rendering evidence.
- Link inspection found QtCore/QtXml and headless KF dependencies, with no
  QtGui or QtWidgets dependency in any of the three applications.
- Source and evidence: `/home/blux/de-xr/calligra` and
  `/home/blux/de-xr/artifacts/calligra-fltk-20261025/`.

These checks establish the current basic editing milestone, not full document
layout fidelity, all office-suite functions, or production readiness.
`PORT_STATUS.md` records the unported original Words/Stage models and the
remaining editing, rendering, interoperability, accessibility and XR work.


## XLSX import milestone (2026-10-25)

The additional `fltk-xlsx` CTest imports a multi-sheet XLSX package with shared
and inline Unicode strings, shared formulas with absolute/relative references,
cross-sheet formulas, named ranges, dates and sparse cells. It edits source
values and verifies dependency recalculation after saving/reopening ODS, rejects
saving to the source XLSX, and compares its bytes. It checks Strict namespace
support, the 1904 date epoch, elapsed time, and rejection of XML entities,
escaping relationships, invalid shared-string indices, array formulas and
signed packages. Both document CTests pass in the optimized Release build.

`xlsx_producer.py` independently creates a workbook using openpyxl (including
inline strings, date/time formats and formulas), then verifies import, edit,
recalculation and ODS round-trip behavior through the C++ backend. The source
bytes remain identical. openpyxl is only a test dependency.

The keyboard UI smoke test additionally opens XLSX, edits and recalculates a
cell, presses Ctrl+S, accepts the proposed ODS destination, independently reads
its MIME and cell XML, verifies source bytes and confirms that subsequent Save
does not reopen the conversion chooser. Words/Sheets/Stage regression flows
also pass. This does not establish full Excel compatibility or visual fidelity;
see the XLSX limitations in README.md.

## DOCX/PPTX import milestone (2026-10-25)

The optimized Release build passes all three CTests. `fltk-office-import`
checks DOCX Unicode, whitespace, content controls, inserted/deleted text and
basic tables; PPTX slide ordering, slide/frame dimensions (including negative
coordinates), embedded PNG media and editable text frames; both Strict and
Transitional namespace dialects; and ODT/ODP save/reload. It checks rejected
Office save destinations and unchanged source bytes, as well as entity XML,
escaping archive members/relationships, invalid geometry, wrong content types,
external main parts, signatures and macros. Failed imports retain the previous
live presentation.

`office_producer.py` independently generates DOCX with python-docx and PPTX
with python-pptx, including Unicode text, table cells, PNG/JPEG media and a
layout-inherited title placeholder. The C++ backend imports, edits and saves
ODT/ODP copies and checks their content and media after reload. Source bytes
remain unchanged. The producer libraries are test dependencies only.

The private Xvfb/D-Bus keyboard smoke test passes DOCX and PPTX import, text
editing, Ctrl+S conversion with the proposed ODT/ODP destination, source-byte
protection and subsequent Save without a second chooser. XLSX and all existing
Words/Sheets/Stage editing, menu, undo and presentation checks also pass.
Evidence is in `/home/blux/de-xr/artifacts/calligra-office-import-20261025/ui/`.
These checks do not establish Office layout fidelity; see the explicit import
subsets and omissions in README.md.

The DomeD association helper now maps ODT/DOCX to Words, ODS/XLSX to Sheets,
and ODP/PPTX to Stage. Its merge was checked for idempotence, legacy preference
cleanup and preservation of unrelated MIME preferences. The release staging
helper validates the generated desktop entries, installed executable launch,
library resolution and absence of QtGui/QtWidgets dependencies before sudo.
