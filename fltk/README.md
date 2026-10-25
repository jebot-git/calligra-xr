# Calligra for DomeD — first FLTK port milestone

This source-tree port includes **Words, Sheets and Stage**. All three have
native FLTK editing applications, OpenDocument load/save, DomeD appearance,
Unicode bitmap fonts, OpenGL widgets and universal-menu commands. They default
to Wayland; `FLTK_BACKEND=x11` explicitly selects the compatibility backend.

This is an early, working port, **not a feature-complete replacement for the
upstream suite**. Read [PORT_STATUS.md](PORT_STATUS.md) before using it for complex
documents. In particular, Words is currently a paragraph-text editing view,
not the original paginated layout engine; Stage provides text/image slide
preview and text-frame editing, not the original full shape/animation editor.

## Applications

- `calligra-words-fltk [document.odt|document.docx]`: edit paragraph text, retain existing
  character-style spans and links while changing text, insert/remove paragraphs
  in flat documents, undo/redo text edits, import DOCX text/tables, and save ODT.
- `calligra-sheets-fltk [workbook.ods|workbook.xlsx]`: upstream Calligra formula engine and
  function modules, multiple worksheets, cell editing, dependency recalculation,
  cross-sheet references, undo/redo cell edits, rectangular TSV paste and ODS.
  XLSX cell data can be imported and saved as ODS; XLSX export is unavailable.
  Click the formula field or press F2 to edit the selected cell.
- `calligra-stage-fltk [presentation.odp|presentation.pptx]`: select slides and text frames, edit
  text with live preview, import PPTX text/images, add/delete slides, save ODP,
  and present with F5.
  Arrow/Page keys advance during presentation; Escape returns to editing.
  `--present` opens Stage directly in presentation mode.

Commands are exported to DomeD's universal menu. There is no private menu bar.
The Menu button provides a popup fallback outside DomeD. Standard file and
editing shortcuts include Ctrl+N/O/S/Q, Ctrl+Shift+S, Ctrl+Z and Ctrl+Shift+Z;
Stage also supports Ctrl+M for a new slide. File choosers are FLTK widgets.

OpenDocument package members not edited by the port are retained, including
styles, metadata and embedded media. Saves use a temporary archive, fsync and
atomic replacement; the mimetype member is first and uncompressed. Repeated
spreadsheet rows/cells are split when an individual cell is edited. Signed
files are read-only and encrypted files are rejected. Package import is bounded
at 10,000 entries, 64 MiB per entry and 256 MiB in total; nonempty spreadsheets
are limited to one million imported cells. No external images are fetched.

## DOCX and PPTX import

Words imports `.docx` body paragraphs, visible run/field-result text, Unicode,
spaces, tabs, line breaks, content controls and basic table cells. Inserted
revision text is included; deleted text and field instructions are omitted.
Table structure is retained, but merged cells, widths and table formatting are
not imported. Words' existing restrictions on structural edits in tables and
mixed inline/paragraph breaks still apply. Paragraph/run formatting, list
numbering, hyperlink targets, page layout, headers/footers, notes, drawings and
embedded objects are omitted.

Stage imports `.pptx` slides in presentation order, direct text boxes and
embedded PNG/JPEG pictures, slide dimensions, and explicit frame positions and
sizes. Placeholder geometry is read from the slide layout when available;
otherwise the existing basic frame defaults are used. Themes, master content,
full style/layout inheritance, text formatting, groups, rotations, image crops,
tables, charts, media, notes, transitions and animations are omitted. Linked
images are omitted; no external relationships are fetched.

Both importers accept Transitional and Strict OOXML namespaces, use the existing
headless QtCore/XML and libzip backend, and require no office converter or Qt
frontend. This is **content import, not visual fidelity**. A conversion notice
appears after opening an Office file. Ctrl+S proposes an `.odt` or `.odp` copy;
after the first successful conversion, ordinary Save works normally. Words and
Stage reject every save destination without the matching OpenDocument suffix,
so imported Office files cannot be overwritten by an ODF save.

Archive size and XML depth/entity checks apply. Imports are limited to 100,000
text/object nodes, 32 Mi UTF-16 text units and 4,096 slides. Signed,
macro-enabled, encrypted, malformed and escaping packages are rejected. Legacy
binary DOC/PPT and macro-enabled DOCM/PPTM files remain unsupported. Retain the
source Office files for content/features outside these import subsets.

## XLSX import

Sheets imports standard `.xlsx` workbooks (Transitional and Strict namespaces)
using the existing QtCore/XML and libzip backend, with Calligra's original
Excel formula translator, reference adjustment and calculation engine. This
adds no QtGui/QtWidgets frontend and requires no external office converter.

Import includes multiple worksheets, sparse cells, shared/inline Unicode
strings, Boolean/numeric values, common formulas and shared formula groups,
workbook-global named ranges, and date/time values in the 1900/1904 systems.
Formula support is limited to the existing calculation engine; newer Excel
functions and external references are not guaranteed to calculate. No external
relationships are fetched. Local named ranges and named expressions are omitted.

This is **cell-data import**, not visual fidelity: formatting, merged layout,
charts, drawings, pivots, filters and other workbook features are omitted.
The status bar and initial save dialog identify the conversion. Signed,
macro-enabled and encrypted files, array/data-table formulas and invalid input
are rejected. The fictitious Excel date 1900-02-29 is rejected rather than
silently shifted. XML entities and archive traversal are rejected; archive size,
cell count and expanded text limits apply.

Ctrl+S on an imported XLSX opens Save As with an `.ods` destination. The backend
also rejects every save destination without an `.ods` extension. The source
XLSX is never overwritten. After the first ODS save, ordinary Save works normally.
Binary `.xls`, `.xlsb` and `.xlsm` remain unsupported.

## Reused Calligra code and build defaults

Sheets uses `sheets/engine` and all upstream function modules, through the
existing `SHEETS_NO_PLUGINMODULES` path. Formula and region conversion routines
were moved unchanged from `sheets/core/odf` into `sheets/engine/OdfFormula.*`;
the original Qt frontend includes the shared declarations. The engine retains
QtCore/QtXml and headless KDE framework dependencies. The three FLTK applications
link **neither QtGui nor QtWidgets**, and create no QApplication or Qt windows.

The shared ODF package/text adapters are new port code. The full Words
`KWDocument`/text-layout stack and Stage `KPrDocument`/Flake stack are **not yet
ported into these applications**. Existing source remains available for the
next milestones; this distinction is central to the port-status assessment.

FLTK is the root CMake default. The original Qt/KDE applications require a
separate, explicit opt-in build:

```sh
cmake -S . -B build-qt -DCALLIGRA_BUILD_FLTK=OFF \
  -DCALLIGRA_BUILD_QT_FRONTENDS=ON
```

## Release build

Dependencies: CMake 3.29+, Ninja, a C++20 compiler, ECM, Qt6 Core/Xml development
files, KF6 CoreAddons/Config/I18n, Eigen, libzip, Fontconfig, zlib, GIO, X11 and
Wayland development files, wayland-scanner, OpenGL, nlohmann-json, and de-xr's
patched FLTK 1.4.5 toolkit. The shared de-xr shell requires installed Unicode BDF
fonts. Fedora's KF6Config CMake package also asks for Qt6 Gui/Qml development
metadata even though those targets are not linked by this port.

```sh
./fltk/scripts/build.sh
ctest --test-dir build-fltk --output-on-failure
./build-fltk/fltk/calligra-words-fltk
./build-fltk/fltk/calligra-sheets-fltk
./build-fltk/fltk/calligra-stage-fltk
```

`FLTK_PREFIX` defaults to `/opt/fltk-vr/toolkit`. Set
`CALLIGRA_BACKEND_PREFIX` to a privately prepared development prefix when system
Qt/KF development packages are absent. `fltk/scripts/prepare-fedora-deps.py`
downloads only the selected development packages and runtime dependencies,
checks RPM signatures, extracts into a private prefix and relocates KF CMake
metadata. It never installs system packages or launches a Qt frontend.

Build as the regular user. Stage and verify the optimized artifacts before sudo:

```sh
python3 fltk/packaging/stage-system.py --stage /tmp/calligra-fltk-stage
python3 fltk/packaging/install-system.py /tmp/calligra-fltk-stage --verify-only
SUDO_ASKPASS=/usr/local/bin/dome-askpass sudo -A -k \
  python3 fltk/packaging/install-system.py /tmp/calligra-fltk-stage
```

Installation uses `/opt/calligra-fltk`, `/usr/local/bin` wrappers and separate
FLTK desktop entries. It does not alter MIME defaults or restart sessions.

To select Words, Sheets and Stage as DomeD defaults for the supported ODT,
ODS, ODP and import-only DOCX/XLSX/PPTX formats, run the separate association helper:

```sh
python3 fltk/packaging/set-dome-defaults.py --dry-run
python3 fltk/packaging/set-dome-defaults.py
SUDO_ASKPASS=/usr/local/bin/dome-askpass sudo -A \
  python3 fltk/packaging/set-dome-defaults.py --system
```

It backs up and updates only Dome-specific preferences, removing legacy
DOC/XLS/PPT associations from those files. The system MIME definitions and
other desktops' preferences remain available. DOCX, XLSX and PPTX are assigned
to Words, Sheets and Stage for import. Binary DOC/XLS/PPT and macro-enabled Office formats are not advertised or assigned.

## Verification

`ctest` covers real upstream formula evaluation and dependencies, Unicode,
ODT/ODS/ODP round trips, repeated-cell splitting, span retention, binary package
member preservation, signature protection and XML entity rejection.

```sh
uv run --no-project --with python-xlib python fltk/tests/ui_smoke.py
uv run --no-project --with openpyxl python fltk/tests/xlsx_producer.py
uv run --no-project --with python-docx --with python-pptx python fltk/tests/office_producer.py
```

This test uses private Xvfb and D-Bus, edits and saves all three applications,
checks import-to-ODF Save As and unchanged Office sources, checks the universal
menu, exercises spreadsheet undo/redo and Stage presentation
shortcuts, and inspects the resulting ODF packages independently. Xvfb rendering
is software and is not evidence of GPU acceleration. Real Wayland captures on
this workstation separately confirmed Intel Arc A770 rendering and menu
attachment for all three applications. See `VALIDATION.md` for the evidence.

`--report FILE.json`, `--capture FILE.ppm` and `--quit-after-ms N` provide QA
hooks. Pixel readback is confined to explicit capture; normal widgets and slide
preview draw through the FLTK OpenGL driver. Image decoding and initial glyph
rasterization remain CPU work before texture upload. Standard FLTK dialogs have
not yet been audited for GPU rendering.

The shared shell files in `src/suite` originate in de-xr's VRChat launcher and
retain their MIT SPDX notices. The new office UI/adapters are GPL-2.0-or-later;
upstream Calligra engines retain their original LGPL/GPL notices.
