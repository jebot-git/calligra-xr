#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP producer and source/style oracle for Stage formatting."""

from pathlib import Path
import os
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentPresentation
from odf.style import Style, TextProperties, ParagraphProperties, GraphicProperties
from odf.style import PageLayout, PageLayoutProperties, MasterPage
from odf.draw import Page, Frame, TextBox, G, Rect
from odf.text import P, Span, A
from stage_move_producer import entries
from stage_objects_producer import normalized, D, O, S, V, T, X

F = "{urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0}"
L = "{http://www.w3.org/1999/xlink}"
M = "{urn:oasis:names:tc:opendocument:xmlns:manifest:1.0}"
TEXT = "AČeský Ωlink Z"


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentPresentation()
    layout = PageLayout(name="Wide")
    layout.addElement(PageLayoutProperties(pagewidth="400pt", pageheight="240pt"))
    doc.automaticstyles.addElement(layout)
    graphic = Style(name="Graphic", family="graphic")
    graphic.addElement(GraphicProperties(fill="none", stroke="none"))
    graphic.addElement(
        TextProperties(fontfamily="DejaVu Sans", fontsize="20pt", color="#223344")
    )
    doc.styles.addElement(graphic)
    paragraph = Style(name="FrameP", family="paragraph")
    paragraph.addElement(
        ParagraphProperties(marginleft="3pt", lineheight="140%", textalign="left")
    )
    doc.styles.addElement(paragraph)
    strong = Style(name="Strong", family="text")
    strong.addElement(TextProperties(fontweight="700"))
    doc.styles.addElement(strong)
    master = MasterPage(name="Standard", pagelayoutname=layout)
    master.addElement(Rect(x="0pt", y="0pt", width="400pt", height="10pt"))
    doc.masterstyles.addElement(master)
    page = Page(name="Formatting", masterpagename=master)
    group = G()
    page.addElement(group)

    def frame(parent, name, x, y, width, height):
        item = Frame(
            name=name,
            stylename=graphic,
            textstylename=paragraph,
            x=f"{x}pt",
            y=f"{y}pt",
            width=f"{width}pt",
            height=f"{height}pt",
        )
        box = TextBox()
        item.addElement(box)
        parent.addElement(item)
        return box

    box = frame(group, "Rich", 20, 30, 300, 100)
    group.childNodes[0].setAttrNS(D[1:-1], "transform", "translate(10pt 20pt)")
    p = P(text="A")
    p.setAttrNS(X[1:-1], "id", "paragraph")
    span = Span(stylename=strong, text="Český")
    p.addElement(span)
    p.addElement(A(href="#paragraph", type="simple", text=" Ωlink"))
    p.addText(" Z")
    box.addElement(p)
    frame(page, "Other", 20, 170, 250, 40).addElement(P(text="Other frame stays"))
    frame(page, "Empty", 320, 170, 60, 40).addElement(P())
    doc.presentation.addElement(page)
    second = Page(name="Second", masterpagename=master)
    frame(second, "Second", 30, 60, 300, 100).addElement(P(text="Second slide stays"))
    doc.presentation.addElement(second)
    source = directory / "formatting.odp"
    doc.save(str(source))
    archive = entries(source)
    root = ET.fromstring(archive["content.xml"])
    other = root.find(
        ".//" + D + "frame[@" + D + 'name="Other"]/' + D + "text-box/" + T + "p"
    )
    other.set(X + "id", "strong")
    other.set(
        T + "style-name", "WordsText1"
    )  # Reserve an unresolved foreign reference.
    archive["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    manifest = ET.fromstring(archive["META-INF/manifest.xml"])
    manifest.set(M + "version", "1.2")
    ET.SubElement(
        manifest,
        M + "file-entry",
        {
            M + "full-path": "opaque/original",
            M + "media-type": "application/octet-stream",
        },
    )
    archive["META-INF/manifest.xml"] = ET.tostring(
        manifest, encoding="utf-8", xml_declaration=True
    )
    archive["opaque/original"] = b"keep\0original bytes"
    with zipfile.ZipFile(source, "w") as z:
        for name, value in archive.items():
            z.writestr(
                name,
                value,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    return source


def resolved(source, name="Rich"):
    """Read scalar runs from raw XML using our own small, explicit style cascade."""
    archive = entries(source)
    content = ET.fromstring(archive["content.xml"])
    styles = {}
    for root in [ET.fromstring(archive["styles.xml"]), content]:
        for node in root.iter(S + "style"):
            styles[node.get(S + "name")] = node

    def properties(name, tag, trail=()):
        assert name not in trail, "Style cycle"
        node = styles.get(name)
        if node is None:
            return {}
        result = properties(node.get(S + "parent-style-name"), tag, (*trail, name))
        props = node.find(S + tag)
        if props is not None:
            result.update(props.attrib)
        return result

    frame = content.find(".//" + D + "frame[@" + D + f'name="{name}"]')
    base = properties(frame.get(D + "style-name"), "text-properties")
    base.update(properties(frame.get(D + "text-style-name"), "text-properties"))
    paragraph_base = properties(
        frame.get(D + "text-style-name"), "paragraph-properties"
    )
    paragraphs = []
    for p in frame.findall(D + "text-box/" + T + "p"):
        paragraph = {
            **paragraph_base,
            **properties(p.get(T + "style-name"), "paragraph-properties"),
        }
        runs = []

        def walk(node, inherited):
            fmt = {
                **inherited,
                **properties(node.get(T + "style-name"), "text-properties"),
            }
            runs.extend((c, fmt.copy()) for c in node.text or "")
            for child in node:
                if child.tag == T + "s":
                    runs.extend(
                        (" ", fmt.copy()) for _ in range(int(child.get(T + "c", "1")))
                    )
                elif child.tag == T + "tab":
                    runs.append(("\t", fmt.copy()))
                elif child.tag == T + "line-break":
                    runs.append(("\n", fmt.copy()))
                else:
                    walk(child, fmt)
                runs.extend((c, fmt.copy()) for c in child.tail or "")

        walk(p, base)
        paragraphs.append((paragraph, runs))
    return content, frame, paragraphs


def verify_saved(original, saved, typed=True):
    a, b = entries(original), entries(saved)
    assert a.keys() == b.keys()
    for name in a:
        if name != "content.xml":
            assert a[name] == b[name], name
    before, oldframe, original_p = resolved(original)
    after, frame, paragraphs = resolved(saved)
    paragraph, runs = paragraphs[0]
    prefix = "xy" if typed else ""
    assert "".join(c for c, _ in runs) == prefix + TEXT
    assert frame.attrib == oldframe.attrib
    assert paragraph[F + "text-align"] == "center"
    for key, value in [
        ("margin-left", "7pt"),
        ("margin-right", "9pt"),
        ("margin-top", "4pt"),
        ("margin-bottom", "5pt"),
        ("line-height", "180%"),
    ]:
        assert paragraph[F + key] == value, (key, paragraph)
    offset = len(prefix)
    for _, fmt in runs[offset + 1 : offset + 6]:
        for key, value in [
            (F + "font-size", "24pt"),
            (F + "color", "#ff0000"),
            (F + "background-color", "#ffee00"),
            (S + "text-underline-style", "solid"),
            (F + "font-weight", "700"),
        ]:
            assert fmt[key] == value, (key, fmt)
    for i in [0, *range(6, len(TEXT))]:
        assert runs[offset + i][1] == original_p[0][1][i][1], i
    if typed:
        for _, fmt in runs[:2]:
            assert (
                fmt[F + "font-size"] == "14pt"
                and fmt[F + "font-weight"] == "700"
                and fmt[F + "font-style"] == "italic"
            ), fmt
    assert len(list(frame.iter(T + "a"))) == 1
    link = next(frame.iter(T + "a"))
    assert link.get(L + "href") == "#paragraph" and "".join(link.itertext()) == " Ωlink"
    for ident in ("paragraph", "strong"):
        assert len([n for n in after.iter() if n.get(X + "id") == ident]) == 1
    # Remove the deliberately edited paragraph and new automatic styles, then
    # compare the entire source tree, including other frames/slides and IDs.
    oldbox, newbox = oldframe.find(D + "text-box"), frame.find(D + "text-box")
    oldbox.clear()
    newbox.clear()
    oldauto, newauto = (
        before.find(O + "automatic-styles"),
        after.find(O + "automatic-styles"),
    )
    oldnames = {n.get(S + "name") for n in oldauto}
    for node in list(newauto):
        if node.get(S + "name") not in oldnames:
            assert node.tag == S + "style" and node.get(S + "family") in [
                "text",
                "paragraph",
            ]
            assert node.get(S + "name") != "WordsText1"
            newauto.remove(node)
    assert normalized(before) == normalized(after), "Unrelated XML changed"
    print(
        "PASS independent Stage character/paragraph/typing semantics, links/IDs and exact unrelated source/package preservation",
        flush=True,
    )


def verify_repeated(original, saved):
    a, b = entries(original), entries(saved)
    assert a.keys() == b.keys()
    for name in a:
        if name != "content.xml":
            assert a[name] == b[name], name
    _, _, before = resolved(original)
    _, _, after = resolved(saved)
    assert before[0][0] == after[0][0]
    old, new = before[0][1], after[0][1]
    assert "".join(c for c, _ in new) == "xxy" + TEXT
    assert new[1:] == old
    assert new[0][0] == "x" and new[0][1][F + "font-size"] == "28pt"
    print(
        "PASS repeated-letter insertion uses exact source range and leaves following runs unchanged",
        flush=True,
    )


def verify(source, bin_dir, art):
    art = Path(art)
    art.mkdir(parents=True, exist_ok=True)
    output = art / "model-saved.odp"
    result = subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-text-format-test"),
            "--document",
            str(source),
            str(output),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        capture_output=True,
        text=True,
    )
    (art / "model.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    verify_saved(source, output)
    return output
