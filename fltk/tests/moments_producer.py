#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODS/XLSX higher-moment workbooks, aliases, matrices and raw caches."""

import argparse
from decimal import Decimal as D, localcontext
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
from moments_reference import moment

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
DATA = "{[.A1];[.B1];6;8;20}"
FORMULAS = [f"{name}({DATA})" for name in ("SKEW", "SKEWP", "KURT", "KURTP")] + [
    f"TRIMMEAN({DATA};0.4)",
    "STANDARDIZE([.A1];[.B1];2)",
    f"_xlfn.SKEW.P({DATA})",
    *[
        f"COM.MICROSOFT.{name}({DATA})"
        for name in ("STDEV.S", "STDEV.P", "VAR.S", "VAR.P")
    ],
    f"VARIANCE({DATA})",
    "STANDARDIZE({[.A1];[.B1]};0;2)",
    f"TRIMMEAN({DATA};{{0;0.4}})",
    "KURT(1;1;1;1)",
    "NATIVE_UNAVAILABLE_MOMENT()",
    "IFERROR([.R1];77)",
    f"TRIMMEAN({DATA};1)",
    "_xlfn.STANDARDIZE(1E308;-1E308;1E308)",
]


def expected(money=2):
    values = [money, 4, 6, 8, 20]
    n = len(values)
    with localcontext() as ctx:
        ctx.prec = 80
        v = list(map(D, values))
        mean = sum(v) / n
        squares = sum((x - mean) ** 2 for x in v)
        var = squares / (n - 1)
        pop = squares / n
        trimmed = float(sum(sorted(v)[1:-1]) / 3)
        return [
            money,
            4,
            *[moment(name, values) for name in ("SKEW", "SKEWP", "KURT", "KURTP")],
            trimmed,
            (money - 4) / 2,
            moment("SKEWP", values),
            float(var.sqrt()),
            float(pop.sqrt()),
            float(var),
            float(pop),
            float(var),
            money / 2,
            float(mean),
            "#DIV/0!",
            "#NAME?",
            77,
            "#NUM!",
            2,
        ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Higher moments")
    doc.spreadsheet.addElement(sheet)
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    row = TableRow()
    sheet.addElement(row)
    for value in (2, 4):
        row.addElement(TableCell(valuetype="float", value=value))
    for formula in FORMULAS:
        cell = TableCell(formula="of:=" + formula, valuetype="float", value=901)
        cell.addElement(P(text="901"))
        row.addElement(cell)
    blank = TableRow()
    blank.addElement(TableCell())
    sheet.addElement(blank)
    for formula, cols in [
        ("STANDARDIZE({[.A1];[.B1]};0;2)", 3),
        (f"TRIMMEAN({DATA};{{0;0.4}})", 2),
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
    path = directory / "moments.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr("opaque/moments.bin", b"unchanged moments producer data\x00\xff")
    book = Workbook()
    ws = book.active
    ws.title = "Higher moments"
    ws.append([2, 4])
    for col, formula in enumerate(FORMULAS, 3):
        formula = (
            re.sub(
                r"\[\.([A-Z]+[0-9]+)(?::\.([A-Z]+[0-9]+))?\]",
                lambda m: m[1] + (":" + m[2] if m[2] else ""),
                formula,
            )
            .replace(";", ",")
            .replace("COM.MICROSOFT.", "_xlfn.")
        )
        ws.cell(1, col, "=" + formula)
    xlsx = directory / "moments.xlsx"
    book.save(xlsx)
    return [path, xlsx]


def verify_caches(path, money=2, source_cache=True):
    with zipfile.ZipFile(path) as z:
        rows = list(ET.fromstring(z.read("content.xml")).iter(TABLE + "table-row"))
        for col, want in enumerate(expected(money), 1):
            cell = rows[0].findall(TABLE + "table-cell")[col - 1]
            if col in (18, 19):
                if not source_cache:
                    continue
                assert cell.get(OFFICE + "value") == "901", (
                    path,
                    col,
                    "unsupported cache",
                )
            elif isinstance(want, (int, float)):
                assert math.isclose(
                    float(cell.get(OFFICE + "value")),
                    want,
                    rel_tol=2e-12,
                    abs_tol=1e-12,
                ), (path, col)
            else:
                assert cell.get(OFFICE + "string-value") == want, (path, col)
        if len(rows) > 1:
            targets = [
                (2, 0, money / 2),
                (2, 1, 2),
                (2, 2, "#N/A"),
                (3, 0, sum([money, 4, 6, 8, 20]) / 5),
                (3, 1, sum(sorted([money, 4, 6, 8, 20])[1:-1]) / 3),
            ]
            for row, col, want in targets:
                cell = rows[row].findall(TABLE + "table-cell")[col]
                assert (
                    cell.get(OFFICE + "string-value") == want
                    if isinstance(want, str)
                    else math.isclose(
                        float(cell.get(OFFICE + "value")),
                        want,
                        rel_tol=2e-12,
                        abs_tol=1e-12,
                    )
                ), (path, row, col)
            assert (
                z.read("opaque/moments.bin")
                == b"unchanged moments producer data\x00\xff"
            )
    print("PASS raw exported/saved caches", Path(path).name, flush=True)


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
        assert (
            got["display"] == want
            if isinstance(want, str)
            else got["kind"] == 1
            and math.isclose(got["value"], want, rel_tol=2e-12, abs_tol=1e-12)
        ), (path, col, got, want)
        assert got["incomplete"] == (col in (18, 19)), (path, col, got)
    verify_caches(saved, money, source_cache=path.suffix == ".ods")
    assert path.read_bytes() == before
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    return expected(money)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", required=True, type=Path)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
