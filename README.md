# Calligra XR — DomeD FLTK port

A development fork of KDE Calligra for the [de-xr](https://github.com/topics/de-xr)
workspace. The default build provides native FLTK applications for **Words,
Sheets and Stage**, with DomeD universal menus, appearance and Wayland/OpenGL
integration. The upstream Qt/KDE frontend is an explicit opt-in.

**This is the first working port milestone, not a complete office replacement.**
Sheets reuses the upstream calculation engine. Words currently edits ODT
paragraph text; Stage currently edits/previews ODP text/image frames. The
original Words pagination and complete Stage shape/animation models still need
porting. Existing ODF package data is retained, but full layout fidelity and
format interoperability are not yet established. DOCX body text/tables, XLSX
cell data and PPTX direct text/images can be imported and saved as ODT, ODS and
ODP; Office export is unavailable. See the build guide for import limitations.

- [Build, run and install](fltk/README.md)
- [Current capabilities and remaining port work](fltk/PORT_STATUS.md)
- [Validation and hardware evidence](fltk/VALIDATION.md)

```sh
./fltk/scripts/build.sh
ctest --test-dir build-fltk --output-on-failure
./build-fltk/fltk/calligra-words-fltk
./build-fltk/fltk/calligra-sheets-fltk
./build-fltk/fltk/calligra-stage-fltk
```

See the build guide for dependencies and the optional private Fedora development
prefix. The corresponding source retains Calligra's original history and
licenses. Upstream baseline: `edb7aee19cf4c38b55389149ea052982d7a6d41e`.

---

## Calligra Suite

Calligra Suite is a collection of office applications linked together by a
common base. This common base assures that all office application can
work together and also share a common look and feel.

Calligra Suite is free software, mostly under LGPL 2+ but also under the GPL.
See COPYING and COPYING.LIB for the details. See also copyright and
licensing notices in individual files.

## Contents

The applications currently included in Calligra Suite (version 2.1) are:

Office productivity:

- **Words:** Word processor
- **Sheets:** Spreadsheet calculator
- **Stage:** Presentation program

Graphics:

- **Karbon:** Vector graphics 

Advanced plugins:

- **Chart:** Graphic data visualization
- **KFormula:** Mathematical formulas

More information can be found at https://calligra.org


## Building and running

Calligra Suite is based on the [KDE Frameworks](https://develop.kde.org/products/frameworks) which are
needed to run Calligra Suite. Note that you don't have to actually run
Plasma Desktop in order to use Calligra Suite. Calligra Suite works fine
on any desktop or platform.

For importing some external file formats or other plugins, you may
also need some other dependencies.  For more details on how to build
Calligra Suite from the sources, see:

https://community.kde.org/Calligra/Building

## Contact info

Developer and User mailing list: [calligra-devel@kde.org](mailto:calligra-devel@kde.org)

Subscribing and list information: https://mail.kde.org/mailman/listinfo/calligra-devel

IRC channel for developers: [#calligra on Libera Chat](irc://irc.libera.chat/calligra)

Calligra Suite forums: http://forum.kde.org/calligra

If you have questions about this README file or about Calligra Suite in general,
please mail to the Calligra Suite mailing list: [calligra-devel@kde.org](mailto:calligra-devel@kde.org)
