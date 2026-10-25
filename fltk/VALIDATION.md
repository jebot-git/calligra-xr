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
