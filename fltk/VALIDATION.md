# Native engine validation — 2026-10-28

The Qt-free Release build passes the three original document CTests with their
assertions retained, plus fifteen native calculation/matrix/text/scene/history/handle/list regression suites. Actual
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

The native list suite covers decimal/padded/alphabetic/Roman/script numbering,
letter synchronization, nested/consecutive counters, continuation, start values,
headers, item overrides, numbered paragraphs, scoped list/character styles,
inherited frame styles, both spacing modes, RTL labels, separators/tabs,
bullet metrics, label pagination, source offsets, XML-preserving edits and
resource/invalid-input checks. Labels use separate native glyph display lists.
An odfpy producer independently checks counters, Unicode labels, edit/save and
retained archive entries/list structure. GUI tests check actual label pixels
and edit/save in Words and Stage, with Stage native undo/redo.

Six upstream LibreOffice list documents contribute nine generated labels,
including padded decimal and custom-template cases. The broader source audit
also records a malformed `office:texture-mode` body rejected by the loader and
a field-numbering document with no list elements; these are not counted as list
coverage. Outline/field numbering and the other remaining list
features are listed in PORT_STATUS.md. List semantics follow
[ODF 1.4 Part 3](https://docs.oasis-open.org/office/OpenDocument/part3-schema/OpenDocument-v1.4-os-part3-schema.html)
and the retained KoListLevelProperties/KoOdfNumberDefinition/KoTextLoader code.

The native paragraph-tab suite checks fixed/delimiter anchors, explicit/default
stops, relative/absolute origins, hanging indents, RTL mixed text, Unicode decimal
separators, stop-preserving justification, hard/soft wrapping, long paragraphs,
leader patterns/font metrics and invalid settings. Independent odfpy fixtures
exercise inherited/cleared stop lists, default-only settings and common versus
automatic leader-style scopes. Keyboard tests check actual leader pixels in
Words and Stage, repeated glyph-cache reuse, unchanged tab/style XML through
edit/save/reopen and native Stage undo/redo. Shaped tab controls have source
clusters without four rendered spaces; generated leaders have separate display
lists. References include retained KoParagraphStyle/KoTextLayoutArea code,
ODF 1.4 tab properties and
[QTextOption tab semantics](https://doc.qt.io/qt-6/qtextoption-tab.html).

The native image suite checks immutable archive sharing, asset replacement and
retained lifetime, package-only URI resolution, folded inline base64, image-label
geometry, wrapping, RTL, both spacing modes, vertical alignment and save/reload.
Independent odfpy/Pillow fixtures exercise PNG/JPEG/SVG/GIF/BMP and inline labels
in both applications. Actual pixels verify colors and PNG/GIF transparency.
Keyboard tests verify unchanged style/image payloads through edit/save/reopen,
missing/corrupt placeholders, content-based cache reuse through page reflow and
slide undo/redo, repeated-frame reuse, and Stage fullscreen
context changes. The Present toolbar control also receives a real mouse click.
These checks run with system FLTK; they do not establish broad image/SVG fidelity.

The native presentation-history suite checks cross-frame and cross-slide text,
slide insertion/deletion, selection restoration, byte-exact styled XML undo,
unknown content and binary archive entries, save checkpoints, branching,
typing groups, canceled/no-op edits, both history bounds, failed-operation
rollback, successful-load reset and signed-document protection. Nested
paragraph-count edits are rejected before any affected paragraph is changed.
The keyboard flow exercises the editor bindings, canvas-focus shortcuts,
universal-menu slide deletion, cross-frame undo, Ctrl+Y redo and saved-state
restoration, then independently checks the saved ODP text and geometry.

The native adjustment suite covers modern and legacy Cartesian/polar handles,
clockwise degree angles, radius limits and defaults, one-sided/coupled ranges,
fixed coordinates, mirrors, switched directions, offset view boxes, producer
metadata and legacy polar ordering, grouped/nontext shapes, round-trip XML,
save/undo/redo, canceled drags preserving redo, rejected geometry updates,
invalid parameters and signed-document protection. `handles_producer.py`
independently produces an ODF 1.4 handle with odfpy. The keyboard/pointer flow
drags Cartesian, mirrored polar and grouped legacy handles, tests bounds and
Escape, mixes geometry with text undo, and independently inspects saved
modifiers, source geometry, styles and an opaque archive entry. Semantics are
based on [ODF 1.4 Part 3](https://docs.oasis-open.org/office/OpenDocument/part3-schema/OpenDocument-v1.4-os-part3-schema.html)
and the retained upstream EnhancedPathHandle/KoOdfWorkaround implementation.
The 19 upstream flowchart stencils contain 29 editable handles. Actuating them
in both directions produces 56 bounded geometry changes with valid paths.

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

The native rich-text suite checks separate master/content automatic paragraph
styles, font declarations resolved in the XML containing their style, inherited
paragraph fonts/colors, inline font/weight/italic/color runs, styled links,
Arabic bidi, independent frame padding, vertical alignment and continuous
overflow layout. It checks native and reference-metric display lists, source
coverage, retained styles after Words/Stage edits and saves, invalid paint and
oversized edit rejection. Words defaults to black ink and white paper, reads
declared page colors, and retains the session theme in its editor controls.
An independent odfpy producer imports, edits and round-trips a styled ODP.
Framebuffer checks count red/green/blue/purple runs in Words and Stage and
verify text-frame overflow clipping and bottom alignment. Keyboard tests edit
the rich Stage frame and inspect retained style names and original styles.xml.
Eleven Release and ASan/UBSan document suites pass; these checks do not establish
full typography, structured text editing or full office-suite parity.

The native path suite covers all SVG path commands, relative/repeated numbers,
reflected controls, compact arc flags, radius correction/degeneracies, analytic
Bézier/arc bounds, adaptive refinement, nonzero/even-odd compound hit testing,
implicit fill closure, rounded rectangles and partial ellipses. It checks ODP
geometry/text edits and source-style preservation, malformed paths/viewBoxes
and failed-load rollback. `path_reference.py` compares 44 fixed/seeded curves
against independent svgpathtools samples under affine transformation, checking
bounds and a .02-unit approximation tolerance. `path_producer.py` independently
creates ODP path data with an offset, nonuniform viewBox and a rounded rectangle.
Framebuffer samples check curved fills/exteriors, holes, rounded corners and
pie/chord geometry on X11 and native Wayland. The keyboard test enters/exits
fullscreen, verifies increased curve refinement and checks unchanged source
bytes. Twelve Release and ASan/UBSan document suites pass. These are geometry
subset checks; custom/enhanced shapes, advanced stroke/paint and shape editing
remain incomplete.

The native enhanced-geometry suite covers standard formula functions/operators,
modifiers, named/Unicode equation references, lazy conditionals, cycle/domain
rejection, all enhanced path commands and independent F/S/N paint sets. It
checks mirrors, horizontal text areas, changed modifiers, direct custom-shape
text edits and retained geometry/styles after save/reopen. Reversed text-area
coordinates in a real upstream stencil are normalized and diagnosed. All 19
upstream enhanced-geometry stencils load and produce bounded native contours;
this is geometry-read evidence, not complete stencil visual/editing parity.
An odfpy producer independently creates equations/modifiers/text areas and
checks direct shape editing and save. Independent Python math checks 65 formula
values and nine modern/legacy ellipse/bounds cases, including multi-turn G arcs.
Framebuffer checks verify modifier arrows, mirroring, F/S paint suppression,
even-odd holes, separately composited alpha sets and modern clockwise arcs.
The keyboard flow selects mirrored/custom shapes, edits and undoes/redoes text,
then independently compares saved equation/modifier/geometry attributes and
styles.xml. Thirteen Release and ASan/UBSan suites pass. Preset lookup, geometry
editing/handles, stretch points, vertical text, extrusion and text-on-path
remain gaps.

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


## Native engineering functions — 2026-10-29

The native engineering module ports all 50 names and 39 analysis aliases from
`sheets/functions/engineering.cpp`, without Qt/KDE dependencies or a Python runtime.
The 19 Release document suites and 19 ASan/UBSan document suites pass. The 1,640 numerical
reference cases include 131 literal retained upstream cases, 244 SciPy Bessel
cases, 964 cmath complex cases, 30 high-precision mpmath error-function cases,
166 unit cases checked with Pint/physical definitions, 57 signed/base boundary
cases, nine invalid-domain checks and all 39 legacy aliases derived directly from
upstream registrations. `fltk/tests/engineering_reference.py` records per-category
counts and failures as JSON; dependencies are test-only.

The unit reference uncovered rounded/wrong legacy mass, energy and volume factors.
Exact inch/pound/gravity definitions now fix pennyweight, measurement and registered
tons, foot-pounds, calories, horsepower and associated conversions. Modern Julian
light-year/IAU parsec definitions and CODATA atomic mass are used. Sources:
[ODF 1.4 formula specification](https://docs.oasis-open.org/office/OpenDocument/v1.4/os/part4-formula/OpenDocument-v1.4-os-part4-formula.html),
[NIST conversion tables](https://www.nist.gov/pml/special-publication-811/nist-guide-si-appendix-b-conversion-factors/nist-guide-si-appendix-b9)
and [Microsoft unit names](https://support.microsoft.com/en-us/excel/functions/convert-function).
The reference maps BTU to Pint's `Btu_it`, matching ODF's International Table unit;
Pint's plain `Btu` instead denotes the rounded ISO factor. This is an intentional
accuracy improvement, not bit-for-bit retention of the old coefficients.

`engineering_producer.py` loads independently produced odfpy and openpyxl documents,
checking legacy/Excel namespaces, dependent complex/numeric values and unsupported
calculation status without changing the source. The GUI smoke suite edits a
real ODS input, checks the complex/absolute/unit/radix/Bessel dependents with
keyboard navigation, exercises undo/redo, saves, inspects the actual ODF typed
caches and reopens the workbook. Shared array/coercion and prior Words/Stage
regressions remain part of the broader validation. Passing these checks establishes
this module's tested behavior, not full spreadsheet or office-suite parity.
Diagnostic JSON snapshots now replace their destination atomically; concurrent
GUI readers cannot observe an empty or partially rewritten report.
