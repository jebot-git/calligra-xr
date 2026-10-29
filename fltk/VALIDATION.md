# Native engine validation — 2026-10-28

This is a chronological milestone log. Later sections supersede earlier function
limits; README.md and PORT_STATUS.md describe the current implementation.

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


## Ordered native table source model — 2026-10-29

All 20 Release document suites and all 20 ASan/UBSan suites pass, with core
leak detection enabled. Independent producer checks also pass in both builds.
The new `native-table-model` suite verifies ordered root/nested blocks, source
paragraph identity across repetition, row/column spans, covered-cell text, header
rows/columns, fixed/relative widths, padding/borders/backgrounds and inherited
cell styles. Content automatic styles and master/header automatic styles are
checked independently, including callers with separately parsed XML owners and
repeated frame reads after a failed load. Source editing/save/reopen preserves
the grid and original paragraph mapping.

Malformed spans/coverage, cycles, invalid styles, nesting and expansion limits
are checked, together with a 40,000-cell repeated grid that reads one source
paragraph once. `table_model_producer.py` creates an independent odfpy ODT with
headers, merged/repeated/nested cells and checks its model without modifying the
package. These checks establish the source model. The subsequent continuous frame
renderer and initial Words table pagination are covered below.


## Continuous native table layout — 2026-10-29

All 21 Release document suites and all 21 ASan/UBSan suites pass with core leak
checking enabled. The `native-table-layout` suite checks column sizing, RTL
source identities, collapsed/separate borders, hidden and zero-height rows,
fixed-row clipping, vertical alignment, nested/repeated cell formatting and
source-preserving text edits with inline line breaks. The shared full GUI
regression passes in Release and ASan/UBSan builds. GUI leak checking is disabled
for retained third-party toolkit allocations; core leak checking stays enabled.

Independent odfpy ODT/ODP fixtures exercise merged/header cells, inherited text
styles, fixed-height overflow and all border patterns. Private Xvfb/D-Bus tests
check actual background/border/glyph pixels, a clipped two-color image bullet,
and tab leaders both present inside the cell and absent from its neighbor.
A rotated-frame test rectifies affine cell corners and checks clipping in source
coordinates. Text edit, undo/redo, save and reopen retain the grid, styles and
inline breaks; untouched package members remain byte-identical. These normal
and rotated tests pass in both Release and sanitizer builds. Reproduce with:

```sh
uv run --with odfpy --with Pillow --with python-xlib python3 fltk/tests/table_layout_ui.py
uv run --with odfpy --with Pillow --with python-xlib python3 fltk/tests/table_transform_ui.py
```

Use `--bin-dir` and `--artifacts` for an alternate build and separate output.
Tests use isolated sessions and do not restart the active desktop. The current
native Wayland proof covers all three applications, system FLTK mapping without
Qt, rendered Stage table pixels and keyboard undo/redo. GTK3 can still load
through system FLTK's libdecor decoration plugin. This is continuous frame
layout validation; the initial Words pagination stage below adds repeating
headers, master-width reflow and source cell selection. Split rows, merged-cell
continuations and rich structural editing remain unfinished.
Evidence: `/home/blux/de-xr/artifacts/calligra-native-table-layout-20261029/`.


## Initial paginated native table flow — 2026-10-29

The `native-table-pagination` suite exercises ordered root/cell source identities,
repeated headers, width changes between masters, merged row groups, whole-table
keep mode, explicit table breaks, empty-cell tables, bounded oversized-group
diagnostics and source-aware selection across columns/nested cells. A distinct
odfpy document contains 18 two-column body rows and header rows on three pages.
`table_pagination_ui.py` checks actual header/background/text pixels, source cell
selection, text edit/undo/redo, package-preserving save, page navigation and
reopen. The GUI test exposed Words' focused editor bypassing application Undo;
the shared editor bindings now route Undo/Redo through the native journal.

Oversized row splitting, merged continuations and keep-with-next across tables
are not implemented by this stage. The next page-layout work requires per-cell
source cursors rather than slicing glyphs across arbitrary horizontal bands.

At this stage, 22 Release suites and 22 ASan/UBSan suites passed with core leak
checking. The full shared GUI regression and the three-page table GUI fixture
pass in both builds; a fresh private
native Wayland run verifies rendered Words table/header pixels and all three
applications using system FLTK without Qt mapped. GTK3 remains dynamically loaded
by system decorations. Reproduce the focused GUI check with:

```sh
uv run --with odfpy --with Pillow --with python-xlib python3 fltk/tests/table_pagination_ui.py
```

Evidence: `/home/blux/de-xr/artifacts/calligra-native-table-pagination-20261029/`.


## Independent paginated cell source flow — 2026-10-29

The `native-cell-flow` suite checks independent long/short cells, merged and
nested continuations, source ranges across master-width changes, fixed/minimum
row dimensions, blank continuations, hard/trailing line breaks, paragraph page
breaks, real HarfBuzz/ICU Unicode shaping and single-instance list labels.
Impossible empty-cell padding fails immediately rather than making thousands of
pages without source progress. Retained `TestTableLayout` merged-row cases use
three columns, paired row spans, two original paragraphs per cell and small
200 x 90/110 root areas; their source coverage and multi-page placement remain
asserted. The retained upstream larger-than-area case is marked as an expected
failure upstream; the native fixed-height case now checks bounded fragments
whose total height equals the original dimension and whose source text appears
once.

`cell_flow_producer.py` independently creates an ODT with a two-row merged cell,
a nested table, styled red/green text, hard line breaks, repeated headers and
300/380 pt master widths. The test-only inspector checks every original Unicode
source range, complete line/cell bounds and absence of missing glyphs without
modifying the package. `cell_flow_ui.py` checks physical red/green glyph pixels
against the corresponding source cells on every page, then source selection,
edit/undo/redo, XML structure/inline-break retention, untouched package members,
independent saved-document inspection and reopen. GUI checks run in isolated
Xvfb/D-Bus sessions. Reproduce with:

```sh
uv run --with odfpy python3 fltk/tests/cell_flow_producer.py /tmp/calligra-cell-flow
uv run --with odfpy --with Pillow --with python-xlib python3 fltk/tests/cell_flow_ui.py
```

All 23 Release suites and all 23 ASan/UBSan suites pass, including the retained
merged-row topology and final constraint guards. Both full shared GUI regressions
and both focused cell-flow GUI runs pass. Core leak checking remains enabled;
GUI runs exclude retained third-party toolkit leaks. Native Wayland checks use
the four-page split/merged fixture and system
FLTK, with no Qt mapped. GTK3 still loads through libdecor decorations. Table
keeps across blocks, header/body-crossing spans and master changes inside cells
remain diagnosed limitations; full document/layout/editing parity is unfinished.
Evidence: `/home/blux/de-xr/artifacts/calligra-native-cell-flow-20261029/`.

## Paragraph/table/cell keep chains — 2026-10-29

The `native-keep-boundaries` suite covers heading/table/caption chains, hidden
tables, long table and paragraph continuations, fixed-height blank fragments,
nested cell blocks, independent short/long sibling cells, and forced breaks.
Source coverage, whole-line page bounds, retained row dimensions and bounded
progress are asserted. Remaining-height probes inspect active nested cursors
and pending breaks rather than reserving a caption in every sibling cell.
Conflicting row/table/paragraph keeps produce diagnostics and retain content.

Explicit master assignments break before the paragraph/table even when the
assigned master matches the current page, as specified by
[ODF 1.4, section 16.9](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part3-schema.pdf).
This also follows the retained `KoTextLayoutArea` table/paragraph boundary
behavior. The tests cover paragraph-to-table, table-to-paragraph and
paragraph-to-paragraph keeps at these forced boundaries. Break precedence and
satisfiable keep subsets follow [XSL 1.1 section 4.8](https://www.w3.org/TR/xsl11/#keepbreak).

`keep_producer.py` uses odfpy to generate five independent ODT cases: an ordinary
heading/table/caption chain, a long table with short/long sibling cells, nested
cell content, an explicit page break and a same-name master assignment. The
inspector checks exact original Unicode coverage, repeated headers, line/cell
bounds and unchanged inputs. `keep_ui.py` checks actual table/caption pixels,
nested source selection, edit/undo/redo/save/reopen, retained keep/master/grid
attributes and byte-identical untouched package members. Reproduce with:

```sh
uv run --with odfpy python3 fltk/tests/keep_producer.py /tmp/calligra-keeps
uv run --with odfpy --with Pillow --with python-xlib python3 fltk/tests/keep_ui.py
```

All 24 Release suites and all 24 ASan/UBSan suites pass. Core leak checking is
enabled; GUI checks exclude third-party toolkit leaks. All five focused GUI
cases pass in Release and ASan/UBSan. A private native Wayland run checks Words'
End navigation and final table/caption pixels, Sheets' complex-result keyboard
selection, and Stage's table pixels and undo/redo. All three use system FLTK
with no Qt mapped; GTK3 still loads through system decorations. The Wayland
harness waits for compositor focus and uses the reported content viewport.
The tests use software OpenGL and do not establish hardware/spatial XR or full
office-suite fidelity. Header/body-crossing spans and inner master handling
remain unfinished.

Evidence: `/home/blux/de-xr/artifacts/calligra-native-keeps-20261029/`.

## Paragraph master assignments inside table cells — 2026-10-29

ODF 1.4 section 19.501 requires paragraph page-master assignments to be ignored
inside a table. The reader now resolves that rule per paragraph instance, for
both source paragraphs and cached cell formatting. A common inherited style
can still assign a master to root paragraphs using that style. The table-model
regression checks both contexts, repeated cells, nested cells, retained font and
keep properties, and unchanged source XML.

The sixth odfpy keep fixture assigns a master to the base paragraph style used
throughout nested table content. It checks the original page progression and
kept heading/table/caption chain without spurious unsupported-master warnings.
All six producer cases and all six edit/undo/save/reopen GUI cases pass in
Release and ASan/UBSan builds. Both sets of 24 core suites pass with sanitizer
core leak checking enabled. Native Wayland checks verify the two-page nested
case and actual caption/table pixels, alongside Sheets and Stage keyboard and
rendering checks. Original master attributes remain in the saved package.
Nested table master assignments, header/body-crossing spans and full document
fidelity remain separate unfinished work.

Evidence: `/home/blux/de-xr/artifacts/calligra-native-cell-masters-20261029/`.

## Native periodic financial calculations — 2026-10-29

The native financial module ports 33 names from the retained financial group,
including annuities, amortization, periodic cash-flow returns, depreciation and
the three-argument Calligra DURATION extension. It uses native typed values and
C++ math rather than the original Qt-backed Value/ValueCalc classes. The
original variable-declining-balance algorithms and their fractional boundaries
are retained. Shared annuity calculations use stable small-rate series and
`log1p`/`expm1`; cumulative interest uses a closed sum. Rate/return iterations
check convergence and have finite work limits.

`financial_reference.py` checks 1,978 cases in each build: 199 retained upstream
cases, 1,396 NumPy Financial comparisons, 308 high-precision mpmath cases,
9 explicit boundaries and 66 Excel namespace cases. Every registered name is
covered. The references exposed a fractional first-period DDB difference and
negative-return IRR oscillation; retained tests and independent cash flows
cover the corrected behavior. The solver removes the derivative of the
polynomial scaling factor so iterations follow the same NPV function on both
sides of zero. No numerical reference packages participate in the installed
calculation path.
The loan/sequence contracts are checked against
[OpenFormula 1.4](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part4-formula.pdf);
independent periodic calculations use [NumPy Financial](https://numpy.org/numpy-financial/latest/).

The new core suite also checks sparse numeric sequences, direct versus
referenced text/Boolean arguments, scalar array mapping, error propagation,
near-zero individual/cumulative interest, amortization identities and ODS
round trips. `financial_producer.py` creates independent ODS and XLSX files with
deliberately stale formula caches. Native evaluation and saved ODS results are
checked against NumPy Financial. Supported formulas replace stale caches;
ordinary errors update them, while unknown calculations and their IFERROR
dependents preserve the original caches. Standard coupon DURATION is explicitly
incomplete until its date/coupon calculation is ported.

`financial_ui.py` follows actual keyboard selection through 21 dependent cells,
edits the original loan value, checks recalculation/undo/redo, saves through the
application and reopens the file. Unchanged package members and unsupported
caches survive. Both Release and ASan/UBSan GUI runs pass, and all 25 core suites
pass in both builds with core leak checking enabled. GUI checks disable leak
detection for third-party toolkit allocations. A private native Wayland run
checks financial result display and keyboard selection alongside Words/Stage
rendering and Stage undo/redo; all three use system FLTK without Qt mapped.
GTK3 still loads through system decorations. These software-OpenGL checks do
not establish hardware performance or spatial XR fidelity.

```sh
uv run --with numpy-financial --with mpmath python3 fltk/tests/financial_reference.py
uv run --with numpy-financial --with odfpy --with openpyxl python3 fltk/tests/financial_producer.py /tmp/calligra-financial
uv run --with numpy-financial --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/financial_ui.py
```

Coupon/bond/date-basis behavior, irregular-date cash flows, euro conversions and
the remaining original calculation/model/format catalogue remain unfinished.
Evidence: `/home/blux/de-xr/artifacts/calligra-native-financial-20261029/`.

## Native irregular cash flows and numeric labels — 2026-10-29

XIRR/XNPV now use native Gregorian dates, matched cash/date entries and stable
exponentially scaled sums. A shared converter accepts native serial dates and
ISO date/date-time text over years 1–9999 and truncates fractional serial days.
Equal-date cash flows combine before evaluation; exact cancellation cannot
manufacture a return rate. XIRR uses a log-rate solve with a contracting bracket,
safeguarded Newton steps, finite iteration and term budgets. Leading zero flows
and dates near the supported calendar limits are covered. The full source
calculation-settings/locale/date-basis model remains unfinished.

`dated_cashflow_reference.py` checks 685 cases against retained Calligra cases,
independent SciPy bracketed roots and high-precision mpmath sums. Coverage
includes negative returns, shuffled and duplicate dates, leap years, different
array shapes, multiple roots selected by a guess, long spans and legacy/Excel
namespaces. Both Release and ASan/UBSan runs pass. The existing 1,978 periodic
financial comparisons still pass in each build; the registered financial group
now contains 35 names. Contracts are referenced to
[OpenFormula 1.4 sections 6.12.51–52](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part4-formula.pdf).

The retained date corpus exposed the parser's literal-only array restriction.
Array entries now accept scalar expressions and references, including DATE
calls, with normal dependency/error handling. Nested and multi-cell entries
produce a value error. This is a retained Calligra extension; the independent
XLSX producer instead uses the standard CHOOSE array construction. ODS and
both 1900/1904 XLSX producers check exact normalized serial dates, recalculation,
dependent values, saved caches and unchanged source files. XLSX import can
normalize date-only labels to midnight ISO date-times; both representations
are checked against the same exact serial date.

`dated_cashflow_ui.py` follows the displayed results through a positive-to-negative
return change, keyboard undo/redo, save and reopen. Unchanged package members
and caches for unported bond calculations survive. Visual inspection exposed
clipped numeric exponents: real numeric grid labels now reduce precision to fit,
or show overflow markers. The QA report records the string actually painted by
the grid; GUI checks compare that label's numeric magnitude with the full value.
The full value remains available for copying and editing. Complete original
number/style/locale and complex-number formatting is still unfinished.

All 26 core suites pass in Release and ASan/UBSan with core leak checking. Both
dedicated GUI checks pass; GUI leak checking excludes third-party toolkit
allocations. Private native Wayland checks use system FLTK, with no Qt mapped.
GTK3 remains dynamically loaded through system decorations. Software OpenGL
tests do not establish hardware performance or spatial XR fidelity.

```sh
uv run --with scipy --with mpmath python3 fltk/tests/dated_cashflow_reference.py
uv run --with scipy --with odfpy --with openpyxl python3 fltk/tests/dated_cashflow_producer.py /tmp/calligra-dated
uv run --with scipy --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/dated_cashflow_ui.py
```

Evidence: `/home/blux/de-xr/artifacts/calligra-native-dated-cashflows-20261029/`.


## Native calendar bases and dated securities — 2026-10-29

The shared native Gregorian calendar now supplies `YEARFRAC`, `DAYS360` and
reusable day-count/year-length interfaces for the five OpenFormula bases.
`ACCRINTM`, `DISC`, `INTRATE`, `PRICEDISC`, `RECEIVED` and `YIELDDISC` use those
interfaces without Qt. The financial catalogue now has 41 names. XIRR/XNPV use
the same civil-date conversion as the new calendar module; their actual/365
cash-flow exponents are unchanged. Common-epoch serials, truncation, ISO dates
and civil date-times, and bounded year 1–9999 validation remain shared.

The reference is OpenFormula 1.4 §§4.11.7, 6.10.7, 6.10.25 and the respective
financial functions:
https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part4-formula.pdf
Financial Basis 0 follows Procedure A's ordered month-end adjustments and swaps
reversed dates. US DAYS360 instead keeps the original date order and applies its
own February/31st rules. European DAYS360 preserves the sign of reversed dates.
Actual/Actual follows Procedure E, including its explicit leap start-year rule
across a year boundary. Long intervals use the inclusive average year length;
Gregorian leap counts are constant-time and do not loop across years.

`calendar_reference.py` checks 6,859 cases: 88 retained upstream expressions,
920 Gregorian basis cases, 368 signed DAYS360 cases, 5,460 dated securities
cases, 16 Excel namespace cases and seven analysis aliases. Python datetime,
calendar and Fraction provide the independent civil-date and rational arithmetic
reference. Fixtures include leap centuries, February/31st boundaries, reversed
intervals, year 1–9999 spans and short cross-year intervals. The native suite also
covers typed array mapping, conversion/errors, dependent recalculation and
unsupported-status propagation through IFERROR.

All 88 retained expressions are evaluated. Nine expectations deliberately differ
from the old source tests; they are reported with both values in reference.json:

- Six YEARFRAC Actual/Actual cases begin after February in a leap year and end
  in the following year. Procedure E uses 366; the old tests use 365.
- ACCRINTM from 2004-02-01 to 2004-05-01 at 10% on 1,000 with Basis 0 is 25.
  The old financial helper/test counts 89 days and expects 24.722222.
- The reversed-date DISC test expects Boolean false, although its retained
  function rejects that domain. The native function returns ordinary #NUM!.
- The multi-year INTRATE Actual/Actual case uses the inclusive average-year
  denominator, rather than the old financial helper's start-year denominator.

These differences are not excluded from coverage or called historical parity.
The corpus asserts the retained case count and the nine known deviations.
Remaining coupon schedules, treasury-bill legacy differences and full calculation
settings, number/style/locale formatting and original document model still need
porting. Standard coupon DURATION remains incomplete and preserves source caches.

Independent odfpy/openpyxl producers create ODS, XLSX 1900 and XLSX 1904 inputs
with typed dates and intentionally stale caches. Native calculation replaces all
supported caches; invalid-basis IFERROR is an ordinary completed calculation;
unknown/coupon-dependent caches stay unchanged. Original inputs and date serials
are verified. GUI tests edit the principal, check actual painted labels, undo,
redo, save and reopen, and retain unrelated package members.

Reproduce the focused checks with:

```sh
python3 fltk/tests/calendar_reference.py
uv run --with odfpy --with openpyxl python3 fltk/tests/calendar_producer.py /tmp/calligra-calendar
uv run --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/calendar_ui.py
```

Release and ASan/UBSan core suites both pass all 27 tests, with core leak detection
on. Both builds pass all 6,859 calendar references, independent producer formats
and the focused GUI workflow; GUI leak checking is disabled for third-party
allocations. The previous 685 dated cash-flow and 1,978 periodic references are
also rerun against the shared conversion layer. Artifacts are under
`/home/blux/de-xr/artifacts/calligra-native-calendar-20261029/`.

Both full shared GUI regressions also pass. A private nested DomeD/Wayland
session verifies all three system-FLTK applications, Words table/caption pixels
and page navigation, Sheets calendar calculation and painted values with native
keyboard selection, and Stage table rendering and keyboard undo/redo. No active
desktop/display-manager session is restarted. Maps show no Qt; GTK3 still loads
through system FLTK/libdecor. Software GL checks do not establish hardware or
spatial XR readiness. Installation remains a separate authenticated step; a
verified stage does not mean the installed version has advanced.


## Native coupon schedules — 2026-10-29

COUPDAYBS, COUPDAYS, COUPDAYSNC, COUPNCD, COUPNUM and COUPPCD now run through a
shared native C++ coupon model. The financial catalogue has 47 names. Civil
date conversion and Excel epoch normalization are shared with the native calendar
layer; no Qt date, value or calculation classes participate. A month quotient
and one boundary adjustment locate the current coupon period. Every date is
anchored directly to maturity, retaining the original day or end-of-month rule;
a short February cannot shift a later August coupon. The model provides previous
and next dates, remaining payments, coupon days, accrued days and remaining days
for the subsequent native bond-pricing/duration port.

The behavioral source is the retained coupon implementation in
`sheets/functions/financial.cpp`, originally adapted there from Gnumeric.
All six queries retain its MSRB 30/360, Actual/Actual, Actual/360, Actual/365,
30E/360 and 30E+/360 conventions. In particular, MSRB independently adjusts both
last-day-of-February endpoints; this differs from the shared YEARFRAC Basis 0
procedure and is not silently substituted with that routine. The normal standard
interface uses annual/semiannual/quarterly schedules and bases 0–4. Existing
Calligra extensions retain frequencies 3, 6 and 12, basis 5 and COUPNUM's fifth
end-of-month flag. Other queries retain their registered three/four-argument
interface, even though the upstream internal helper also has an EOM argument.
These extensions are not claimed as standard Excel syntax. OpenFormula's coupon
function descriptions are in §§6.12.5–6.12.10; the documented MSRB variant and
extensions here describe the retained Calligra behavior rather than blanket
OpenFormula day-count conformance. LibreOffice's published examples for all six queries are included as external
cases:
https://help.libreoffice.org/latest/en-US/text/scalc/01/04060118.html

`coupon_reference.py` checks 22,890 cases: all 61 retained expressions with their
original expectations, 21,600 independently enumerated schedule/convention cases,
1,200 explicit EOM cases, eleven documented boundary cases and 18 namespace
aliases. Its Python datetime/calendar implementation enumerates dates from
maturity instead of using the C++ constant-time lookup. Leap centuries, short
Februarys, payment-date settlement, all retained frequencies/bases and both EOM
choices are covered. There are no retained expectation deviations in this corpus.
The native suite additionally checks typed arrays, errors, partial-status
propagation, dependent edits, fractional argument truncation and year 1–9999
counting. A previous coupon outside the native representable year range returns
ordinary #NUM! when requested as a date; complete date/settings compatibility
remains part of the larger port.

Independent odfpy/openpyxl producers generate ODS, XLSX 1900 and XLSX 1904 inputs
with typed source dates and stale numeric caches. They check all six functions,
date subtraction, Boolean results, ordinary invalid-frequency errors and retained
unsupported/bond-dependent caches. Saved coupon dates must have ODF date types
and date-value attributes, with no stale numeric value attribute. Excel inputs
use standard four-argument functions and replace the ODS-only basis-5 example
with the equivalent actual one-day boundary case. Neither source documents nor
unrelated package members are overwritten by import checks.

GUI tests change the coupon frequency from two to four, verify changed dates,
counts and day values, undo, redo, save and reopen. All three producer formats
and this focused GUI workflow pass in Release and ASan/UBSan builds. Both core
builds pass all 28 suites with leak detection enabled. GUI leak detection is
disabled for third-party allocations. The existing 6,859 calendar, 685 dated
cash-flow and 1,978 periodic reference cases are rerun separately against the
new build. These results do not establish the remaining bond functions or full
calculation/document model.

```sh
python3 fltk/tests/coupon_reference.py
uv run --with odfpy --with openpyxl python3 fltk/tests/coupon_producer.py /tmp/calligra-coupons
uv run --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/coupon_ui.py
```

Evidence is under `/home/blux/de-xr/artifacts/calligra-native-coupons-20261029/`.

A private native Wayland run verifies Words/Sheets/Stage against system FLTK,
including coupon day values and typed next/previous dates selected with native
keyboard input. Words table/caption rendering and navigation, and Stage table
rendering and undo/redo also pass. Process maps contain no Qt; GTK3 remains loaded
through system FLTK/libdecor. These software-GL checks do not establish hardware
or spatial XR readiness. The current authenticated installation remains separate
from this staged build, and no active desktop or display manager is restarted.


## Native regular-coupon bonds and dated duration — 2026-10-29

Native PRICE, YIELD, MDURATION and DURATION_ADD expand the financial catalogue to
51 names. The existing DURATION entry now implements its five/six-argument dated
form as well as the retained three-argument annuity extension. Previously
unsupported dated DURATION expressions now return calculated numbers and update
their package caches. Unsupported-cache fixtures now use ODDLPRICE instead;
the new bond producer explicitly checks completed DURATION caches, so the old
unsupported behavior is not retained by changing the fixtures alone.

Valuation uses the shared maturity-anchored CouponPeriod. Binary concatenation
computes the finite discounted sum and first moment in logarithmic work per
evaluation. Positive cash flows are normalized before evaluation. For negative
log rates, the polynomial is reversed and its final time factor is retained in
log space. This avoids overflowing long-span dirty values in the yield solver,
and avoids losing finite duration when a zero-coupon value underflows. Zero rates
need no cancellation-prone division by the rate.

YIELD solves the monotone log dirty-price equation, uses its analytic mean-time
derivative, and contracts a bracket when Newton would make insufficient progress.
Its 256-iteration bound and representable annual-yield limits prevent open-ended
iteration. Premium bonds may produce negative yields above minus frequency;
public PRICE retains its nonnegative-yield domain and now accepts the zero-yield
limit. No-root, unrepresentable-yield and zero-time degeneracies produce ordinary
calculation errors, not unsupported-function status. PRICE keeps its original
extra frequencies dividing 12; YIELD and duration keep frequencies 1, 2 and 4.

All dated duration entry points retain the source algorithm's YEARFRAC timing of
the final payment; intermediate payments have integer period spacing. They do
not silently use PRICE's coupon-period exponent. Both retained Calligra's
`duration` helper and LibreOffice's GetDuration use that clock:
https://raw.githubusercontent.com/LibreOffice/core/master/scaddins/source/analysis/analysishelper.cxx
The three retained DURATION_ADD/MDURATION expressions pass their original
expectations. This does not claim all historical numerical results are identical,
particularly where the shared native YEARFRAC has the nine recorded differences
in the calendar milestone above.

There are explicit interoperability limits. PRICE follows retained/OpenFormula
exponential discounting even with a single payment; Microsoft's documented
single-payment PRICE branch uses a separate formula. The current published Excel
DURATION and MDURATION examples also differ from the retained/native calendar
clock. Both published rounded values, their source URLs, and the independently
calculated native values remain in the reference report as compatibility
observations; they are not relabeled as matching examples or omitted:
https://support.microsoft.com/en-us/excel/functions/price-function
https://support.microsoft.com/en-us/excel/functions/duration-function
https://support.microsoft.com/en-us/excel/functions/mduration-function

`bond_reference.py` checks 1,028 cases using 90-digit mpmath and explicitly
enumerated cash flows, independently of the native binary-series evaluator:
three retained expressions, 540 price/duration cases, 180 retained-clock cases,
180 inverse yields, five negative yields, 90 extreme-rate cases, three extreme
inversions, an unrepresentable root, a year 1–9999 monthly span, three retained
frequency extensions, six documented/degenerate cases, two published-clock
compatibility observations and 14 namespace aliases. It covers zero/near-zero
rates, zero coupons, rates through 1e300, near-minus-frequency yields and a
single-payment zero-day-count degeneracy. The native core suite also checks typed
arrays, error/partial-status propagation and dependent edits.

Independent odfpy/openpyxl producers cover ODS and both Excel date epochs. Native
price, inverse yield, all duration entry points, zero-coupon values and dependent
results replace deliberately stale caches. Supported errors are completed;
unknown/odd-period formulas and their dependent fallback caches remain intact.
The Excel producer uses DURATION in place of the Calligra-only DURATION_ADD name,
and PDURATION in place of the three-argument DURATION extension. This makes the
input syntax valid for Excel while retaining the recorded numerical limits.
Older financial, dated-cash-flow, calendar and coupon producer families are also
rerun after moving their unsupported examples to the genuinely unported odd-period
function. Original imported files and unrelated package members remain intact.

GUI checks edit the yield, verify price/yield/duration recalculation and actual
painted numeric magnitudes, undo, redo, save and reopen. Both Release and
ASan/UBSan builds run the focused document/GUI workflow; core leak detection is
enabled and third-party GUI leak detection is disabled. Evidence is under
`/home/blux/de-xr/artifacts/calligra-native-bonds-20261029/`.

```sh
uv run --with mpmath python3 fltk/tests/bond_reference.py
uv run --with mpmath --with odfpy --with openpyxl python3 fltk/tests/bond_producer.py /tmp/calligra-bonds
uv run --with mpmath --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/bond_ui.py
```

Odd-period bonds, remaining dated financial functions, currency conversion, full
calculation settings/model/style handling and the rest of the office engines are
still incomplete. This milestone is not full original-engine or Excel parity.

Both final builds pass all 29 core suites and all five numerical reference
families (33,440 cases per build). All 14 producer inputs per build pass after
the final duration-clock correction, and the focused GUI workflow passes again.
A private native Wayland session verifies all three applications with system
FLTK: Words table/caption pixels and page navigation, Sheets bond values and
painted numeric magnitudes with native keyboard selection, and Stage table
rendering and undo/redo. Maps show no Qt; GTK3 still loads through libdecor.
Software GL results do not establish hardware or spatial XR readiness, and no
active desktop/display-manager session is restarted. Installation is verified
separately against the exact staged manifest.


## Remaining retained financial registrations — 2026-10-29

The native financial catalogue now matches all 64 active original registrations,
including ACCRINT, AMORDEGRC/AMORLINC, EURO/EUROCONVERT, ORG.OPENOFFICE.CONVERT,
ODDLPRICE/ODDLYIELD, PRICEMAT/YIELDMAT and the three treasury-bill functions.
The earlier source inventory counted only unqualified uppercase names and missed
the dotted currency-conversion entry. The new audit strips C++ comments, includes
dotted names and compares the complete original/native sets: 64 each, no missing
or spurious registrations. Commented-out ODDFPRICE/ODDFYIELD are not treated as
active upstream capabilities. This audit proves handler coverage, not the full
calculation/value/settings model or every numerical behavior of the old engine.

Accrued interest retains the old standalone financial day-count helper, including
its unusual within-year February adjustment and start-year Actual/Actual divisor.
The native port validates First as a date but, like the retained calculation,
does not use it to partition coupon periods. It preserves the registered six/seven
arguments; the newer eight-argument CalcMethod is not implemented. Maturity and
odd-last formulas use shared YEARFRAC and retain their domain/frequency rules.
The frequency cancels algebraically in the retained odd-last equations.

Treasury functions preserve the source's 30/360/inclusive-day conventions and
its separate horizon/integral-fraction exclusions. This is deliberately not a
claim of OpenFormula's actual/360 treasury-bill calculation or modern Excel parity.
Source examples that document these differences remain in the corpus. Shared
calendar YEARFRAC differences from the older helper remain documented above.

French amortization retains the original first-period fraction, degressive
coefficient bands, whole-unit rounding and terminal recurrence behavior. AMORLINC
retains its whole-array rejection instead of mapping it element by element. A
zero cost/rate gives zero depreciation, avoiding the old undefined float-to-integer
conversion. Period calculations use bounded numeric comparisons instead of
unchecked integer casts. A rounded recurrence fixed point resolves distant
periods directly; otherwise 100,000 iterations produce an incomplete #LIMIT!
so the original cache survives. The core suite checks this distinction.

Historical currency conversion retains the fixed table and destination decimals,
case-insensitive EURO/EUROCONVERT, uppercase-only/full-precision legacy CONVERT,
the LUX compatibility alias, identity conversions and optional triangulation.
Large requested precision cannot overflow a power or integer conversion. The
constants are historical entry rates, not market exchange-rate information.
The original table is checked against the historical ECB appendix and the
OpenFormula currency-conversion specification:
https://www.ecb.europa.eu/pub/pdf/other/the_first_euros_2015en.pdf
https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part4-formula.pdf

`financial_completion_reference.py` checks 2,699 cases: all 331 retained expressions
for these entries, 200 maturity values, 200 odd-last values, 100 accrued-interest
cases, 200 amortization cases, 300 treasury cases, 882 historical currency pairs,
441 triangulations, 36 namespace aliases, six bounded/identity cases and three
binary64 coefficient/half-unit boundaries. It uses
Python Gregorian dates, rational day counts and Decimal arithmetic with explicit
half-away rounding, independently of the C++ implementation. All retained
expressions match within their original macro tolerances; no retained deviations
are omitted. The complete-name audit runs with the same corpus.

Independent odfpy/openpyxl producers cover all thirteen new entries in ODS and
both Excel epochs. Actual dates and all supported financial results replace stale
numeric caches. The Excel producer substitutes standard EUROCONVERT for the two
Calligra-specific currency entry points. Unknown/odd-first formulas and dependent
IFERROR values retain their original caches. Previous financial/cash-flow/calendar/
coupon/bond producer families now use the genuinely unregistered ODDFPRICE for
unsupported-cache checks, while new supported odd-last results have direct tests.
The imported originals and unrelated package members remain intact.

The focused GUI edits the principal, checks the full 21-cell row including dates,
currency and depreciation results, undoes/redoes, saves and reopens. Core tests
also cover typed arrays, AMORLINC's array rejection, dependencies, domain errors,
partial-status propagation, zero-rate behavior and the degressive work limit.
The full document/calculation model, remaining statistical functions and complete
interoperability remain outside this completed registration milestone.

```sh
python3 fltk/tests/financial_completion_reference.py
uv run --with odfpy --with openpyxl python3 fltk/tests/financial_completion_producer.py /tmp/calligra-financial-completion
uv run --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/financial_completion_ui.py
```

Artifacts are under
`/home/blux/de-xr/artifacts/calligra-native-financial-completion-20261029/`.

The principal-edit GUI caught a source-precision boundary in AMORDEGRC: retaining
an unrounded wider rate made .15 * 2.5 * 1316 round down instead of producing the
source's 494. The coefficient selection and recurrence now explicitly retain
binary64 intermediates, as in the source. Regression cases also cover rate .2
at the five-year coefficient boundary and rate 1/6 at the six-year boundary.
The independent Decimal reference models these binary64 boundaries explicitly.

The audit also compares registered argument ranges, normalizing the upstream
unbounded NPV list to the native engine's 255-argument limit. The only widened
range is the documented DURATION dated overload; other retained argument ranges
match. Final Release and ASan/UBSan builds pass all 30 suites, with core leak
detection enabled. All six numerical families pass 36,139 cases per build, and
all 17 producer inputs per build pass. The corrected principal edit/undo/redo/save/
reopen GUI passes in both builds; GUI leak detection is disabled for third-party
allocations. Private native Wayland checks cover all three applications, including
accrued-interest and historical conversion values selected by keyboard. Process
maps use system FLTK and no Qt; GTK3 remains dynamically loaded by libdecor.
Software GL is not hardware/spatial XR validation. Installation remains separately
verified against the exact stage, without restarting active sessions.

## Workbook-owned date calculation context — 2026-10-29

The native evaluator now takes an explicit per-engine reference date. ODF
`table:calculation-settings/table:null-date` is loaded before cell parsing and
applies to date literals/datetimes, DATE, YEAR/MONTH/DAY, YEARFRAC/DAYS360 and
all dated financial families, including mixed serial/ISO cash-flow arrays and
typed coupon results. Engines own immutable copies; evaluating or loading another
workbook cannot change an existing workbook's epoch. Missing/invalid date
settings follow the retained loader's default fallback. Other settings and
unknown metadata are preserved, without claiming that their behavior is ported.

XLSX 1904 imports now generate a 1904-01-01 null date. Previously only styled
date cells were rebased, leaving unformatted serials and formula constants in a
different epoch. Raw serials, DATE results and styled dates now agree, including
after ODS save/reopen. Default/1900 import normalization and its unsupported
phantom/early-date cases are unchanged. Negative serials are valid when their
civil date remains within years 1–9999. Date extraction also accepts ISO strings.
Complete original DATE normalization, locale parsing, Boolean/empty coercion,
reference-year handling and the remaining calculation settings are still work
for the full engine port.

The 31 Release and ASan/UBSan CTest suites pass, with leak detection enabled for
core tests. The new suite interleaves default and five other engine epochs,
checks every dated function family, numeric/civil mixtures, typed output,
range errors, invalid settings, independently open documents, saved caches,
unsupported-cache retention and reset on reload. Independent odfpy/openpyxl
producers add seven ODS epochs (years 1–9999) and both XLSX date systems, with
Python Gregorian/date and explicit financial references. All nine documents
pass in both builds, including saved metadata/cache inspection and full model
comparison after reopen. The previous 17 producer documents and 36,139 numeric
references also pass in each build, retaining the previously documented nine
calendar deviations and bond/Excel convention differences.

`calculation_settings_ui.py` edits the principal in an ODS workbook whose epoch
is 2000-01-01, visits all 21 calculated cells, verifies actual painted numeric
labels, undo/redo, save/reopen, typed coupon dates and unchanged unrelated ZIP
members. Release and sanitizer flows pass. The initial new producer expectation
omitted first-period rounding in AMORDEGRC; it was corrected to the retained
algorithm before the final passing runs. GUI leak checks remain disabled for
third-party runtime allocations; core leak checks remain enabled.

References: [ODF 1.4 Part 3 §9.4.2](https://docs.oasis-open.org/office/OpenDocument/part3-schema/OpenDocument-v1.4-os-part3-schema.html),
retained `CalculationSettings`, `SheetsOdfDoc` and `Value::asDate` implementations.
Reproduce the new independent documents and GUI flow with:

```sh
uv run --with odfpy --with openpyxl python3 fltk/tests/calculation_settings_producer.py /tmp/calligra-date-context
uv run --with odfpy --with openpyxl --with Pillow --with python-xlib python3 fltk/tests/calculation_settings_ui.py
```

Evidence: `/home/blux/de-xr/artifacts/calligra-native-date-context-20261029/`.


## OpenDocument export and CUPS print baseline — 2026-10-29

The user's acceptance baseline is OpenDocument export and CUPS printing; full
upstream editing/layout/calculation/filter parity is optional future work.
The new native print backend uses Cairo PDF, retained native glyph positions
and FreeType faces (including font-instance lifetimes/variations), existing page
and frame/table layouts, slide geometry and the shared bounded image decoder.
Scalable text remains vector output; bitmap fonts retain the native alpha-mask
behavior. Sheets paginates a calculated-value grid across all/current worksheets.
This is the existing native model's supported content, not full upstream print
style/filter fidelity.

File/Print and Ctrl+P use an FLTK printer dialog. Printer selection, copies,
validated page ranges, all/current worksheets, cancellation, unsupported-feature
confirmation and submission errors are handled without a GTK print dialog or
Qt. CUPS client commands receive separate argv elements without a shell. Print
PDFs are completed/synchronized before atomic replacement and before submission.
The shared temporary-file owner now removes only files it actually created;
a rejected PDF filename cannot delete an unopened template path.

The 32 Release and ASan/UBSan core suites pass with core leak detection enabled.
Independent odfpy fixtures produce two Words pages with Unicode/table paint,
five worksheet tiles including a second sheet, and two slides with vector/text/
transparent PNG content. Exported ODT/ODS/ODP packages reopen and print again;
the source files remain unchanged. All nine PDF pages are rasterized with
Poppler and inspected visually. The PDF engine's image surfaces own their data
through Cairo's complete recording lifetime. Real CUPS schedulers, running as
the regular user on private loopback ports, accept and complete jobs through a
capture backend. These tests do not add/change system queues or print to a
physical printer. The real keyboard workflow opens the FLTK dialog and submits
from each application. GUI sanitizer runs disable third-party leak checking;
headless/core sanitizer checks keep it enabled.

OpenDocument ZIP checks verify the first, uncompressed mimetype entry with no
local/central extra data. odflint's Python 3 comparison of `zi.comment` (bytes)
against an empty string produces a false header warning. The artifact copy of
the linter corrects only that comparison to `b""`; the exported files pass its
remaining checks unchanged. No export packaging defect was found or patched.

References: [CUPS printing/options](https://www.cups.org/doc/options.html),
[Cairo PDF surfaces](https://www.cairographics.org/manual/cairo-PDF-Surfaces.html)
and [FreeType font ownership](https://www.cairographics.org/manual/cairo-FreeType-Fonts.html).
Reproduce the print/export and private CUPS tests with:

```sh
uv run --with odfpy --with openpyxl --with Pillow --with pypdf python3 fltk/tests/print_producer.py /tmp/calligra-print --cups
uv run --with odfpy --with openpyxl --with Pillow --with pypdf --with python-xlib python3 fltk/tests/print_ui.py --artifacts /tmp/calligra-print-ui
uv run --with odfpy --with openpyxl --with Pillow --with pypdf --with python-xlib python3 fltk/tests/print_wayland.py /tmp/calligra-print-wayland
```

Evidence: `/home/blux/de-xr/artifacts/calligra-native-print-baseline-20261029/`.

The final print controls are a modal FLTK panel in the existing GPU window.
A separate transient toplevel exposed a DomeD visual-position/hit-test mismatch;
the in-window panel avoids that mismatch and keeps the document's GPU driver.
Final native Wayland tests use actual pointer clicks on Print in all three
applications and complete CUPS jobs. X11 Release/sanitizer tests also check
Escape cancellation and CUPS job attributes for copies/page ranges. Shared
editing/save/menu, Office-to-ODF conversion, native image and tab/leader GUI
regressions pass. Physical printer output has not been exercised because no
system printer is configured.

Printing does not commit edits into read-only documents. The GUI path skips
model mutation for protected/signed input; const print rendering remains
available while OpenDocument saving continues to preserve read-only protection.

Installed launchers default to Wayland. A final headless Stage test found that
FLTK's default image driver tried to connect to Wayland while decoding PNG data.
The batch-only branch now selects the nonconnecting X11 graphics driver for
CPU image decoding; it never creates a window or opens an X display. GUI paths
still default to native Wayland. The independent batch test explicitly exports
FLTK_BACKEND=wayland with both display variables absent to cover this launcher
case, including Stage image content and CUPS submission.

## Words structural editing — 2026-10-29

Implementation priority is now Words layout/editing, then Sheets functions,
with Stage last. This increment replaces Words' frontend text snapshots with
native XML transactions. Enter and paragraph deletion preserve inline styles,
links and whitespace in body, section and same-cell paragraph splits/joins;
Shift+Enter inserts an inline line break. Continuations follow next styles and
clear inherited forced page/master assignments. Copied style chains retain
parent-relative font sizes instead of flattening them. Equivalent adjacent
inline runs are compacted, preventing one span per typed key.

The native suite checks Unicode offsets, IDs, next styles, inherited relative
sizes, continuation-style reuse, byte-exact XML undo/redo, branching saved-state
tracking, selection restoration, bounded command history, opaque ZIP retention,
failed loads and atomic rejection of cross-cell/list/conditional-style edits.
All 33 core suites pass in Release and ASan/UBSan, with leak checking enabled
for the core suites.

The independent odfpy keyboard test covers Enter/Backspace in cells and
sections, Shift+Enter, saved checkpoint traversal, selection/caret restoration,
rejected cross-cell joins/list splits, continued typing after rejection,
saved XML inspection and paginated reopen. It passes on private X11 and native
Wayland sessions using system FLTK with no Qt mapped, and under ASan/UBSan on
X11 (third-party GUI leak detection disabled). Page-view screenshots were
checked after editing and reopening. Real input caught and fixed FLTK moving
the caret before its predelete callback and after a rejected edit; caret state
is now captured before the event and restored after FLTK finishes it.

```sh
ctest --test-dir build-native --output-on-failure
uv run --no-project --with python-xlib --with Pillow --with odfpy python \
  fltk/tests/words_edit_ui.py --artifacts /absolute/path/words-x11
uv run --no-project --with python-xlib --with Pillow --with odfpy python \
  fltk/tests/words_edit_ui.py --wayland --artifacts /absolute/path/words-wayland
```

The shared GUI regression edits each original paragraph when testing style/page
preservation. Selecting and deleting the entire document correctly removes
those old paragraph/page-break structures, so that operation no longer stands
in for editing the existing paragraphs. Full rich editing, structural list/table
tools, typing groups and incremental XML operations remain unfinished, along
with the Words layout work listed in PORT_STATUS.md.

## Words list paragraph editing — 2026-10-29

Words now splits paragraphs into sibling list items and joins compatible
adjacent items in the same list. A split retains the original item's ID and
restart; new items retain its style override while receiving new numbering.
Following unselected paragraphs/sub-lists travel with the suffix. Header
continuations become numbered items without changing the original header.
Joins retain the first item's numbering and the final item's trailing blocks,
and discard removed producer label caches. The native reader resolves each
inherited sub-list style before a move; named/paragraph-derived styles stay
explicit in their new scope, and an unnamed native default is materialized when
needed. This prevents a destination item's override from restyling the moved
sub-list. Hidden/nested intervening structures
are rejected rather than consumed by the flattened editing view. Structural
list changes run the native flow validator before committing, including checks
for overflow in consecutive or continued counters.

The new native-words-lists suite checks rich Unicode runs, header/continuation
splits, multiple pasted breaks, style overrides/restarts, nested suffixes,
same-cell list edits, joins, continued counters, exact XML undo/redo and atomic
rejection of invisible structures/counter overflow. The odfpy keyboard fixture
now contains a header, restarted numbered items, an unnumbered paragraph,
styled inline text, a nested list and a Roman style override. It independently
inspects the saved XML and generated page labels after Enter/Backspace, checks
undo checkpoints and nested-list boundary rejection, then saves/reopens the
paginated result. All 34 core suites pass in Release and ASan/UBSan, including
leak checking for the core suites. The source and expanded fixture follow
[ODF 1.4 list/item/header and continuation semantics](https://docs.oasis-open.org/office/OpenDocument/v1.4/OpenDocument-v1.4-part3-schema.pdf).

Empty-item exit, list indent/outdent and formatting controls remain the next
Words editing requirements; this increment does not complete structural lists
or advance Stage implementation.

## Words list levels and empty-item exit — 2026-10-29

Native XML commands now indent/outdent whole list items and same-level ranges,
including their descendant content. Tab/Shift+Tab uses real paragraph boundaries;
ordinary text tabs and tabs after inline breaks keep their text-editing behavior.
The universal Format → List menu also acts from inside an item. A first item can
be indented under a structural header without inventing an editable paragraph.
Top-level outdent is a no-op. Enter on a sole empty root item removes numbering;
a terminal nested empty item can leave all list levels. List tails carry explicit
continuation anchors, and later references to the original whole list follow the
tail. Body text/order, styles, item/paragraph IDs, selection, saved-state identity
and XML undo/redo are preserved. Outdent before trailing parent text and complex
empty/nested continuation exits still fail atomically; they remain part of the
unfinished paragraph-level editor.

The native-words-levels suite adds range endpoints, descendant deduplication,
root/nested/first item cases, same-cell/section behavior, continued/consecutive
counters, ID collision avoidance, read-only protection, metadata/CDATA edits,
saved XML undo and unsupported-operation rollback. All 35 core suites pass in
Release and ASan/UBSan with core leak checking. Real X11/Wayland keyboard tests
exercise Tab/Shift+Tab, selected ranges, menu actions, rendered indent positions,
Enter twice, formatting-only save checkpoints and ordinary tabs. The GUI
sanitizer run disables third-party leak detection.

Strict, version-matched OASIS schema checks caught a nonconforming ID anchor
during development. It now uses text:meta instead of assigning xml:id to a span.
Native rendering treats metadata as transparent; empty anchors are editable and
retained by both replacement APIs. Text-bearing metadata edits remain restricted.
The independent odfpy fixture now declares table columns and uses IDs only on
elements that allow them. The older tolerant-preservation suites still exercise
nonstandard input separately. Saved intermediate core and GUI packages validate
their actual content.xml/styles.xml against the corresponding official schemas;
validation does not remove attributes or upgrade the document version to pass.
Schema checks cover structural headers, empty ID-anchor lists, continued tails,
metadata anchors and undo/redo exports, as permitted by the
[ODF 1.4 schema](https://docs.oasis-open.org/office/OpenDocument/v1.4/os/schemas/OpenDocument-v1.4-schema.rng).

```sh
build-native/fltk/calligra-native-words-levels-test --artifacts /new/core-exports
uv run --no-project --with lxml python fltk/tests/odf_schema.py \
  --schema-dir /path/to/official-schemas --output /path/to/results.json \
  /new/core-exports /path/to/words-gui-artifacts
```

The local schema directory uses names such as OpenDocument-v1.2-schema.rng and
OpenDocument-v1.3-schema.rng. The validator is offline; source URLs and hashes of
the official schema files are recorded with the local verification artifacts.

## Words compound editing transactions — 2026-10-29

Selection replacement previously issued separate native deletion and insertion
commands. If deletion was rejected across a section/table boundary, FLTK still
inserted the new text; if insertion failed after a successful deletion, the
deleted text and redo branch were lost. Words now defers the history update
until the complete frontend buffer event succeeds. On failure it restores the
original package and event selection after FLTK's final caret update. Accepted
replacements take one Undo. Paste requests from Ctrl+V, Shift+Insert, the
universal menu and the standard FLTK context menu wait for clipboard delivery
before removing selected text. Empty clipboard events leave it intact.

The native-words-transactions suite checks Unicode replacement as one command,
exact styled XML undo/redo, saved-state identities, failure on the first and
later callbacks, suppression of mutations after cancellation, explicit cancel,
invalid final selections, bulk/load failure, no-op events, save/history/load
boundaries and signed-document protection. Exports retain opaque ZIP entries.
The independent odfpy GUI fixtures check accepted and rejected replacements
through real keys and all four paste routes. A conditional page style forces
insertion failure after deletion; text, selection, modified state and a prior
redo branch must all survive. Existing paragraph/list/layout regressions still
run. Multi-event drag/drop and cross-keystroke typing coalescing remain outside
this increment.

All 36 core suites pass in optimized Release and ASan/UBSan (including core
leak checking). The expanded Words GUI test passes under system FLTK on X11,
native Wayland, and ASan/UBSan on X11 (third-party GUI leak checking disabled).
Version-matched official OASIS schemas validate the retained core and GUI
content.xml/styles.xml exports without rewriting them.

## Words character and paragraph formatting — 2026-10-29

Words now applies local native ODF text/paragraph styles through FLTK and the
universal Format menu. Character controls cover bold, italic, family, size and
color; paragraph controls cover alignment, margins/indent, spacing, keep rules,
page-before and widow/orphan limits. Mixed dialog fields stay unchanged unless
the user chooses a value. A caret holds a transient typing patch; empty
paragraph defaults persist in ODF after Enter or deletion. Event cancellation
restores the typing patch with the document. Formatting keeps text, links,
metadata, source paragraph IDs and archive entries. Simple direct spans are
split/merged through reusable styles instead of acquiring unbounded wrappers.
Common and automatic styles, family/name collisions and namespace aliases keep
their scope. Output lengths use decimal lexical forms, including small values.

The native-words-format suite adds Unicode ranges, inherited/mixed weights,
cell-inherited fonts, metadata/link preservation, repeated toggles, paragraph
ranges, caret/empty-paragraph defaults, saved XML undo/redo, malformed input,
read-only protection and compound rollback. Strict schema checks cover retained
exports against their actual ODF version. The expanded odfpy keyboard fixture
uses both character shortcuts, Font and Paragraph dialogs, choices and numeric
fields, alignment, cancellation/unchanged Apply, native page rendering and
caret typing. UI checks exposed and fixed a menu-label slash interpreted as a
submenu, a modal D-Bus menu reply delay, and FLTK Select All leaving an interior
insertion caret that could truncate native selection restoration. Dialogs now
open after the menu reply; Select All uses an endpoint, and context ranges are
normalized. Tests wait for the reported full selection before editing it.

All 37 core suites pass in optimized Release and ASan/UBSan with core leak
checking. The editor remains a plain text input buffer with a separate rich
Pages view. Direct paginated editing, more formatting properties and complete
style/layout fidelity are still required. Source semantics follow the
[ODF 1.3 text/span and style model](https://docs.oasis-open.org/office/OpenDocument/v1.3/OpenDocument-v1.3-part3-schema.html);
exports are also checked against the version-matched OASIS schemas.

The final UI checks pass on system-FLTK X11, native Wayland and ASan/UBSan X11
(with third-party GUI leak detection disabled), along with the shared suite GUI
regressions. The font fixture uses the installed Hack italic face so the print
check can verify the actual embedded face. `words_format_print.py` takes the
retained UI snapshots, checks colored 22-point glyph pixels and the embedded
italic font, verifies paragraph geometry, exports/reopens ODF and compares all
rendered pages to the actual CUPS captures. Its six private CUPS jobs include
the unchanged three-application baseline. The nine baseline raster pages remain
byte-identical to the accepted reference; no system printer is changed.

```sh
uv run --no-project --with odfpy --with Pillow --with pypdf python \
  fltk/tests/words_format_print.py --ui-artifacts /path/to/words-ui \
  --artifacts /new/print-results --bin-dir /usr/local/bin
```

## Font validation and styled empty lines — 2026-10-29

A real Font-dialog repro accepted `Hack'`, saved it, then failed native printing
with "Invalid quoted document font family". Edit validation and resolution now
share one quote/comma parser with the existing 32-family limit. Explicit edits
also reject lists containing no names. Font and Paragraph panels retain invalid
fields and show the error inline, allowing correction or cancellation without
changing XML, selection, revision, saved-state identity, redo or an active typing
patch. Select All clears a transient typing patch through keyboard and menu.

A separate native regression reproduced leading/interior blank lines resolving
the paragraph font instead of the styled explicit break. Empty ranges inside
source text now select the source run with a bounded lookup; terminal ranges
retain the paragraph default. Nonempty smaller text already used its run's
metrics, so the paragraph default is not rewritten to obtain smaller lines.
Native tests cover quoted commas, Unicode names, both mismatched quotes,
32/33-family boundaries, malformed caret/range edits, compound rollback, blank
line metrics, smaller runs and terminal defaults. All 37 Release and ASan/UBSan
core suites pass (with core leak checking). Real X11/Wayland and sanitizer GUI
checks exercise invalid font/size/color/spacing correction, Cancel, quoted
fallback rendering, selection and undo/redo, plus the shared GUI regressions.

The print integration test now runs eight private CUPS jobs. Its quoted-font
snapshot must embed Hack-Bold; its odfpy styled-break fixture (paragraph 24pt,
visible runs/breaks 6pt) must produce exactly the same pixels as a uniform 6pt
reference. Export/reopen and CUPS capture pixels must also agree. The original
nine Words/Sheets/Stage baseline pages remain byte-identical. Schema checks use
the actual ODF version and do not rewrite exports to pass validation.

## Native page caret positions — 2026-10-29

Native shaping now retains visual cluster origins, direction and Unicode
extended-grapheme stops. Single-glyph ligatures use matching GDEF caret data;
missing, mismatched or unusable data falls back to equal grapheme advances.
The Noto Sans ffi test distinguishes its real 315/631 design-unit positions per 1000-unit em
from equal thirds. Tests also cover combining/Indic/emoji sequences, bidi
boundary affinity, wrapping, blank lines and tab-containing blank paragraphs.
Layout carries these positions through RTL trailing-space offsets, each tab
alignment and justification. Native page hit testing retains original source
identity across repeated table instances and respects cell clips.

Double-clicking Pages now enters Edit at the clicked scalar position instead
of the paragraph start. Real system-FLTK X11, native Wayland and ASan/UBSan X11
checks exercise a multibyte source position, insertion, undo/redo and saving.
The Wayland harness waits for initial resizing and sends a true rapid double
click; two slower single-click helper calls did not meet its double-click
threshold. Right-cell and split/merged/nested-cell UI regressions also pass.

All 38 core suites pass in optimized Release and ASan/UBSan with core leak
checking. GUI sanitizer leak checking remains disabled for third-party GUI
libraries. Eight private CUPS jobs pass, formatted exports/reopens match their
captures, and the original nine baseline PNGs remain byte-identical. Saved
packages are validated offline against their actual ODF version's schema.
The page canvas still does not accept text or paint an editing selection;
these caret/source APIs are the foundation for that next Words increment.


## Direct native Words page editing — 2026-10-30

Pages now retains native caret and range selection while typing, editing,
formatting and using the clipboard. Mouse hit testing, dragging, word/paragraph
selection, visual bidi arrows, vertical preferred position, word/paragraph
movement and grapheme deletion use the native shaped source positions. Page
reflow retains scroll and reveals the caret; Ctrl+Home exposes the first page.
Paragraph splits/joins, soft breaks, lists, XML history, saved state and signed-
package read-only protection share the existing native transaction model.
Escape is consumed in the editor so cancelling composition/selection cannot
invoke FLTK's default window-close behavior. Read-only Pages still forwards
global window shortcuts.

A real Fcitx/Anthy test runs on a private DomeD Wayland connection and D-Bus
session. It verifies acknowledged preedit, Japanese conversion, candidate
anchoring, commit as one undo step, redo, cancellation restoring exact XML and
saved state, repeated Escape leaving Words open, and ODF saving. This does not
establish every IME or input protocol. Full keyboard regressions run on X11;
the focused native page flow runs on Wayland with IME and on ASan/UBSan X11.
Real pointer drags and signed-package editing refusal are included. Split,
merged and nested cell continuations, repeated headers, and Words/Stage tab
leader regressions also pass with system FLTK.

All 39 core suites pass in optimized Release and ASan/UBSan, with core leak
checking enabled. GUI leak checking remains disabled for third-party libraries.
The native selection suite covers source scalar/grapheme/word maps, GDEF
ligature interiors, disjoint bidi ranges, tabs, explicit and paragraph breaks,
clipping, and strong RTL line ends after a numeric run. Wrapped shaped lines
retain shared source text after their shaper is destroyed.

The IME print check exposed glyph-to-Unicode ambiguity: the font's 日 glyph
was reverse-mapped to radical ⽇, and decomposed accents were collapsed. Native
printing now passes exact source UTF-8 and HarfBuzz cluster mappings through
[Cairo's text/glyph API](https://www.cairographics.org/manual/cairo-text.html#cairo-show-text-glyphs)
for scalable-font text. Cluster order is kept intact even when mark ink overlaps
its neighbor. A separate odfpy fixture checks Poppler extraction and independently inspects
encoded PDF ActualText for ligatures, NFC/NFD accents and Japanese/radical
characters. The current pypdf extractor ignores ActualText and still returns
font aliases; its raw extraction is retained as a diagnostic limitation. Decorative tab
leaders remain glyph-only. No Pango or toolkit dependency was added.

Eleven actual private CUPS jobs pass, including native page-edit and IME
snapshots and the Unicode-source fixture. Every formatted PDF raster matches
its reopened ODF and captured CUPS job; all nine accepted suite baseline pages
remain byte-identical. Saved documents pass version-matched OASIS schemas,
with no normalization or attribute stripping. No system printer is changed.

Further work includes native default-view integration, drag-scroll repetition,
typing/reflow performance and more complete layout/formatting fidelity. Words
remains the priority, followed by Sheets functions; Stage stays last.
