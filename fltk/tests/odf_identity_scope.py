#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ODF1.2 per-XML-file identity regression for the shared schema gate."""

import argparse
import json
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

D = "{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}"
S = "{urn:oasis:names:tc:opendocument:xmlns:style:1.0}"
V = "{urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0}"
X = "{http://www.w3.org/XML/1998/namespace}"

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--source", type=Path, required=True)
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--schema-dir", type=Path, required=True)
p.add_argument("--jing-jar", type=Path)
p.add_argument("--java", default="java")
a = p.parse_args()
a.artifacts.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(a.source) as archive:
    original = {name: archive.read(name) for name in archive.namelist()}
assert original["mimetype"] == b"application/vnd.oasis.opendocument.presentation"
roots = {name: ET.fromstring(original[name]) for name in ["content.xml", "styles.xml"]}
reserved = {n.get(X + "id") for r in roots.values() for n in r.iter()}
identifier = "PartIdentity"
while identifier in reserved:
    identifier += "X"
for name, root in roots.items():
    parent = root.find(
        ".//" + (D + "page" if name == "content.xml" else S + "master-page")
    )
    assert parent is not None
    ET.SubElement(
        parent,
        D + "rect",
        {X + "id": identifier, V + "width": "10pt", V + "height": "10pt"},
    )
common = {
    name: ET.tostring(root, encoding="utf-8", xml_declaration=True)
    for name, root in roots.items()
}
results = []
for case in ["cross-part", "duplicate-content", "duplicate-styles"]:
    parts = {**original, **common}
    if case != "cross-part":
        name = "content.xml" if case == "duplicate-content" else "styles.xml"
        root = ET.fromstring(parts[name])
        parent = root.find(
            ".//" + (D + "page" if name == "content.xml" else S + "master-page")
        )
        ET.SubElement(
            parent,
            D + "rect",
            {X + "id": identifier, V + "width": "10pt", V + "height": "10pt"},
        )
        parts[name] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    document = a.artifacts / (case + ".odp")
    with zipfile.ZipFile(document, "w") as archive:
        for name, data in parts.items():
            archive.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    output = a.artifacts / (case + ".json")
    command = [
        sys.executable,
        str(Path(__file__).with_name("odf_schema.py")),
        "--schema-dir",
        str(a.schema_dir),
        "--output",
        str(output),
    ]
    if a.jing_jar:
        command += ["--jing-jar", str(a.jing_jar), "--java", a.java]
    run = subprocess.run(
        [*command, str(document)], capture_output=True, text=True, check=False
    )
    (a.artifacts / (case + ".log")).write_text(run.stdout + run.stderr)
    result = json.loads(output.read_text())
    expected = case == "cross-part"
    assert (run.returncode == 0) == result["valid"] == expected, (case, result)
    if not expected:
        diagnostics = json.dumps(result)
        assert "ID " in diagnostics or "Duplicate XML identity" in diagnostics, (
            diagnostics
        )
    results.append(
        {"case": case, "expected_valid": expected, "observed_valid": result["valid"]}
    )
(a.artifacts / "results.json").write_text(
    json.dumps(
        {
            "passed": True,
            "scope": "ODF1.2 Part1 section19.914; IDs unique within each XML file",
            "cases": results,
        },
        indent=2,
    )
    + "\n"
)
print(
    "PASS reused IDs across parts accepted; duplicates within either XML file rejected"
)
