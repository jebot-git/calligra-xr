# Native engine milestone — 2026-10-29

The user-defined acceptance baseline is now **OpenDocument export and CUPS
printing** for the Qt-free system-FLTK applications. Full upstream functional
parity is optional future work. Save/Save as and batch export write ODT/ODS/ODP;
a native PDF print backend and FLTK CUPS dialog cover all three applications.
Words prints native pages, Stage prints slide scenes, and Sheets prints a
paginated calculated-value grid with all/current worksheet selection. Copies,
page ranges, cancellation and CUPS submission errors have explicit handling.
That baseline remains the minimum acceptance floor. The current implementation
priority is **Words layout and editing first, Sheets functions second, Stage
last**. The remaining-work inventory records the unfinished scope; the baseline
does not imply that Words or the full suite is finished.

The first Words editing increment adds immediate native document transactions,
styled paragraph splits/joins in the body, sections and individual table cells,
explicit soft breaks (Shift+Enter), XML undo/redo and saved-state/selection
restoration. Paragraph continuation follows next-style declarations and clears
inherited forced page/master assignments while retaining other formatting.
Cross-container joins and unsupported embedded-object paragraphs are rejected
without changing the document.

List paragraph editing now creates real sibling items on Enter, including from
a list header or an additional unnumbered paragraph. Following paragraphs and
nested lists move with the suffix. New items inherit style overrides without
copying restart values or XML identities. Backspace/Delete can join adjacent
items of the same list while retaining the final item's trailing content;
generated and continued counters are validated before the change commits.

Words now also provides item/range indent and outdent through Tab/Shift+Tab and
the universal Format → List menu. Enter leaves a sole empty root item or a
terminal nested item. These commands preserve paragraph text/order, selection,
IDs, styles and native undo checkpoints; split list tails retain continuation
references. More complex paragraph-level list cases remain unfinished below.

Words also has native character and paragraph formatting through the universal
Format menu. Bold/italic, font family, size and color act on Unicode ranges or
future typing at a caret; empty-paragraph defaults survive save/reopen.
Paragraph controls cover alignment, margins, first-line indent, spacing,
keep rules, page breaks and widow/orphan limits. Direct styles preserve source
runs, links, metadata and structural containers. Formatting shares native XML
undo/redo and saved-state tracking. The legacy Edit view remains a plain text
buffer; Pages displays document formatting and now accepts direct editing.

Native Words page editing provides a grapheme-safe caret, visual bidi/ligature
navigation, vertical and word/paragraph movement, GPU range selection, mouse
dragging, double-click words and triple-click paragraphs. Typing, clipboard,
formatting, list actions, paragraph splits/joins and soft breaks share the
native XML transactions and undo history. Reflow preserves scrolling and
reveals the caret, including in repeated table instances. Native Wayland Fcitx
preedit/commit/cancel uses one history transaction and anchors candidates to
the page caret. Escape cancels composition/selection without closing Words.
Further native editing polish, performance and default-view integration remain;
Edit is still the initial view and inactive Pages keeps preview navigation.

The default Words/Sheets/Stage document, XML/package and spreadsheet calculation
paths now use native C++20, libxml2, libzip and the installed system FLTK. They
require no Qt or KDE headers/libraries. The upstream Qt/KDE build remains an
explicit opt-in. No fake Qt shared library or static Qt copy is used.

Words now has a native ODF style/page-flow model and an FLTK GPU page view,
with Unicode wrapping, master continuation, keep rules and widow/orphan control.
Its new native HarfBuzz/FreeType path provides ligatures, kerning, joining and
mark placement, document-font/style selection, script fallback and ICU bidi
visual runs. The page display uses those glyph positions and a GPU glyph cache.
Its full original frame/text-layout behavior remains unported. Stage now has a
native scene model, referenced master/page-layout resolution, inherited basic
paint, ordered groups, affine transforms, inverse hit testing, basic vectors,
transformed images/text and nested frame edits. Stage now shares the scoped ODF
paragraph/run reader with Words: rich fonts/colors, bidi, wrapping, spacing and
line heights work inside continuously laid out, padded/aligned text frames.
Words retains document ink and paper colors. The full original document and
shape/animation stacks remain unported; editing retains the existing basic paragraph/slide scope.
Stage now retains native cubic/quadratic/elliptical path segments, analytic
curve bounds, viewport/pixel-density adaptive contours, compound winding fills,
rounded rectangles and partial ellipses. ODP path geometry is preserved on save.
Explicit ODF enhanced geometry now has a native equation/command engine,
modifier references, independent path paint sets, mirrors and horizontal text
areas. Text-bearing supported shapes participate in native selection/editing;
their equations and modifiers are retained by saves.
Sheets now uses a native parser/evaluator rather than the full upstream
engine. It now has typed arrays, 19 additional reference/lookup/matrix functions,
fixed ODF/XLSX matrix-block recalculation and cached-result saving. Calculation status
separates unsupported operations from ordinary errors, including through error
handlers and dependencies. The smaller catalogue is listed in README.md. Unknown
functions return errors and their existing package caches remain intact.
The engineering module now covers all 50 retained upstream engineering names,
including complex arithmetic, units, Bessel/error functions and base conversion.
Complex results retain both coefficients in the native model and save ODF caches
as strings. Independent SciPy, cmath, mpmath and Pint references, the retained
upstream cases, external ODF/XLSX producers and real keyboard edit/save/reopen
checks cover this module. A native financial module now covers all 64 active retained annuity,
amortization, periodic cash-flow, depreciation and related names. It shares
stable near-zero-rate calculations, preserves sparse sequence semantics and
checks convergence/work limits. The original fractional VDB algorithms and
three-argument DURATION extension are retained. XIRR/XNPV now use native date
conversion, paired cash flows, stable scaled sums and safeguarded log-rate
solving. Both Excel epochs are covered by independent documents. Scalar array
entries also accept the expressions used by retained Calligra date formulas.
Real numeric grid labels now fit their column without clipping an exponent.
Shared Gregorian day counts now support YEARFRAC, signed DAYS360 and six
additional dated securities functions. Six native coupon queries now share
maturity-anchored previous/next dates, remaining payment counts and the retained
MSRB/actual/European day conventions; date caches stay typed. Regular-coupon
PRICE/YIELD now share that schedule and a stable scaled cash-flow model. Dated
DURATION/DURATION_ADD/MDURATION use the retained YEARFRAC clock, while the
three-argument DURATION extension remains available. Duration caches are now
calculated. Accrued interest, odd-last/maturity securities, retained treasury-bill
rules, French amortization and fixed historical euro conversion are now native.
A comment-aware inventory includes the previously missed dotted currency entry
and matches all 64 active original financial registrations. This does not port the
complete calculation settings/value/style model or remaining statistical catalogue.
Workbook-owned reference dates now honor ODF null dates across literal dates,
DATE/date extraction, calendar calculations and all dated financial families.
XLSX 1904 imports retain their epoch for raw serials and formula constants as well
as typed dates; the null date persists through ODS save/reopen. Other calculation
settings, locale parsing and complete date-function/value coercion remain unfinished.
The original treasury-bill/accrual and duration conventions, newer unimplemented
CalcMethod/odd-first functions, and documented Excel differences remain compatibility
limits. Bounded degressive depreciation can still report incomplete work rather
than replace a source cache after exceeding its work budget.
Passing the retained document and GUI tests does not establish full function
or office-suite parity. The assessment below distinguishes implemented native
behavior from the remaining full port.

---

# Port status and remaining office-suite work

Baseline: KDE Calligra `edb7aee19cf4c38b55389149ea052982d7a6d41e`, retrieved
2026-10-25. The source reports development version 26.11.70. This milestone
establishes all three FLTK applications; it does not complete the office suite.

| Area | Working in this build | Still required for a full port |
| --- | --- | --- |
| Shared shell | Native Wayland FLTK, OpenGL widgets, DomeD Unicode appearance, universal menus, file dialogs, basic editing shortcuts, OpenDocument export and native CUPS printing | Broader IME/input-method validation, accessibility, session recovery, advanced print/export controls, broad XR workflow tests |
| Words | DOCX paragraph/table import with ODT saving, ODT paragraph text, common/automatic style inheritance, page geometry/master continuation, native HarfBuzz/FreeType shaping and glyph rendering, document fonts and script fallback, ICU bidi line ordering, numeric font weights/standard variable axes, native pagination and GPU page view, ordered native tables, independent split/merged/nested cell cursors, repeated headers/master-width reflow and source cell selection, document ink/page colors, keep/widow/orphan rules, span/link-preserving body/section/cell paragraph splits and joins, explicit soft breaks, item/range list levels and empty-item exit, native range/caret character formatting and paragraph controls, direct page caret/selection/input and Wayland IME transactions, XML undo/redo with selection and saved-state tracking, atomic save | Remaining KWDocument/KoText/textlayout/frame behavior; full font/style/shaping and line-breaking fidelity, embedded fonts, color-font glyphs, synthetic/vertical text, native editing polish/performance/default-view integration, underline/strike/highlight and named-style/list controls, advanced table keeps/header-boundary spans and nested table master behavior, section layout, structural list editing, columns, headers/footers, notes, images/anchors, tracked changes and print/PDF fidelity |
| Sheets | Native C++ parser/calculation engine, typed arrays, reference/lookup/matrix functions, fixed ODF matrices and XLSX array import, dependencies, workbook-specific ODF null dates and retained XLSX 1904 epochs, cross-sheet references, named-range import, multi-sheet ODS and XLSX cell-data import, cell edits, undo/redo, TSV paste, repeated cells, cached formula updates | Remaining upstream calculation catalogue and settings, full core/style model and ODF loader, formatting/date/currency display, selection ranges, merged cells, matrix-block creation/deletion, sort/filter, validation, structural row/column operations, charts, printing and format filters |
| Stage | PPTX direct text/PNG/JPEG import with ODP saving, per-slide ODP master/page layouts, scoped/inherited basic paint, rectangles/ellipses/polygons/lines, native Bézier/arc paths and rounded/partial primitives, explicit enhanced geometry/equations and modifier paint sets, ordered groups and affine transforms, native paragraph/run fonts and colors, bidi and continuous frame layout with side padding/vertical alignment, PNG/JPEG/SVG/GIF/BMP GPU preview, native continuous tables with merged/nested/repeated cells and clipped GPU paint, inverse frame selection and nested edits, slide insert/delete, presentation navigation, package preservation | Complete KPrDocument, KoPADocument and Flake behavior; theme/placeholder fields, structural list/table editing, column and vertical text layout, text auto-growth/fit, advanced typography, custom preset lookup/stretch points, extrusion/text-on-path, connectors/3D objects, advanced fills/strokes, shape editing tools, animation/timing, transitions, speaker notes/view and export fidelity |
| Interchange | ODT/ODS/ODP packages, DOCX/PPTX content import with ODT/ODP saving, XLSX cell-data import with ODS saving, and toolkit-independent upstream ODF formula conversion | Office export and full DOCX/XLSX/PPTX import fidelity, legacy formats, filter/plugin discovery, full external-office interoperability corpus |
| Rendering | Main windows/content widgets use FLTK's GL driver; native Words page display lists and glyph cache; native Wayland GPU tests passed | Full Words text/shape display fidelity, complete Stage shape/3D rendering, hardware/UI profiling, native XR mapping for genuinely spatial content |

## Architecture findings

The earlier Sheets adapter reused the original calculation classes with QtCore
and QtXml. Those classes no longer run in the default build: native C++ value,
formula, document and XML models replace them. The original algorithms and
document/function corpora are references for the remaining behavioral port.
Removing Qt linkage is verified; calculation and document fidelity remain
separate, unfinished requirements.

Words' KWDocument uses KoDocument, KoShapeBasedDocumentBase, frame management,
QTextDocument-based text layout and the Flake shape system. Its existing widget
interfaces cannot simply be linked into an FLTK window. The native paragraph/
style model and page-flow engine now implement basic text pagination without
those classes. Original KoParagraphStyle and KoTextLayoutArea behaviors guide
keep/break/widow/orphan handling, but advanced frame/shape/text behavior remains
unported. The current engine must not be presented as full layout parity.

Stage's KPrDocument derives from KoPADocument, with KoPAPage/Flake shape objects,
master pages and animation plugins. The native scene model now resolves basic
ODP master objects, per-slide layouts, paint and transformed geometry; it does
not run those complete models. Its shared GPU glyph renderer uses native shaping
for frame paragraphs and their resolved inline font/color runs. Paragraph
alignment, margins, line spacing and bidi are shared with Words; side padding
and vertical alignment position continuous text within each frame. Unknown
objects and extension data remain in the saved package, but preservation does
not imply correct display or editing support.

## Implementation priorities — Words, then Sheets, then Stage

1. Finish Words layout and editing. Continue native KoText/text-layout and
   KWDocument behavior: rich formatting/input/selection, structural lists and
   tables, sections/columns, headers/footers, notes, images/anchors and the
   remaining pagination/typography requirements. Preserve loader, style and
   XML undo semantics. Validate real document corpora, OpenDocument round trips,
   print/export, scripts, IME and both compositor realms as each part lands.
2. Complete Sheets functions after Words. Port the remaining original
   calculation catalogue and settings, using the upstream corpora and independent
   references. Retain unsupported source caches until calculation is implemented;
   then address the remaining model/style and filter work in that order.
3. Leave Stage implementation last. Its remaining KPrDocument/master/shape/
   animation behavior and editing tools follow Words and Sheets. Shared-engine
   fixes still require Stage regression checks during earlier work.

Keep native document/geometry/persistence interfaces independent of the toolkit,
and use system FLTK for input and display. Qt6 frontends remain explicit opt-ins.

## Current editing limits

Words provides text editing and a separate native paginated view. Character
controls (Ctrl+B/Ctrl+I and the Font dialog, Ctrl+D) preserve mixed values and
apply only changed dialog fields. A caret changes future typing; an empty
paragraph stores its default in ODF. Navigation clears a transient typing
format, while a cancelled compound event restores it. Invalid character and
paragraph fields stay in their dialog without changing document/history state.
Font-family validation shares the resolver's quote/comma grammar and family
limit, preventing an unmatched quote from being saved and later failing print.
Select All clears transient typing formatting and places the
caret at a range endpoint, and context-menu ranges are normalized before
native selection/history queries. Paragraph alignment has Ctrl+L/E/R/J
shortcuts; its dialog also controls point-based margins/spacing, line-height
multipliers, keep rules, page-before and widow/orphan thresholds. Native
formatting can span sections, table cells and list items without restructuring
them. Fields and other specialized inline objects still restrict character
formatting; their surrounding editable text is supported. Source styles and
unknown properties remain intact. Accepted editor changes immediately update
the native model. Enter splits a paragraph;
Backspace/Delete can join adjacent paragraphs under the same parent, including
within a section or table cell. Shift+Enter creates an inline line break, and
deleting that break preserves the paragraph. Existing span/link styles,
whitespace controls and unique XML identities are retained; equivalent adjacent
runs are compacted so typing does not accumulate one span per key. Paragraph
continuations retain formatting without repeating a forced page/master break.
Within a list, Enter creates a sibling item and adjacent compatible items can
be joined. A header stays unnumbered when its continuation becomes an item;
style overrides and explicit restarts retain their appropriate source scope.
Joins across cells, unrelated/nested lists, headers or frames, conditional
styles requiring unsupported continuation rewriting and paragraphs containing
embedded objects are intentionally restricted. Tab/Shift+Tab changes whole list
items and their descendants at a real paragraph start or across a selected
paragraph range; the universal Format → List menu also works inside an item.
Selected items must share a list/level after contained descendants are removed
from the operation. Outdent at the top level is a no-op. Enter on a sole empty
root item leaves numbering, and terminal nested empty items can leave all list
levels. No paragraph text or editing offsets are invented by these operations.
List fragments retain ID anchors and following continuation references; when
an unlisted item and its paragraph both have IDs, a valid empty text:meta anchor
retains the item's identity. Metadata text renders transparently, and empty
metadata anchors survive ordinary edits. Editing nonempty metadata still needs
the rich editor. Exit of empty continuation paragraphs/items with nested content,
nested exit before following content, and outdent before trailing parent text
remain unsupported and reject atomically. List-formatting controls, broader
paragraph/mixed-level list edits and general table structure tools remain
unfinished. Underline/strike/highlight, named styles, more character controls
and editing directly on paginated rich text also remain unfinished. Undo restores styled XML,
selection/caret and saved-state identity. Each frontend buffer-edit event now
has a shared native transaction: selection deletion and insertion form one
command, and a rejected callback rolls back the entire event without consuming
redo history. Keyboard, universal-menu and standard context-menu paste defer
deletion until clipboard delivery; an unavailable/empty clipboard leaves the
selection intact. Native commands also validate before committing. These
transactions cover callbacks within one event; drag/drop transfers and longer
cross-keystroke typing groups still need dedicated behavior and tests.
The journal is bounded to 100 commands and 32 MiB
of changed XML bytes; edits larger than that clear history. Each edit currently
serializes content.xml, so large documents still need incremental operations
and typing groups. The page view
does not implement direct rich editing, complete typography or the advanced
structural layout listed above. Document-font fallback is selected through a
private Fontconfig context; missing glyphs are reported in layout diagnostics.
Leading/interior empty lines use the font of their source run, including styled
soft breaks. A terminal empty range keeps the paragraph-mark/default font.
Stored font/style declarations remain unchanged. Colored font bitmaps currently
contribute an alpha mask; they are not painted in their original colors.

The shared native text engine now resolves textual list styles, nested and
consecutive counters, restarts, explicit/implicit continuation, item overrides,
headers, additional unnumbered paragraphs and legacy numbered paragraphs.
Generated labels have separate styled/shaped display lists, so document text
and editing offsets exclude numbering. Both ODF spacing modes, relative bullet
sizes, RTL placement, label tabs, paragraph overrides and source-scoped styles
are supported. Native formats include decimal/padded decimal, alphabetic,
Roman and the legacy KoOdfNumberDefinition scripts; LibreOffice custom label
templates are retained and evaluated. Image bullets share retained immutable
archive assets and a bounded GPU cache across Words labels and Stage labels/frames.
Packaged and inline PNG/JPEG/SVG/GIF/BMP data, declared dimensions, baseline/text/
character/line vertical references and both label spacing modes are supported.
Invalid/missing sources render placeholders; external URLs are diagnosed without
loading external resources. SVG rasterization adapts to display scale; animated
GIFs use their first frame. Other image formats and full SVG fidelity remain
unported. Named ODF 1.4 number
formats, extended locale/spell-out numbering and outline/field numbering still
need work. Missing continuation targets are diagnosed and restarted. Full
structural list editing remains incomplete beyond paragraph/item splits and
compatible joins; existing
list XML and styles are retained during supported text edits.

Paragraph tabs now have a native style/model/display path shared by Words and
Stage. Stop collections replace inherited lists, including explicit empty lists.
Default spacing and ODF 1.4 relative origins come from paragraph defaults.
Left/right/center and arbitrary Unicode delimiter alignment support RTL text,
indents, wrapping and source offsets. Overfull aligned fields can consume a
zero-width tab instead of overlapping preceding text. Justification preserves
established columns and uses available space in the final field where its
alignment permits. Solid/dotted/dash/long-dash/dot-dash/dot-dot-dash/wave,
single/double and styled textual leaders render through native display lists.
Thin preview strokes retain a pixel footprint with coverage-scaled opacity.
Direct tab-stop formatting controls remain part of the unported rich editor.


Sheets currently uses the calculation layer's textual display, not the original
style-aware formatter. Do not expect date/currency styles, images, merged
cells or chart canvases to display like upstream. Fixed matrix results are
calculated; matrix creation and dynamic spill behavior are not implemented. Existing entries and
attributes are retained when unedited, but full fidelity is unverified. Sheet
protection causes read-only mode; range/structure editing is incomplete.

Stage resolves referenced ODP masters and page layouts rather than using the
first stored layout. It renders solid basic vectors, transformed images and
native text, with stable group/z ordering and inverse frame hit testing. Rich
text retains paragraph/inline font and color styles with continuous clipped
flow, including the shared textual list and continuous table engines. Column/vertical
layout, structural table editing, remaining list features above, auto-growth, fit-to-size and rich
editing controls remain incomplete. Gradient/bitmap/hatch fills, stroke dashes/caps/joins,
custom preset lookup, stretch points, extrusion, text on
paths, master placeholder fields and image crop remain incomplete. Explicit
enhanced geometry supports standard formulas/commands, modifiers, mirrors,
independent paint sets and horizontal text areas. Vertical text remains
unsupported. SVG path commands, rounded rectangles and full/arc/chord/pie
ellipses now use native curve geometry with analytic bounds and adaptive GPU
contours. Compound fills support both winding rules; open stroke contours stay
open even when their fill closes implicitly. Native editing covers text on
supported shapes and custom-shape adjustment handles. Cartesian/polar handles
support modern ODF 1.4 attributes and legacy pairs, modifier constraints,
mirrors and direction switching. Producer metadata selects the legacy polar
parameter order used by OpenOffice-family applications. Dragging is one native
undo command; Escape restores the document and its prior redo history. Handles
with two coordinates aliased to one modifier remain read-only and diagnosed;
formula/constant coordinates are not inverted into modifiers. General geometry
editing tools remain incomplete. Unrecognized shapes and animations are
preserved in packages, not rendered. Native undo/redo spans text edits and slide
insertion/deletion, restores the affected selection and exact styled XML, and
tracks the saved state across branches. The journal retains up to 100 commands
and 32 MiB of changed XML bytes; an edit exceeding that budget clears history.
Each edit currently serializes content.xml to derive its delta, and undo/redo
validates the restored document and rebuilds its scenes. Large presentations
still need operation-specific deltas and incremental scene updates. Nested
list/table paragraph-count changes are rejected before mutation.
X11/Xvfb fullscreen transitions can emit GLX diagnostics;
native Wayland is the default and the target for presentation work.

Keep complex originals until the full-model integrations and interoperability
checks above are complete. This is a concrete limitation of this milestone,
not a claim of a finished office replacement.


## Structural table model — 2026-10-29

The native flow now records ordered paragraph/table blocks and a bounded table
grid. Nested tables, merged spans and covered cells, repeated rows/cells, header
rows/columns, column widths/ratios, row dimensions, padding/background/border
properties, alignment/direction, page/keep properties and scoped style inheritance
are represented without Qt. Repeated content shares original paragraph identities;
hidden source text in covered cells retains its editing offsets. A million-record
expansion budget, 1,024 columns, 100,000 rows and 16 nested tables bound input.

The continuous frame path now lays out and paints this grid. Cell text styles
inherit before paragraph/span overrides; repeated instances share source identity
while retaining distinct inherited formatting. Fixed/relative columns, hidden
rows/columns, auto/minimum/fixed row heights, spans, cell padding, RTL columns and
vertical alignment resolve in native geometry. Empty cells have a formatting
strut without inventing an editable source paragraph. Backgrounds and separate/
collapsed borders use bounded GPU paint lists, including solid, double, dashed,
dotted, groove, ridge, inset and outset patterns. Cell clipping applies to glyphs,
tab leaders and image bullets before frame transforms, including rotated frames.
Optimal column sizing currently uses declared widths and reports that limitation.
Page/keep properties are retained but diagnosed as ignored in unlinked frames.

Words now consumes the ordered root blocks and paints table row groups in its
page flow. Whole row groups use cached geometry; independent source cursors
continue split rows, vertical spans and nested tables. Complete header groups
repeat, and a header moves with its first body group when both fit a fresh page.
The remaining source row index survives a master-width change; the next width
resolves its cells and repeated headers afresh. Tables that disallow breaks
between rows move as one group. Native display/index budgets also bound repeated
headers. Page selection distinguishes columns sharing the same line height,
chooses nested cells before parents and retains original paragraph identities.
Empty cells without a source paragraph do not invent an editing position.

Each continuing cell retains its paragraph/block index and Unicode source
offset. Fresh widths shape the remaining source with whole-paragraph bidi
context; same-width continuations reuse wrapped geometry. Ending cells determine
row height; merged cells that do not end in that row fill their established page
area once. This also lets nested tables continue without committing child glyphs
to successively growing bands. Fixed/minimum dimensions, blank continuations,
paragraph breaks, keeps/widow/orphan thresholds and final-fragment vertical
alignment participate in the flow. Display-only fragment grids reuse the shared
background/border resolver; source grids and XML remain intact. Empty-cell
formatting struts do not invent editable paragraphs. Impossible page/padding/keep
constraints are bounded and explicitly reported or rejected.

A shared native keep planner now connects paragraphs and tables, including
nested cell blocks and hidden-table gaps. Whole connected chains move together
when they fit; oversized chains reserve the following prefix at the terminal
fragment. Read-only source-cursor probes use remaining content and mandatory
breaks to identify the final fragment's controlling cells, without unnecessarily
delaying shorter siblings. Cached block extents and lazy connected-chain
measurements avoid formatting unrelated master-width scopes. Explicit breaks
and every master assignment override a keep, even when the master name matches
the current page. Impossible row/table/paragraph keep combinations are reported
while preserving bounded progress and source coverage.

A vertical span crossing the header/body boundary still disables repetition with
a diagnostic. ODF paragraph master assignments inside table cells are ignored
per instance, including inherited styles, repeated cells and nested table
content. Root paragraphs using the same style retain their master assignment,
and the original style XML remains unchanged. Nested table master behavior
remains unfinished. These are remaining pagination requirements, not full Words
structure parity.
Structural cell/row editing is unfinished; supported
edits preserve the original grid and inline-break XML.
Words and Stage text-editor undo shortcuts now route through application history,
so native editor Undo does not discard the application's Redo chain.
