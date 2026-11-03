#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real document identity and live environment fixtures for native Sheets."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from information_producer import produce as produce_information

O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
T = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
FORMULAS = [
    "TYPE(FILENAME())",
    "LEN(FILENAME())>0",
    'OR(RIGHT(FILENAME();4)=".ods";RIGHT(FILENAME();5)=".xlsx")',
    'TYPE(INFO("directory"))',
    'RIGHT(INFO("directory");1)',
    'TYPE(INFO("system"))',
    'LEN(INFO("system"))>0',
    'TYPE(INFO("osversion"))',
    'LEN(INFO("release"))>0',
    'INFO("recalc")',
    'INFO("numfile")',
    'TYPE(INFO("memavail"))',
    'INFO("memavail")>=0',
    'TYPE(INFO("totmem"))',
    'INFO("totmem")>0',
    'TYPE(INFO("memused"))',
    "SUM([.$A$3];[.$A$1])",
    'TYPE(INFO({"system";"recalc"}))',
    'IFERROR(INFO("missing");"unknown")',
]
SECOND = [
    "FILENAME()",
    'IFERROR(INFO("origin");"no viewport")',
    'INFO("missing")',
    'IFERROR([.$C$2];"unknown")',
    "TYPE(FILENAME())",
    'TYPE(INFO({"system";"recalc"}))',
    "LEN([.$A$2])",
    "ISNONTEXT(FILENAME())",
    'INFO("recalc")',
    'TYPE(INFO("numfile"))',
    'TYPE(INFO("origin"))',
    "IFERROR([.$K$2];999)",
]
UNAVAILABLE = {(2, 2), (2, 11), (2, 12), (5, 1)}


def produce(directory):
    return produce_information(directory, FORMULAS, SECOND)


def expected(money=2):
    return [
        money,
        4,
        2,
        "TRUE",
        "TRUE",
        2,
        "/",
        2,
        "TRUE",
        2,
        "TRUE",
        "Automatic",
        1,
        1,
        "TRUE",
        1,
        "TRUE",
        1,
        3 * money,
        64,
        "unknown",
    ]


def same(actual, want):
    return (
        actual == want
        if isinstance(want, str)
        else math.isclose(float(actual), want, rel_tol=1e-12, abs_tol=1e-12)
    )


def raw_cells(path):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read("content.xml"))
        table = root.find(".//" + T + "table")
        result = {}
        r = 1
        for row in table.findall(T + "table-row"):
            nr = int(row.get(T + "number-rows-repeated", "1"))
            c = 1
            for cell in row:
                if cell.tag not in (T + "table-cell", T + "covered-table-cell"):
                    continue
                nc = int(cell.get(T + "number-columns-repeated", "1"))
                assert nr * nc < 10000
                for rr in range(r, r + nr):
                    for cc in range(c, c + nc):
                        result[rr, cc] = cell
                c += nc
            r += nr
        return result


def cache_snapshot(cell):
    return (
        {k: v for k, v in cell.attrib.items() if k.startswith(O)},
        list(cell.itertext()),
    )


def verify_caches(path, money=2):
    path = Path(path)
    cells = raw_cells(path)

    def check(row, col, want):
        cell = cells[row, col]
        if want in ("TRUE", "FALSE"):
            assert (
                cell.get(O + "value-type") == "boolean"
                and cell.get(O + "boolean-value") == want.lower()
            ), (path, row, col, want, cell.attrib)
        elif isinstance(want, str):
            assert (
                cell.get(O + "value-type") == "string"
                and cell.get(O + "string-value") == want
            ), (path, row, col, want, cell.attrib)
        else:
            assert same(cell.get(O + "value"), want), (
                path,
                row,
                col,
                want,
                cell.attrib,
            )

    for col, want in enumerate(expected(money), 1):
        check(1, col, want)
    name = str(path.resolve())
    for col, want in {
        1: name,
        3: "#VALUE!",
        4: "unknown",
        5: 2,
        6: 64,
        7: len(name),
        8: "FALSE",
        9: "Automatic",
        10: 1,
    }.items():
        check(2, col, want)
    origin = cells[2, 2]
    if origin.get(O + "value-type") == "string":
        assert re.fullmatch(
            r"\$A:\$[A-Z]+\$[1-9][0-9]*", origin.get(O + "string-value", "")
        ), (path, origin.attrib)
    else:
        assert origin.get(O + "value") in ("904", "0"), (path, origin.attrib)
    for col in (11, 12):
        assert cells[2, col].get(O + "value") in ("901", "0", "2"), (
            path,
            col,
            cells[2, col].attrib,
        )
    assert cells[5, 1].get(O + "value") in ("907", "0"), (path, cells[5, 1].attrib)
    for row, col, values in [
        (13, 1, [["TRUE", "FALSE"], ["TRUE", "FALSE"]]),
        (16, 1, [[2, "#N/A"], [7, 3]]),
        (19, 1, [[64, 64]]),
    ]:
        for r, data in enumerate(values):
            for c, want in enumerate(data):
                check(row + r, col + c, want)
    print("PASS raw context caches", path.name, flush=True)


def verify(path, test_bin_dir, directory, money=2):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = Path(path)
    before = path.read_bytes()
    saved = directory / (path.name + "-recalculated.ods")
    cached = (
        {
            position: cache_snapshot(cell)
            for position, cell in raw_cells(path).items()
            if position in UNAVAILABLE
        }
        if path.suffix == ".ods"
        else None
    )
    values = json.loads(
        subprocess.check_output(
            [
                str(Path(test_bin_dir).resolve() / "calligra-native-order-test"),
                "--document",
                str(path.resolve()),
                str(saved.resolve()),
            ],
            text=True,
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        )
    )
    for col, want in enumerate(expected(money)):
        got = values[0][0][col]
        assert not got["incomplete"] and same(got.get("value", got["display"]), want), (
            path,
            col,
            want,
            got,
        )
    second = [
        str(path.resolve()),
        "no viewport",
        "#VALUE!",
        "unknown",
        2,
        64,
        len(str(path.resolve())),
        "FALSE",
        "Automatic",
        1,
        16,
        16,
    ]
    for col, want in enumerate(second):
        got = values[0][1][col]
        assert got["incomplete"] == ((2, col + 1) in UNAVAILABLE) and same(
            got.get("value", got["display"]), want
        ), (path, col, want, got)
    verify_caches(saved, money)
    if cached:
        cells = raw_cells(saved)
        assert {
            position: cache_snapshot(cells[position]) for position in UNAVAILABLE
        } == cached
    with zipfile.ZipFile(saved) as z:
        if path.suffix == ".ods":
            assert (
                z.read("opaque/information.bin") == b"information source metadata\0\xff"
            )
    assert path.read_bytes() == before
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(values, indent=2) + "\n"
    )
    return expected(money)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--test-bin-dir", type=Path, required=True)
    a = p.parse_args()
    for path in produce(a.directory):
        verify(path, a.test_bin_dir, a.directory / "verified")
