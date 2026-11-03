# Calligra native engines and system FLTK

Words, Sheets and Stage now use native C++20 document engines, libxml2 and
libzip. The default build requires **no Qt or KDE libraries or headers**.
It uses the installed system FLTK shared libraries; an alternative FLTK is an
explicit build option. Wayland remains the default, with X11 as an opt-in.

This remains a basic editing port, not the complete upstream office suite.
Words edits paragraph text and existing inline-style runs and has a native page-flow view; Stage edits slide
text and previews masters, basic vector shapes and text/images; Sheets edits multiple worksheets and calculates
formulas. The complete original Words frame/text-layout behavior, Stage shape/animation model,
and full upstream spreadsheet function catalogue are not reimplemented here.
The original Qt/KDE source remains available only through the opt-in frontend
build. See [PORT_STATUS.md](PORT_STATUS.md).

Sheets functions are the current focus. Words is paused at its rudimentary
layout/editing milestone; its experimental columns remain uninstalled. Stage is last. OpenDocument export and CUPS printing remain the
acceptance floor while this work proceeds.

## Build and install

Dependencies: CMake 3.29+, Ninja, a C++20 compiler, system FLTK 1.4 development
files, OpenGL/GLU, libxml2, libzip, zlib, Fontconfig 2.17+, FreeType, HarfBuzz,
GLib/GIO, ICU, Wayland and X11
headers, Boost.Math/Multiprecision headers, and nlohmann-json. No Qt, ECM, KDE, Eigen or Ixion is used by the native
build. The system FLTK shared-library finder handles distributions whose CMake
config exports static archives absent from their shared-devel package.

```sh
fltk/scripts/build.sh
ctest --test-dir build-native --output-on-failure
python3 fltk/packaging/stage-system.py --stage /absolute/path/to/new-stage
python3 fltk/packaging/install-system.py /absolute/path/to/new-stage --verify-only
sudo -A python3 fltk/packaging/install-system.py /absolute/path/to/new-stage
```

Build and stage as the regular user. Only installation writes system files.
The stage checks every application's runtime links and rejects any Qt or GTK
linkage. Launchers remove the optional DomeD FLTK SDK from the inherited
library search path so the system FLTK is used. No session restart is needed.
On this Fedora system, FLTK's Wayland libdecor decoration plugin dynamically
loads GTK3. The document engines and application ELF dependencies contain no
GTK or Qt; removing this optional decoration path is a separate change.
To select another installed FLTK explicitly, set `FLTK_PREFIX` for the build
script or configure `FLTK_DIR` directly. Native output stays under
`/opt/calligra-fltk`, with command/desktop launchers under `/usr/local`.

The upstream Qt/KDE applications still require explicit configuration:

```sh
cmake -S . -B build-qt -DCALLIGRA_BUILD_FLTK=OFF -DCALLIGRA_BUILD_QT_FRONTENDS=ON
```

## Export and printing baseline

The accepted baseline is OpenDocument export and CUPS printing in Words, Sheets
and Stage. Sheets functions are the active development focus; the remaining
scope and subsequent priorities are documented in PORT_STATUS.md.

Save or Save as writes ODT, ODS or ODP through the native package/model paths.
Imported DOCX/XLSX/PPTX content can be saved in the corresponding OpenDocument
format. Existing unsupported package content and calculation caches retain the
preservation limits described below.

File → Print (Ctrl+P) opens an FLTK dialog with CUPS printer selection, copies,
page ranges and all/current worksheet selection. Words uses native pagination;
Stage prints all slide scenes; Sheets prints calculated cell values in a
paginated A4 grid. Print pages retain source Unicode for scalable-font text extraction, native glyph positioning, supported table
paint, slide geometry and decoded images. CUPS receives a complete PDF job,
using the selected printer's media with fit-to-page. Missing glyphs/unsupported
features are reported before interactive submission. A configured CUPS queue
and the `cups-client` commands `lp`/`lpstat` are required at runtime. No GTK print
dialog or Qt library is used. Cairo PDF/FreeType development files are required
at build time. The system FLTK/libdecor decoration path can still load GTK3.

Batch export/printing does not require a display:

```sh
calligra-words-fltk input.docx --export-odf output.odt
calligra-sheets-fltk input.ods --print-to PRINTER
calligra-stage-fltk input.odp --print-pdf preview.pdf
```

The print preview PDF is generated from the document model, not a screenshot.
Scalable fonts remain vector glyphs; bitmap fonts and supported images use the
bounded native raster path. Source documents are not changed by printing.
Advanced spreadsheet print styles/areas and full upstream office fidelity remain
optional. No physical printer is configured on this system; validation uses real
CUPS test queues.

## Document support

- Words loads ODT and imports DOCX body text, paragraphs and basic tables. It
  preserves existing ODT character-style spans and links while editing runs,
  splits/joins paragraphs in the body, sections and individual table cells,
  distinguishes Enter from Shift+Enter soft breaks, and saves ODT. Native XML
  undo/redo restores formatting, selection/caret and saved-state identity.
  Paragraph continuations follow next styles and avoid repeating forced page
  breaks. Enter creates list items, and Backspace/Delete can join compatible
  items in the same list. Nested suffixes, style overrides and restarts retain
  their scope. Tab/Shift+Tab at paragraph starts or across paragraph selections,
  and Format → List → Indent/Outdent, change item levels without changing text.
  Enter leaves a sole empty root item or a terminal nested empty item. Undo
  restores these formatting-only changes and their selections. In repeated
  table cells, level changes and empty-item exit isolate the selected displayed
  copy, including nested repeated ancestors. A valid no-op keeps repeats compact.
  Cross-cell/unrelated-list joins, complex paragraph-level list moves and
  list-formatting
  controls remain restricted or unfinished.
  Format → Character adds bold/italic (Ctrl+B/I), family, size and color;
  Ctrl+D opens the Font dialog. A selection formats existing runs; a caret
  formats future typing, with saved defaults for empty paragraphs. Valid empty
  ODT bodies gain their first paragraph when typing, formatting or assigning a
  style; undo can restore the empty body.
  Format → Paragraph edits alignment, point-based margins/indent and spacing,
  line-height multipliers, keeps, page-before and widow/orphan limits.
  Ctrl+L/E/R/J aligns left/center/right/justified. Mixed or unchanged dialog
  fields preserve their source values, and native undo restores the styled XML.
  Invalid values remain in the dialog for correction. Font-family lists use
  the same quote/comma parser as rendering; invalid or empty names cannot be
  committed. Select All ends a transient typing format.
  The edit buffer stays plain text; Pages and print output display formatting.
  The Font dialog also sets character highlighting (`#RRGGBB`) or clears it
  (`transparent`), preserving inherited styles, mixed selections and typing
  formats. Native pages and PDF/CUPS share cluster background geometry, including
  bidi, justified spaces and tab gaps; selection remains visible over highlights.
  Ctrl+U toggles underline; the Character menu and Font dialog also toggle
  strikethrough. Imported solid, dotted, dashed and wavy lines retain their
  colors, widths, double lines and whitespace mode. Font metrics position the
  effects; source ranges use ligature carets without changing glyph advances.
  PDF/CUPS and native GPU pages share the same strokes. Text replacement
  strikethrough currently uses the ODF line-style fallback.
  Format → Paragraph styles lists the document's named paragraph styles.
  New/Edit changes names, inheritance, next styles, outline roles and text or
  paragraph properties. Apply commits staged definitions and optional paragraph
  assignment together; Cancel discards them. Existing style identifiers stay
  stable when renamed, and imported properties retain their original scope.
  Assignment replaces paragraph-direct overrides while keeping inline character
  styles, links and IDs. Explicit outline roles convert paragraphs/headings;
  Enter follows the next style and its heading role. Only unused definitions
  can be deleted. Source and changed ZIP parts share one undo/redo step, including
  exact styles/manifest bytes, missing-part restoration and grouped rollback.
  Format → Character styles manages named text styles with stable identifiers,
  inheritance and text properties. Range assignment replaces character overrides
  while retaining paragraph/cell defaults, links, metadata and source IDs;
  unselected text retains its original style chain. Default character style
  clears the selected inline overrides. At a caret, the chosen name applies to
  future typing, so definition edits remain live until insertion. Selection
  changes and undo end transient typing styles. Definitions and range assignment
  share the source/ZIP undo transaction. Visited-link/style-class assignment and
  outline-number editing still need their specialized controls.
  File → Page setup edits the page style at the caret in either editor view.
  A4/A5/Letter/Legal presets, orientation, custom paper dimensions and margins
  use millimetres. Apply checks native pagination before committing; an invalid
  area stays in the dialog for correction. Cancel and unchanged Apply preserve
  the document. Shared layout definitions are copied when editing one master;
  private layouts are updated without accumulating unused definitions. Other
  masters, source units/properties, headers and footer data are retained.
  Page definitions use the same archive-part undo, including creating a missing
  styles part and restoring its absence.
  Native page layout and PDF/CUPS printing now render headers and footers,
  with default, first and left variants, three aligned regions, box borders,
  padding/background, minimum/fixed height and dynamic spacing. Dynamic page
  numbers/counts respect numbering restarts and adjacent-page fields. Body
  pagination reserves their space without mixing their source offsets into
  body editing. File → Header and footer, or clicking an existing header/footer,
  selects the page style, variant and region. The scoped editor reuses Words
  text, formatting, list and table commands. Finish applies one parent undo
  step; Cancel retains the parent and its redo history. Save applies the story
  and saves the complete document. Show on pages suppresses a variant while
  retaining its source. Insert → Page number / Page count adds dynamic fields;
  fields show stored values while editing and update on document pages. Select
  a whole field to replace or format it. Header automatic styles stay in
  styles.xml independently of same-named body styles; shared named-style edits
  affect the document. Direct content is retained as the left region when
  creating aligned regions. Adding first-page content upgrades older ODF
  content/styles to version 1.3. Shadows, mirrored/blank-page rules, anchored
  objects and broader header/footer fidelity remain unfinished.
  Table → Insert table adds a bordered grid at the caret in the body, a section,
  or an existing cell, with configurable rows/columns and an optional repeating
  first row. Rich paragraph text on either side is retained. New tables fit
  within 10000 cells; native pagination checks the proposed layout before
  insertion. Table → Delete table removes the nearest enclosing table; nested
  deletion retains its outer cell. Root page transitions remain on a caret
  paragraph. Insertion/deletion, selected ranges and changed package parts use
  one structural undo transaction. Table → Rows/Columns inserts above/below or
  left/right and deletes the current row/column. Spans adjust across the edited
  boundary; a surviving merge retains its anchor content and formatting, including hidden
  paragraphs and IDs from the surviving covered cell.
  Header groups and repeated cells keep their source semantics. The selected
  repeated cell, including enclosing repeated cells around nested tables, becomes
  independently editable after the command. Undo restores its cell instance and
  exact archive parts. Native page typing, deletion, cut/paste and paragraph/soft
  breaks also localize preexisting repeated cells, including nested ancestors,
  before changing text. Materialization and the text edit share one undo step;
  cancelled/empty events restore the original source and transient typing format.
  Character/paragraph formats, named assignments and tab stops also localize a
  selected repeated cell and return its remapped selection. Caret-only character
  formatting stays transient on nonempty text; empty paragraph defaults persist
  only in the selected copy. Named-definition edits and document tab defaults
  keep their global scope. Cross-cell ranges in repeated source still need richer
  selection metadata. Row/column edits reject formula-bearing tables until
  reference adjustment is implemented. Table → Cells → Merge cells combines a
  rectangle starting at the current cell; every existing span must fit inside it.
  Paragraphs, lists, links and nested tables remain in row order. Moved content
  retains its inherited appearance through explicit paragraph formatting when
  cell styles differ. Split cell restores an existing merge's underlying grid;
  combined text stays in its first cell, while retained covered-cell content
  becomes editable in its own cells. Both operations have one-step exact undo.
  Merge rejects repeating-header/body boundaries and selected numeric/formula
  data pending value/reference merging. Subdivide cell accepts explicit row and
  column part counts, including an independent cell. It refines existing grid
  boundaries and extends neighboring cells across the new slots; original text
  and values stay in the first part. Fixed measures and flexible-column ratios
  are retained, and the whole change has one-step exact undo. Existing merged
  cells follow their underlying grid proportions by default. Equal measured sizes
  instead creates equal physical row/column parts across those measures, retaining
  old boundaries for neighboring cells. The measured sizes become explicit ODF
  dimensions; later page-size changes keep them fixed. Row parts grow uniformly
  if narrower cells need more text height. Subdivision under implicit fixed-column
  shrink also preserves the current resolved widths. The selected page and nested
  cell padding/borders determine the measurement context. Direct placement in
  lists still needs an anchored table or list splitting.
  Page selections retain both displayed cell paths. Shift navigation and mouse
  selection can cross repeated/nested cells even when their source offsets match.
  Highlighting, copying, character/paragraph formatting, named styles and tabs
  use that displayed range and retain exact structural undo/redo. Covered-cell
  metadata is excluded. Typing, paste, Cut and deletion across cells retain
  tables and other container boundaries, clear selected text, and insert at the
  logical range start. Paragraphs and supported sibling list items join within
  each cell; paragraph and soft breaks stay in the starting cell. Preparation and
  input share one undo command, with exact rollback on cancelled/empty input.
  Numeric/formula and unsupported embedded-object edits reject atomically.
  Empty source cells have virtual carets without adding XML. Typing, paste and
  paragraph/soft breaks create a real paragraph in only the addressed cell;
  repeated and nested instances remain independent. Cancelled or empty input
  and boundary deletion preserve the original empty source. Undo restores its
  virtual caret. Cell text formatting is inherited by the first input.
  List Indent/Outdent act on displayed ranges across cells and independent lists.
  Multi-paragraph items move once; selected descendants travel with their parent.
  Sibling groups retain numbering/styles, source order and exact selection/history.
  Root-level outdent discards structural preparation as a no-op. Native page
  Tab/Shift+Tab use these list operations before text replacement. Unsupported
  paragraph-level moves with source content after a nested list remain guarded.
  Table → Cells → Cell format edits each side's padding and border, background
  color (including transparent) and vertical alignment. It retains effective
  inherited styles and changes only the chosen displayed instance, including
  repeated/nested content. Blank fields retain existing properties. The caret
  is retained across materialization and the complete edit has one-step undo.
  Table → Sizing edits the current table's available, fixed or percentage width
  and left/center/right alignment, the anchor row's automatic/minimum/exact
  height, and the anchor column's fixed width or relative weight. Physical sizes
  use millimetres. Exact heights clip overflow; fixed columns retain their widths
  while relative columns share the remaining table width. Private style copies
  retain unrelated inherited properties, source units, values and empty cells.
  Repeated/nested instances keep their scope and the caret; source/style parts
  share one exact undo step. Cancel, invalid and unchanged Apply preserve history.
  Fixed conversion relocates an inherited default percentage to existing table
  style roots and unstyled peers, retaining their appearance and parent-style
  inheritance. Other document-default properties remain. Scale fixed columns
  proportionally uses their current rendered widths and the selected page/nested
  text area; weighted columns retain their ratios. It also preserves resolved
  shrink-to-fit proportions. Individual column sizing is disabled while scaling.
  A visible fixed column that has shrunk to zero needs a positive table width
  applied without proportional scaling first; an entirely zero-width table has
  no proportions to resize.
  Repeated formula-containing source
  cloning still requires reference adjustment; independent value/formula cells
  support geometry-only edits. Optimal content-based sizing remains unfinished.
  Format → Tabs adds, edits and deletes stops for the selected paragraphs.
  Stops support left/right/center/Unicode-character alignment, line patterns,
  double strokes, color, width and text leaders with named character styles.
  Clear all explicitly suppresses inherited stops; Use inherited removes the
  paragraph's own override. Mixed selections remain unchanged until replaced.
  The interval is a document-wide paragraph default, not a per-selection value.
  Changing the default origin requires an existing ODF 1.4 document; older
  versions remain unchanged. Apply validates the candidate's native pagination
  and commits source/defaults as one undo step; Cancel discards staged changes.
  Words opens in native paginated editing with an active caret. Click for a caret, drag or use
  Shift+movement for a range, double-click a word or triple-click a paragraph.
  Edge-held selection drags keep scrolling until release or Escape. Vertical
  arrows preserve the horizontal target across indented lines.
  In tables, Up/Down finish the current cell/subtree and retain its column;
  merged/nested cells and repeated headers preserve source identity. Entry and
  page jumps select through cell bounds, including empty continuations.
  Visual arrows follow grapheme/ligature and bidi positions; Ctrl+arrows move
  by word/paragraph, Ctrl+Home/End reach document boundaries. Typing, clipboard,
  formatting and list commands share the native undo history. Wayland IME
  composition has one undo step, cancellation and a native candidate anchor.
  Escape cancels preedit/selection. Space inserts text immediately; navigation
  keys move the document caret. The wheel scrolls the viewport.
  View → Text view (or `--text-view`) explicitly opens the legacy text buffer;
  View → Page layout returns to native editing and reveals the same selection.
  `--page-layout` remains accepted. New/Open reset the caret to the start and
  clear old page geometry. Further editing and layout polish remains pending.
  Native textual lists support nested numbering/bullets, continuation, restarts,
  headers, label styling and both ODF spacing modes. Generated labels stay out
  of the editable source text. The same engine renders lists in Stage frames.
  Image bullets retain packaged or inline PNG/JPEG/SVG/GIF/BMP assets, with
  declared size and vertical alignment. Missing/invalid images show a placeholder;
  external image URLs are diagnosed without fetching files or network data.
  Paragraph tabs use inherited/custom stops and default intervals, with
  left/right/center/Unicode delimiter alignment, margin origins, hanging indents,
  RTL fields and native wrapping. Line leaders support solid/dotted/dashed/wave
  and double patterns; text leaders retain scoped font/color styles. Their
  geometry and cached glyphs stay outside the editable source text.
- Sheets loads ODS and imports XLSX cell data, shared/inline Unicode strings,
  numeric/Boolean values, shared and fixed array formulas, cross-sheet references, global named
  ranges, sparse cells, and common 1900/1904 date/time values. It supports cell
  editing, undo/redo, TSV paste and ODS saves. It does not export XLSX.
- Stage loads ODP and imports PPTX text boxes and embedded PNG/JPEG images,
  dimensions, order and frame positions. It supports text editing, slide
  insertion/deletion, document-wide native undo/redo, ODP saves and a basic F5
  presentation view. Undo restores the edited slide/frame and its styled XML;
  typing is grouped until a frame/slide selection, save, undo/redo or slide
  operation ends the group. Saved-state tracking works in both history directions. Placeholder
  positions can come from a PPTX layout; full PPTX master/theme inheritance is absent.
  Native ODP scenes resolve each slide's referenced master and page layout,
  master backgrounds/objects, scoped automatic styles and basic inherited paint.
  Selected custom shapes expose native Cartesian/polar adjustment handles.
  Dragging updates writable modifiers with bounds and group/mirror transforms;
  Escape cancels the drag, and one Undo restores the whole completed gesture.
  Shapes without text can also be selected and adjusted.
  Rectangles, full ellipses/circles, polygons, polylines and lines use GPU geometry;
  ordered groups, affine transforms and inverse hit testing retain editable
  nested text-frame identity. Images and native shaped text use those transforms.
  The shared glyph cache is bounded to 64 MiB/4,096 entries; the shared Words/Stage image cache to
  128 MiB/4,096 entries, keyed by encoded content and SVG raster size so inline
  images survive reflow and undo without repeated uploads. Polygon tessellation uses GLU with at most 1,024 input points.
  Native ODP frame text retains separate paragraphs, inherited paragraph styles,
  styled spans/links, fonts and foreground colors. The shared reader resolves
  automatic styles and font declarations in their original XML scopes. Frame
  layout uses native shaping, bidi, wrapping, margins, spacing and line heights,
  independent side padding and top/middle/bottom alignment. Overflow remains a
  continuous flow clipped by the frame rather than a paginated first-page preview.
  SVG path data in ODP retains move/line, cubic/quadratic Bézier, shorthand and
  elliptical arc commands, compound subpaths and closure. Exact curve bounds,
  viewBox mapping, nonzero/even-odd fills and inverse hit tests use the native
  geometry model. GPU contours refine to a quarter device pixel as the viewport
  or pixel density changes. Rounded rectangles and full/arc/chord/pie ellipses
  use the same curve model. Open subpaths close for fills while remaining open
  for strokes. Input paths are bounded to 16,384 segments/subpaths, one MiB of
  data and 16,384 flattened points per path; a slide has a million-point display
  budget. GLU fill output remains bounded to 300,000 triangle vertices per shape.
  Custom shapes with explicit ODF enhanced geometry now use native equation and
  path engines: modifier/equation references, arithmetic, the ten standard
  functions and geometry identifiers, all enhanced commands including G, and
  independent even-odd path sets with F/S fill/stroke control. Geometry mirrors
  and formula-defined horizontal text areas work without Qt. Text on supported
  shapes can be selected, edited and saved with the original equations/modifiers
  retained. Legacy ODF T/U arc behavior follows the original loader through ODF
  1.3; ODF 1.4 uses radial angles and clockwise arcs. Formula evaluation is
  bounded and checks invalid references, cycles and non-finite results.
  Native tables in unlinked frames support nested/merged/repeated cells, scoped
  cell text styles, fixed/relative columns, row heights, padding and vertical
  alignment. GPU backgrounds and border patterns follow frame transforms; cell
  clipping covers glyphs, tab leaders and image bullets. Supported text edits
  preserve the source grid and inline line breaks through undo/save/reopen.
  Structural list/table editing, text auto-growth/fit, vertical writing, custom
  preset lookup, stretch points, extrusion/text-on-path, advanced strokes/fills,
  master placeholder fields and animation/timing still require a full port.

Office imports support Transitional and Strict namespaces and are content/data
conversions, not full visual fidelity. DOCX formatting, headers/footers,
numbering and drawings are omitted. PPTX themes, master content, rotations,
crops, charts, media and animations are omitted. XLSX visual formatting, charts,
pivots, macros, XLSX data-table formulas and local named expressions are not
supported. Ctrl+S proposes an ODF copy; imported Office files cannot be
accidentally overwritten by an ODF save. Legacy binary formats are unsupported.

Unedited ODF package members, styles, metadata and media are preserved. Saves
use a temporary ZIP, fsync and atomic rename, with the mimetype first and
uncompressed. Signatures make ODF documents read-only; signed/macro-enabled
Office packages and encrypted documents are rejected. XML entities/DTDs,
excessive depth, escaping paths and external media fetches are rejected.
Limits remain 10,000 entries, 64 MiB per entry, 256 MiB total, one million
imported spreadsheet value cells, 100,000 imported text/shape nodes and 4,096 slides.

## Native Words page flow

The native C++ model reads ODF common/automatic paragraph and character styles,
parent/default inheritance, font-size percentages, inline runs, page dimensions
and margins, master-page continuation and explicit page breaks. Its pagination
supports paragraph margins/indentation, line spacing, alignment/justification,
keep-together, keep-with-next and widow/orphan thresholds. Unicode line-break
and grapheme boundaries use [ICU](https://unicode-org.github.io/icu/userguide/boundaryanalysis/);
forced wrapping does not split combining sequences or emoji ZWJ clusters.
The layout retains paragraph/source offsets independently of the UTF-8 editor
and screen zoom. Unchanged page widths reuse wrapped lines across page breaks.

The page engine shapes through HarfBuzz and renders FreeType glyphs directly.
Measurements, ligature/combining source clusters, kerning, joining forms and
glyph positions share this path; FLTK/Pango metrics no longer shape document
text. ICU resolves paragraph direction and visual runs for each wrapped line,
including mixed RTL/LTR text and numbers. Soft line breaks retain paragraph
direction. Start/end alignment and indentation follow that direction. Existing
space justification expands the positioned glyph advances.

Declared family lists, numeric weights, bold/italic faces, language and the
weight/italic/slant axes of variable faces are supported. A private system
Fontconfig configuration selects document fonts and script fallback without
changing the FLTK session configuration. Unstyled content prefers the session
font, with a system monospace fallback. Both FreeType bitmap strikes and outline
faces work. Font handles retain their own library and shaping face; raster size
changes cannot change the calculation metrics. The implementation follows
[HarfBuzz cluster semantics](https://harfbuzz.github.io/working-with-harfbuzz-clusters.html)
and [ICU line bidi runs](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ubidi_8h.html).

The FLTK page view uploads glyph masks only on cache misses, then draws their
positioned textures on the GPU. Scrolling reuses textures; a new raster scale
can populate additional entries. Session appearance changes refresh the layout.
This establishes a native shaping/rendering path, not full typography or print
parity. Embedded document fonts, complete complex-script style properties,
font synthesis, arbitrary variable axes, color glyph painting, advanced
justification/hyphenation and vertical text remain incomplete. Rich formatting
controls, advanced table pagination/section layout, structural list editing, columns, notes,
advanced header/footer behavior, anchored shapes and page-style details remain
to be ported.
Words paints native table grids in ordered page flow and repeats complete
header groups. Independent source cursors split long cells, continue merged cells
and nested tables, and rewrap their remaining text when the next master changes
page width. Whole glyph lines move between pages; declared fixed-height cell
cropping remains intentional. Fixed row dimensions retain
their total height and minimum row heights can span pages. Source cell selection
includes nested cells and repeated headers. Impossible line/keep constraints are
diagnosed, and impossible padding fails without emitting decoration-only pages.
Keep-with-next now crosses paragraph/table boundaries and nested cell blocks.
Chains that fit a fresh page move together; long chains keep their terminal
fragment with the following paragraph/table prefix when constraints permit.
Remaining-source probes reserve that prefix only in cells controlling the final
fragment, so unrelated short cells can finish naturally. Explicit page breaks
and master assignments take precedence, including assigning the current master
again. Conflicting keep/row/widow constraints retain bounded source progress and
diagnostics. Paragraph page-master assignments inside cells are ignored as
required by ODF, including inherited styles in repeated and nested cells; the
same styles still assign page masters to root paragraphs. Nested table master
handling and header spans crossing into body rows remain unfinished. The
continuous Stage frame path shares cell geometry and GPU paint. DOCX
import still omits styles/page geometry. No PDF/printing parity is claimed.

Layout limits are one million Unicode scalars per paragraph, four million per
document, 100,000 paragraphs and 10,000 pages. The glyph cache holds at most 4,096
glyph/size entries and 64 MiB of alpha textures and rejects oversized raster
masks. Typeface and fallback caches are bounded. Layout and page viewing preserve
the package's existing styles; editing/saving uses the existing ODT model.

The shaping reference tests require `hb-shape` and the Noto Sans/Serif,
Arabic/Hebrew/Devanagari, Liberation Mono and Unifont reference families. Run
`python3 fltk/tests/shaping_reference.py` after the build to compare glyph IDs,
source clusters and all advances/offsets with the independently invoked CLI.

## Native spreadsheet calculation

The native engine has a Unicode-aware parser, typed values, ranges, cross-sheet
and named references, recalculation, lazy IF/IFERROR evaluation, cycle/depth
checks, and error propagation. It uses standard C++ math and GLib Unicode
operations; no external calculation engine or Python process is required.
Formula input is limited to 8,192 Unicode characters and expression nesting to
256 levels. Excessive input reports a limit/depth error.

Typed, two-dimensional arrays preserve numbers, Booleans, Unicode strings,
blanks and errors. Array constants use `{1,2;3,4}` in the editor and the correct
OpenFormula column/row separators in saved ODF. Scalar functions and arithmetic
map arrays element by element. Ordinary range expressions use scalar
intersection; range-consuming functions and fixed matrix formulas evaluate
complete arrays, including nested scalar functions.
The retained Calligra extension also permits scalar expressions and references
inside array entries, including `DATE(...)`. Nested/multi-cell array entries
report a value error instead of silently choosing an element.

Reference and lookup functions include CHOOSE, INDEX, MATCH, LOOKUP,
VLOOKUP/HLOOKUP, OFFSET, INDIRECT, ROW/COLUMN, ROWS/COLUMNS and SHEET/SHEETS.
Lookups provide case-insensitive literal matching and sorted approximate
matching. INDIRECT supports A1 references and absolute R1C1 references.
SUMPRODUCT, TRANSPOSE, MMULT, MDETERM and MINVERSE operate on typed arrays.
Materialized arrays are limited to one million cells; inversion/determinant
matrices to 512 by 512, and matrix multiplication to 100 million products.
Sparse range aggregates and dimension queries do not materialize huge ranges.

Existing ODF fixed matrix blocks and imported XLSX array formulas recalculate all their result cells, including
dependent formulas and missing/stale imported caches. Save updates each result
cache while preserving the block's extent. Edit the master formula in its
top-left cell; individual result cells are protected. The UI does not yet
provide matrix-block creation/deletion or dynamic spill ranges. An array result
in a normal cell displays and caches its top-left element.
XLSX array masters retain their declared rectangle as ODF matrix spans; missing
result caches are calculated, and unchanged unsupported formulas retain their
source caches. Malformed, overlapping or conflicting blocks are rejected, with
a total limit of one million matrix result cells per workbook. Single-cell
array formulas also keep array evaluation for their input ranges. This follows
the [SpreadsheetML formula model](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.cellformula).

The engineering module implements the retained upstream group of 50 functions:
`BASE`, `DECIMAL`, the twelve binary/octal/hex/decimal conversions, `DELTA`,
`GESTEP`, `ERF`/`ERFC`, four Bessel functions, `CONVERT`, `COMPLEX`, and all 26
`IM*` operations. Legacy LibreOffice analysis aliases and applicable Excel
`_xlfn.` names resolve to the same implementations. Complex values retain real
and imaginary coefficients and an `i`/`j` suffix through references and arrays.
`IMSUM`/`IMPRODUCT` consume complete ranges; scalar operations map array inputs.
Ordinary real-valued arithmetic uses the real coefficient, following the retained
Calligra value converter. `ISNUMBER` recognizes the distinct complex type;
`ISTEXT` does not. ODF has no complex cache type, so complex formula results save
as text and regain their calculation type when their formulas are reevaluated.

`CONVERT` supports the 13 upstream dimensions, case-sensitive aliases, SI prefixes
on metric units, binary prefixes on information units, squared/cubed prefixes
and affine temperature scales (including prefixed Kelvin). Exact unit symbols
win over possible prefix interpretations. Factors follow ODF definitions with
modern physical constants; inaccurate inherited mass/energy/volume factors are
corrected. Astronomical units use the modern IAU parsec and Julian year. The
reference test explicitly distinguishes IT BTU from rounded ISO BTU. Compatibility
extensions retain one-argument `BASE` and two-argument `ERFC`. Invalid complex
text is ignored by `IMSUM` and rejected by `IMPRODUCT`; zero-base `IMPOWER` returns
`#NUM!` under the ODF constraint. Floating-point overflow/domain failures are cell
errors; special-function non-convergence cannot terminate the application.

The native financial module implements all 64 active retained names: `PMT`, `PV`, `FV`, `NPER`,
`RATE`, `IPMT`, `PPMT`, `ISPMT`, `CUMIPMT`, `CUMPRINC`, `NPV`, `IRR`, `MIRR`,
`FVSCHEDULE`, `EFFECT`/`EFFECTIVE`, `NOMINAL`, `PDURATION`, `DURATION`, `RRI`, `COMPOUND`, `CONTINUOUS`, `FV_ANNUITY`, `PV_ANNUITY`,
`LEVEL_COUPON`, `ZERO_COUPON`, `SLN`, `SYD`, `DB`, `DDB`, `VDB`, `DOLLARDE`,
`DOLLARFR`, `XIRR`, `XNPV`, `ACCRINTM`, `DISC`, `INTRATE`, `PRICEDISC`,
`RECEIVED`, `YIELDDISC`, `COUPDAYBS`, `COUPDAYS`, `COUPDAYSNC`, `COUPNCD`,
`COUPNUM`, `COUPPCD`, `PRICE`, `YIELD`, `MDURATION`, `DURATION_ADD`, `ACCRINT`,
`AMORDEGRC`, `AMORLINC`, `EURO`, `EUROCONVERT`, `ORG.OPENOFFICE.CONVERT`,
`ODDLPRICE`, `ODDLYIELD`, `PRICEMAT`, `YIELDMAT`, `TBILLEQ`, `TBILLPRICE` and
`TBILLYIELD`. Loan functions share stable annuity calculations, including
zero/near-zero rates, and cumulative interest uses a closed sum. Iterative
return/rate solvers have bounded work and report non-convergence as a cell
error. Fractional VDB boundaries retain the original calculation behavior.
Range-consuming financial functions retain row order and use sparse range
visits. Referenced text, Boolean and empty cells are excluded from numeric
sequences; direct scalar arguments follow numeric conversion. Scalar financial
functions map typed arrays. Applicable legacy analysis, Excel `_xlfn.` and
`COM.MICROSOFT.` names resolve to the same implementations.

`XIRR`/`XNPV` retain cash/date pairing and combine equal-date flows. Dates use
the native Gregorian calendar and an explicit workbook-owned serial epoch. ODF
`table:null-date` applies to imported dates, `DATE`, date extraction, calendar
functions and all dated financial families. XLSX 1904 imports retain 1904-01-01
as their ODF null date, so raw serials and formula constants agree with styled
date cells. Default/1900 imports retain the existing 1899-12-30 normalization
and early-Excel-date limits. ISO date text is accepted, and
fractional serial days are truncated. The solver works in log-rate space with
a contracting bracket and bounded Newton steps; zero leading flows and extreme
date spans do not change its root. Array shape differences retain row-wise
pairing; invalid dates, mismatched counts and non-convergence return cell errors.
Real numeric grid labels reduce precision to fit the column, retaining their
exponent; insufficient widths show overflow markers. Copying and editing retain
the full value. Complete number/style/locale formatting remains unported.
Each engine owns its context; opening another workbook does not change its
calculation epoch. The original calculation settings beyond this reference date,
locale parsing and complete date-function/coercion fidelity remain unported.

The shared native Gregorian calendar now implements `YEARFRAC` and `DAYS360`
with all five OpenFormula financial bases. Financial US Basis 0 and the signed
US `DAYS360` method retain their distinct February/end-of-month rules.
Actual/Actual uses the specified leap/average-year denominator, with constant-time
Gregorian leap counting for long intervals. The six dated securities functions
above share this model, including typed arrays, dependencies and analysis aliases.
Nine differences from retained upstream test expectations are documented in
VALIDATION.md; this is an explicit OpenFormula choice rather than a claim of
identical historical numerical behavior.

The native coupon model now supplies previous/next dates, remaining payments
and before/after/period day counts. Dates are anchored to maturity with its
original day or end-of-month rule, avoiding cumulative February clamping drift.
The six coupon queries preserve the retained MSRB, actual, 30E and 30E+ day
conventions. The coupon MSRB convention differs from YEARFRAC Basis 0 at
February endpoints; these are separate documented behaviors. Retained extensions
include frequencies 3, 6 and 12, basis 5 (30E+/360), and COUPNUM's fifth end-of-month
argument. Other coupon queries retain their original three/four-argument interface.
COUPNCD/COUPPCD return typed dates and save date caches. Schedule lookup uses
constant-time month arithmetic, including wide year spans.

Regular-coupon `PRICE` and `YIELD` now use the shared schedule. Valuation uses
scaled positive cash flows and logarithmic-rate inversion, with bounded iteration
and an explicit no-root result for degenerate zero-time payments. Binary polynomial
sums compute values and first moments in logarithmic work per evaluation, avoiding
loops across every payment and cancellation at zero rates. PRICE keeps the retained
OpenFormula exponential single-payment convention; it does not implement Excel's
special linear single-payment branch. Zero-yield PRICE is supported, and YIELD
can return negative rates for premium bonds within its representable domain.

Five/six-argument `DURATION`, `DURATION_ADD` and `MDURATION` now share the retained
YEARFRAC-timed duration calculation, independently of PRICE's period clock.
Three-argument `DURATION` remains the Calligra annuity extension. The calendar
clock has documented differences from published Excel duration examples; it is
not silently replaced with another clock to match those examples. Supported
duration caches are now recalculated. Odd-last-period and maturity-price/yield
functions now share the native calendar layer. ACCRINT retains its older simple
accrual algorithm, including its separate financial day-count helper; it validates
but does not use First to split coupon periods. Its registered six/seven-argument
form does not implement the newer CalcMethod argument. Treasury bills preserve
the original 30/360 and inclusive-maturity rules, which differ from the actual-day
standard formula.

French AMORLINC/AMORDEGRC retain the original first-period and rounded recurrence
behavior. AMORLINC rejects arrays as in the original module; zero cost/rate has a
safe zero result. Degressive calculations detect rounded fixed points, and a
100,000-step work bound preserves source caches if a recurrence cannot finish.
EURO/EUROCONVERT use the retained fixed historical table, including the LUX
compatibility spelling, destination rounding and optional euro triangulation.
ORG.OPENOFFICE.CONVERT retains uppercase-only codes and full precision. These
functions use no live exchange rates.

The 64-entry count includes the dotted legacy conversion entry omitted by the
earlier source inventory. A comment-aware audit now matches every active original
registration to a native handler. This is catalogue coverage, not complete
calculation settings/model or Excel/OpenFormula interoperability fidelity. The
originally unregistered odd-first-period functions remain unsupported; unknown
formula caches continue to be preserved.

The descriptive statistics increment adds `AVERAGEA`, `AVEDEV`, `DEVSQ`,
`DEVSQA` (a retained Calligra extension), `GEOMEAN`, `HARMEAN`, `STDEVA`,
`STDEVPA`, `VARA` and `VARPA`. Complete typed arrays, named/cross-sheet references
and sparse ranges are consumed as datasets. Empty cells are excluded; the A
variants count all text, including numeric and empty strings, as zero and logical
values as zero/one. Other variants ignore collection text/logicals and retain
the existing numeric coercion for direct scalar arguments. Complex observations
use their real coefficient, as in the existing aggregates.

Compensated extended-precision sums preserve small deviations around large
origins. Geometric means use logarithms; harmonic means use scaled reciprocals,
so intermediate products/reciprocals do not overflow for representable results.
Geometric/harmonic means require positive observations. Insufficient samples
return `#DIV/0!`; nonpositive observations or nonrepresentable results return
`#NUM!`. Collected inputs are bounded to one million records. ODS save supplies required
column declarations and a blank row for newly created/empty sheets and imports
that lack them, preserving existing column groups and source caches. A function-name
inventory is catalogue coverage only; other retained groups remain unfinished.

Conditional aggregates now include `COUNTIF`/`COUNTIFS`, `SUMIF`/`SUMIFS` and
`AVERAGEIF`/`AVERAGEIFS`. Criteria support comparisons, numeric text, logical
values, empty cells versus empty strings, literal text, wildcard escaping and
Unicode regular expressions. Conditional searches are case-insensitive. ODF
search-mode and whole-cell settings belong to each workbook; XLSX imports use
wildcards. Explicit wildcard mode takes precedence if both flags are present.
Invalid regex syntax falls back to literal text. Matching has bounded work and
regex stack usage; exceeding the budget reports an incomplete `#LIMIT!` and
retains the previous cache.

Sparse range traversal counts equivalent absent positions without expanding the
range. Multiple ranges require identical two-dimensional geometry. Single-
criterion sum/average targets start at their reference's top-left and inherit
criteria geometry, clipping at sheet bounds. Named and dynamic references,
typed array inputs, array criteria and fixed matrix results are supported.
General locale parsing remains unfinished. ISO and
US English date criteria use the workbook's serial epoch. Formula text
conversion of typed dates/durations uses their numeric value, independently of
formatted grid display. This is broader function coverage, not full calculation
or regular-expression interoperability parity.

Order statistics include `SMALL`, `LARGE`, `RANK`/`RANK.EQ`, `RANK.AVG`,
`PERCENTILE`/`.INC`/`.EXC`, `QUARTILE`/`.INC`/`.EXC`,
`PERCENTRANK`/`.INC`/`.EXC`, `MODE`, `MODE.SNGL` and `MODE.MULT`.
`MEDIAN` shares the same bounded sorted dataset and overflow-safe interpolation.
Dataset references include sparse, named and dynamic ranges; selector/order/
significance arrays reuse one sorted dataset. Collection text, logical values
and empty cells are ignored; direct scalar arguments retain numeric coercion.
Input errors and unavailable calculations propagate. Fixed matrix blocks save
all calculated cells, including `#N/A` in unused array-result slots.

ODF `MODE` chooses the smallest tied mode; the modern single/multiple variants
retain input encounter order. Percentage ranks truncate decimal places, matching
Microsoft's published examples and retained Calligra behavior; exact integer
rank fractions avoid floating-point truncation loss. Inclusive singleton rank is
one per OpenFormula; exclusive singleton rank uses one half. The retained Calc
corpus has rounding, singleton and older mode-order cache differences, recorded
explicitly in `order_reference.py`. One rank case also exposes an inherited
binary duration-subtraction/`INT` boundary discrepancy; reference intersection and other unavailable dependencies remain unfinished. Catalogue counts measure
dispatch coverage, not numerical or interoperability fidelity.

Higher moments include `SKEW`, `SKEWP`/`SKEW.P`, `KURT` and `KURTP`.
Shifted, scaled observations and compensated extended-precision moment sums
retain small differences around large offsets and avoid cubed/fourth-power
overflow. Skewness needs three numeric observations; kurtosis needs four.
Insufficient or constant samples return `#DIV/0!`. `KURTP` uses population
excess kurtosis, matching the retained help example; the old implementation's
sample-variance denominator was inconsistent with that definition.

`TRIMMEAN` sorts the numeric dataset, removes the same floored count from each
end and computes a compensated mean. Its trim fraction must be at least zero
and below one. Trim-fraction arrays reuse the sorted dataset. `STANDARDIZE`
maps arrays element by element and uses extended intermediates for `(x-mean)/sigma`,
requiring positive sigma. `VARIANCE` and Microsoft-prefixed standard-deviation/
variance aliases dispatch to the existing native aggregates.

`fltk/tests/moments_reference.py` records two old skewness caches whose large-
offset precision disagrees with independent Decimal arithmetic; native results
match the independent inputs. Native/Calc error-label conventions and a trailing
omitted-argument semantic limitation remain explicit. Full calculation settings,
reference intersection and general locale/model interoperability remain unfinished.

Sheets GUI validation now has a fully native Wayland path:
`fltk/tests/native_sheets_ui.py --suite moments --bin-dir BUILD/fltk
--test-bin-dir BUILD/fltk --artifacts OUTPUT`. Suites `criteria`, `order`, `paired`, `distribution`, `inference`, `regression`, `log_regression` and `database`
are also supported. A test-only input helper, Cage's headless backend and nested
DomeD provide private virtual keyboard/pointer input and render captures. The
fixture uses no X server and never directs input to the user's desktop. It
checks painted/calculated values, editing/history, native saved caches and
save/reopen. The old Xvfb-based harness is retained for X11-enabled development
systems. Neither input helper nor protocol test infrastructure is installed.

Paired statistics include `CORREL`/`PEARSON`, `COVAR`/`COVARIANCE.P`,
`COVARIANCE.S`, `SLOPE`, `INTERCEPT`, `RSQ`, `STEYX`, `FORECAST`/`FORECAST.LINEAR`
and `SUMX2MY2`, `SUMX2PY2`, `SUMXMY2`/`SUM2XMY`. Dataset geometry must match in
both dimensions. Corresponding positions are paired before filtering collection
text, logical values and empty cells. Direct scalar parameters retain numeric
coercion. Sparse/named/dynamic references and fixed matrices participate; errors
and unavailable calculations propagate even beside a nonnumeric partner.
FORECAST reference queries use implicit intersection outside array context;
explicit array and matrix queries reuse the same dataset.

The kernel decomposes finite binary64 values into integers and powers of two.
Exact sums and products cancel before final quad-precision division/square root
and storage as doubles, preserving small intercepts, zero residuals and tiny
correlation datasets. Boost.Multiprecision headers are needed at build time;
no additional runtime math library or UI toolkit is introduced. Collection and
integer-work budgets preserve incomplete `#LIMIT!` provenance for excessive
work. A standalone one-million-observation exact-regression benchmark takes
about 0.09 seconds in the local Release build; this excludes workbook loading
and rendering. Other statistical and calculation-model work remain unfinished.

Probability distributions now cover beta, binomial (including interval and
critical probabilities), chi-square, exponential, F, Gamma, hypergeometric,
lognormal, negative binomial, normal, Poisson, Student t and Weibull families,
plus Fisher transforms, Gaussian area/density, Gamma/log-Gamma and normal/t
confidence intervals. Modern dotted names and recognized Microsoft prefixes
retain distinct signatures where their argument order, required parameters,
boundaries or tail definitions differ. OpenFormula `FDIST`/`FINV` use left tails;
Excel's old names translate to `LEGACY.FDIST`/`LEGACY.FINV` on XLSX import, without
altering quoted strings or sheet names. `TDIST` remains a one/two-tail function;
`T.DIST` is a signed CDF/density and `T.INV` is a signed left quantile.

Boost.Math evaluates distributions with extended intermediates. Complement
CDFs/quantiles retain small right tails; `expm1` and `erf` retain small areas.
Critical binomial probabilities use a bounded integer search for the least
qualifying result. Discrete counts truncate before use. ODF `CONFIDENCE` retains
its numeric sample size; modern confidence variants truncate it. Scalar/array
mapping, named/dynamic dependencies and fixed matrix caches use the native engine.
The explicit OpenFormula zero-density convention for `CHISQDIST` differs from
the modern function's endpoint density. Gamma endpoint densities are evaluated
explicitly, including the singular case, rather than trusting Boost's zero
shortcut. Singular/nonrepresentable values are errors, not fabricated zeros.

Convergence is bounded to 100,000 series iterations and 256 root iterations.
Binomial counts above the consecutive binary64 integer range and nondegenerate
hypergeometric populations beyond the backend's unsigned range are incomplete
`#LIMIT!` results; hypergeometric CDF support wider than 100,000 is also bounded.
Incomplete calculations preserve imported caches even through `IFERROR`.
`distribution_reference.py` retains 27 unavailable/malformed/omitted-argument
cases, four fixed-matrix padding differences from Calc, five boundary-convention
cache differences and one independently resolved inaccurate old chi-square
inverse cache. Original fixtures are unchanged. Distribution support is broader
coverage, not complete spreadsheet interoperability. Full calculation-model/settings
fidelity remains unfinished.

Dataset probability/frequency and hypothesis tests include `PROB`, `FREQUENCY`,
`FTEST`/`F.TEST`, `TTEST`/`T.TEST`, `ZTEST`/`Z.TEST` and
`CHITEST`/`LEGACY.CHITEST`/`CHISQ.TEST`. PROB pairs the numeric sequences after
independent collection filtering, checks probability bounds and the total with
only a narrow input-rounding allowance, and uses a balanced range-sum tree.
It retains tiny isolated masses without subtracting nearly equal prefix totals.
Zero probability is accepted as an extension; reversed query bounds return zero.
Query arrays reuse the tree. Scalar option references intersect outside array
context; fixed matrices retain full reference arrays. FREQUENCY sorts boundaries internally and maps
counts back to their original positions, including stable duplicate boundaries.
It returns a column vector with an overflow bin, accepts horizontal boundaries
as an extension, and counts all numeric data when no numeric bins remain.

Statistical samples accumulate exact binary integer sums and squares. Differences
of means cancel integers before conversion to quad precision; paired differences
can exceed binary64's range internally. This preserves a one-ULP perturbation
among a million large observations. F-tests use twice the smaller CDF/tail,
Z-tests use signed normal survival probabilities, and Welch tests keep fractional
degrees of freedom. The sample and integer-work budgets preserve incomplete
provenance. The existing paired-statistics kernel shares the binary decoder;
its independent reference checks remain unchanged.

Paired t-tests match two-dimensional geometry and filter corresponding positions.
OpenFormula TTEST applies paired filtering to same-shaped independent samples;
modern T.TEST filters independent samples separately. Unequal independent sample
shapes are accepted as an extension. Known-sigma modern Z.TEST accepts a singleton;
ODF ZTEST requires two observations. XLSX old TTEST/ZTEST calls translate to the
modern functions for their filtering/sample rules. Chi-square tests ignore pairs
with nonnumeric entries and use the original array geometry for degrees of
freedom; an all-empty array consequently has a zero statistic and tail one.
The retained Calc empty-array errors, singleton Z-test error and two constant-
paired-difference zero caches remain explicit boundary/error differences.
No function result is manufactured from a source cache.

Matrix linear regression now includes `LINEST` and `TREND`. Equal-shaped inputs
use one predictor and column-major observation order. A response column accepts
predictor columns; a response row accepts predictor rows. Omitted/empty KnownX
uses 1..k and omitted/empty NewX predicts the training inputs. Empty optional
Const/Stats parameters use their defaults as an extension. Other functions do
not acquire guessed empty-argument semantics: unsupported omissions remain
incomplete and preserve source caches. INDEX accepts explicit empty selectors
as zero, enabling whole row/column selection.

Input slots convert to numbers, including blank zero and logical values; arbitrary
text is an error. Sparse ranges retain their full geometry without allocating
empty observations. Exact binary integer Gram products and rational symmetric LDL
factorization identify a maximal independent predictor subset, retaining the
intercept first. Removed predictors have zero coefficients and standard errors;
degrees of freedom follow the selected rank. Rational coefficient sums cancel
before forecast conversion, so an overflowing displayed slope can still yield a
finite forecast. Five-row statistics include standard errors, R², residual error,
F, degrees of freedom and regression/residual sums of squares. Undefined statistics
are errors; no result is manufactured from a saved cache. The OpenFormula minimum
observation constraint applies to the original predictor count, including when
Stats is false. This can reject small samples accepted by other implementations.

Matrix storage/output is limited to one million records/cells and arithmetic to
50 million budgeted operations weighted by integer limb sizes. Exceeding a budget
is incomplete `#LIMIT!`. Representable nonzero binary64 subnormal literals/XML
values are retained even when strtod signals ERANGE; complete underflow/overflow
remain invalid. Array-valued Const/Stats are not implemented as separate fits.
`regression_reference.py` checks 434 formulas/4,976 cells against independent
Fraction Gaussian elimination and 110-digit square roots; seven untouched retained
TREND caches agree. `regression_producer.py` checks independent editable ODS/XLSX
workbooks, fixed matrix results and raw ODS caches. LOGEST/GROWTH use the logarithmic basis described below.

Exponential matrix regression includes `LOGEST` and `GROWTH`, with the same
geometry, conversion, omission and independent-predictor rules as LINEST/TREND.
Every Y slot must be positive: empty/zero/negative responses are errors. LOGEST
exponentiates its coefficient row only; the other statistics describe ln(Y).
A removed predictor and a disabled multiplicative intercept have factor one.
GROWTH combines exact coefficient weights before exponentiation, so finite
forecasts can survive individually overflowing growth factors.

Each positive binary64 response is factored into powers of two and prime factors
of its at-most-53-bit mantissa. Miller-Rabin tests on the first twelve prime bases
are deterministic in this domain ([Sorenson/Webster](https://arxiv.org/abs/1509.00864));
bounded deterministic Pollard-rho restarts split composites and exact products
verify each decomposition. Each prime-log response basis uses the shared exact
rational LDL fit. Rational coefficients combine within each logarithm basis
before floating evaluation. This preserves exact geometric relationships such
as ln(p*q)=ln(p)+ln(q), including zero residuals and dropped predictors, without
an epsilon rule. Prime-log rational independence identifies exactly zero residual,
regression and total sums of squares.

Multiprecision evaluation uses 128..4096 bits with a further 32 guard bits. The
starting precision accounts for the rational expression magnitudes; neighboring
precision results must agree within one binary64 step. Positive sums of squared
symbolic residuals and fitted/centered forms avoid subtraction of rounded normal
products. Statistical sums also require 70-bit relative convergence. Work remains
bounded to one million symbolic records and 50 million weighted operations,
including factorization, rational arithmetic and multiprecision evaluations.
Unresolved precision or work limits are incomplete `#LIMIT!` and preserve caches.
This can limit large or particularly difficult logarithmic fits.

`log_regression_reference.py` checks 234 formulas/1,955 cells with independent
Fraction Gaussian weights and 160/1,200-digit logarithms. Of 68 retained matrix
regression results, 58 numeric caches agree and ten cases produce known errors
using native error labels. The FORMULA-dependent LINEST case now computes #VALUE!
from its actual text predictor. A composite passing the first seven prime
Miller-Rabin bases is covered by an exact-zero-residual native test.
`log_regression_producer.py` provides ODS/XLSX matrices for export, native Wayland
editing/history/save/reopen, opaque data, raw saved caches and CUPS verification.

Database aggregation includes `DAVERAGE`, `DCOUNT`, `DCOUNTA`, `DGET`, `DMAX`,
`DMIN`, `DPRODUCT`, `DSTDEV`, `DSTDEVP`, `DSUM`, `DVAR` and `DVARP`. Database
ranges, named ranges, dynamic references and explicit arrays retain full geometry.
Field names match case-insensitively; numeric selectors are one-based and truncate
fractional parts. Invalid fields are errors. DCOUNT/DCOUNTA accept an empty Field
parameter, a blank reference or zero to count records; other functions require a
field. Explicit field arrays produce matching result shapes. Ordinary scalar
field references intersect outside array context; fixed matrices retain arrays.

Criteria within a row combine with AND; rows combine with OR, and repeated
headings impose multiple conditions on the same field. Physical blank and empty-
string criteria are ignored. Unused OR rows are ignored when other rows contain
conditions; a wholly blank criteria area selects all records. Use the explicit
text criterion `=` for blank data and `<>` for nonblank data; numeric zero does
not match an absent field. Criteria searches are case-insensitive and share the
workbook wildcard/regular-expression, whole-cell and date-epoch settings.
Numeric text in data is converted for numeric database criteria, including ordered
comparisons. Numeric comparison remains numeric when whole-cell matching is off;
the existing conditional-aggregate matching rules remain unchanged.

Calculated criteria under a heading absent from the database use an actual
criterion-cell formula as a row-relative predicate. The expression is copied
virtually from the first database record, preserving absolute references and
quoted text; it must return a logical or numeric result. Predicate errors and
unavailable calculations propagate. This is an extension beyond ODF field-only
criteria. A prepared selection, including a failed selection, is reused for all
field-array results; partial selections are discarded on failure. Predicate node
visits, cell reads and nested conditional/database work share a 50-million-unit
budget. Sparse unused database rows are counted by multiplicity where possible.

Selected numeric values include logical 0/1 and date/duration numbers, following
Calc's database convention. Text values are excluded from numeric aggregation;
DCOUNTA counts nonempty text, logical values and errors. DCOUNT ignores ordinary
errors, while unavailable values retain incomplete provenance. Numeric aggregates
propagate selected errors. DGET preserves the selected scalar's type, reports
`#VALUE!` for no result or an absent result cell and `#NUM!` for multiple records.
Empty numeric sums/products/minima/maxima are zero; averages and insufficient
variance samples retain the native `#DIV/0!` convention. Complex numeric aggregation
is incomplete rather than silently discarded.

Sums, means and variance numerators use exact binary integer accumulation.
Standard deviations take their square root before binary64 conversion, permitting
finite deviations when the variance overflows. Products multiply exact odd
mantissas and combine binary exponents before final normalization; intermediate
binary64 overflow/underflow does not destroy a representable result. Numeric
samples and output arrays are limited to one million entries and weighted
arithmetic shares the work budget. `N` now supplies explicit numeric conversion,
including array inputs and error/incomplete propagation.

The loader imports ODF named database ranges and named expressions as formula
names. XLSX global names carry their required ODF base-cell anchors; reference
unions become named expressions using the reference-union operator, instead of
invalid multi-range attributes. Constants and qualified absolute expressions can
be calculated; relative or unqualified reference expressions remain incomplete
until their base/caller semantics are implemented. Sparse XLSX gaps, formatting-only
rows and new rows created beyond existing data contain the blank cell required by
ODF. Unsupported named reference lists retain an incomplete binding and their raw
package declarations,
so they no longer prevent unrelated workbook calculations from loading. Formula
cells whose result is an empty reference produce numeric zero; functions that
inspect a blank reference directly retain its empty-cell type. Number-locale
parsing and legacy nonrectangular database criteria remain unfinished. Ambiguous decimal/grouping
commas in numeric database comparisons produce incomplete `#VALUE!` and preserve
source caches, including through IFERROR; they are not compared lexically with
numeric data or guessed from the machine locale.

`database_reference.py` checks 1,938 independent formulas/1,968 result cells using
Python typed predicates, exact Fraction arithmetic and 110-digit square roots.
Of 121 retained database results, 103 agree, one is an error-label convention and
17 remain explicitly unavailable: 15 locale-dependent criteria and two named
reference unions. `database_producer.py` checks independent editable ODS/XLSX
fixtures, named database metadata, matrix outputs, row-relative criteria, blank
references and raw source caches for unavailable names, locales and N inputs.

The native reference model follows the [OpenFormula ordered-reference contract](https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part4-formula/OpenDocument-v1.3-os-part4-formula.html)
for `~` unions in aggregates, named
expressions, INDIRECT, scalar-condition IF and scalar-index CHOOSE. Overlapping
areas and duplicate cells remain in sequence. Sparse aggregation counts absent
slots separately, including repeated whole-column blanks, and preserves
unavailable provenance through numeric IFERROR results. Arrays and functions
requiring a single rectangle reject a multi-area list. AREAS reports its area
count and ISREF inspects reference identity without reading ordinary cell errors.
INDEX accepts an optional fourth area selector and omitted row/column selectors;
2D row-only selection returns the complete row. Area selection retains reference
identity for subsequent ROWS/COLUMNS, aggregates and array materialization.
Reference lists are bounded to 4,096 areas and collected values to one million.

`references_reference.py` independently enumerates ordered, overlapping and
cross-sheet areas with Python values and exact Fraction moments, checking 2,983
formulas, updated raw ODF caches and save/reopen. Native tests cover sparse blank
multiplicity, typed values, alias cycles, INDEX errors, lazy reference selection,
quoted Unicode sheet names, metadata and incomplete provenance. Legacy
whitespace-separated named ranges and nonrectangular database criteria remain
unavailable; ODF database criteria require a rectangle. Relative named-expression
base/caller semantics, reference intersection/general range operators and 3D
ranges also remain unfinished.

Native information functions now include FORMULA, ISFORMULA, TYPE, ERROR.TYPE
(the Calligra ERRORTYPE alias), ISNONTEXT/ISNOTTEXT, ISNUM, ISEVEN/ISODD and
ISDATE/ISTIME. The long analysis-addin parity aliases and Excel FORMULATEXT
spelling are supported; XLSX FORMULATEXT calls translate to OpenFormula FORMULA.
INFO/FILENAME context and the distinct ORG.OPENOFFICE.ERRORTYPE numeric error-code
extension remain unavailable.

FORMULA/ISFORMULA inspect stored expressions without calculating referenced
values. This includes error/unsupported/self formulas and unparsed source syntax;
literal text beginning with `=` remains text. Matrix members resolve to their
owner, whose FORMULA text is brace-wrapped. In scalar context FORMULA selects the
first cell of a rectangle and ISFORMULA returns false for multi-cell inputs; array
context maps metadata across the rectangle. Multi-area lists are rejected. Output
is bounded to one million cells and one million formula-text characters. TYPE
consumes complete arrays (64), distinguishes Number/empty (1), Text (2), Logical
(4) and Error (16), and information predicates retain incomplete provenance.
ERROR.TYPE uses the seven standard error indices; unknown error kinds and nonerrors
return #N/A. Parity truncates the actual binary64 value before modulo, including
large values and subnormals; Boolean/complex inputs are rejected.

Date/time identity is carried independently of numeric display/cache formatting
for imported ISO dates/datetimes/durations, DATE/TIME results and coupon dates.
Ordinary numeric/text values remain distinct from temporal values. Number-style
and arithmetic-format inheritance still follow the limited native model; this
does not implement full upstream formatting fidelity. XLSX ISO clock times in
HH:MM[:SS[.milliseconds]] form convert to ODF time cells with their fractions intact;
invalid hours/minutes/seconds and trailing data are rejected.

`information_reference.py` checks 1,043 independent typed predicates and parity
cases, 83 retained unit cases (77 matches, five unavailable INFO dependencies and
one ISREF error convention), and 395 retained FODS results. Of those, 317 agree
with caches, 55 formula-text presentations are independently verified against
source XML and matrix ownership, two TYPE conventions retain the native distinct
Logical/Text types, and 21 existing unavailable contexts remain explicit. Original
fixtures are unchanged. `information_producer.py` checks editable ODS/XLSX metadata,
array results and raw caches: metadata on an unavailable formula is complete while
value-dependent information preserves the source cache. The former unavailable
LINEST predictor containing FORMULA text now reports a known #VALUE! error;
log-regression retained tests require zero unavailable cases.

The current inventory dispatches 472 of 641 retained names, including aliases,
with 169 remaining. The remaining catalogue,
omitted arguments, duration precision and complete calculation
settings/model interoperability remain unfinished. The OpenDocument/CUPS floor,
Words pause and Stage-last priorities remain in force.

Implemented groups include arithmetic/comparison/concatenation, SUM/PRODUCT,
AVERAGE/MIN/MAX/COUNT/COUNTA/COUNTBLANK/MEDIAN, variance/standard deviation,
logical and type tests, rounding, powers/logarithms/trigonometry, basic text
functions, the engineering/financial groups above and basic date/time functions. This is a smaller
catalogue than the original Calligra engine. Unknown functions produce `#NAME?`,
not a fabricated numeric result. Dependent formulas propagate errors. Existing
source caches for unsupported computations are preserved on ODF save, including
dependencies and errors handled by IFERROR. Ordinary runtime errors update the
saved caches. An unavailable calculation is tracked independently of its value;
preserved caches do not prove that those functions were recalculated.
Always keep originals for workbooks outside this implemented subset.

## Desktop behavior and validation

The applications follow DomeD appearance and Unicode font settings, use OpenGL
FLTK windows, export universal-menu commands and use native FLTK file choosers.
Document previews use their declared ink and paper colors; Words defaults to
white paper with black text. Explicit window-font-color styles use the session
foreground. The editor controls retain the DomeD theme.
There is no private menu bar. A popup menu is available outside DomeD.
Standard shortcuts, undo/redo and Stage presentation controls are retained.

The existing three document CTests were ported without deleting assertions and
pass against the Qt-free engines. They exercise ODF round trips, repeated rows,
styles, names/shared formulas, Unicode, embedded media and invalid packages.
The additional native-engine test checks range aggregation, financial math,
lazy branches, error propagation and circular references. Array and matrix
document tests cover the new calculations, cache handling, edit protection and
failed-load rollback. Four upstream FODS fixtures check 85 matrix result cells
against their stored expected values. The keyboard test edits an array formula,
exercises undo/redo and inspects the saved OpenFormula and numeric cache.
Independent Office
producer fixtures and real keyboard/menu/save tests are described in
[VALIDATION.md](VALIDATION.md). Full suite fidelity and all spreadsheet functions
are explicitly outside the current milestone.

Calligra-derived source retains its GPL/LGPL attribution and licenses; native
port code is GPL-2.0-or-later and the shared appearance helpers retain MIT.
