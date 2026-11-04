#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent Python ZIP producer/oracle for file and memory package readers."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import warnings
import xml.etree.ElementTree as ET
import zipfile

OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"
CONTENT = f'''<office:document-content xmlns:office="{OFFICE}" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" office:version="1.2"><office:body><office:presentation><draw:page draw:name="Independent" draw:master-page-name="Default"/></office:presentation></office:body></office:document-content>'''.encode()
STYLES = f'''<office:document-styles xmlns:office="{OFFICE}" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.2"><office:styles/><office:automatic-styles><style:page-layout style:name="pm1"><style:page-layout-properties fo:page-width="28cm" fo:page-height="15.75cm"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name="Default" style:page-layout-name="pm1"/></office:master-styles></office:document-styles>'''.encode()


def archive(parts):
    output = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(output, "w") as z:
            for name, data in parts:
                z.writestr(name, data, compress_type=zipfile.ZIP_STORED)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    binary = args.test_bin.resolve()
    manifest = f'''<manifest:manifest xmlns:manifest="{MANIFEST}" manifest:version="1.2"><manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.presentation"/>'''
    values = [
        ("mimetype", b"application/vnd.oasis.opendocument.presentation"),
        ("content.xml", CONTENT),
        ("styles.xml", STYLES),
        ("opaque.bin", bytes(range(256)) * 257),
        ("empty.bin", b""),
    ]
    for name, _ in values[1:]:
        manifest += f'<manifest:file-entry manifest:full-path="{name}" manifest:media-type="{"text/xml" if name.endswith(".xml") else "application/octet-stream"}"/>'
    manifest += "</manifest:manifest>"
    values.append(("META-INF/manifest.xml", manifest.encode()))
    valid = archive(values)
    source = art / "independent.odp"
    source.write_bytes(valid)
    source_hash = hashlib.sha256(valid).hexdigest()
    valid_outputs = []
    rejected = []

    def invoke(mode, path, output):
        result = subprocess.run(
            [str(binary), mode, "stage", str(path), str(output)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=20,
        )
        (art / (output.stem + ".log")).write_bytes(result.stdout + result.stderr)
        assert not any(
            marker in result.stderr
            for marker in (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:")
        ), result.stderr
        return result

    for mode in ("--roundtrip", "--file-roundtrip"):
        output = art / (mode[2:] + ".odp")
        result = invoke(mode, source, output)
        assert result.returncode == 0, result.stderr
        with zipfile.ZipFile(output) as z:
            assert z.testzip() is None
            first = z.infolist()[0]
            assert (
                first.filename == "mimetype"
                and first.compress_type == zipfile.ZIP_STORED
            )
            assert first.extra == b""
            assert set(z.namelist()) == {name for name, _ in values}
            for name, data in values:
                if name != "content.xml":
                    assert z.read(name) == data, name
            root = ET.fromstring(z.read("content.xml"))
            assert root.tag == f"{{{OFFICE}}}document-content"
            assert root.attrib[f"{{{OFFICE}}}version"] == "1.2"
        valid_outputs.append(str(output))

    bad = {
        "not-zip": b"not a document archive",
        "truncated": valid[:-12],
        "traversal": archive(values + [("../outside", b"secret")]),
        "absolute-path": archive(values + [("/outside", b"secret")]),
        "duplicate": archive(values + [("opaque.bin", b"other")]),
        "too-many-parts": archive([(f"p{i}", b"") for i in range(10001)]),
        "wrong-kind": archive(
            [
                (
                    n,
                    b"application/vnd.oasis.opendocument.text"
                    if n == "mimetype"
                    else d,
                )
                for n, d in values
            ]
        ),
        "malformed-content": archive(
            [(n, b"<malformed" if n == "content.xml" else d) for n, d in values]
        ),
        "entity-content": archive(
            [
                (
                    n,
                    b'<!DOCTYPE root [<!ENTITY x SYSTEM "file:///etc/passwd">]><root>&x;</root>'
                    if n == "content.xml"
                    else d,
                )
                for n, d in values
            ]
        ),
        "deep-content": archive(
            [
                (
                    n,
                    b"<root>" + b"<x>" * 257 + b"</x>" * 257 + b"</root>"
                    if n == "content.xml"
                    else d,
                )
                for n, d in values
            ]
        ),
        "malformed-styles": archive(
            [(n, b"<malformed" if n == "styles.xml" else d) for n, d in values]
        ),
    }
    with zipfile.ZipFile(io.BytesIO(valid)) as z:
        opaque = z.getinfo("opaque.bin")
        start = (
            opaque.header_offset
            + 30
            + len(opaque.filename.encode())
            + len(opaque.extra)
        )
        broken = bytearray(valid)
        broken[start] ^= 1
        bad["bad-data-crc"] = bytes(broken)
    central = []
    at = 0
    while True:
        at = valid.find(b"PK\1\2", at)
        if at < 0:
            break
        length = struct.unpack_from("<H", valid, at + 28)[0]
        central.append((valid[at + 46 : at + 46 + length].decode(), at))
        at += 46 + length
    for name, offset in central:
        if name == "empty.bin":
            broken = bytearray(valid)
            struct.pack_into("<I", broken, offset + 16, 1)
            bad["bad-empty-crc"] = bytes(broken)
        if name == "opaque.bin":
            broken = bytearray(valid)
            struct.pack_into("<I", broken, offset + 24, 64 * 1024 * 1024 + 1)
            bad["oversized-declaration"] = bytes(broken)
            broken = bytearray(valid)
            struct.pack_into("<H", broken, offset + 8, 1)
            struct.pack_into("<H", broken, opaque.header_offset + 6, 1)
            bad["encrypted-entry"] = bytes(broken)
    for case, data in bad.items():
        path = art / (case + ".odp")
        path.write_bytes(data)
        for mode in ("--roundtrip", "--file-roundtrip"):
            output = art / (case + "-" + mode[2:] + ".output")
            result = invoke(mode, path, output)
            assert result.returncode == 1 and not output.exists(), (
                case,
                mode,
                result.returncode,
                result.stderr.decode(errors="replace"),
            )
        rejected.append(case)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
    result = dict(
        valid_outputs=valid_outputs,
        rejected=rejected,
        both_readers_checked=True,
        source_unchanged=True,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    )
    (art / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(
        f"Independent ZIP oracle: two valid round trips and {len(rejected)} rejected cases per reader"
    )


if __name__ == "__main__":
    main()
