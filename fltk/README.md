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

## Build and install

Dependencies: CMake 3.29+, Ninja, a C++20 compiler, system FLTK 1.4 development
files, OpenGL/GLU, libxml2, libzip, zlib, Fontconfig 2.17+, FreeType, HarfBuzz,
GLib/GIO, ICU, Wayland and X11
headers, and nlohmann-json. No Qt, ECM, KDE, Eigen or Ixion is used by the native
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

## Document support

- Words loads ODT and imports DOCX body text, paragraphs and basic tables. It
  preserves existing ODT character-style spans and links while editing runs,
  has undo/redo, and saves ODT. Pages switches to the native page-flow view;
  Edit returns to text editing, and double-clicking a rendered line opens its
  paragraph in the editor. Page Up/Down, Home/End and the wheel navigate pages.
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
  Lists/tables, text auto-growth/fit, vertical writing, custom preset lookup,
  adjustment handles/stretch points, extrusion/text-on-path, advanced strokes/fills,
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
justification/hyphenation and vertical text remain incomplete. Rich formatting controls, table/list/section
layout, columns, notes, headers/footers, anchored shapes and page-style details
remain to be ported. Nested structural content is currently flattened for this
view and its unsupported categories are reported by the layout model. DOCX
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

Implemented groups include arithmetic/comparison/concatenation, SUM/PRODUCT,
AVERAGE/MIN/MAX/COUNT/COUNTA/COUNTBLANK/MEDIAN, variance/standard deviation,
logical and type tests, rounding, powers/logarithms/trigonometry, basic text
functions, PMT/PV/FV/NPV and basic date/time functions. This is a smaller
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
