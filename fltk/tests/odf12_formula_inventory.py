#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Probe native signatures against official ODF 1.2 Part 2 function headings.

Obtain OpenDocument-v1.2-os-part2.html from OASIS separately and pass --spec.
Signature availability does not prove arithmetic, type or settings semantics.
"""

import argparse
from html.parser import HTMLParser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SOURCE = "https://docs.oasis-open.org/office/v1.2/os/OpenDocument-v1.2-os-part2.html"


class Headings(HTMLParser):
    def __init__(self):
        super().__init__()
        self.active = False
        self.text = []
        self.functions = {}

    def handle_starttag(self, tag, attrs):
        if re.fullmatch("h[1-6]", tag):
            self.active = True
            self.text = []

    def handle_data(self, text):
        if self.active:
            self.text.append(text)

    def handle_endtag(self, tag):
        if re.fullmatch("h[1-6]", tag) and self.active:
            heading = " ".join("".join(self.text).split())
            self.active = False
            match = re.fullmatch(r"(6\.\d+\.\d+)\s*([A-Z][A-Z0-9_.]*)", heading)
            if match:
                self.functions[match[2]] = match[1]


def inventory(spec, test_bin):
    raw = spec.read_bytes()
    parser = Headings()
    parser.feed(raw.decode("utf-8"))
    names = sorted(parser.functions)
    if len(names) != 392 or not {"SUM", "ADDRESS", "IMABS", "ACOTH", "LINEST"} <= set(
        names
    ):
        raise ValueError(
            "Expected the complete 392-function ODF 1.2 OASIS Standard heading inventory"
        )
    output = subprocess.check_output(
        [str(test_bin.resolve()), "--eval"],
        input="".join(n + "()\n" for n in names),
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    values = [json.loads(line) for line in output.splitlines()]
    assert len(values) == len(names)
    available = [n for n, v in zip(names, values) if v["display"] != "#NAME?"]
    return dict(
        scope="ODF 1.2 Part 2 function signatures; semantics and conformance remain separate evidence",
        specification=SOURCE,
        specification_sha256=hashlib.sha256(raw).hexdigest(),
        defined_functions=len(names),
        available_signatures=len(available),
        missing_signatures=len(names) - len(available),
        functions=[
            dict(
                name=n, section=parser.functions[n], signature_available=n in available
            )
            for n in names
        ],
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--spec", required=True, type=Path)
    p.add_argument("--test-bin", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    result = inventory(a.spec, a.test_bin)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(
        "ODF 1.2:",
        result["available_signatures"],
        "of",
        result["defined_functions"],
        "function signatures;",
        result["missing_signatures"],
        "missing; semantic conformance unproven",
    )
