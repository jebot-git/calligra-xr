#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent cut source/archive oracle and real PDF/CUPS regression."""

import argparse
from collections import Counter
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct

from PIL import Image
from pypdf import PdfReader
from cups_runtime import PrivateCups
import print_producer
from stage_fragment_producer import N, freeze, package, xml
from stage_fragment_codec_reference import unpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--test-bin", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument(
        "--clipboard-artifacts",
        type=Path,
        help="Also validate and print both actual private Wayland receipts",
    )
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    docs = art / "documents"
    subprocess.run([str(args.test_bin.resolve()), "--documents", str(docs)], check=True)
    original, cut, selected = (
        package(docs / (name + ".odp")) for name in ("source", "cut", "selected")
    )
    assert original.keys() == cut.keys()
    for name, data in original.items():
        if name != "content.xml":
            assert cut[name] == data, name
    expected = copy.deepcopy(xml(original["content.xml"]))
    for name in ("X", "Y"):
        found = expected.xpath("//*[@draw:name=$name]", name=name, namespaces=N)
        assert len(found) == 1
        found[0].getparent().remove(found[0])
    page = expected.xpath("//draw:page", namespaces=N)[0]
    page.set("{" + N["draw"] + "}nav-order", "a b n1 n2 c")
    assert freeze(xml(cut["content.xml"])) == freeze(expected)
    chosen = xml(selected["content.xml"])
    shapes = chosen.xpath("//draw:rect|//draw:frame|//draw:line", namespaces=N)
    assert [node.get("{" + N["draw"] + "}name") for node in shapes] == ["X", "Y"]
    assert selected["Pictures/image.svg"] == original["Pictures/image.svg"]
    assert "opaque/source" not in selected
    source_content = xml(original["content.xml"])
    for node in shapes:
        name = node.get("{" + N["draw"] + "}name")
        source_node = source_content.xpath(
            "//*[@draw:name=$name]", name=name, namespaces=N
        )[0]
        assert freeze(node) == freeze(source_node)

    cases = [("source", 2), ("cut", 2), ("selected", 1)]
    transfers = []
    if args.clipboard_artifacts:
        for receiver in ("native", "external"):
            received = args.clipboard_artifacts.resolve() / (
                "received-" + receiver + ".cstage"
            )
            canonical = art / ("received-" + receiver + ".cstage")
            result = subprocess.run(
                [
                    str(
                        args.bin_dir.resolve()
                        / "calligra-native-stage-fragment-codec-test"
                    ),
                    "--canonicalize",
                    str(received),
                    str(canonical),
                ],
                check=True,
                capture_output=True,
                text=True,
                timeout=30,
            )
            metadata = json.loads(result.stdout)
            wire = canonical.read_bytes()
            paths, names, actual = unpack(wire)
            assert metadata["names"] == names == ["Y", "X"]
            assert metadata["selection"] == paths and metadata["text"] == "X"
            assert actual.keys() == selected.keys()
            for name, data in selected.items():
                if name != "content.xml":
                    assert actual[name] == data, (receiver, name)
            actual_content, expected_content = (
                xml(actual["content.xml"]),
                xml(selected["content.xml"]),
            )
            assert freeze(actual_content) == freeze(expected_content)
            # Re-import can repeat inherited namespace declarations. Expanded
            # XML and every effective QName binding must still agree exactly.
            assert [node.nsmap for node in actual_content.iter()] == [
                node.nsmap for node in expected_content.iter()
            ]
            (archive_size,) = struct.unpack_from(">Q", wire, 8)
            name = "received-" + receiver
            (docs / (name + ".odp")).write_bytes(wire[-archive_size:])
            cases.append((name, 1))
            transfers.append(
                dict(
                    receiver=receiver,
                    selection=names,
                    text=metadata["text"],
                    noncontent_parts_exact=True,
                    expanded_xml_and_effective_namespaces_equal=True,
                )
            )

    environment = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        environment.pop(key, None)
    binary = args.bin_dir.resolve() / "calligra-stage-fltk"
    expected_pdfs, reviewed = [], []
    with PrivateCups(art / "cups") as cups:
        print_producer.verify(args.bin_dir, art / "suite", cups=cups, bitmap_fonts=True)
        environment.update(CUPS_SERVER=cups.server, LC_ALL="C")
        for name, count in cases:
            source, output, saved = (
                docs / (name + ".odp"),
                art / (name + ".pdf"),
                art / (name + "-saved.odp"),
            )
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            run = subprocess.run(
                [
                    str(binary),
                    str(source),
                    "--export-odf",
                    str(saved),
                    "--print-pdf",
                    str(output),
                    "--print-to",
                    cups.name,
                ],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=60,
            )
            (art / (name + "-print.log")).write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, run.stderr
            assert len(PdfReader(output).pages) == count
            reopened = art / (name + "-reopened.pdf")
            subprocess.run(
                [str(binary), str(saved), "--print-pdf", str(reopened)],
                env=environment,
                check=True,
                capture_output=True,
                timeout=60,
            )
            for file, prefix in ((output, name), (reopened, name + "-reopened")):
                subprocess.run(
                    ["pdftoppm", "-r", "100", "-png", str(file), str(art / prefix)],
                    check=True,
                    capture_output=True,
                    timeout=60,
                )
            for page_index in range(1, count + 1):
                image = art / f"{name}-{page_index}.png"
                assert (
                    image.read_bytes()
                    == (art / f"{name}-reopened-{page_index}.png").read_bytes()
                )
                reviewed.append(str(image))
            center = (
                Image.open(art / (name + "-1.png"))
                .convert("RGB")
                .getpixel((int(210 * 100 / 72), int(110 * 100 / 72)))
            )
            expected_center = (255, 255, 255) if name == "cut" else (255, 0, 0)
            # SVG/PDF interpolation can round a red channel from255 to254.
            # Check the analytic color with the existing two-level print bound.
            assert max(abs(a - b) for a, b in zip(center, expected_center)) <= 2, (
                name,
                center,
            )
            assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
            expected_pdfs.append(output)
        expected_pdfs += [
            art / "suite" / (name + ".pdf") for name in ("words", "sheets", "stage")
        ]
        jobs, page_count = len(cases) + 3, sum(count for _, count in cases) + 9
        captured = cups.completed(jobs)
        assert Counter(
            hashlib.sha256(p.read_bytes()).hexdigest() for p in captured
        ) == Counter(hashlib.sha256(p.read_bytes()).hexdigest() for p in expected_pdfs)
        assert sum(len(PdfReader(p).pages) for p in captured) == page_count
    report = dict(
        cups_jobs=jobs,
        page_candidates=page_count,
        cut_page_candidates=reviewed,
        independent_full_content_removal_oracle=True,
        all_noncontent_parts_exact=True,
        transferred_private_model_cases=transfers,
        copied_image_exact=True,
        removed_image_white=True,
        all_reopened_pages_exact=True,
        system_queues_changed=False,
        scope="Prepared cut/model and selected output cases; no clipboard UI or destination import claim",
    )
    (art / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"PASS independent cut XML/assets oracle and {jobs} CUPS jobs/{page_count} pages"
    )


if __name__ == "__main__":
    main()
