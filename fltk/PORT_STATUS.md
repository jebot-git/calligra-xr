# Native engine milestone — 2026-10-28

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
| Shared shell | Native Wayland FLTK, OpenGL widgets, DomeD Unicode appearance, universal menus, file dialogs, basic editing shortcuts | Full IME/preedit validation, accessibility, session recovery, printing/export UI, broad XR workflow tests |
| Words | DOCX paragraph/table import with ODT saving, ODT paragraph text, common/automatic style inheritance, page geometry/master continuation, native HarfBuzz/FreeType shaping and glyph rendering, document fonts and script fallback, ICU bidi line ordering, numeric font weights/standard variable axes, native pagination and GPU page view, document ink/page colors, keep/widow/orphan rules, existing span/link preservation, flat paragraph insertion/deletion, text undo/redo, atomic save | Remaining KWDocument/KoText/textlayout/frame behavior; full font/style/shaping and line-breaking fidelity, embedded fonts, color-font glyphs, synthetic/vertical text, rich formatting controls, table/list/section layout, columns, headers/footers, notes, images/anchors, tracked changes and print/PDF fidelity |
| Sheets | Native C++ parser/calculation engine, typed arrays, reference/lookup/matrix functions, fixed ODF matrices and XLSX array import, dependencies, cross-sheet references, named-range import, multi-sheet ODS and XLSX cell-data import, cell edits, undo/redo, TSV paste, repeated cells, cached formula updates | Remaining upstream calculation catalogue and settings, full core/style model and ODF loader, formatting/date/currency display, selection ranges, merged cells, matrix-block creation/deletion, sort/filter, validation, structural row/column operations, charts, printing and format filters |
| Stage | PPTX direct text/PNG/JPEG import with ODP saving, per-slide ODP master/page layouts, scoped/inherited basic paint, rectangles/ellipses/polygons/lines, native Bézier/arc paths and rounded/partial primitives, explicit enhanced geometry/equations and modifier paint sets, ordered groups and affine transforms, native paragraph/run fonts and colors, bidi and continuous frame layout with side padding/vertical alignment, PNG/JPEG/SVG/GIF/BMP GPU preview, inverse frame selection and nested edits, slide insert/delete, presentation navigation, package preservation | Complete KPrDocument, KoPADocument and Flake behavior; theme/placeholder fields, list/table/column and vertical text layout, text auto-growth/fit, advanced typography, custom preset lookup/adjustment handles/stretch points, extrusion/text-on-path, connectors/3D objects, advanced fills/strokes, shape editing tools, animation/timing, transitions, speaker notes/view and export fidelity |
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

## Next source-port milestones

1. Continue porting the original calculation behaviors, settings, model/style
   handling and document filters to native C++, using upstream corpora to check
   results and retaining unsupported source caches until they can be calculated.
2. Port the KoText/text-layout algorithms and Flake shape models to native text,
   geometry and persistence interfaces, replacing Qt text/layout dependencies.
   Supply toolkit-independent display lists and input/selection adapters; keep
   rasterization and upload out of steady-state presentation.
3. Port KWDocument's pagination/frame management and KPrDocument's master/shape/
   animation behavior to those interfaces, replacing the basic paragraph/frame
   engines. Preserve the original loader, style and undo semantics.
4. Port the remaining editing tools and dialogs to FLTK and validate real
   document corpora, print/export, scripts, IME and both compositor realms.

## Current editing limits

Words provides text editing and a separate native paginated view. It preserves span/link
attributes during text edits, but structural edits in nested tables/lists and
paragraphs containing embedded objects are intentionally restricted. Mixed
inline line breaks and paragraph breaks need a richer editing model. Undo
history is bounded to 100 text states and a 32 MiB history budget. The page view
does not implement direct rich editing, complete typography or the advanced
structural layout listed above. Document-font fallback is selected through a
private Fontconfig context; missing glyphs are reported in layout diagnostics.
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
paragraph tab layout and structural list editing remain incomplete; existing
list XML and styles are retained during supported text edits.

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
flow, including the shared textual list engine. Full table/column/vertical
layout, remaining list features above, auto-growth, fit-to-size and rich
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
