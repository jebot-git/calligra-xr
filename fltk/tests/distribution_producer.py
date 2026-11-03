#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Distribution workbooks: producer imports, arrays and raw native saved caches."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
import mpmath as mp
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableCell, TableColumn, TableRow
from odf.text import P
from openpyxl import Workbook

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
FORMULAS = [
    "NORM.DIST([.B1];[.A1];3;TRUE)",
    "BETA.DIST(.5;[.A1];3;TRUE)",
    "BINOMDIST([.A1];10;.5;TRUE)",
    "BINOM.INV(10;.5;.5)",
    "CHISQ.DIST([.B1];[.A1];TRUE)",
    "EXPON.DIST([.B1];[.A1]/10;TRUE)",
    "F.DIST([.B1];[.A1];2;TRUE)",
    "GAMMA.DIST([.B1];[.A1];3;TRUE)",
    "HYPGEOMDIST([.A1];10;10;20;TRUE)",
    "LOGNORMDIST([.B1];0;[.A1];TRUE)",
    "NEGBINOM.DIST([.B1];[.A1];.5;TRUE)",
    "POISSON.DIST([.B1];[.A1];TRUE)",
    "T.DIST([.B1];[.A1];TRUE)",
    "WEIBULL.DIST([.B1];[.A1];3;TRUE)",
    "_xlfn.NORM.S.INV(.975)",
    "NATIVE_UNAVAILABLE_DISTRIBUTION()",
    "IFERROR([.R1];77)",
    "GAMMA.INV(.5;[.A1];3)",
    "BINOM.DIST.RANGE(10;.5;[.A1];9)",
]


def expected(money=2):
    with mp.workdps(70):
        x, n = mp.mpf(4), mp.mpf(money)
        gamma = lambda q, a: mp.gammainc(a, 0, q, regularized=True)
        # Independent bisection in the producer, rather than using Boost's
        # inverse routine or trusting an imported formula cache.
        lo, hi = mp.mpf(0), 100 * n
        for _ in range(250):
            mid = (lo + hi) / 2
            if gamma(mid / 3, n) < mp.mpf(".5"):
                lo = mid
            else:
                hi = mid
        values = [
            money,
            4,
            mp.erfc(-(x - n) / 3 / mp.sqrt(2)) / 2,
            mp.betainc(n, 3, 0, mp.mpf(".5"), regularized=True),
            sum(math.comb(10, k) for k in range(money + 1)) / 1024,
            5,
            gamma(x / 2, n / 2),
            -mp.expm1(-x * n / 10),
            (n * x / (n * x + 2)) ** (n / 2),
            gamma(x / 3, n),
            sum(math.comb(10, k) * math.comb(10, 10 - k) for k in range(money + 1))
            / math.comb(20, 10),
            mp.erfc(-mp.log(x) / n / mp.sqrt(2)) / 2,
            sum(
                mp.mpf(math.comb(k + money - 1, k)) / 2 ** (k + money) for k in range(5)
            ),
            mp.gammainc(5, n, mp.inf, regularized=True),
            1
            - mp.betainc(n / 2, mp.mpf(".5"), 0, n / (n + x * x), regularized=True) / 2,
            -mp.expm1(-((x / 3) ** n)),
            mp.sqrt(2) * mp.erfinv(2 * mp.mpf(float(0.975)) - 1),
            "#NAME?",
            77,
            (lo + hi) / 2,
            sum(math.comb(10, k) for k in range(money, 10)) / 1024,
        ]
        return [v if isinstance(v, str) else float(v) for v in values]


def matrix_expected(money):
    with mp.workdps(70):
        return [
            (2, 0, 0.5),
            (2, 1, float(mp.erfc(-1 / mp.sqrt(2)) / 2)),
            (2, 2, "#N/A"),
            *[
                (3, i, float(mp.betainc(money, 3, 0, mp.mpf(x), regularized=True)))
                for i, x in enumerate([".25", ".5"])
            ],
        ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Distributions")
    doc.spreadsheet.addElement(sheet)
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    row = TableRow()
    sheet.addElement(row)
    for v in (2, 4):
        row.addElement(TableCell(valuetype="float", value=v))
    for formula in FORMULAS:
        c = TableCell(formula="of:=" + formula, valuetype="float", value=901)
        c.addElement(P(text="901"))
        row.addElement(c)
    blank = TableRow()
    blank.addElement(TableCell())
    sheet.addElement(blank)
    for formula, cols in [
        ("NORM.S.DIST({0;1};TRUE)", 3),
        ("BETA.DIST({.25;.5};[.A1];3;TRUE)", 2),
    ]:
        row = TableRow()
        sheet.addElement(row)
        row.addElement(
            TableCell(
                formula="of:=" + formula,
                valuetype="float",
                value=901,
                numbermatrixrowsspanned=1,
                numbermatrixcolumnsspanned=cols,
            )
        )
        for _ in range(cols - 1):
            row.addElement(TableCell(valuetype="float", value=901))
    path = directory / "distribution.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr(
            "opaque/distribution.bin", b"unchanged distribution producer data\x00\xff"
        )
    book = Workbook()
    ws = book.active
    ws.title = "Distributions"
    ws.append([2, 4])
    for col, formula in enumerate(FORMULAS, 3):
        formula = re.sub(r"\[\.([A-Z]+[0-9]+)\]", r"\1", formula).replace(";", ",")
        ws.cell(1, col, "=" + formula)
    # Import must translate only legacy Excel F calls, preserving modern ones.
    ws2 = book.create_sheet("Excel tails")
    ws2.append(
        ["=FDIST(3,2,2)", "=FINV(.25,2,2)", "=F.DIST(3,2,2,TRUE)", "=F.INV(.75,2,2)"]
    )
    xlsx = directory / "distribution.xlsx"
    book.save(xlsx)
    return [path, xlsx]


def same(got, want):
    return (
        got == want
        if isinstance(want, str)
        else math.isclose(float(got), want, rel_tol=2e-12, abs_tol=1e-14)
    )


def verify_caches(path, money=2, source_cache=True):
    with zipfile.ZipFile(path) as z:
        tables = list(ET.fromstring(z.read("content.xml")).iter(TABLE + "table"))
        rows = list(tables[0].iter(TABLE + "table-row"))
        for col, want in enumerate(expected(money), 1):
            cell = rows[0].findall(TABLE + "table-cell")[col - 1]
            if col in (18, 19):
                if source_cache:
                    assert cell.get(OFFICE + "value") == "901", (path, col)
            else:
                assert same(
                    cell.get(
                        OFFICE + ("string-value" if isinstance(want, str) else "value")
                    ),
                    want,
                ), (path, col, want)
        if len(rows) > 1:
            for row, col, want in matrix_expected(money):
                cell = rows[row].findall(TABLE + "table-cell")[col]
                assert same(
                    cell.get(
                        OFFICE + ("string-value" if isinstance(want, str) else "value")
                    ),
                    want,
                ), (path, row, col, want)
            assert (
                z.read("opaque/distribution.bin")
                == b"unchanged distribution producer data\x00\xff"
            )
        if len(tables) > 1:
            cells = list(tables[1].iter(TABLE + "table-cell"))
            for cell, want in zip(cells, [0.25, 3, 0.75, 3], strict=True):
                assert same(cell.get(OFFICE + "value"), want), (path, want)
    print("PASS raw distribution caches", Path(path).name, flush=True)


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
    for col, (got, want) in enumerate(zip(rows[0][0], expected(money), strict=True), 1):
        assert same(got.get("value", got["display"]), want), (path, col, got, want)
        assert got["incomplete"] == (col in (18, 19)), (path, col, got)
    if len(rows) > 1:
        for got, want in zip(rows[1][0], [0.25, 3, 0.75, 3], strict=True):
            assert same(got.get("value", got["display"]), want), (path, got, want)
    verify_caches(saved, money, source_cache=path.suffix == ".ods")
    assert before == path.read_bytes()
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    return expected(money)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
