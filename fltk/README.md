# Calligra native engines and system FLTK

Words, Sheets and Stage now use native C++20 document engines, libxml2 and
libzip. The default build requires **no Qt or KDE libraries or headers**.
It uses the installed system FLTK shared libraries; an alternative FLTK is an
explicit build option. Wayland remains the default, with X11 as an opt-in.

This remains a basic editing port, not the complete upstream office suite.
Words edits paragraph text and existing inline-style runs; Stage edits slide
text and previews text/images; Sheets edits multiple worksheets and calculates
formulas. The original paginated Words layout, Stage shape/animation model,
and full upstream spreadsheet function catalogue are not reimplemented here.
The original Qt/KDE source remains available only through the opt-in frontend
build. See [PORT_STATUS.md](PORT_STATUS.md).

## Build and install

Dependencies: CMake 3.29+, Ninja, a C++20 compiler, system FLTK 1.4 development
files, OpenGL, libxml2, libzip, zlib, Fontconfig, GLib/GIO, Wayland and X11
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
  has undo/redo, and saves ODT. It is not a paginated word processor.
- Sheets loads ODS and imports XLSX cell data, shared/inline Unicode strings,
  numeric/Boolean values, shared formulas, cross-sheet references, global named
  ranges, sparse cells, and common 1900/1904 date/time values. It supports cell
  editing, undo/redo, TSV paste and ODS saves. It does not export XLSX.
- Stage loads ODP and imports PPTX text boxes and embedded PNG/JPEG images,
  dimensions, order and frame positions. It supports text editing, slide
  insertion/deletion, ODP saves and a basic F5 presentation view. Placeholder
  positions can come from a PPTX layout; full master/theme inheritance is absent.

Office imports support Transitional and Strict namespaces and are content/data
conversions, not full visual fidelity. DOCX formatting, headers/footers,
numbering and drawings are omitted. PPTX themes, master content, rotations,
crops, charts, media and animations are omitted. XLSX visual formatting, charts,
pivots, macros, array/data-table formulas and local named expressions are not
supported. Ctrl+S proposes an ODF copy; imported Office files cannot be
accidentally overwritten by an ODF save. Legacy binary formats are unsupported.

Unedited ODF package members, styles, metadata and media are preserved. Saves
use a temporary ZIP, fsync and atomic rename, with the mimetype first and
uncompressed. Signatures make ODF documents read-only; signed/macro-enabled
Office packages and encrypted documents are rejected. XML entities/DTDs,
excessive depth, escaping paths and external media fetches are rejected.
Limits remain 10,000 entries, 64 MiB per entry, 256 MiB total, one million
nonempty spreadsheet cells, 100,000 imported text/shape nodes and 4,096 slides.

## Native spreadsheet calculation

The native engine has a Unicode-aware parser, typed values, ranges, cross-sheet
and named references, recalculation, lazy IF/IFERROR evaluation, cycle/depth
checks, and error propagation. It uses standard C++ math and GLib Unicode
operations; no external calculation engine or Python process is required.
Formula input is limited to 8,192 Unicode characters and expression nesting to
256 levels. Excessive input reports a limit/depth error.

Implemented groups include arithmetic/comparison/concatenation, SUM/PRODUCT,
AVERAGE/MIN/MAX/COUNT/COUNTA/COUNTBLANK/MEDIAN, variance/standard deviation,
logical and type tests, rounding, powers/logarithms/trigonometry, basic text
functions, PMT/PV/FV/NPV and basic date/time functions. This is a smaller
catalogue than the original Calligra engine. Unknown functions produce `#NAME?`,
not a fabricated numeric result. Dependent formulas propagate errors. Existing
source caches for formulas that cannot be evaluated are preserved on ODF save;
such caches are not proof that the native engine recalculated those functions.
Always keep originals for workbooks outside this implemented subset.

## Desktop behavior and validation

The applications follow DomeD appearance and Unicode font settings, use OpenGL
FLTK windows, export universal-menu commands and use native FLTK file choosers.
There is no private menu bar. A popup menu is available outside DomeD.
Standard shortcuts, undo/redo and Stage presentation controls are retained.

The existing three document CTests were ported without deleting assertions and
pass against the Qt-free engines. They exercise ODF round trips, repeated rows,
styles, names/shared formulas, Unicode, embedded media and invalid packages.
The additional native-engine test checks range aggregation, financial math,
lazy branches, error propagation and circular references. Independent Office
producer fixtures and real keyboard/menu/save tests are described in
[VALIDATION.md](VALIDATION.md). Full suite fidelity and all spreadsheet functions
are explicitly outside the current milestone.

Calligra-derived source retains its GPL/LGPL attribution and licenses; native
port code is GPL-2.0-or-later and the shared appearance helpers retain MIT.
