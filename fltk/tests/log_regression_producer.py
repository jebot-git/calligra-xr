#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODS/XLSX matrix regression exports and editable native fixtures."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableCell, TableColumn, TableRow
from odf.text import P
from openpyxl import Workbook
from log_regression_reference import reference

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
Y = "{[.A1];4;6;8;11}"
X = "{1;2;3;4;5}"
MULTI = "{1;0;1;2;1|0;1;1;1;2}"
S = f"LOGEST({Y};{X};TRUE;TRUE)"
M = f"LOGEST({Y};{MULTI};TRUE;TRUE)"
FORMULAS = [
    f"INDEX({S};{r};{c})"
    for r, c in [(1, 1), (1, 2), (3, 1), (3, 2), (4, 1), (4, 2), (5, 1), (5, 2)]
] + [
    f"SUM(GROWTH({Y};{X};{{6;7}}))",
    f"INDEX(LOGEST({Y};;TRUE;FALSE);1;1)",
    f"INDEX(LOGEST({Y};{X};FALSE;TRUE);1;1)",
    *[f"INDEX({M};1;{c})" for c in (1, 2, 3)],
    f"SUM(GROWTH({Y};{MULTI};{{3;2|1;3}}))",
    "NATIVE_UNAVAILABLE_REGRESSION()",
    "IFERROR([.R1];77)",
    f"SUM(GROWTH(TRANSPOSE({Y});TRANSPOSE({MULTI});{{3;1|2;3}}))",
    "SUM(INDEX({1;2|3;4};;2))",
]


def model(money=2, multiple=False, constant=True):
    y = [money, 4, 6, 8, 11]
    x = (
        [[1, 0], [0, 1], [1, 1], [2, 1], [1, 2]]
        if multiple
        else [[1], [2], [3], [4], [5]]
    )
    q = [[3, 1], [2, 3]] if multiple else [[6], [7]]
    return reference(y, x, constant, q)


def scalar(x):
    return x if isinstance(x, str) else float(x)


def expected(money=2):
    s, p = model(money)
    m, q = model(money, True)
    nc, _ = model(money, constant=False)
    return [
        money,
        4,
        *[
            scalar(s[r][c])
            for r, c in [(0, 0), (0, 1), (2, 0), (2, 1), (3, 0), (3, 1), (4, 0), (4, 1)]
        ],
        sum(map(float, p)),
        float(s[0][0]),
        float(nc[0][0]),
        *[float(v) for v in m[0]],
        sum(map(float, q)),
        "#NAME?",
        77,
        sum(map(float, q)),
        6,
    ]


def matrices(money):
    s, p = model(money)
    m, q = model(money, True)
    _, extra = reference(
        [money, 4, 6, 8, 11], [[1], [2], [3], [4], [5]], True, [[6], [7], [8], [9]]
    )
    return [
        (2, 0, m),
        (8, 0, [extra[:2], extra[2:]]),
        (11, 0, [[q[0], "#N/A"], [q[1], "#N/A"]]),
        (14, 0, s),
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Logarithmic")
    doc.spreadsheet.addElement(sheet)
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    row = TableRow()
    sheet.addElement(row)
    for v in (2, 4):
        row.addElement(TableCell(valuetype="float", value=v))
    for f in FORMULAS:
        c = TableCell(formula="of:=" + f, valuetype="float", value=901)
        c.addElement(P(text="901"))
        row.addElement(c)
    grid = {}
    for r, c, f, nr, nc in [
        (2, 0, M, 5, 3),
        (8, 0, f"GROWTH({Y};{X};{{6;7|8;9}})", 2, 2),
        (11, 0, f"GROWTH(TRANSPOSE({Y});TRANSPOSE({MULTI});{{3;1|2;3}})", 2, 2),
        (14, 0, S, 5, 2),
    ]:
        for rr in range(r, r + nr):
            for cc in range(c, c + nc):
                grid[rr, cc] = TableCell(valuetype="float", value=901)
        grid[r, c].setAttribute("formula", "of:=" + f)
        grid[r, c].setAttribute("numbermatrixrowsspanned", nr)
        grid[r, c].setAttribute("numbermatrixcolumnsspanned", nc)
    for r in range(1, 19):
        row = TableRow()
        sheet.addElement(row)
        for c in range(max((cc + 1 for rr, cc in grid if rr == r), default=1)):
            row.addElement(grid.get((r, c), TableCell()))
    path = directory / "log-regression.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr(
            "opaque/log-log-regression.bin", b"logarithmic package metadata\x00\xff"
        )
    book = Workbook()
    ws = book.active
    ws.title = "Logarithmic"
    ws.append([2, 4])
    for col, f in enumerate(FORMULAS, 3):
        # ODF array semicolons separate columns; vertical bars separate rows.
        f = re.sub(r"\[\.([A-Z]+[0-9]+)\]", r"\1", f)
        depth = 0
        out = []
        for ch in f:
            if ch == "{":
                depth += 1
            if ch == "}":
                depth -= 1
            out.append(
                ("," if ch == ";" else ";" if ch == "|" else ch)
                if depth
                else ("," if ch == ";" else ch)
            )
        ws.cell(1, col, "=" + "".join(out))
    xlsx = directory / "log-regression.xlsx"
    book.save(xlsx)
    return [path, xlsx]


def same(got, want):
    return (
        got == want
        if isinstance(want, str)
        else math.isclose(float(got), float(want), rel_tol=3e-12, abs_tol=5e-324)
    )


def verify_caches(path, money=2, source_cache=True):
    with zipfile.ZipFile(path) as z:
        table = ET.fromstring(z.read("content.xml")).find(".//" + TABLE + "table")
        rows = table.findall(TABLE + "table-row")
        for c, want in enumerate(expected(money)):
            cell = rows[0].findall(TABLE + "table-cell")[c]
            if c in (17, 18):
                if source_cache:
                    assert cell.get(OFFICE + "value") == "901", (path, c, cell.attrib)
            else:
                assert same(
                    cell.get(
                        OFFICE + ("string-value" if isinstance(want, str) else "value")
                    ),
                    want,
                ), (path, c, want, cell.attrib)
        if len(rows) > 1:
            for row, col, values in matrices(money):
                for r, line in enumerate(values):
                    for c, v in enumerate(line):
                        v = scalar(v)
                        cell = rows[row + r].findall(TABLE + "table-cell")[col + c]
                        assert same(
                            cell.get(
                                OFFICE
                                + ("string-value" if isinstance(v, str) else "value")
                            ),
                            v,
                        ), (path, row + r, col + c, v, cell.attrib)
            assert (
                z.read("opaque/log-log-regression.bin")
                == b"logarithmic package metadata\x00\xff"
            )
    print("PASS raw logarithmic regression caches", Path(path).name, flush=True)


def verify(path, test_bin_dir, directory, money=2):
    path, directory = Path(path), Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    saved = directory / (path.name + "-recalculated.ods")
    before = path.read_bytes()
    rows = json.loads(
        subprocess.check_output(
            [
                str(Path(test_bin_dir).resolve() / "calligra-native-order-test"),
                "--document",
                str(path),
                str(saved),
            ],
            text=True,
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        )
    )
    for c, (got, want) in enumerate(zip(rows[0][0], expected(money), strict=True)):
        assert same(got.get("value", got["display"]), want), (path, c, want, got)
        assert got["incomplete"] == (c in (17, 18)), (path, c, got)
    verify_caches(saved, money, source_cache=path.suffix != ".xlsx")
    assert path.read_bytes() == before
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    return expected(money)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    args = parser.parse_args()
    for p in produce(args.directory):
        verify(p, args.test_bin_dir, args.directory / "verified")
