# Port status and remaining office-suite work

Baseline: KDE Calligra `edb7aee19cf4c38b55389149ea052982d7a6d41e`, retrieved
2026-10-25. The source reports development version 26.11.70. This milestone
establishes all three FLTK applications; it does not complete the office suite.

| Area | Working in this build | Still required for a full port |
| --- | --- | --- |
| Shared shell | Native Wayland FLTK, OpenGL widgets, DomeD Unicode appearance, universal menus, file dialogs, basic editing shortcuts | Full IME/preedit validation, accessibility, session recovery, printing/export UI, broad XR workflow tests |
| Words | ODT paragraph text, Unicode, whitespace, existing span/link preservation, flat paragraph insertion/deletion, text undo/redo, atomic save | Port KWDocument, KoText/textlayout and frame layout; pages, rich formatting controls, tables, lists, headers/footers, footnotes, images/anchors, styles, tracked changes and print/PDF fidelity |
| Sheets | Actual Calligra calculation engine and function modules, dependencies, cross-sheet references, named-range import, multi-sheet ODS, cell edits, undo/redo, TSV paste, repeated cells, cached formula updates | Full core/style model and ODF loader, formatting/date/currency display, selection ranges, merged/array cells, sort/filter, validation, structural row/column operations, charts, printing and format filters |
| Stage | ODP slides, text frames, PNG/JPEG/SVG preview, frame text edits, slide insert/delete, presentation navigation, package preservation | Port KPrDocument, KoPADocument and Flake; theme/master-page resolution, shape tools, arbitrary groups/transforms, animation/timing, transitions, speaker notes/view and export fidelity |
| Interchange | ODT/ODS/ODP packages and toolkit-independent upstream ODF formula conversion | DOCX/XLSX/PPTX, legacy formats, filter/plugin discovery, full external-office interoperability corpus |
| Rendering | Main windows and content widgets use FLTK's GL driver; native Wayland GPU tests passed | Proper Words pagination display lists, complete Stage shape/3D rendering, hardware/UI profiling, native XR mapping for genuinely spatial content |

## Architecture findings

The Sheets engine already separates calculation from widgets. Its static
function-module path can be built without `sheets/ui`, `sheets/part`, `komain`,
`kotext`, or `QtWidgets`. The new adapter deliberately uses those real model and
calculation classes rather than implementing spreadsheet formulas independently.

Words' KWDocument uses KoDocument, KoShapeBasedDocumentBase, frame management,
QTextDocument-based text layout and the Flake shape system. Its existing widget
interfaces cannot simply be linked into an FLTK window. The current paragraph
adapter preserves ODF structures while providing editable text; it does not run
Words' original layout engine and must not be presented as layout parity.

Stage's KPrDocument derives from KoPADocument, with KoPAPage/Flake shape objects,
master pages and animation plugins. The current FLTK canvas reads frame geometry
and embedded image/text content; it does not run those complete models. Unknown
objects and extension data remain in the saved package, but preservation does
not imply correct display or editing support.

## Next source-port milestones

1. Split KoDocument's persistence/undo/filter interfaces from QWidget prompts,
   KXmlGui actions, dialogs and progress widgets. Supply frontend callbacks.
2. Introduce a toolkit-independent shape/text display list and input/selection
   adapter for the existing Flake and text-layout models. Keep rasterization and
   upload out of steady-state presentation; use the DomeD GL/Vulkan path.
3. Connect KWDocument and KPrDocument to those interfaces, replacing the current
   paragraph/frame adapters. Share original loaders, styles, undo commands and
   import filters rather than growing parallel implementations.
4. Port the remaining editing tools and dialogs to FLTK and validate real
   document corpora, print/export, scripts, IME and both compositor realms.

## Current editing limits

Words displays paragraphs rather than paginated layout. It preserves span/link
attributes during text edits, but structural edits in nested tables/lists and
paragraphs containing embedded objects are intentionally restricted. Mixed
inline line breaks and paragraph breaks need a richer editing model. Undo
history is bounded to 100 text states and a 32 MiB history budget.

Sheets currently uses the calculation layer's textual display, not the original
style-aware formatter. Do not expect date/currency styles, images, merged or
array formulas, or chart canvases to display like upstream. Existing entries and
attributes are retained when unedited, but full fidelity is unverified. Sheet
protection causes read-only mode; range/structure editing is incomplete.

Stage renders basic frames and uses the first stored page-layout size; complete
per-slide master/theme resolution is still missing. Unrecognized shapes and
animations are preserved, not rendered. Text undo history is local to the
currently edited frame. X11/Xvfb fullscreen transitions can emit GLX diagnostics;
native Wayland is the default and the target for presentation work.

Keep complex originals until the full-model integrations and interoperability
checks above are complete. This is a concrete limitation of this milestone,
not a claim of a finished office replacement.
