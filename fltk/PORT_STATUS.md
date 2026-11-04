# Native engine milestone — 2026-10-29

The current functionality baseline is **ODF 1.2 document semantics and
OpenFormula, plus CUPS printing**, following the user's 2026-11-03 steering.
The earlier export/printing milestone remains verified on selected fixtures;
the broader ODF 1.2 functionality target is incomplete. See
[ODF12_BASELINE.json](ODF12_BASELINE.json) for the initial area inventory and
completion evidence requirements. It still needs decomposition into individual
normative behaviors. Schema-valid XML, preserved package bytes and recognized
function names each provide separate evidence; they do not establish functional
coverage. Native document semantics, editing, rendering, save/reopen and printed
output need feature-appropriate independent checks.

Stage remains the active implementation priority. Words is paused at its prior
rudimentary milestone; its experimental column worktree stays preserved and
uninstalled. Further Sheets catalogue/settings work waits. Runtime build `353c016`,
including layer authoring, separate frame/image opacity and stroke caps/joins,
is installed and independently verified. Test-only followup `7b5a47f` has the same
installed payload. Clipboard transport, fragment and cut-draft work below is not integrated into Stage
or installed. Subsequent source/build/stage/installation claims require their
own evidence. Historical sections retain their earlier priorities and baseline.

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
Format menu. Bold/italic, underline/strike, highlighting, font family, size and color act on Unicode ranges or
future typing at a caret; empty-paragraph defaults survive save/reopen.
Paragraph controls cover alignment, margins, first-line indent, spacing,
keep rules, page breaks and widow/orphan limits. Direct styles preserve source
runs, links, metadata and structural containers. Formatting shares native XML
undo/redo and saved-state tracking. Words opens in native paginated editing with a caret ready for keyboard input.
The explicit Text view option retains the legacy plain text buffer.

Format → Paragraph styles creates and edits named definitions with parent/next
styles, heading roles and text/paragraph properties. Renaming preserves internal
identifiers. Apply stages definitions and optional assignment as one undo step;
Cancel discards them. Undo restores changed archive parts and their exact
manifest/styles bytes, including an originally absent styles part. Enter follows
the next style and heading role. Only unused definitions may be deleted. Valid
empty ODT bodies support typing, formatting and style assignment, with undo back
to the empty body. Named character styles now have a shared FLTK manager,
inheritance and stable-name edits. Assignment replaces selected character
formatting while keeping semantic containers, links, metadata and IDs once;
displaced styles remain on unselected text. A caret keeps a transient named
reference for future typing, with direct formatting layered afterward. Default
character style returns selected text to paragraph/cell properties. Visited-link
and style-class cascades and outline-number editing remain unimplemented.

File → Page setup now changes the active page style with paper presets,
orientation and millimetre-based dimensions/margins. It resolves the caret's
page in either view. Native pagination rejects an unusable candidate before
source commit. Shared layouts are isolated for one master, while private layouts
stay in place. Unchanged fields retain their source units and properties;
headers/footers retain their source data. Their native rendering and scoped
editing are described in the latest increment below. Creation of missing layout/styles parts and exact undo share the
existing archive-part transaction.

Native table insertion/deletion now has a structural source/package transaction
separate from style-only commits. The FLTK Table menu inserts a bordered grid at
the caret, retains rich paragraph halves and next styles, and offers a repeating
header row. Body, section and nested-cell insertion are supported; invalid
sizes and stale/read-only transactions leave source/history unchanged. Deletion
resolves the nearest source table and preserves outer cells and root-flow page
transitions. Table source nodes now retain stable identities in the layout model.
The Rows/Columns menu now inserts and deletes rows/columns with inherited cell
styles, merged-span adjustment and anchor promotion. Local run splitting keeps
large untouched repetitions compact, gives new cells independent typing and
retains IDs and internal references. Full outer-to-inner cell paths identify
nested tables inside repeated content and restore the actual cell on undo.
Native page text events also isolate preexisting repeated cells and enclosing
repeated ancestors before changing source. Typing, deletion, keyboard/menu cut,
asynchronous paste and paragraph/soft breaks share one exact source/selection
undo transaction with that isolation. Pending caret formatting follows the new
source position. Empty or failed events restore the unexpanded source.
Character/paragraph formats, named assignments and local tab stops now isolate
the selected repeated cell as part of the same source/selection transaction.
Global named-definition edits and document tab defaults retain their global
scope. Ordinary ranges across independent cells remain valid; ranges crossing
repeated cells still require additional selection metadata.
Row/header and column/header groups survive the edits; deleting the last row or
column removes the addressed table. Formula references still need a structural
rewrite before those tables can be edited. ODF forbids direct tables in list
items; anchored placement/list splitting and merge/split controls remain
unfinished.

Table → Sizing now controls available/fixed/percentage table width and alignment,
automatic/minimum/exact anchor-row height and fixed/weighted anchor-column width.
The native transaction isolates repeated rows/columns and nested table instances,
retains non-sizing style inheritance and source values, leaves empty cells empty,
and restores source/style parts and the displayed caret together on undo.
Invalid, cancelled and unchanged commands preserve history. Exact row heights
clip overflowing text; fixed columns retain their declared widths while weighted
columns share remaining space. Fixed conversion relocates a default percentage
to existing named style roots and unstyled peer tables, preserving their prior
widths and inheritance. Proportional scaling uses rendered fixed-column widths
and the selected master/nested cell's text area while retaining relative weights.
It preserves implicit shrink proportions and hidden-column metadata. A visible
fixed column that has shrunk to zero needs a positive table width applied without
proportional scaling first. Geometry measurement now shares pagination's prepared
cell-paragraph cache. Narrow cell text areas wrap and clip within the original
cell geometry; body and floating-frame margin validation remains strict.
Repeated formula-bearing source cloning and optimal content-based sizing remain
unfinished.

Native Words page editing provides a grapheme-safe caret, visual bidi/ligature
navigation, vertical and word/paragraph movement, GPU range selection, mouse
dragging, double-click words and triple-click paragraphs. Typing, clipboard,
formatting, list actions, paragraph splits/joins and soft breaks share the
native XML transactions and undo history. Reflow preserves scrolling and
reveals the caret, including in repeated table instances. Vertical arrows visit
the adjacent visual row before choosing a horizontal stop, preserving the
preferred x across indented lines. Inside tables, source cell identities keep
vertical movement in the current cell/subtree and column, including merged
cells, nested tables and repeated headers. Cell entry and viewport jumps use
cell boxes across page continuations before choosing a glyph line. Holding a selection drag at either edge
continues scrolling until release, Escape or focus loss. Shared GPU compositing
preserves opaque paper beneath translucent highlights, glyphs and images. Native Wayland Fcitx
preedit/commit/cancel uses one history transaction and anchors candidates to
the page caret. Escape cancels composition/selection without closing Words.
Space and navigation keys always edit or move within the document; the wheel
scrolls the view. Page layout and Text view are explicit, idempotent menu
choices. New/Open reset the document caret and cached layout, while switching
views preserves selection and reveals the native caret. Further native editing
polish, performance and layout/formatting work remain.

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
| Words | DOCX paragraph/table import with ODT saving, ODT paragraph text, common/automatic style inheritance, page geometry/master continuation, native HarfBuzz/FreeType shaping and glyph rendering, document fonts and script fallback, ICU bidi line ordering, numeric font weights/standard variable axes, native pagination and GPU page view, ordered native tables with row/column editing and structural undo, independent split/merged/nested cell cursors, repeated headers/master-width reflow and source cell selection, document ink/page colors, keep/widow/orphan rules, span/link-preserving body/section/cell paragraph splits and joins, explicit soft breaks, item/range list levels and empty-item exit, native range/caret character formatting and paragraph controls, named paragraph/character style management and assignment with archive-part undo, native page setup with shared-layout isolation, scoped header/footer editing and native rendering with dynamic page fields, direct tab-stop/leader controls and document defaults, direct page caret/selection/input and Wayland IME transactions, XML undo/redo with selection and saved-state tracking, atomic save | Remaining KWDocument/KoText/textlayout/frame behavior; full font/style/shaping and line-breaking fidelity, embedded fonts, color-font glyphs, synthetic/vertical text, remaining table navigation, remaining table styles/optimal sizing and reflow performance, complex paragraph-level list reparenting, remaining character/list controls and visited-link/style-class cascades, advanced table keeps/header-boundary spans and nested table master behavior, section layout, structural list editing, columns, advanced header/footer geometry and anchored content, notes, images/anchors, tracked changes and print/PDF fidelity |
| Sheets | Native C++ parser/calculation engine, typed arrays, reference/lookup/matrix functions, fixed ODF matrices and XLSX array import, dependencies, workbook-specific ODF null dates and retained XLSX 1904 epochs, cross-sheet references, named-range import, multi-sheet ODS and XLSX cell-data import, cell edits, undo/redo, TSV paste, repeated cells, cached formula updates | Remaining upstream calculation catalogue and settings, full core/style model and ODF loader, formatting/date/currency display, selection ranges, merged cells, matrix-block creation/deletion, sort/filter, validation, structural row/column operations, charts, printing and format filters |
| Stage | PPTX direct text/PNG/JPEG import with ODP saving, per-slide ODP master/page layouts, scoped/inherited basic paint, rectangles/ellipses/polygons/lines, native Bézier/arc paths and rounded/partial primitives, explicit enhanced geometry/equations and modifier paint sets, ordered groups and affine transforms, native paragraph/run fonts and colors, bidi and continuous frame layout with side padding/vertical alignment, PNG/JPEG/SVG/GIF/BMP GPU preview, native continuous tables with merged/nested/repeated cells and clipped GPU paint, inverse frame selection and nested edits, primitive/text creation, own-object deletion, position/size/rotation panel, direct affine resize/center-rotation/line-endpoint handles, embedded image insertion, scoped character/paragraph controls with future typing and selection history, slide insert/delete, presentation navigation, package preservation | Complete KPrDocument, KoPADocument and Flake behavior; theme/placeholder fields, structural list/table editing, column and vertical text layout, text auto-growth/fit, advanced typography, custom preset lookup/stretch points, extrusion/text-on-path, connectors/3D objects, advanced fills/strokes, layer authoring, animation/timing, transitions, speaker notes/view and export fidelity |
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

## Implementation priorities — Stage now

The user switched focus to Sheets on 2026-11-03 after the installed Words build
reached rudimentary compliance, then requested Stage after the current Sheets
build. Broader Words layout/editing remains unfinished; the separate experimental
page-column worktree is uninstalled and preserved.

1. Work on native Stage layout/editing, preserving OpenDocument export and CUPS
   printing. Native movement and primitive/text creation/deletion are available;
   numeric position/size/rotation and embedded images are installed; richer text
   formatting, direct resize/rotation, stacking and group/selection tools are installed.
   Layer visibility/protection and separate screen/print scenes are installed.
   Transactional layer editing and separate image/fill opacity are installed.
   Native stroke caps and joins are installed. A native multi-MIME clipboard
   helper and selected-object ODP extraction exist behind model APIs;
   destination import, cut and actual clipboard/menu integration remain unfinished.
2. Sheets context build 930f2cc is committed, pushed, installed and independently
   verified, including native Wayland editing and five CUPS jobs/21 reviewed pages.
3. Keep Words paused and preserve the remaining Sheets catalogue/settings backlog.
   Do not extend Sheets before the requested Stage work. Shared-engine changes
   still require suite regression checks, OpenDocument round trips and CUPS printing.

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
levels. List operations within repeated table cells isolate the clicked copy
and all repeated ancestors, rebase its source selection, and retain pending
caret formatting. Other displayed copies keep their original lists. No-op
commands leave repeated XML compact and do not add history. Undo restores the
original source and complete cell path in one step. Paragraph text and source
order within the edited cell stay unchanged.
List fragments retain ID anchors and following continuation references; when
an unlisted item and its paragraph both have IDs, a valid empty text:meta anchor
retains the item's identity. Metadata text renders transparently, and empty
metadata anchors survive ordinary edits. Editing nonempty metadata still needs
the rich editor. Exit of empty continuation paragraphs/items with nested content,
nested exit before following content, and outdent before trailing parent text
remain unsupported and reject atomically. List-formatting controls, broader
paragraph/mixed-level list edits and general table structure tools remain
unfinished. More character controls, visited-link/style-class cascades and
specialized outline-number editing remain unfinished. Undo restores styled XML,
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
Format → Tabs now adds, edits, deletes, clears or inherits the selected
paragraphs' stops. The FLTK editor covers alignment, Unicode delimiters,
line/text leaders and scoped text styles. Interval and ODF 1.4 origin controls
edit document paragraph defaults; older document versions are not upgraded.
Effective lists include table-cell inheritance. Common, content-automatic and
master-automatic leader fonts retain their scope when stops become direct
formatting, including colliding style names. Changed source/default archive
parts share one undo step; unchanged Apply and staged cancellation retain exact
source bytes. Native pagination validates the candidate before commit.


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


## Native cell merging and splitting — 2026-11-02

The universal Table → Cells menu merges a rectangular logical range from the
current anchor and splits an existing merge back into its underlying grid. The
engine localizes repeated target rows/cells and all repeated ancestors, validates
complete existing spans before changing XML, and retains covered-cell identities.
Merging appends source paragraphs, lists, links and nested tables in row order;
covered text is retained and made visible. Splitting keeps combined anchor text
in its first cell and restores covered content to separate editable cells.

Moving between different cell styles freezes inherited character/paragraph
appearance on the moved source, retaining explicit runs and named parents.
Nested repetitions are materialized where distinct inherited contexts require
independent source. Effective tab collections are captured before the move and
reapplied afterward, including common/automatic leader-style scope. No-op menu
commands preserve the current selection and history. Dialog cancellation and
invalid dimensions leave the live document untouched. All real edits use one
structural history command and preflight native pagination.

Header/body-boundary merging and numeric/formula value/reference merging remain
unfinished. Numeric/matrix/formula
metadata in the merge range rejects atomically; text-only string caches are
invalidated when their paragraphs are combined. These operations do not complete
remaining Words section, column, header/footer, note or image-anchor editing.


## Cell subdivision — 2026-11-02

Table → Cells → Subdivide cell accepts row and column counts for independent or
merged cells. The engine refines the original logical grid by the necessary
common factors and gives neighboring cells spans over the new slots. Existing
row/column groups, covered data, source identities, opaque parts and cell styles
are retained; repeated targets and their enclosing instances are materialized.
The first part retains the original contents and cached numeric value. Other
parts are independently editable and inherit the selected cell style. Source
formulas and matrix references reject refinement pending coordinate adjustment.

Fixed row heights/column widths are divided with their original measures, while
flexible-column proportions are normalized together into valid ODF integer
weights. Native relative weights can now reach 10^12, allowing normalization
without the previous 10^6 limit invalidating ordinary source ratios. Candidate
pagination precedes one structural history command; Cancel, invalid dimensions
and a one-by-one request leave the document unchanged.

Subdivisions across unequal existing grid measures follow logical proportions
by default. The measured-size option and fixed-width preservation described below
cover equal physical parts and implicit shrink. Richer cross-cell selection and
other Words layout/editing requirements remain. Sheets functions next; Stage last.


## Measured subdivision geometry — 2026-11-02

Subdivision uses the actual caret page and the complete nested-cell path to
resolve table widths with the shared native block layouter. Parent content
widths include padding and border resolution. When implicit fixed-column shrink
would redistribute space after adding columns, the current resolved measures
are retained as explicit standard ODF widths instead. Neighboring cells retain
their current physical widths, including RTL tables.

Equal measured sizes inserts equal physical cuts into the existing measured
grid. Existing boundaries, including zero-size hidden slots, remain available
for neighboring spans and source metadata. Unequal original column/row sizes
therefore no longer force unequal parts. Row parts grow uniformly when narrower
content needs more height; this retains equality and avoids cropping that
content to an undersized measured row. Row/column measures are explicit after
this operation and remain fixed if the page size later changes. Normal logical
subdivision remains available. Both modes use one exact structural undo step.

Cross-cell range selection, formula/reference adjustment, remaining list and
section/column/header/footer/note/image-anchor editing remain unfinished. These
geometry corrections do not complete Words or the broader goal. Sheets follows
Words; Stage stays last.


## Native cell formatting — 2026-11-02

The universal Table → Cells → Cell format panel edits independent padding and
border style/width/color for all four sides, cell background (including
transparent), and top/middle/bottom alignment. It uses the existing native
paint/layout/printing model. Effective row/column/table default styles and
common styles.xml inheritance remain the base of the new automatic style;
unrelated paragraph/text properties and opaque XML/ZIP data are retained.

Only the addressed displayed cell changes, including merged cells and repeated
nested ancestors. Empty source cells stay empty. Validation and native pagination
precede one structural transaction, with exact archive-part undo/redo. No-op and
Cancel preserve repetitions, history and the caret. Actual edits retain the
caret's offset inside the materialized cell. This closes the basic cell-paint
and padding editing gap, while cross-cell selection, table size/style controls,
formula/reference operations and broader Words layout/editing remain open.
Goal work resumed after the previous rudimentary milestone pause. Words stays
first, Sheets functions next, Stage last.


## Displayed Words ranges — 2026-11-02

Selections now retain the full outer-to-inner paths of both endpoints. Native
logical document order distinguishes repeated paragraph instances independently
of numeric source offsets and pagination fragments. Page highlighting, copy,
Shift mouse/keyboard extension, word/paragraph/document navigation and range
collapse use the same identities. Repeated headers retain one document identity.

Character/paragraph formatting, named style assignment and tab editing prepare
only the selected displayed paragraph/cell instances, preserving inherited styles
and opaque source content. Covered-cell paragraphs are excluded from formatting
and tab queries. Both endpoints are rebased after localization, including ranges
with coincident source offsets and reversed ranges. Semantic no-ops discard the
prepared candidate; real changes use one exact structural history command.

Cross-cell text replacement/Cut and list structure edits remain guarded. Further
work includes their cell-preserving semantics, empty-source-cell text editing,
selection/reflow performance on large documents, table size/style controls,
formula/reference operations and the remaining section/column/header/footer/note/
image-anchor requirements. Words remains first, Sheets functions next, Stage last.


## Cell-preserving Words range editing — 2026-11-02

Typing, paste, Backspace/Delete, keyboard/menu Cut and Shift+Delete now consume
cross-cell displayed text selections in one native event transaction. Source
ranges clear in reverse order; adjacent paragraphs and supported sibling list
items join only inside the same cell and across compatible source structures.
Nested tables, cell spans, row/column dimensions, sections and unrelated tails
remain in place. Replacement begins at the logical first endpoint in either
gesture direction. Body-ended ranges spanning tables also keep their grids.
Table/object deletion remains a separate structural action.

The first inline insertion context survives preparation. Empty styled span/link
queries resolve the format used by actual typing. Modified cells and enclosing
cells lose stale optional string-value caches; unmodified cells retain theirs.
Numeric/formula/matrix caches require their value editor and reject atomically,
as do unsupported rich objects. No delivered input, cancelled preedit and failed
replacements roll back text removal, materialization, caches and selection;
explicit erase/Cut consumes the prepared deletion. A selection containing only
an undeletable cell boundary is a true no-op.

Cross-cell list level/structure commands, empty-source-cell text editing,
large-range performance and broader Words table sizing, formula/reference,
section/column/header/footer/note/image-anchor work remain unfinished. Words
stays first, Sheets functions next, Stage last; the full goal remains active.


## Empty Words source-cell editing — 2026-11-02

Truly empty table cells now expose a native caret and participate in displayed
navigation and selections. Virtual paragraph identities follow real source
indices without adding source text or XML separators. Empty cells with the same
format share shaping while retaining their own source identity, geometry and
complete repeated/nested cell paths. Covered and hidden cells remain excluded.

First typing, paste or a paragraph/soft break creates a real paragraph in the
addressed cell. Repeated rows/cells and enclosing instances are isolated in the
same event transaction. Inherited cell text formatting applies, stale string
caches are invalidated, and untouched cells remain empty. No delivered input,
cancelled preedit and failed edits restore exact original XML and selection;
undo returns to the original virtual caret. Backspace/Delete at a collapsed
virtual caret are no-ops. Value/formula or unsupported-object cells still
require their dedicated editors and reject text preparation atomically.

The applicable Release/sanitizer, GUI, export and CUPS evidence is recorded in
VALIDATION.md. Cross-cell list structure, table sizing, value/reference adjustment,
large-document performance and the remaining section/column/header/footer/note/
image-anchor work are unfinished. Words stays first, Sheets functions next,
Stage last. The full goal remains active.


## Displayed Words list level ranges — 2026-11-02

List Indent/Outdent now acts on displayed selections across independent,
repeated and nested cells and unrelated source lists. Displayed ranges are
localized together, then each selected item is collected once. A selected
ancestor carries its descendants, avoiding repeated indentation of continuation
paragraphs and nested items. Actual contiguous sibling groups move in reverse
source order using native list-style, numbering and continuation rules. Covered
metadata and unselected source gaps do not enter a scalar envelope.

The operation checks source paragraph identity/order/text and resolves both
selection endpoints after materialization. All groups commit in one exact undo
operation. Root-level outdent discards localization entirely; invalid endpoints,
non-list content and unsupported reparenting discard the entire candidate,
including earlier group moves. Native page Tab/Shift+Tab route cross-cell
selections to list operations before text replacement. Menu and keyboard paths
retain the selection, and saved documents reopen with their list structure.

This closes displayed range list-level editing, including multi-paragraph item
deduplication. Complex paragraph-level reparenting after nested lists, remaining
list/numbering controls, table sizing, value/reference adjustment, large-document
performance and broader section/column/header/footer/note/image-anchor behavior
remain unfinished. Words stays first, Sheets functions next, Stage last.


## Native Words headers and footers — 2026-11-03

Words now renders source-scoped header/footer flows in the GPU page view and
native PDF/CUPS path. Default, first and left variants follow master transitions
and logical page numbering; explicit suppression retains its source. Three
regions share a row with left/center/right alignment. Borders, background,
padding, margins, minimum/fixed height and dynamic spacing reserve body space.
Page-number/count fields resolve after pagination, including restarts, fixed
caches, previous/next pages, number formats and adjustments. A bounded fixed
point rejects unstable field-dependent pagination.

The Header and footer command and page-story clicks open a scope selector.
The existing native Words editor then operates on that story/region, with its
text, formatting, list and table transactions. Finish merges one parent archive
transaction and restores the body caret/view; Cancel preserves parent history.
Save/Print apply the scope before acting on the full parent document. Page
setup and scope switching require finishing the current scope. Body text,
unrelated stories/archive data and separate body automatic styles are retained.
New identities/table names avoid body collisions. Shared named styles remain
shared, and deletion still checks the projected-away body's references.

Page fields remain atomic rich source nodes. Their editing caches do not change
the document-page values; insertion creates native dynamic fields. First-page
creation upgrades older content/styles to ODF 1.3. Missing masters/layout/styles
parts can be created and restored exactly by undo. A direct header/footer can
be converted into regions without dropping its existing content.

This is an increment toward Words, not complete layout parity. Mirrored page
usage/blank-page insertion, anchored images/shapes, notes, sections/columns,
box shadows, additional field types and broader document-corpus fidelity remain.
Content-based table sizing, formula/reference updates, richer lists and large
file performance also remain. Sheets functions follow Words; Stage stays last.


## Sheets focus and descriptive statistics — 2026-11-03

Sheets functions are now the active implementation priority. The native engine
adds AVERAGEA, AVEDEV, DEVSQ, DEVSQA, GEOMEAN, HARMEAN, STDEVA, STDEVPA, VARA and
VARPA. These consume complete typed arrays, named/cross-sheet references and
sparse ranges; missing cells are excluded. A variants count text as zero and
logical values as zero/one. Other variants ignore text/logicals in collections,
with the existing native numeric conversion for direct arguments. Complex
values use their real coefficient, following the existing aggregate convention.

Compensated extended-precision arithmetic retains small deviations at large
origins, geometric means avoid product overflow/underflow, and harmonic means
avoid reciprocal overflow. The functions bound collected input to one million
records. Nonpositive geometric/harmonic observations report NUM errors;
insufficient samples report DIV/0, consistent with existing native VAR/STDEV.
DEVSQA is retained as a Calligra extension. These are ten added functions, not
completion of the statistical group or full calculation compatibility.

ODS saving now supplies missing column declarations and an empty row when
needed, including new sheets and XLSX imports. Existing column groups/styles,
row data and unsupported caches remain in place. Independent schema validation
checks the source and exported packages.

A comment-aware audit probes active retained registrations and alternate names.
It reports 307 covered and 334 remaining registered names, including aliases;
these counts measure catalogue dispatch separately from numerical fidelity. Unsupported
functions and their dependent source caches remain preserved. Validation and
installation status are recorded separately; experimental Words columns are not
part of this increment.


## Native Sheets conditional aggregates — 2026-11-03

COUNTIF/COUNTIFS, SUMIF/SUMIFS and AVERAGEIF/AVERAGEIFS now share a native typed
criterion matcher and sparse paired-range traversal. Workbook-owned ODF regex,
wildcard and whole-cell flags survive save/reopen; XLSX imports explicitly use
wildcards with their original date epoch. Conditional searches are case-
insensitive independently of the general comparison flag. Equal numeric text,
ordered numeric values, escaped wildcard characters, Unicode regex, substring
mode, empty strings, absent cells and error labels retain their distinct cases.
Date criteria support ISO and US English civil dates in the workbook's epoch.
General date/time formula text conversion now uses numeric serial values.

Multi-criterion dimensions are strict. Single-criterion target geometry is
expanded from its top-left and clipped at the sheet boundary. Native typed array
inputs and criteria, names, INDIRECT/OFFSET/INDEX references and existing fixed
matrix blocks participate in recalculation/cache saving. Sparse counts can
cover huge mostly-empty ranges. A shared work budget, one-million-position cap
and ICU regex stack bound report incomplete limits instead of replacing source
caches. Malformed regex is treated as literal text.

The inventory now covers 313 of 641 active retained names (aliases included),
with 328 remaining. This is dispatch coverage only. Reference-list unions,
general locale parsing, advanced regex interoperability, order/rank/percentile
functions, distributions/regression, database functions and remaining calculation
settings/model behavior are still open. Two contradictory FODS caches and twelve
cases with unavailable dependencies are recorded separately from 188 agreeing
retained results and 362 independent numeric checks. Words remains at its
rudimentary milestone; Sheets stays active and Stage remains last.

## Native Sheets order statistics — 2026-11-03

Sheets is the active priority, with Words paused at its rudimentary milestone
and Stage last. Thirteen additional canonical functions cover SMALL/LARGE,
competition/average ranks, inclusive/exclusive percentiles, quartiles and
percentage ranks, and single/multiple modes. MEDIAN now shares overflow-safe
interpolation. Recognized INC/EQ and Microsoft-prefixed aliases remain native;
Qt frontends stay opt-in. Datasets sort once per function evaluation and serve
all array selectors. Typed collection filtering, sparse/named/dynamic ranges,
errors, unavailable-dependency propagation and the one-million-record bound
remain enforced. Fixed arrays use #N/A for unused result slots.

The retained corpus audit includes 333 results, including MODE.MULT children:
298 agree, four retain unavailable dependencies, and 31 explicit discrepancies
remain visible. Twenty-six percentage-rank caches use Calc rounding rather than
the selected decimal truncation, one uses a different exclusive singleton
convention, three predate encounter-order mode results, and one inherits binary
duration arithmetic before INT. The latter is a remaining calculation issue,
not a successful rank-compatibility result. Original corpus files are unchanged.
Independent exact-rational checks cover 1,345 cases. SMALL/LARGE also make six
previously unavailable conditional-aggregate cases calculable: 194 of 202
conditional caches now agree, with two contradictory source caches and six
unavailable dependencies still recorded separately.

Function dispatch covers 332 of 641 active retained names, with 309 remaining.
This is catalogue coverage only. Distributions, higher moments, regression,
database functions, reference unions, general locale/settings/model behavior
and broader interoperability fidelity remain open. OpenDocument export and
CUPS printing remain the acceptance floor for each installed increment.


Validation: 58 applicable Release suites and 58 AddressSanitizer/UndefinedBehavior-
Sanitizer suites pass; the three previously excluded outline-font assumption
suites remain outside the bitmap-font run. Independent ODS/XLSX producers check
stale caches, errors, aliases and fixed matrices. Native Wayland and X11 keyboard
editing, recalculation, history, save/reopen and an ASan Wayland run pass.
Twenty-three generated packages pass the version-matched ODF schemas. Five
private-queue CUPS jobs cover the increment and the three-application baseline;
21 print pages reopen identically, and all nine baseline pages match the previous
installed increment. The optimized stage verifies 29 artifacts with system FLTK
and no Qt/GTK ELF linkage. Runtime decoration plugins are a separate concern.


## Native Sheets higher moments and trimmed means — 2026-11-03

SKEW, SKEWP/SKEW.P, KURT, KURTP, TRIMMEAN and STANDARDIZE are native, including
recognized Microsoft aliases. VARIANCE and four Microsoft-prefixed standard-
deviation/variance names now reach the existing native implementations. The
inventory covers 343 of 641 active retained names, with 298 remaining. This is
dispatch coverage only; distributions, paired statistics/regression, database,
remaining function groups and full calculation settings/model remain unfinished.

Higher moments use shifted/scaled observations and compensated extended sums,
so huge/tiny scales and large offsets remain usable. KURTP follows the documented
population excess-kurtosis example, correcting the old sample-variance-based
implementation. Minimum sample counts are three for skewness and four for
kurtosis; insufficient or constant samples return #DIV/0!. TRIMMEAN shares the
bounded sorted dataset and supports array trim fractions. STANDARDIZE broadcasts
scalar arguments, maps same-shaped arrays and avoids intermediate subtraction
overflow. Runtime errors and unavailable dependencies keep their provenance.

All 59 applicable Release suites and 59 ASan/UBSan suites pass, retaining the
three longstanding outline-font exclusions under the bitmap-font policy. The
new reference audit has 582 independent Decimal cases and 149 retained results:
140 agreeing caches, six native/Calc error-label differences, two independently
resolved precision differences, and one unavailable trailing omitted argument.
The two skewness cases independently read 198 raw FODS observations each;
native results agree with Decimal, while the old caches lose offset precision.
Original corpora remain unchanged. Earlier descriptive, conditional and order
reference audits continue to pass with their previously documented exceptions.

After the system's separate X11 retirement removed Xvfb, a private headless
Cage/nested-DomeD fixture now tests real Wayland keyboard/pointer input without
an X server. It never connects input to the user's desktop. New-function Release
and ASan UI runs verify all calculated/painted cells, editing, undo/redo, direct
raw saved-cache checks, fixed matrices, save/reopen and opaque archive data.
The conditional and order suites also pass through this new native fixture.
The test-only helper/protocol definitions are excluded from installation.

Twenty-two generated packages pass ODF schemas. Five private CUPS jobs cover
the increment and three-application baseline; 21 pages match after reopen, and
all nine baseline pages match the preceding installed increment. The prepared
Release stage verifies 29 artifacts, system FLTK and no Qt/GTK ELF linkage.
Optional runtime decoration plugins remain separate from ELF dependencies.
Words stays paused at rudimentary compliance, with its experimental columns
preserved and uninstalled; Sheets remains active and Stage last.


## Native Sheets paired statistics and linear regression — 2026-11-03

The preceding higher-moments increment 173dadb is installed and independently
verified. Paired statistics add eleven canonical functions and recognized
aliases: CORREL/PEARSON, population/sample covariance, SLOPE, INTERCEPT, RSQ,
STEYX, FORECAST/FORECAST.LINEAR, and the three pairwise square sums (including
legacy SUM2XMY). The dispatch inventory covers 355 of 641 retained names, with
286 remaining; this is catalogue coverage only. Distributions, full matrix
regression (LINEST/TREND/GROWTH/LOGEST), database and remaining function groups,
reference unions/omitted arguments and full settings/model fidelity remain open.

Sparse traversal preserves positions across same-shaped datasets, rather than
compacting each sequence independently. Direct scalar coercion differs from
collection filtering. Nonnumeric partners do not hide errors or unavailable
calculations. Named/dynamic references, fixed matrix results and array forecasts
are supported. Scalar-context FORECAST range queries use implicit intersection.

The kernel accumulates exact binary sums/products using header-only Boost
Multiprecision integers. Integer cancellation precedes final quad-precision
ratios/square roots and double storage, eliminating false large intercepts,
residual cancellation and intermediate overflow/underflow. One million ordinary
paired observations produce exact slope/intercept/zero residuals in ~0.087 sec
in Release and ~0.780 sec under ASan, excluding workbook loading/rendering.
Gathered positions and integer work are bounded; exhausted budgets preserve
incomplete provenance. No Qt/GTK or new runtime math library is introduced.

All 60 applicable Release and 60 ASan/UBSan suites pass (the same three outline-
font exclusions remain under bitmap policy). Independent exact rational checks
cover 1,100 cases. The selected paired corpus has 147 results: 124 agreeing
caches, 19 native/Calc error labels, four independently resolved precision
cases and no unavailable dependencies. Three covariance caches lose large-
offset precision; one RSQ cache underflows to a false division-by-zero error.
Raw inputs and simple power-of-ten input formulas are independently evaluated;
original fixtures are unchanged. Paired support also makes the formerly
unavailable LARGE result calculable and validates three additional helper cells
in each of the older order and higher-moment corpora. Their earlier documented
cache conventions and remaining parser/reference limits stay explicit.

Release and ASan native Wayland tests pass editing/history, recalculation, raw
saved caches, matrices, save/reopen and opaque-data preservation, using the
private headless Cage/nested-DomeD fixture without an X server. Twenty-two
generated packages pass ODF schemas. Five private CUPS jobs cover 21 pages;
all reopen identically and all nine three-application baseline pages match the
previous installed increment. The Release stage verifies 29 artifacts with
system FLTK and no Qt/GTK ELF dependency. Words remains paused at its rudimentary
milestone; experimental columns stay uninstalled, and Stage remains last.


## Native Sheets probability distributions — 2026-11-03

The paired-statistics increment 8d2464f is installed and independently verified.
This increment adds 55 native function signatures plus aliases across thirteen
continuous/discrete distribution families, Fisher transforms, Gamma/log-Gamma,
Gaussian area/density and normal/t confidence intervals. The retained-name
dispatch inventory now covers 430 of 641 names, with 211 remaining. Alias counts
measure catalogue coverage only, not the fraction of the full goal completed.

OpenFormula and modern signatures retain cumulative/density, left/right tail,
legacy t-tail, beta bound ordering, optional-argument and endpoint distinctions.
XLSX import translates Excel's old FDIST/FINV calls to ODF right-tail names while
preserving strings and quoted sheet names. Extended Boost.Math intermediates,
complement probabilities, cancellation-safe small areas, explicit Gamma density
endpoints and least-integer binomial search avoid common numerical failures.
Convergence, integer representability and hypergeometric backend work are bounded;
incomplete errors retain old caches. This adds no Qt/GTK or runtime math library.

All 61 applicable Release and ASan/UBSan suites pass with the same three bitmap-
policy exclusions. Independent 100-digit mpmath/exact-rational checks cover
2,383 direct/inverse cases; inverse checks account for binary64 output spacing.
The selected untouched FODS corpus has 2,745 results: 2,458 agreeing caches,
250 error-label differences, 27 unavailable inputs/parser cases and 10 explicitly
retained differences. Four reflect Calc repeating a row in an oversized matrix
where native pads with #N/A; five follow the specified ODF/Microsoft boundary
conventions instead of the old caches. The final chi-square inverse discrepancy
is resolved independently from its raw inputs using a finite survival sum.
These differences remain limitations/evidence, not silently passing assertions.

Native Wayland Release/ASan editing, recalculation, undo/redo, raw saved caches,
arrays, import/export, save/reopen and opaque-data tests pass. Twenty-one print
pages were visually reviewed, reopen identically and cover five actual private
CUPS jobs. All nine three-application baseline pages match installed 8d2464f.
Installation evidence is recorded separately from build and staged validation.
Words remains paused at rudimentary compliance; experimental columns stay
uninstalled. Sheets remains active; Stage is last. Hypothesis tests, dataset
probability/frequency, matrix regression, database functions, omitted arguments,
reference unions, duration precision and complete model/settings fidelity remain.


## Native Sheets inference and dataset probability — 2026-11-03

Distribution increment 80b88bd is installed and independently verified. This
increment adds six function groups: PROB, FREQUENCY, F-test, paired/pooled/Welch
t-tests, Z-test and chi-square test, with modern/legacy aliases. Catalogue dispatch
reaches 440/641 retained names, with 201 remaining; this counts aliases and does not
measure completion of the full model or numerical interoperability.

PROB independently filters numeric sequences, uses a narrow input-roundoff
allowance and a range-sum tree that retains tiny masses. FREQUENCY handles
unsorted/duplicate bins without the retained code's hard-coded index reversals.
Exact binary sample sums/squares and integer mean cancellation precede quad
ratios and stable Boost probability tails. A one-ULP perturbation among a million
large observations remains significant; the paired-statistics kernel shares the
binary decoder and retains its exact rational checks. Welch degrees of freedom
remain fractional. F-tests use twice the smaller tail, and Z-tests return the
specified signed one-tailed probability rather than the retained code's different
central probability. Data sizes, integer work and convergence remain bounded.

ODF/modern typed filtering and known-sigma singleton rules are explicit. XLSX
legacy TTEST/ZTEST calls use the modern sample rules; string and quoted-sheet
contents are preserved. Sparse/named/dynamic references, parameter arrays,
fixed matrices, errors and unavailable dependencies participate without Qt/GTK.
All 62 applicable Release and ASan/UBSan suites pass, including the million-point
precision case. 1,763 independent 110-digit/exact-rational checks pass. The 229
retained results include 214 agreeing caches, 10 error labels and five documented
boundary/error conventions; none depend on unavailable functions. Earlier
statistics, criteria, order, moment, paired and distribution checks also pass.

Full matrix regression, database and remaining functions, reference unions,
omitted arguments, duration precision and full model/settings fidelity remain.
Words remains paused at rudimentary compliance, its columns preserved/uninstalled;
Stage remains last. Export, CUPS, staging and installed evidence are recorded
separately; this section does not claim an unverified installation.

## Native Sheets live context — 2026-11-03

The current Sheets build adds real INFO/FILENAME context after the committed
matrix regression, database, reference-union and formula-metadata increments.
FILENAME follows document load/Save As identity, exports caches against the
actual target path and rolls runtime identity back on failed load/save. INFO
uses the real host facts, live worksheet count, native Automatic calculation
policy and grid viewport. Headless origin remains unavailable rather than
inventing a viewport. Linux memory values are estimates with their exact
measurement conventions documented in README.md. Lazy query epochs refresh
volatile dependencies without scanning the entire cache for each displayed cell.

All 68 applicable Release and ASan/UBSan suites pass with the established three
outline-font exclusions. Independent information checks cover 1,053 cases, 83
retained unit cases and 400 retained FODS results, with known conventions and
unavailable cases reported explicitly. Native Wayland Release/sanitizer checks
cover real vertical/horizontal scrolling, origin updates without document
modification, editing/history, raw filename/cache preservation and save/reopen.
Five private CUPS jobs cover 21 reviewed pages; the nine three-application
baseline pages match the previous reviewed release. Nineteen current fixture,
UI and export packages pass version-matched schemas; reference-union packages
are separately schema-validated. The optimized stage verifies 29 artifacts,
system FLTK and matching code sections without Qt/GTK ELF linkage. Installation
and independent installed behavior remain separate evidence recorded in the
artifact directory; passing staging does not claim installation.

Catalogue dispatch is 474/641 retained names including aliases, with 167 names
remaining. This is not a whole-suite completion percentage. Manual calculation
settings, locale/format inheritance, relative named-expression semantics,
reference intersection/range/3D and other catalogue functions remain unfinished.
Per the latest user steering, finish this Sheets build and switch to Stage;
further Sheets catalogue work waits. Words stays paused, and experimental
columns remain preserved and uninstalled.

## Native Stage object movement — 2026-11-03

Sheets context build 930f2cc is installed and independently verified; Stage is
now active per the user's latest steering. This increment adds general native
object movement alongside existing custom adjustment handles. Drag a supported
leaf object's body or use arrows (1 pt; Shift 10 pt). Clicks have a four-pixel
threshold. Master objects and read-only/signed documents remain protected.

The model retains accumulated parent transforms so slide-space motion converts
correctly through nested rotations, scales, reflections and shears, including
legacy transformed-group inputs accepted by the reader. Source geometry,
SVG position/dimension values, styled text, paths, image bytes, surrounding
containers and opaque package entries remain intact. A bounded canonical
matrix/translate list stores each moved leaf's transform. Matrix parameters are
unitless and explicit translate parameters carry length units, following
[ODF draw:transform](https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part3-schema/OpenDocument-v1.3-os-part3-schema.html).
The independently produced baseline uses schema-valid groups and transformed
leaf frames; legacy group-transform acceptance is not a claim of schema validity.
Moved frames set presentation:user-transformed so their edited position overrides
master-supplied placement; undo/cancel restores the original flag or its absence.

Movement builds and validates isolated candidates before replacing live state.
Dragging uses one native journal transaction, Escape/focus loss restores its
original XML/state/redo, and a return to origin leaves no edit. Undo/redo restores
selection and exact source XML. Saving an active move closes the gesture and
records the saved point. Invalid coordinates, singular parent transforms and
out-of-bounds candidate geometry fail before changing the live document.

Stage resize/rotate, shape insertion/deletion, stacking/group tools, richer
text editing/layout and the previously listed rendering/model features remain
unfinished. Words stays paused, its experimental columns preserved/uninstalled;
remaining Sheets catalogue/settings work waits.

Validation for this increment passes 69 applicable Release and 69 ASan/UBSan
suites, retaining the same three outline-font exclusions. Native Wayland input
checks pass in both builds for body dragging, exact keyboard nudges, master
protection, click/no-op, Escape/focus cancellation, redo preservation, frame
placement flags, rich source and ODP save/reopen. Thirteen generated/exported
packages, including a moved frame with its user-position flag, pass version-
matched content/styles schemas. Five actual private CUPS jobs cover 13 reviewed
pages; original/edited Stage pages reopen identically and the nine suite baseline
pages match installed Sheets build 930f2cc. The optimized stage verifies 29
artifacts, matching Release sections, system FLTK and no Qt/GTK ELF linkage.
Installed behavior is verified separately after authentication; this section
records build/stage evidence rather than assuming installation succeeded.

## Native Stage object creation and deletion — 2026-11-03

The universal Insert menu now creates rectangles, ellipses, lines and text boxes
through native source transactions. Rectangles and ellipses include editable
paragraphs; a new text box selects its initial text for typing. Default objects
have explicit portable graphic/text styles. Geometry is bounded and validated,
including reverse line endpoints and coincident endpoints after binary64
rounding. Unique drawing names, graphic styles and XML IDs avoid existing names
in both content and styles, including referenced-but-missing names. Insertion
preserves existing fallback paint instead of accidentally resolving an unrelated
missing style. New objects appear above current root objects;
existing explicit stacking values remain unchanged. An existing navigation
order receives the new object's ID.

Canvas Delete and Edit → Delete object remove an editable own-slide leaf;
text-editor Delete continues to edit text. Masters and signed/read-only files
remain protected. Empty slides and groups are valid: deleting a group's final
leaf retains its metadata/container. Other objects, rich source XML, image
entries and opaque archive data remain intact. Navigation lists remove deleted
IDs. Known outside links (including escaped URI fragments), animation/event,
connector, copy and frame-chain references reject deletion before mutation;
automatic relationship rewriting remains unfinished. Reference scans are bounded.

Isolated candidate scenes are validated before replacing live state. Insertion,
deletion, selection and text edits use the native journal, saved points and
branching undo/redo. Primitive and paragraph additions live in content.xml;
other archive parts remain byte-preserved. Shared source-clone lookup also keeps
the earlier movement transaction's exact whitespace-sibling behavior.

The independent ODP producer/oracle checks all new geometry, styles, text,
identifiers, navigation order and unrelated source/archive data. Native Wayland
checks activate the real universal menu, type into a new text box, distinguish
editor Delete from canvas Delete, move an inserted ellipse, exercise history,
and save/reopen. Words stays paused with columns preserved/uninstalled; remaining
Sheets catalogue/settings work waits while Stage remains active. Resize/rotation,
image insertion, stacking/group tools and richer text layout/editing remain.

Validation passes 70 applicable Release and 70 ASan/UBSan suites with the same
three outline-font exclusions. The missing-style collision was reproduced as
an unrelated object's paint changing before the fix; its regression now verifies
unchanged fallback paint. Native Wayland Release/sanitizer input and independent
source/oracle checks pass. Fourteen ODP/ODT/ODS packages pass version-matched
content/styles schemas. Five private CUPS jobs cover 13 reviewed pages; all pages
reopen identically, and the final outputs match the reviewed pages pixel for
pixel. The nine suite baseline pages also match the preceding reviewed build.
The optimized stage verifies 29 artifacts, matching Release sections, system
FLTK and no Qt/GTK ELF linkage. Installation remains separate evidence; the
previous movement installer's authentication wait does not prove this build
installed. No duplicate installer is launched while that handle is live.


## Native Stage position, size and rotation — 2026-11-03

The universal Format → Position and size panel edits an own-slide object's
origin in slide coordinates, frame size before rotation/scaling, and absolute
slide rotation. A line instead exposes its slide-space start, length and angle.
Grouped and reflected/sheared objects retain their existing affine stretch,
shear and reflection. Source geometry changes resize text frames without
scaling fonts; paragraphs reflow and retain their runs and styles. Unchanged
fields, blank fields and complete rotations preserve exact source and redo.
Invalid input stays in the panel; Cancel/Escape have no source effect. Menu
commands return their D-Bus reply before entering the native overlay loop.

The native journal validates an isolated candidate scene before committing.
Position/angle edits preserve dimensions and path data. Size edits preserve
unmodified dimension units; center/radius ellipse aliases normalize only when
resized. Existing viewports retain curve data. Unmapped paths/polygons gain a
viewport where possible; degenerate or large extents use direct source geometry
scaling, including elliptical arcs and Bézier controls. Degenerate path axes,
singular parent transforms, signed documents and resource limits retain explicit
failure/capability handling. Font sizes, identifiers, styles, image data, other
objects and opaque archive parts remain unchanged.

Stage remains active. Words stays paused with experimental columns preserved
and uninstalled; further Sheets catalogue/settings work waits. Drag resize and
rotation handles, archive-part transactions for image insertion, stacking/group
tools, richer text layout/editing and relationship rewriting remain unfinished.
Installation is separate from source, build and staged evidence. The previous
movement installer terminated without installing; the installed manifest still
matches Sheets build 930f2cc. Install the newest fully verified stage directly.

Validation covers 71 applicable Release and 71 ASan/UBSan suites, with the same
three outline-font exclusions. Native Wayland input checks the real universal
menu/panel, Enter and Apply, Cancel/Escape, invalid values, unchanged history,
text reflow from four to eight lines, no overflow in the final resized frame,
and exact ODP source/save/reopen. GUI leak checking stays enabled with narrowly
scoped suppressions for system Fontconfig font-registration/reinitialization
and Mesa Wayland EGL initialization caches; native allocations are unsuppressed.
Six independent SVG arc/Bézier cases compare 3,030 samples and unchanged source.
Those deliberately non-conforming no-viewBox legacy inputs test extensions;
they are separate from strict version-matched ODF schema evidence. Fourteen
supported ODP/ODT/ODS packages pass their content/styles schemas. Five private
CUPS jobs cover thirteen pages: original/edited Stage exports reopen identically,
and all nine suite baseline pages match the preceding reviewed build. The
optimized stage checks 29 artifacts, matching Release sections, system FLTK
and no Qt/GTK ELF linkage. The installed build requires its own receipt.


## ODF 1.2 baseline and embedded Stage images — 2026-11-03

New native ODT/ODS/ODP documents now declare version 1.2 for content/styles and
manifest. Editing existing 1.3/1.4 documents retains their source versions. The
explicit first-page-story operation still upgrades the necessary XML to 1.3;
its test now checks the exact version-only content change and original-source
isolation. New images use the ODF 1.2 image element attributes and package
manifest media type. The earlier draw:mime-type-on-image output was reproduced
as invalid against the official 1.2 schema and corrected.

The native Stage image value validates PNG/JPEG/SVG/GIF/BMP encoded data through
the shared bounded decoder. Local files are regular, at most 16 MiB; supported
raster images retain the existing pixel limits. SVG validation rasterizes at a
bounded probe size and stores the original vector bytes. The Image menu opens
an FLTK chooser and embeds the original bytes, fitting the intrinsic aspect
ratio inside the slide. New names reserve existing archive parts, manifest paths
and unresolved content/styles references to avoid changing other objects.
The image frame has explicit portable graphic properties, XML identity and
navigation order. Package quotas are checked before committing any state.

Stage history now includes non-content archive byte changes alongside exact
content XML. Undo of insertion removes its new image and restores the original
manifest bytes; redo restores them. Ordinary image deletion retains archive
assets for other references. Text-only events share immutable image data and
skip archive comparisons; merge boundaries and the history byte budget include
asset changes. Candidate scenes and all replayed parts validate before live
replacement. Mixed image/text/movement/delete history, saved points, branching,
missing references, signed files and package/history limits have native checks.

The ODF 1.2 function census is separate from retained Calligra registrations:
312 of 392 Part 2 function signatures are available, with 80 missing. This does
not measure numerical, type, reference or settings conformance. OpenFormula
1.2 functions and behaviors take priority over later-version/vendor extensions
when Sheets work resumes. The initial package checker covers manifest schema,
mimetype layout, ZIP integrity/compression and manifest/file correspondence;
full encrypted/signature/RDF/package and consumer/producer conformance remain
unfinished. All functional coverage beyond proven cases remains open.


The final increment passes 73 selected Release and 73 ASan/UBSan suites, retaining
the three existing outline-font exclusions. Real native Wayland tests exercise
the FLTK image chooser, cancel/redo preservation, GPU decoding, movement,
deletion, save/reopen and archive preservation in both builds. GUI leak detection
stays enabled with the existing narrowly scoped system Fontconfig/Mesa startup
cache suppressions. Ten private CUPS jobs cover 23 reviewed pages, including all
five inserted image formats. Reopened pages match exactly; all nine suite
baseline pages match the installed geometry build.

Forty-six packages pass version-matched content/styles schemas. Forty-three
selected packages pass the ODF 1.2 manifest schema and implemented Part 3
checks; the three original legacy print inputs lacking manifest versions are
separate from their repaired exports. Eight malformed package controls are
rejected. Three actual application exports repair missing manifest versions
while retaining unrelated XML and archive data. The repair is serialization-only:
original in-memory manifest bytes remain available to exact history replay.
Newly generated ODF fixtures include the required manifest version. Thirty
staged artifacts include the installed initial coverage inventory; optimized
Release sections and system-FLTK/no-Qt/GTK ELF linkage are verified. Installation
requires a separate installed receipt. Full ODF 1.2 functionality remains open.


## Native Stage scoped text formatting — 2026-11-03

Stage now shares the native source-aware character/paragraph formatter with
Words. The universal Format menu exposes font family/size/weight, italic,
underline, strike, foreground/highlight, alignment, margins, first-line indent
and before/after/line spacing. Only changed fields are written. Effective
format queries include graphic/frame paragraph inheritance, inline runs,
empty styled insertion spans and formatted table text. Name allocation reserves
unresolved foreign style references in both content and styles parts.

Selections use Unicode scalar positions. Character formatting at a nonempty
caret sets transient future typing; empty-frame defaults are a persistent source
edit. Typing updates the exact replacement range supplied by FLTK, rather than
inferring the caret from identical text prefixes. The shared paragraph editor
preserves source runs, hyperlinks, IDs and paragraph/list structures on supported
splits/joins. Cross-container or unsupported embedded-content edits reject on a
candidate document. Candidate scenes validate before publication. Formatting and
text undo/redo restore the source frame, selection/caret and saved state; merged
typing keeps its original cursor and final insertion position.

The lower editor remains a plain-text editing surface with rich formatting
shown in the native GPU preview and printing. Stage's continuous text frames do
not paginate internally, so page break, keep and widow/orphan controls are
inactive and rejected by its formatting API. Full in-canvas rich-text editing,
vertical/column/automatic-fit layout, structural table/list controls and broader
IME transactions remain unfinished. This increment is partial ODF 1.2 text
functionality, not suite conformance. Strict schema coverage uses an independently
produced ODF 1.2 fixture; legacy opaque attributes in model fixtures are separate
compatibility probes. Installation needs its own verified receipt.


## Native Stage direct object transforms — 2026-11-03

The canvas exposes eight resize handles, a center-rotation handle and line
endpoints, with custom modifier diamonds retaining hit-test priority. Square
handles use shape-local axes through arbitrary affine transforms. Opposite
anchors stay fixed; crossing an anchor reflects the frame while serializing
positive dimensions. Shift constrains aspect ratio, or snaps rotations/line
angles to15degrees. Rotation preserves the frame center and existing stretch,
shear and reflection. Side handles retain the opposite edge midpoint. Geometry
updates preserve font sizes, text/styles, identifiers, encoded image data and
unrelated source. Text reflows within the new local frame.

The immutable gesture solver produces size changes and slide-space actions.
A shared candidate-only geometry mutation serves numeric controls and gestures;
complete scenes validate before publication. Each preview derives from the
original source, so rounding does not accumulate. Commit creates one undo
command; Escape/unfocus or returning to the starting geometry restores exact
source, saved-state semantics and the prior redo branch. Read-only/master,
singular-axis and resource limits keep their existing capability guards.

Independent affine point/anchor tests cover132 resize, aspect, reflection,
rotation, snapping and endpoint cases. Native model tests also cover nested
legacy affine groups, frame/image/circle resizing, center rotation, endpoint
anchors, repeated previews, cancellation, saved points, one-command history,
branch preservation and asset/source round trips. Transformed-group legacy
fixtures are extension-consumer probes; independently produced ODF1.2 packages
supply strict schema coverage. Stage remains active and full ODF1.2 functionality
remains incomplete. Stacking/group tools, richer drawing styles, connectors,
frame layout and presentation details still require implementation.


## Native Stage stacking commands — 2026-11-03

The universal Arrange menu adds four sibling-order commands and four matching
commands for the selected object's nearest containing group. A selected child
stays selected when its group moves. Immediate groups remain independent stacking
contexts, so an object cannot inadvertently leave its group. Explicit consecutive
indices normalize tied/missing/saturated indices without moving XML children or
changing navigation order. Front/back boundary commands preserve exact source,
saved points and redo. Candidate scenes and selection identity validate before
publication; history retains one command with previous/next selected indices.

The renderer, insertion path and ordering commands now share the stacking-item
inventory. Opaque drawing, 3D-scene and annotation siblings participate in source
ordering without claiming native rendering support for those contents. Existing
drawing-only editor paths remain stable. Read-only and master objects reject
writes. Nesting/shape-count/index limits remain bounded. Geometry, styled text,
links, IDs, navigation, unrelated slides and archive parts stay unchanged.

The independent strict ODF1.2 fixture covers distinct overlap colors, nested
groups, ties/missing/max indices, a retained connector and source navigation.
The C++ compatibility corpus separately keeps legacy per-shape draw:display and
opaque annotation/3D probes. ODF1.2 draw:display belongs to draw:layer; native layer
visibility/protection and screen-only/print-only layer rules remain unsupported.
Layer authoring and full group creation/ungrouping, multi-selection, drawing
styles/connectors and broader ODF1.2 functionality remain open. Stage is active;
Words stays paused, columns preserved/uninstalled, and further Sheets work waits.
Source, build, stage and installed evidence remain separate gates.


## Native Stage groups and object selection — 2026-11-03

Group-aware object references address a rendered leaf or any containing group,
including anonymous groups. Canonical selections discard duplicates/contained
units without quadratic scans. History positions retain the complete selection
and account for its memory. Neutral sibling grouping keeps original child source
and z order; the new group occupies the frontmost selected slot. Ungrouping
promotes one level with its stacking block intact. Legacy group transforms are
flattened into child transforms as separate consumer-compatibility probes.
Unhandled group-level styling/metadata/anchors/events/glue points and references
to a removed group identity reject ungrouping atomically; their semantic rewrites
remain outstanding ODF1.2 work.

The native canvas adds Shift-click toggles, Ctrl-click individual-member selection,
primary-only containing-group selection, canvas Select all, explicit empty
selection and Ctrl+G/Ctrl+Shift+G grouping commands. GPU outlines show all selected
members and an aggregate frame. The text editor is inactive for groups/multiple
objects. One shared immutable gesture applies world translation, scaling,
rotation or reflection to selected leaves through existing parent transforms.
Group transforms are baked into supported child transforms, preserving valid
strict ODF group structure. Original source is restored on cancel/focus loss or
return-to-start, keeping redo. Frame sizes, font declarations and asset bytes stay
unchanged; group resize scales content while individual-frame resize reflows text.
Singular parent transforms, unsupported transformed contents and coordinate/count
limits retain capability guards.

Batch ordering retains stable selected blocks per immediate stacking context.
Containing-group ordering from a leaf keeps that leaf selected. Batch deletion
checks known external references against the entire removed selection, permits
internal removed references, updates navigation and retains encoded archive
assets. Complete candidates and selection identities validate before publication;
new structural actions retain one undo command and previous/next selections.
Numeric geometry remains a single-shape command; group geometry uses the canvas
selection handles. Full object clipboard operations and richer group/property
editing remain open.

Structural group moves exposed a shared source-addressing defect: adjacent text
nodes can coalesce when XML is serialized/reparsed. Element-only clone paths now
retain source identity across that normalization and verify namespace/local name.
Words scoped-format inspection and all Stage source callers share this helper.
A direct adjacent-text regression and nested wrapper group/ungroup reproduce the
old failure; complete shared-engine regression remains an acceptance gate.

Generic command errors now record status and defer their existing FLTK dialog
until after the desktop-menu D-Bus reply. Error messages remain ordered, avoid
nested error dialogs and block editing while a modal error is active. The native
Wayland test exercises a referenced-object deletion through the universal menu,
closes the real dialog and verifies unchanged source/redo. This fixes the menu
interaction without changing the reference protection.

Source/build/UI/CUPS/schema evidence and installed receipts remain separate.
Groups are partial ODF1.2 drawing functionality; layer display/protection,
connector/chart/3D semantics, group-property/reference rewrites, frame editing and
presentation details remain incomplete. Stage stays active, Words paused with
experimental columns uninstalled, and further Sheets catalogue/settings waits.


## Native Stage layer consumption and protection — 2026-11-03

Native scenes resolve document, page and master-page layer sets using ODF1.2
Part1 sections10.2.2–10.2.3,19.132,19.189 and19.207. A local set replaces the
whole document set, including when empty. Each master uses its own effective
set independently of the slide. Immutable shared policies avoid duplicating a
large document layer catalog in every scene. Layer assignment does not reorder
objects. Default display is always; default protection is false. The four
standard display modes select screen, print, both or neither. Missing layer
references render with default settings and an explicit unsupported diagnostic;
this fallback does not claim that the missing definition has been recovered.
Duplicate names/sets, missing names and invalid display/protection values reject
scene construction. Legacy per-object display attributes remain separate
consumer-compatibility behavior and are not strict ODF1.2 producer evidence.

PDF/CUPS now build print-target scenes independently of the retained screen
scenes and their editor/selection indices. Print-only content is rendered and
screen-only content is excluded, including on masters and locally overridden
pages. Printing does not replace the editing scene or change revision/history.
Native source guards protect text/formatting, shape modifiers, geometry,
movement, group/batch operations, ordering, deletion and whole-slide deletion.
Guards traverse selected source subtrees, including hidden and unrendered
protected members. An editable member of a mixed group can still be edited
individually. Protected visible objects remain selectable for inspection, with
the text editor and transform/adjustment controls inactive. Rejected writes
preserve source, saved state and undo/redo. Ordering editable peers retains
protected z-index spelling/absence; if fixed labels leave no integer slot
without changing protected source or XML/navigation order, the command rejects
atomically. Broader stacking-label/navigation rewriting remains open.

Independent strict ODF1.2 documents exercise all display modes, page/master
replacement, an empty local set, protected text and hidden protected group
members. Model, actual native Wayland controls/pixels, raw source/archive,
export/reopen, PDF raster and private CUPS checks provide separate evidence.
Layer creation/property/assignment controls, missing-reference repair, and
broader group/property/connector/frame/presentation semantics remain unfinished.
This is partial ODF1.2 functionality; source/build/stage and installation require
separate receipts. Stage remains active, Words paused and further Sheets work
waits.


## Native Stage layer editing — 2026-11-03

Arrange → Layers now provides a detached native editor for document, slide and
master definitions. The FLTK panel fits the minimum780×560 application window.
Local sets copy the effective document definitions; returning to inheritance
rejects references to local-only names until the objects are reassigned. New
layers, Unicode names, display/protection, SVG title/description, deletion of
unused layers and assignment to own-slide objects/groups share one Apply/Undo
transaction. Only changed properties are written. Cancel, unchanged commands,
rejected draft actions and stale/foreign-document drafts preserve live source
and history. Draft actions clone their working XML before mutation, so a failed
action also retains the prior draft. Archive limits and both screen/print scenes
validate before publication. Read-only files retain their write guards.

Known draw:layer references are renamed only among consumers of that effective
set. Page/master-local sets shadow the entire document set, so global renames
retain their references, including those on unused local masters. New names
reserve unresolved references. Define missing layer deliberately creates the
missing definition in the chosen scope instead of silently claiming an unrelated
name. Layer title/description values use plain XML text, including newlines;
unchanged metadata, other properties, text/styles, geometry and archive assets
stay retained. General RDF/reference interpretation remains unfinished.

Assignments address the current slide's effective set. Selected groups assign
supported drawable members rather than writing a nonstandard group layer
attribute. Hidden/unrendered members participate in the source guard; protected
objects require an explicit layer unlock before assignment. Intentionally changing
layer protection is allowed through the definitions editor. Hidden selections
become explicitly empty; source identity restores all selected objects/groups on
undo/redo. The history journal includes styles.xml creation/edits and exact
manifest/archive-part restoration, alongside content and selection. New styles
parts retain the document's version. Master geometry itself remains noneditable.

Independent style/content prefixes exposed a shared XML serialization defect:
the old namespace reconciler could reuse a prefix shadowed within an imported
subtree, serializing SVG metadata into the drawing namespace. Imports now retain
source-scope bindings, including prefixes used only in values, and serialization
uses libxml2 DOM namespace reconciliation. A direct reparse regression covers
shadowed element/attribute names, value QName bindings, repeated serialization,
CDATA and source identity. Default element namespaces exposed a second issue:
attributes must retain a nonempty prefix when they have a namespace. Attribute
writes now select or allocate an unshadowed prefix without rebinding existing
QName values; empty-namespace reads, presence checks and removal use the
libxml2 null namespace convention. The independent source corpus exercises
default drawing namespaces in content/styles and a default manifest namespace.
The official API reference is
https://gnome.pages.gitlab.gnome.org/libxml2/html/tree_8h.html.
Selected whole-suite regression and actual exports remain required because this
helper is shared by Words, Sheets and Stage. This does not claim complete QName,
RDF or namespace handling for every structural editor operation.

The actual native Wayland tests use the universal menu, real controls, complete
selection/history, scope-aware renaming, visibility, unlocking/movement,
inheritance, missing-name repair, hidden group members and minimum-window Apply.
The new Stage path restores its caret with the existing Stage helper; sanitizer
coverage reproduced and caught the earlier Words-only helper error. Independent
source/metadata/archive oracles, schema/package checks, exact export/reopen
rasters and CUPS jobs provide separate evidence. Source/build/stage and installed
receipts remain separate gates. Full ODF1.2 functionality, broader group/style/
connector/frame/presentation semantics and object clipboard workflows remain
unfinished. Stage stays active, Words paused, and further Sheets work waits.

## Native Stage fill and stroke authoring — 2026-11-03

The universal Format menu exposes a native FLTK Fill and stroke panel for the
current object selection. It edits no/solid fill and stroke, RGB colors, fill
and stroke opacity, stroke width and the nonzero/even-odd fill rule. Blank or
mixed fields remain unchanged; unchanged displayed values do not materialize
new source formatting. Changes apply together, while cancellation and rejected
values preserve source, saved state and redo. Protected selections are available
for inspection with disabled controls. The panel fits the minimum application
window and returns the universal-menu reply before entering its modal loop.

Selected groups apply the requested properties to drawable members, including
hidden members. Source guards reject the whole edit if any selected subtree
contains protected content. Unsupported member kinds reject atomically. Group
geometry/identity, object order, source text, assets and unrelated styles remain
in place. This does not implement group-level effects or inherited group paint.

Automatic styles are cloned with their untouched attributes and child properties;
common styles get an automatic child with the existing common style as parent.
Presentation objects retain presentation-family references. New names reserve
existing definitions and unresolved references across content and styles so
unrelated objects cannot acquire a formerly missing style. Shared common and
automatic styles, other slides, text properties, padding and opaque archive parts
are preserved. Parent styles must be defined common styles as ODF1.2 requires.
Missing style definitions, style class lists and uniform opacity over an opacity
gradient are explicit pending authoring cases. Existing advanced source data is
retained; this increment does not complete gradients, bitmap/hatch fills,
dashes, joins/caps, markers, shadows or group/style-class rendering.

Both screen and print scenes and archive limits validate before publication.
Each accepted action records complete source and selected-object history;
single-leaf caret/text selection is retained across undo/redo. No-op edits leave
revision and future typing/history unchanged. Independent source/model/Wayland,
GPU pixels, schema/package and CUPS evidence are required separately from stage
and installed receipts. Stage stays active, Words paused and further Sheets
catalogue/settings work waits. Full ODF1.2 functionality remains incomplete.

Actual Wayland opacity pixels reproduced an older shape-rendering defect. The
slide GL state used the source alpha factor for alpha itself, leaving a partially
transparent window over an opaque page. The separate RGB/alpha blending already
used by native text/images is required for shape paint too. The independent
pixel oracle retains the mathematically expected page composite; the desktop
background must not affect the document color. Reference:
https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlendFuncSeparate.xhtml.

Image transparency uses `draw:image-opacity`, independently of the frame fill's
`draw:opacity`. The encoded image's intrinsic alpha is multiplied by image opacity
in native GPU rendering and PDF/CUPS output. Image bytes are retained. The
previous 676f0db stage coupled image transparency to fill opacity; its queued
installation was withdrawn before it ran after the specification/upstream audit.
The corrected corpus keeps a nontrivial fill opacity on image frames so the
old coupling cannot satisfy the image-opacity oracle.

## Independent ODF image opacity — 2026-11-03

ODF1.2 Part1 section20.133 defines image opacity separately from fill opacity in
section20.145. Upstream Calligra PictureShape and LibreOffice's graphic-property
mapping also keep the two values separate. Native scenes now retain an independent
image opacity with an opaque default. Common, automatic, presentation and master
style parsing use the same scoped cascade as other graphic properties. Fill-only
changes do not fade image content. Encoded alpha and image opacity combine in
GPU preview and PDF/CUPS output; a separately painted frame background retains
its own fill opacity. Opacity gradients remain unsupported and now produce an
explicit diagnostic rather than being silently treated as uniform opacity.

The native paint panel exposes a separate Image opacity control and disables it
when the selection contains no image frames. Mixed/group selection values are
aggregated over image frames only. Image-only edits leave ordinary shapes' style
references unchanged, including within selected groups. All existing source
protection, detached-candidate validation, saved-state and exact history rules
remain in force. Standard percentage attributes are written; image/archive bytes
and common parent styles remain unchanged. This correction does not complete
advanced image effects, gradients, cropping, style class lists or ODF1.2.

References: https://docs.oasis-open.org/office/v1.2/os/OpenDocument-v1.2-os-part1.html
and https://raw.githubusercontent.com/LibreOffice/core/master/xmloff/source/draw/sdpropls.cxx.

## Native Stage stroke caps and joins — 2026-11-04

The native scene and scoped Fill and stroke panel now carry the ODF 1.2
`svg:stroke-linecap` and `draw:stroke-linejoin` properties. Butt/round/square
caps and miter/round/bevel/none joins use the existing scoped style cascade,
group-member authoring, source protection and exact history. The default cap
is butt, the default join miter and the initial miter limit 4, following SVG.
ODF 1.2 has no separate standard miter-limit attribute. The Calligra extension
is retained and diagnosed; deprecated middle joins also remain unsupported.
A child join override clears an inherited middle-join diagnostic.

GPU stroke geometry uses a bounded local-space outline and CPU GLU union,
then a single tessellated coverage per paint group. Endpoint/corner shapes,
closed paths, reflections/shear and translucent intersections retain their
geometry without repeatedly blending segment overlap. SVG zero-length
segments/close paths have round/square ink; moveto-only paths stay empty.
PDF retains vector stroke operators for ordinary cap/join modes, preserving
thin-stroke hinting. None joins and degenerate paths use the native outline
because Cairo cannot directly express their full SVG behavior. Headless PDF
and CUPS operation need no GL context.

`native-stage-strokes` supplies independent uniform-space Cairo stroke masks
projected through three affine transforms for 756 cases, analytic geometry
probes, default/inherited style and scoped source/history checks. Stable pixel
regions avoid comparing rasterizers' different edge coverage. Retained probes
separate a Cairo image-backend boundary pixel from the correct vector/PDF
coverage; the exact zigzag also appears in the native Wayland/CUPS corpus.
The independent source includes a near-miter-limit affine case. Actual controls,
protected selections, unchanged style peers, save/reopen and printed output
require the separate per-build receipts. This increment leaves the broader
ODF 1.2 functionality target incomplete.

## Native rich clipboard transport — 2026-11-04

The new `calligra-wayland-clipboard` helper offers several MIME representations
through `ext-data-control-v1`, without Qt, GTK, FLTK backend changes or an X
server. DomeD exposes this protocol. The helper keeps ownership independently
of the launching process, retires on replacement, and transfers data through
nonblocking descriptors with payload, concurrency and deadline limits. Missing
formats have a separate exit status. An unavailable protocol or ambiguous seat
is an explicit error. It never changes the primary selection.

The process bundle is binary framing for multiple representations, not an ODF
fragment format. Independent codec fixtures exercise NUL/non-UTF-8 bytes, empty
representations, truncation, duplicate types and declared quota overflows. The
private native Wayland acceptance harness passes 16 checks in both optimized
Release and ASan/UBSan builds with leak detection enabled. It covers external
`wl-copy`/`wl-paste` interoperability, exact Unicode/binary/empty/8 MiB data,
eight concurrent transfers, stalled/closed targets, stalled/oversized sources,
replacement, survival after launcher exit and compositor disconnect. Tests use
only isolated compositor clipboards and do not access the desktop clipboard.

Stage object copy/cut/paste is still unfinished. Required work includes isolated
ODF object fragments with style/default/font/media dependency closure; inherited
group placement and layer behavior; collision-safe IDs, names and references;
protected/read-only behavior; atomic source/archive/selection history; and real
menu/keyboard, save/reopen, schema/package and CUPS checks. Publishing the entire
original package would expose unselected document content and is not an
acceptable clipboard implementation. The helper is not integrated into Stage
or installed yet; the verified runtime remains `353c016`.

## Selected-object ODP fragments and memory packages — 2026-11-04

Native package loading and serialization now have bounded memory paths, sharing
the file reader's archive/XML checks and the file writer's manifest repair.
Memory export leaves source XML/parts, image identities and saved/history state
unchanged. Failed loads prepare every part before publication. The shared reader
now forces a final EOF/CRC check after the declared payload, including empty
parts; reading exactly the declared length and closing alone had accepted
damaged data. Permanent native tests mutate both empty and nonempty ZIP CRCs.
An independent Python ZIP producer tests 15 malformed cases through both file
and memory readers, separately from schema and rendering validation.

`Presentation::copy_objects` serializes a selected-object subset as a standalone
ODP plus plain paragraph text. It normalizes overlapping selections, prunes
unselected siblings while retaining ancestor group context, and follows scoped
style/default/font, layer and package-resource dependencies. Parent/next styles,
inline styles, encoded media, supported resource-relative links, source version
and namespace bindings survive. Source metadata, unused definitions/assets,
other slides and the original package as a whole are not published. Copying is
allowed for read-only documents and protected objects without changing their
source, history or protection. A live gesture must finish first.

Only a declared embedded ODF object with matching content may own a directory
subpackage; its own metadata and opaque parts are retained. Image/font folder
references, invalid embedded roots and cached directory aliases are rejected
without collecting unrelated assets. Link closure uses the selected source
subtrees, excluding context-only ancestor wrappers and copied layer definitions.

Standard ODF cases use transforms on shapes; ODF1.2 does not allow
`draw:transform` on `draw:g`. The independent source/XML/pixel/print corpus uses
standard leaf matrices. Separate model tests retain legacy nonstandard group
transforms and foreign QName-valued data; those cases do not count as strict
ODF schema evidence. Existing native aggregate gestures already transform the
leaves rather than writing new group transforms.

This remains an unfinished clipboard workflow. Destination default/style/font/
layer isolation, collision-safe IDs/names/references, selection metadata,
prepared atomic cut and actual native clipboard/menu/keyboard integration remain.
General global text/field/settings dependencies, additional object kinds, base
URIs and broader reference interoperability also need further work. Current
extraction rejects missing dependencies, links to unselected targets, xml:base
and unresolved relative hyperlinks explicitly. The verified installed suite is
unchanged; source-only model work is not an installed object clipboard feature.

The source increment passes 88 selected Release and 88 ASan/UBSan suites, with
the three existing outline-font exclusions retained. The final focused tests
also pass after strengthening CRC coverage and separating standard/legacy
fixtures; all six application binaries remain identical to those full-suite
runs. Core leak detection is enabled without suppressions. Seventeen selected
packages pass content/styles schemas and fourteen pass the implemented plain
Part3 checks. Seven private CUPS jobs produce fourteen reviewed pages, with
exact reopen output; all nine existing suite pages match the installed build.
The actual selected-group ODP also transfers byte-for-byte through both the
native helper and external Wayland receivers. These results cover selected
fragment extraction cases, not complete clipboard or ODF1.2 functionality.


## Stage selection metadata and prepared cut — 2026-11-04

Selected-object fragments now carry element-child paths for the original
normalized roots. Source paint order stays in the ODP; the path order retains
the primary selection. Context-only ancestor groups remain distinct from
selected groups. A private versioned MIME envelope carries these paths beside a
standalone ODP without adding private attributes to standard document XML.
Encoder and decoder independently resolve and canonicalize the selected subset,
rederive plain text and exclude unrelated source metadata and assets. Length,
count, depth, traversal, duplicate/overlap and trailing-data limits apply before
or alongside ZIP/XML/dependency checks. Path and batch scene resolution avoid
rescanning every sibling/shape for each selected object. Wire framing and limits
are documented in protocols/README.md.

A move-only native cut draft validates deletion references, editable selected
subtrees and both screen/print candidates without changing live source, archive
parts, selection history, saved state, redo or active gestures. Cancellation and
failed publication therefore need no document rollback. After a caller obtains
clipboard ownership, a separate commit checks the exact model instance,
document identity and revision before using the shared structural transaction.
A copied model sharing the same archive/document token cannot accept the draft;
this regression failed before adding the instance guard. Save As without a
source edit remains compatible with the current saved-state/history markers.
Protected print-only descendants, inbound references and read-only documents
prohibit cut; copying read-only/protected source remains supported.

Ordinary Delete and delayed Cut share detached source removal. Structural
commits now validate screen and print scenes and retain non-content archive
part changes in the existing history journal. Tests cover normalized group
selections, deleting every object from a slide while retaining the slide,
move/cancel/stale/foreign/copied drafts, source/archive/selection undo/redo and
save/reopen. The independent Python protocol producer has three valid cases and
26 invalid cases, including CRC corruption. It parses outgoing envelopes and
ZIP/XML independently and checks source-order text and unselected-data removal.

Destination default/style/font/layer isolation and identity/reference rewriting
remain unfinished. These source APIs have no native clipboard/menu/keyboard
integration yet. The verified installed runtime remains353c016; preparing cut
transactions does not establish a usable clipboard workflow or full ODF1.2
functionality.
