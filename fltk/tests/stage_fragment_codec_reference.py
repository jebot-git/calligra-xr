#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent Stage envelope/ZIP producer and selection/privacy oracle.

Uses the standard library, not the native encoder, for incoming envelopes.
This is model/protocol coverage; it does not claim a working Stage clipboard UI.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

from native_memory_archive import OFFICE, MANIFEST, STYLES, archive

DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
MIME = b"application/vnd.oasis.opendocument.presentation"
MAGIC = b"CStage\0\1"


def frame(odp, paths):
    result = bytearray(MAGIC + struct.pack(">QI", len(odp), len(paths)))
    for path in paths:
        result += struct.pack(">H", len(path))
        result += b"".join(struct.pack(">I", rank) for rank in path)
    return result + odp


def unpack(wire):
    assert wire[:8] == MAGIC
    size, count = struct.unpack_from(">QI", wire, 8)
    at, paths = 20, []
    for _ in range(count):
        (length,) = struct.unpack_from(">H", wire, at)
        at += 2
        paths.append(list(struct.unpack_from(">" + "I" * length, wire, at)))
        at += 4 * length
    assert len(wire) - at == size
    with zipfile.ZipFile(io.BytesIO(wire[at:])) as package:
        assert package.testzip() is None
        parts = {name: package.read(name) for name in package.namelist()}
    content = ET.fromstring(parts["content.xml"])
    pages = content.findall(f".//{{{OFFICE}}}presentation/{{{DRAW}}}page")
    assert len(pages) == 1
    selected = []
    for path in paths:
        node = pages[0]
        for rank in path:
            node = list(node)[rank]
        selected.append(node.attrib[f"{{{DRAW}}}name"])
    return paths, selected, parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    test_bin = args.test_bin.resolve()
    content = f'''<office:document-content office:version="1.2" xmlns:office="{OFFICE}" xmlns:draw="{DRAW}" xmlns:text="{TEXT}" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"><office:body><office:presentation><draw:page draw:name="Independent" draw:master-page-name="Default">
<!-- non-element siblings must not change element-child paths -->
<draw:g draw:name="Outer" xml:id="outer">
 <draw:rect draw:name="Alpha" xml:id="alpha" svg:x="10pt" svg:y="10pt" svg:width="80pt" svg:height="30pt"><text:p>Independent Č λ</text:p></draw:rect>
 <!-- comment between selected siblings -->
 <draw:rect draw:name="Beta" xml:id="beta" svg:x="100pt" svg:y="10pt" svg:width="80pt" svg:height="30pt"><text:p>Second</text:p></draw:rect>
 <draw:rect draw:name="UnselectedChild" svg:width="20pt" svg:height="20pt"><text:p>GROUP_CHILD_SECRET</text:p></draw:rect>
</draw:g>
<draw:rect draw:name="Outside" svg:width="20pt" svg:height="20pt"><text:p>OUTSIDE_SECRET</text:p></draw:rect>
</draw:page></office:presentation></office:body></office:document-content>'''.encode()
    parts = [
        ("mimetype", MIME),
        ("content.xml", content),
        ("styles.xml", STYLES),
        ("unused/binary", b"UNSELECTED_ARCHIVE_SECRET\0\xff"),
        (
            "meta.xml",
            f'<office:document-meta xmlns:office="{OFFICE}" office:version="1.2"><office:meta/></office:document-meta>'.encode(),
        ),
    ]
    manifest = f'<manifest:manifest xmlns:manifest="{MANIFEST}" manifest:version="1.2"><manifest:file-entry manifest:full-path="/" manifest:media-type="{MIME.decode()}"/>'
    for name, _ in parts[1:]:
        media = "text/xml" if name.endswith(".xml") else "application/octet-stream"
        manifest += f'<manifest:file-entry manifest:full-path="{name}" manifest:media-type="{media}"/>'
    manifest += "</manifest:manifest>"
    parts.append(("META-INF/manifest.xml", manifest.encode()))
    odp = archive(parts)
    (art / "independent-source.odp").write_bytes(odp)
    valid = {
        "children": (
            [(0, 1), (0, 0)],
            ["Beta", "Alpha"],
            "Independent Č λ\nSecond",
            False,
        ),
        "group": (
            [(0,)],
            ["Outer"],
            "Independent Č λ\nSecond\nGROUP_CHILD_SECRET",
            True,
        ),
        "single": ([(0, 1)], ["Beta"], "Second", False),
    }

    def invoke(name, wire):
        input_path, output_path = (
            art / (name + ".cstage"),
            art / (name + "-output.cstage"),
        )
        input_path.write_bytes(wire)
        output_path.unlink(missing_ok=True)
        result = subprocess.run(
            [str(test_bin), "--canonicalize", str(input_path), str(output_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        (art / (name + ".log")).write_bytes(result.stdout + result.stderr)
        assert not any(
            marker in result.stderr
            for marker in (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:")
        ), result.stderr
        return result, output_path

    accepted = []
    for name, (paths, names, text, group) in valid.items():
        wire = frame(odp, paths)
        result, output = invoke(name, wire)
        assert result.returncode == 0, result.stderr
        response = json.loads(result.stdout)
        canonical_paths, canonical_names, clean = unpack(output.read_bytes())
        assert canonical_names == names == response["names"]
        assert canonical_paths == response["selection"]
        assert response["text"] == text
        assert set(clean) == {
            "mimetype",
            "content.xml",
            "styles.xml",
            "META-INF/manifest.xml",
        }
        assert clean["mimetype"] == MIME
        assert b"OUTSIDE_SECRET" not in clean["content.xml"]
        assert (b"GROUP_CHILD_SECRET" in clean["content.xml"]) == group
        # A second canonicalization must retain exact XML parts and selection,
        # independent of ZIP container timestamps/compression conventions.
        again, repeated = invoke(name + "-again", output.read_bytes())
        assert again.returncode == 0, again.stderr
        repeated_paths, repeated_names, repeated_parts = unpack(repeated.read_bytes())
        assert (repeated_paths, repeated_names, repeated_parts) == (
            canonical_paths,
            canonical_names,
            clean,
        )
        accepted.append(name)

    base = bytes(frame(odp, [(0, 1), (0, 0)]))

    def changed(at, value):
        return base[:at] + value + base[at + len(value) :]

    bad = {
        "bad-magic": b"X" + base[1:],
        "future-version": changed(7, b"\x02"),
        "empty": b"",
        "truncated-header": base[:19],
        "truncated-path": base[:25],
        "truncated-archive": base[:-1],
        "trailing-data": base + b"x",
        "zero-archive": changed(8, struct.pack(">Q", 0)),
        "oversized-archive": changed(8, struct.pack(">Q", 64 * 1024 * 1024 + 1)),
        "overflow-archive": changed(8, b"\xff" * 8),
        "empty-selection": frame(odp, []),
        "large-count": changed(16, struct.pack(">I", 100001)),
        "overflow-count": changed(16, b"\xff" * 4),
        "zero-depth": frame(odp, [()]),
        "large-depth": frame(odp, [(0,) * 257]),
        "large-rank": frame(odp, [(1000001,)]),
        "duplicate": frame(odp, [(0, 1), (0, 1)]),
        "ancestor-overlap": frame(odp, [(0, 1), (0,)]),
        "outside-page": frame(odp, [(20,)]),
        "outside-group": frame(odp, [(0, 20)]),
        "text-node": frame(odp, [(0, 1, 0)]),
        "not-zip": frame(b"not an ODP archive", [(0,)]),
        "truncated-zip": frame(odp[:-12], [(0,)]),
        "too-many-path-components": frame(odp, [(0,) * 256] * 3907),
        "too-many-pages": frame(
            archive(
                [
                    (
                        name,
                        data.replace(
                            b"</office:presentation>",
                            b'<draw:page draw:name="Extra" draw:master-page-name="Default"/></office:presentation>',
                        )
                        if name == "content.xml"
                        else data,
                    )
                    for name, data in parts
                ]
            ),
            [(0,)],
        ),
        "crc-content": frame(odp.replace(b"Independent", b"Xndependent", 1), [(0,)]),
    }
    # The last mutation must preserve stored member sizes and leave a stale CRC.
    assert len(bad["crc-content"]) == len(frame(odp, [(0,)]))
    rejected = []
    for name, wire in bad.items():
        result, output = invoke("bad-" + name, wire)
        assert result.returncode == 1 and not output.exists(), (
            name,
            result.returncode,
            result.stdout,
            result.stderr,
        )
        rejected.append(name)
    report = dict(
        accepted=accepted,
        rejected=rejected,
        selected_order_and_group_identity=True,
        source_order_text=True,
        unselected_source_metadata_assets_excluded=True,
        independent_producer=True,
        independent_output_parser=True,
        byte_count=len(base),
        source_sha256=hashlib.sha256(odp).hexdigest(),
        scope="Selected private protocol/model cases; no destination import or UI claim",
    )
    (art / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"Stage private fragment: {len(accepted)} independent valid cases, {len(rejected)} invalid cases passed"
    )


if __name__ == "__main__":
    main()
