#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent paired-statistics ODS/XLSX workbooks and raw cache checks."""

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
from paired_reference import references

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
YS = "{[.A1];[.B1];6;8;11}"
XS = "{1;2;3;4;5}"
FORMULAS = [
    f"{name}({YS};{XS})"
    for name in (
        "CORREL",
        "PEARSON",
        "COVAR",
        "COVARIANCE.S",
        "SLOPE",
        "INTERCEPT",
        "RSQ",
        "STEYX",
    )
] + [
    f"FORECAST(10;{YS};{XS})",
    *[f"{name}({YS};{XS})" for name in ("SUMX2MY2", "SUMX2PY2", "SUMXMY2")],
    f"COM.MICROSOFT.COVARIANCE.P({YS};{XS})",
    f"_xlfn.FORECAST.LINEAR({{0;10}};{YS};{XS})",
    "CORREL({1;1};{2;3})",
    "NATIVE_UNAVAILABLE_PAIR()",
    "IFERROR([.R1];77)",
    "SLOPE({1;2};{1;2;3})",
    f"_xlfn.SUM2XMY({YS};{XS})",
]


def expected(money=2):
    values = references([money, 4, 6, 8, 11], [1, 2, 3, 4, 5], 10.0)
    return [
        money,
        4,
        values["CORREL"],
        values["CORREL"],
        *[
            values[n]
            for n in (
                "COVAR",
                "COVARIANCE.S",
                "SLOPE",
                "INTERCEPT",
                "RSQ",
                "STEYX",
                "FORECAST",
                "SUMX2MY2",
                "SUMX2PY2",
                "SUMXMY2",
            )
        ],
        values["COVAR"],
        values["INTERCEPT"],
        "#DIV/0!",
        "#NAME?",
        77,
        "#N/A",
        values["SUMXMY2"],
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Paired statistics")
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
    for query, cols in [("{0;10}", 3), ("{1;5}", 2)]:
        row = TableRow()
        sheet.addElement(row)
        row.addElement(
            TableCell(
                formula=f"of:=FORECAST({query};{YS};{XS})",
                valuetype="float",
                value=901,
                numbermatrixrowsspanned=1,
                numbermatrixcolumnsspanned=cols,
            )
        )
        for _ in range(cols - 1):
            row.addElement(TableCell(valuetype="float", value=901))
    path = directory / "paired.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr("opaque/paired.bin", b"unchanged paired producer data\x00\xff")
    book = Workbook()
    ws = book.active
    ws.title = "Paired statistics"
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
    xlsx = directory / "paired.xlsx"
    book.save(xlsx)
    return [path, xlsx]


def verify_caches(path, money=2, source_cache=True):
    with zipfile.ZipFile(path) as z:
        rows = list(ET.fromstring(z.read("content.xml")).iter(TABLE + "table-row"))
        for col, want in enumerate(expected(money), 1):
            cell = rows[0].findall(TABLE + "table-cell")[col - 1]
            if col in (18, 19):
                if source_cache:
                    assert cell.get(OFFICE + "value") == "901", (path, col)
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
            fit = references([money, 4, 6, 8, 11], [1, 2, 3, 4, 5], 10.0)
            b = fit["INTERCEPT"]
            a = fit["SLOPE"]
            for row, col, want in [
                (2, 0, b),
                (2, 1, b + a * 10),
                (2, 2, "#N/A"),
                (3, 0, b + a),
                (3, 1, b + a * 5),
            ]:
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
                z.read("opaque/paired.bin") == b"unchanged paired producer data\x00\xff"
            )
    print("PASS raw paired caches", Path(path).name, flush=True)


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
