#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODS/XLSX order-statistics fixtures, stale caches and matrices."""

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

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
FORMULAS = [
    "SMALL([.A1:.B1];1)",
    "LARGE([.A1:.B1];1)",
    "RANK([.A1];[.A1:.B1])",
    "RANK.AVG(4;{2;4;4;8})",
    "PERCENTILE([.A1:.B1];0.25)",
    "PERCENTILE.EXC([.A1:.B1];0.5)",
    "QUARTILE([.A1:.B1];1)",
    "QUARTILE.EXC({[.A1];[.B1];6};1)",
    "PERCENTRANK({[.A1];[.B1];6};[.B1])",
    "PERCENTRANK.EXC({[.A1];[.B1];6};[.B1])",
    "MODE({[.A1];[.B1];[.A1];[.B1]})",
    "COM.MICROSOFT.MODE.SNGL({[.A1];[.B1];[.A1];[.B1]})",
    "MEDIAN([.A1:.B1])",
    "COM.MICROSOFT.MODE.MULT({[.A1];[.B1];[.A1];[.B1]})",
    "_xlfn.SMALL([.A1:.B1];{1;2})",
    "NATIVE_UNAVAILABLE_ORDER()",
    "IFERROR([.R1];77)",
    "PERCENTILE.EXC([.A1:.B1];0)",
    "_xlfn.RANK.EQ([.B1];[.A1:.B1])",
]


def expected(money=2):
    lo, hi = sorted([money, 4])
    return [
        money,
        4,
        lo,
        hi,
        2 if money < 4 else 1,
        2.5,
        lo + (hi - lo) / 4,
        (money + 4) / 2,
        lo + (hi - lo) / 4,
        min(money, 4, 6),
        0.5 if money < 4 else 0,
        0.5 if money < 4 else 0.25,
        lo,
        money,
        (money + 4) / 2,
        money,
        lo,
        "#NAME?",
        77,
        "#NUM!",
        1 if money < 4 else 2,
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Order statistics")
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
    # A3:A5 deliberately reserves an extra slot; B3:C3 is a selector array.
    for r in range(3):
        row = TableRow()
        sheet.addElement(row)
        cell = TableCell(valuetype="float", value=901)
        if r == 0:
            cell.setAttribute(
                "formula", "of:=COM.MICROSOFT.MODE.MULT({[.A1];[.B1];[.A1];[.B1]})"
            )
            cell.setAttribute("numbermatrixrowsspanned", 3)
            cell.setAttribute("numbermatrixcolumnsspanned", 1)
        row.addElement(cell)
        if r == 0:
            cell = TableCell(
                formula="of:=SMALL([.A1:.B1];{1;2})",
                valuetype="float",
                value=901,
                numbermatrixrowsspanned=1,
                numbermatrixcolumnsspanned=2,
            )
            row.addElement(cell)
            row.addElement(TableCell(valuetype="float", value=901))
    path = directory / "order.ods"
    doc.save(str(path))
    with zipfile.ZipFile(path, "a") as z:
        z.writestr("opaque/order.bin", b"unchanged order producer data\x00\xff")
    book = Workbook()
    ws = book.active
    ws.title = "Order statistics"
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
    xlsx = directory / "order.xlsx"
    book.save(xlsx)
    return [path, xlsx]


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
            and math.isclose(got["value"], want, rel_tol=1e-12, abs_tol=1e-12)
        ), (path, col, got, want)
        assert got["incomplete"] == (col in (18, 19)), (path, col, got)
    with zipfile.ZipFile(saved) as z:
        xml = ET.fromstring(z.read("content.xml"))
        xr = list(xml.iter(TABLE + "table-row"))
        for col, want in enumerate(expected(money), 1):
            cell = xr[0].findall(TABLE + "table-cell")[col - 1]
            if col in (18, 19):
                if path.suffix == ".ods":
                    assert cell.get(OFFICE + "value") == "901"
            elif isinstance(want, (int, float)):
                assert math.isclose(
                    float(cell.get(OFFICE + "value")),
                    want,
                    rel_tol=1e-12,
                    abs_tol=1e-12,
                )
            elif want.startswith("#"):
                assert cell.get(OFFICE + "string-value") == want, (
                    col,
                    ET.tostring(cell),
                )
        if path.suffix == ".ods":
            for row, col, want in [
                (2, 0, money),
                (3, 0, 4),
                (4, 0, "#N/A"),
                (2, 1, min(money, 4)),
                (2, 2, max(money, 4)),
            ]:
                got = rows[0][row][col]
                assert (
                    got["display"] == want
                    if isinstance(want, str)
                    else got["value"] == want
                ), (row, col, got, want)
                cell = xr[row].findall(TABLE + "table-cell")[col]
                assert (
                    cell.get(OFFICE + "string-value") == want
                    if isinstance(want, str)
                    else float(cell.get(OFFICE + "value")) == want
                )
            assert (
                z.read("opaque/order.bin") == b"unchanged order producer data\x00\xff"
            )
    assert path.read_bytes() == before
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    print(
        "PASS",
        path.name,
        "fourteen order functions, caches, errors, opaque data and fixed matrices",
        flush=True,
    )
    return expected(money)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", required=True, type=Path)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
