#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent inference workbooks, import semantics and raw saved cache checks."""

import argparse
from fractions import Fraction as F
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
from inference_reference import tests, sample, real

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
YS = "{[.A1];4;6;8;11}"
XS = "{1;2;3;4;5}"
TYPED = '{[.A1];4;"skip";8;11}'
FORMULAS = [
    "PROB({1;2;3};{.2;.3;.5};[.A1];[.A1]+1)",
    f"SUM(FREQUENCY({YS};{{4;8}}))",
    f"F.TEST({YS};{XS})",
    f"TTEST({YS};{XS};2;1)",
    f"T.TEST({YS};{XS};2;2)",
    f"T.TEST({YS};{XS};2;3)",
    f"ZTEST({YS};5)",
    f"Z.TEST({YS};5;2)",
    "CHISQ.TEST({[.A1];4};{5;5})",
    "PROB({1;2};{1;1E-300};2)",
    "PROB({1;2;3};{.2;.3;.5};{1;3})",
    f"TTEST({TYPED};{XS};2;2)",
    f"T.TEST({TYPED};{XS};2;2)",
    f"_xlfn.T.TEST({YS};{XS};{{1;2}};{{2;3}})",
    "Z.TEST({[.A1]};5;2)",
    "NATIVE_UNAVAILABLE_INFERENCE()",
    "IFERROR([.R1];77)",
    "CHISQ.TEST({1;2;3;4;5;[.A1]};{2;2;3;3;5;6})",
    "PROB({1;2;3};{.2;.3;.5};8;2)",
]


def ztest(values, query, sigma=None):
    n, mean, var = sample(values)
    sd = real(sigma) if sigma is not None else mp.sqrt(real(var))
    z = real(mean - F(query)) * mp.sqrt(n) / sd
    return float(mp.erfc(z / mp.sqrt(2)) / 2)


def chi(observed, expected, df):
    statistic = sum(
        (F(o) - e) ** 2 / e for o, e in zip(observed, expected, strict=True)
    )
    return float(
        mp.gammainc(mp.mpf(df) / 2, real(statistic) / 2, mp.inf, regularized=True)
    )


def expected(money=2, excel=False):
    ys = [money, 4, 6, 8, 11]
    xs = [1, 2, 3, 4, 5]
    values = tests(ys, xs)
    typed = tests([money, 4, 8, 11], [1, 2, 4, 5])["T2"]
    modern = tests([money, 4, 8, 11], xs)["T2"]
    return [
        money,
        4,
        0.8 if money == 2 else 0,
        5,
        values["FTEST"],
        values["T1"],
        values["T2"],
        values["T3"],
        ztest(ys, 5),
        ztest(ys, 5, 2),
        chi([money, 4], [5, 5], 1),
        1e-300,
        0.2,
        modern if excel else typed,
        modern,
        values["T2"] / 2,
        ztest([money], 5, 2),
        "#NAME?",
        77,
        chi([1, 2, 3, 4, 5, money], [2, 2, 3, 3, 5, 6], 5),
        0,
    ]


def matrix_expected(money):
    ys = [money, 4, 6, 8, 11]
    counts = [
        sum(v <= 4 for v in ys),
        sum(4 < v <= 8 for v in ys),
        sum(v > 8 for v in ys),
        "#N/A",
    ]
    return (
        [(2 + i, 0, n) for i, n in enumerate(counts)]
        + [(7, i, v) for i, v in enumerate([0.2, 0.3, 0.5, "#N/A"])]
        + [
            (9 + r, c, ztest(ys, q))
            for r, row in enumerate([[4, 5], [6, 7]])
            for c, q in enumerate(row)
        ]
    )


def excel_expected():
    return [0.25, 3, 0.75, 3, tests([1, 2, 3], [2, 3, 4, 5])["T3"], ztest([2], 5, 2)]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Inference")
    doc.spreadsheet.addElement(sheet)
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    row = TableRow()
    sheet.addElement(row)
    for v in [2, 4]:
        row.addElement(TableCell(valuetype="float", value=v))
    for formula in FORMULAS:
        c = TableCell(formula="of:=" + formula, valuetype="float", value=901)
        c.addElement(P(text="901"))
        row.addElement(c)
    grid = {}
    for r, c, formula, nr, nc in [
        (2, 0, f"FREQUENCY({YS};{{4;8}})", 4, 1),
        (7, 0, "PROB({1;2;3};{.2;.3;.5};{1;2;3})", 1, 4),
        (9, 0, f"ZTEST({YS};{{4;5|6;7}})", 2, 2),
    ]:
        for rr in range(r, r + nr):
            for cc in range(c, c + nc):
                grid[(rr, cc)] = TableCell(valuetype="float", value=901)
        grid[(r, c)].setAttribute("formula", "of:=" + formula)
        grid[(r, c)].setAttribute("numbermatrixrowsspanned", nr)
        grid[(r, c)].setAttribute("numbermatrixcolumnsspanned", nc)
    for r in range(1, 11):
        row = TableRow()
        sheet.addElement(row)
        columns = max((c + 1 for rr, c in grid if rr == r), default=1)
        for c in range(columns):
            row.addElement(grid.get((r, c), TableCell()))
    path = directory / "inference.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr("opaque/inference.bin", b"unchanged inference producer data\x00\xff")
    book = Workbook()
    ws = book.active
    ws.title = "Inference"
    ws.append([2, 4])
    for col, formula in enumerate(FORMULAS, 3):
        formula = re.sub(r"\[\.([A-Z]+[0-9]+)\]", r"\1", formula).replace(";", ",")
        ws.cell(1, col, "=" + formula)
    ws2 = book.create_sheet("Excel semantics")
    ws2.append(
        [
            "=FDIST(3,2,2)",
            "=FINV(.25,2,2)",
            "=F.DIST(3,2,2,TRUE)",
            "=F.INV(.75,2,2)",
            "=TTEST({1,2,3},{2,3,4,5},2,3)",
            "=ZTEST(2,5,2)",
        ]
    )
    xlsx = directory / "inference.xlsx"
    book.save(xlsx)
    return [path, xlsx]


def same(got, want):
    return (
        got == want
        if isinstance(want, str)
        else math.isclose(float(got), want, rel_tol=2e-12, abs_tol=5e-324)
    )


def verify_caches(path, money=2, source_cache=True, excel=False):
    with zipfile.ZipFile(path) as z:
        tables = list(ET.fromstring(z.read("content.xml")).iter(TABLE + "table"))
        rows = list(tables[0].iter(TABLE + "table-row"))
        for col, want in enumerate(expected(money, excel), 1):
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
                ), (path, col, want, cell.attrib)
        if len(rows) > 1:
            for row, col, want in matrix_expected(money):
                cell = rows[row].findall(TABLE + "table-cell")[col]
                assert same(
                    cell.get(
                        OFFICE + ("string-value" if isinstance(want, str) else "value")
                    ),
                    want,
                ), (path, row, col, want, cell.attrib)
            assert (
                z.read("opaque/inference.bin")
                == b"unchanged inference producer data\x00\xff"
            )
        if len(tables) > 1:
            for cell, want in zip(
                tables[1].iter(TABLE + "table-cell"), excel_expected(), strict=True
            ):
                assert same(cell.get(OFFICE + "value"), want), (path, want)
    print("PASS raw inference caches", Path(path).name, flush=True)


def verify(path, test_bin_dir, directory, money=2):
    path, directory = Path(path), Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    saved = directory / (path.name + "-recalculated.ods")
    before = path.read_bytes()
    excel = path.suffix == ".xlsx"
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
    for col, (got, want) in enumerate(
        zip(rows[0][0], expected(money, excel), strict=True), 1
    ):
        assert same(got.get("value", got["display"]), want), (path, col, got, want)
        assert got["incomplete"] == (col in (18, 19)), (path, col, got)
    if len(rows) > 1:
        for got, want in zip(rows[1][0], excel_expected(), strict=True):
            assert same(got.get("value", got["display"]), want), (path, got, want)
    verify_caches(saved, money, source_cache=not excel, excel=excel)
    assert before == path.read_bytes()
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    return expected(money, excel)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
