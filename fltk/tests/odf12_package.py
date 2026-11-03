#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check selected ODF 1.2 Part 3 plain-package constraints and manifest schema.

Separate from content/styles schemas, rendering and formula semantics.
Obtain OpenDocument-v1.2-os-manifest-schema.rng from OASIS separately.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
from lxml import etree as ET

M = "{urn:oasis:names:tc:opendocument:xmlns:manifest:1.0}"


def validate(path, schema):
    with zipfile.ZipFile(path) as z:
        items = z.infolist()
        names = [i.filename for i in items]
        assert len(names) == len(set(names)), "Duplicate ZIP entry"
        assert (
            items and items[0].filename == "mimetype" and items[0].header_offset == 0
        ), "Mimetype is not the first ZIP entry"
        first = items[0]
        assert first.compress_type == zipfile.ZIP_STORED, "Compressed mimetype"
        with path.open("rb") as f:
            header = f.read(30)
        fields = struct.unpack("<IHHHHHIIIHH", header)
        assert fields[0] == 0x04034B50 and fields[-1] == 0, (
            "Mimetype local header has extra fields"
        )
        assert not first.flag_bits & 1, "Encrypted mimetype"
        media = z.read("mimetype").decode("ascii")
        assert media in {
            "application/vnd.oasis.opendocument.text",
            "application/vnd.oasis.opendocument.spreadsheet",
            "application/vnd.oasis.opendocument.presentation",
        }, "Unexpected document MIME type"
        assert all(
            i.compress_type in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED] for i in items
        ), "Unsupported ZIP compression"
        assert z.testzip() is None, "ZIP CRC failure"
        root = ET.fromstring(
            z.read("META-INF/manifest.xml"),
            ET.XMLParser(resolve_entities=False, no_network=True),
        )
        assert schema.validate(root), str(schema.error_log)
        assert root.get(M + "version") == "1.2", "Manifest is not ODF1.2"
        entries = root.findall(M + "file-entry")
        paths = [e.get(M + "full-path") for e in entries]
        assert len(paths) == len(set(paths)), "Duplicate manifest path"
        mapped = dict(zip(paths, entries))
        assert "/" in mapped and mapped["/"].get(M + "media-type") == media, (
            "Root manifest media type differs"
        )
        assert "mimetype" not in mapped and "META-INF/manifest.xml" not in mapped, (
            "Forbidden self/mimetype manifest entry"
        )
        for item in items:
            if (
                item.is_dir()
                or item.filename == "mimetype"
                or item.filename.startswith("META-INF/")
            ):
                continue
            assert item.filename in mapped, "Unmanifested file: " + item.filename
        for name in paths:
            if name == "/" or name.endswith("/"):
                continue
            assert name in names, "Missing manifested file: " + name
        return dict(
            file=str(path.resolve()),
            valid=True,
            zip_entries=len(items),
            manifest_entries=len(entries),
            mimetype_first_stored_without_extra=True,
        )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest-schema", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("inputs", nargs="+", type=Path)
    a = p.parse_args()
    schema = ET.RelaxNG(ET.parse(str(a.manifest_schema)))
    files = set()
    for path in a.inputs:
        if path.is_dir():
            files.update(
                q for q in path.rglob("*") if q.suffix in [".odt", ".ods", ".odp"]
            )
        else:
            files.add(path)
    if not files:
        raise SystemExit("No ODF packages")
    records = []
    for path in sorted(files):
        try:
            record = validate(path, schema)
        except (
            AssertionError,
            OSError,
            ValueError,
            zipfile.BadZipFile,
            ET.XMLSyntaxError,
            KeyError,
            struct.error,
        ) as error:
            record = dict(file=str(path.resolve()), valid=False, error=str(error))
        records.append(record)
    result = dict(
        scope="ODF1.2 manifest schema and selected Part3 plain-package constraints; no functional conformance claim",
        schema_sha256=hashlib.sha256(a.manifest_schema.read_bytes()).hexdigest(),
        valid=all(r["valid"] for r in records),
        documents=records,
    )
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS" if result["valid"] else "FAIL", len(records), "ODF1.2 package checks")
    raise SystemExit(0 if result["valid"] else 1)
