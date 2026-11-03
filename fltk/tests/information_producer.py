#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent editable ODS/XLSX information-function fixtures and raw caches."""

import argparse
from datetime import date, datetime, time
import json
import math
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableColumn, TableRow, TableCell
from odf.text import P
from openpyxl import Workbook
from openpyxl.worksheet.formula import ArrayFormula

O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
T = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
FORMULAS = [
    "FORMULA([.$A$3])",
    "ISFORMULA([.$A$3])",
    "FORMULA([.$A$4])",
    "ISFORMULA([.$A$4])",
    "TYPE([.$A$4])",
    "ERROR.TYPE([.$A$4])",
    "ISNONTEXT([.$A$6])",
    "ISNUM([.$A$3])",
    "ISEVEN([.$A$1]+1)",
    "ISODD([.$A$1]+1)",
    "ISDATE([.$A$8])",
    "ISTIME([.$A$8])",
    "ISTIME([.$A$9])",
    "ISDATE([.$A$10])",
    "ISTIME([.$A$10])",
    "TYPE([.$A$3:.$A$4])",
    "SUM(TYPE({1;TRUE()});[.$A$3])",
    "FORMULA([.$A$5])",
    "ISFORMULA([.$A$5])",
]
SECOND = [
    "TYPE([.$A$5])",
    "IFERROR([.$A$5];5)",
    "ERROR.TYPE([.$A$5])",
    "ISFORMULA([.$A$5])",
    "FORMULA([.$A$5])",
    "ISNONTEXT([.$A$5])",
    "TYPE([.Z999])",
    "TYPE({1})",
    'ISNOTTEXT("plain")',
    "COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISEVEN(2.9)",
    "COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETISODD(-3.9)",
    "ISEVEN(TRUE())",
]
MATRICES = [
    (13, 1, 2, 2, "ISFORMULA([.$A$3:.$B$4])"),
    (16, 1, 2, 2, 'ERROR.TYPE({#DIV/0!;"text"|#N/A;#VALUE!})'),
    (19, 1, 1, 2, "TYPE({1;TRUE()})"),
]


def expected(money=2):
    return [
        money,
        4,
        "=$A$1*2",
        "TRUE",
        "=1/0",
        "TRUE",
        16,
        2,
        "FALSE",
        "TRUE",
        "FALSE",
        "TRUE",
        "TRUE",
        "FALSE",
        "TRUE",
        "TRUE",
        "TRUE",
        64,
        64 + 2 * money,
        "=NATIVE_MISSING()",
        "TRUE",
    ]


def second():
    return [
        16,
        5,
        5,
        "TRUE",
        "=NATIVE_MISSING()",
        "TRUE",
        1,
        64,
        "FALSE",
        "TRUE",
        "TRUE",
        "#VALUE!",
    ]


def excel(formula):
    text = re.sub(r"\[\.([^\]]+)\]", lambda m: m[1].replace(":.", ":"), formula)
    text = re.sub(r"\bFORMULA\(", "_xlfn.FORMULATEXT(", text)
    literal = False
    out = []
    for ch in text:
        if ch == '"':
            literal = not literal
        out.append(
            ("," if ch == ";" else ";" if ch == "|" else ch) if not literal else ch
        )
    return "=" + "".join(out)


def cell(value=None, formula=None):
    if value is None:
        c = TableCell()
    elif type(value) is bool:
        c = TableCell(valuetype="boolean", booleanvalue=str(value).lower())
    elif isinstance(value, str):
        c = TableCell(valuetype="string", stringvalue=value)
    else:
        c = TableCell(valuetype="float", value=value)
    if value is not None:
        c.addElement(P(text=str(value)))
    if formula:
        c.setAttribute("formula", "of:=" + formula)
    return c


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    table = Table(name="Information")
    table.addElement(TableColumn(numbercolumnsrepeated=21))
    grid = {(1, 1): cell(2), (1, 2): cell(4)}
    for col, f in enumerate(FORMULAS, 3):
        grid[1, col] = cell(901, f)
    for col, f in enumerate(SECOND, 1):
        grid[2, col] = cell({1: 903, 2: 904, 3: 905, 6: 906}.get(col, 901), f)
    grid[3, 1] = cell(901, "[.$A$1]*2")
    grid[3, 2] = cell("anchor")
    grid[4, 1] = cell(901, "1/0")
    grid[5, 1] = cell(907, "NATIVE_MISSING()")
    grid[6, 1] = cell("=literal")
    grid[7, 1] = cell(True)
    grid[8, 1] = TableCell(valuetype="date", datevalue="2001-02-03")
    grid[9, 1] = TableCell(valuetype="time", timevalue="PT4H5M6.25S")
    grid[10, 1] = TableCell(valuetype="date", datevalue="2001-02-03T04:05:06.250")
    for row, col, rows, cols, f in MATRICES:
        c = cell(901, f)
        c.setAttribute("numbermatrixrowsspanned", rows)
        c.setAttribute("numbermatrixcolumnsspanned", cols)
        grid[row, col] = c
        for rr in range(row, row + rows):
            for cc in range(col, col + cols):
                grid.setdefault((rr, cc), cell(901))
    for row in range(1, 21):
        element = TableRow()
        for col in range(1, 22):
            element.addElement(grid.get((row, col), cell()))
        table.addElement(element)
    doc.spreadsheet.addElement(table)
    ods = directory / "information.ods"
    doc.save(str(ods))
    with zipfile.ZipFile(ods, "a") as z:
        z.writestr("opaque/information.bin", b"information source metadata\0\xff")
    book = Workbook()
    book.iso_dates = True
    ws = book.active
    ws.title = "Information"
    ws.cell(1, 1, 2)
    ws.cell(1, 2, 4)
    for col, f in enumerate(FORMULAS, 3):
        ws.cell(1, col, excel(f))
    for col, f in enumerate(SECOND, 1):
        ws.cell(2, col, excel(f))
    ws.cell(3, 1, "=$A$1*2")
    ws.cell(3, 2, "anchor")
    ws.cell(4, 1, "=1/0")
    ws.cell(5, 1, "=NATIVE_MISSING()")
    ws.cell(6, 1, "=literal").data_type = "s"
    ws.cell(7, 1, True)
    ws.cell(8, 1, date(2001, 2, 3))
    ws.cell(9, 1, time(4, 5, 6, 250000))
    ws.cell(10, 1, datetime(2001, 2, 3, 4, 5, 6, 250000))
    for row, col, rows, cols, f in MATRICES:
        end = f"{chr(64 + col + cols - 1)}{row + rows - 1}"
        start = f"{chr(64 + col)}{row}"
        ws.cell(row, col, ArrayFormula(ref=f"{start}:{end}", text=excel(f)))
    ws.row_dimensions[20].height = 25
    xlsx = directory / "information.xlsx"
    book.save(xlsx)
    return [ods, xlsx]


def same(actual, want):
    return (
        actual == want
        if isinstance(want, str)
        else math.isclose(float(actual), want, rel_tol=1e-12, abs_tol=1e-12)
    )


def verify_caches(path, money=2, source_cache=True, excel=False):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read("content.xml"))
        rows = root.find(".//" + T + "table").findall(T + "table-row")
        cells = {}
        row_number = 1
        for row in rows:
            nr = int(row.get(T + "number-rows-repeated", "1"))
            column = 1
            for cell in row:
                if cell.tag not in (T + "table-cell", T + "covered-table-cell"):
                    continue
                nc = int(cell.get(T + "number-columns-repeated", "1"))
                assert nr * nc < 10000
                for rr in range(row_number, row_number + nr):
                    for cc in range(column, column + nc):
                        cells[rr, cc] = cell
                column += nc
            row_number += nr

        def check(row, col, want):
            v = cells[row, col]
            if want in ("TRUE", "FALSE"):
                assert (
                    v.get(O + "value-type") == "boolean"
                    and v.get(O + "boolean-value") == want.lower()
                ), (path, row, col, v.attrib)
            elif isinstance(want, str):
                assert (
                    v.get(O + "value-type") == "string"
                    and v.get(O + "string-value") == want
                ), (path, row, col, want, v.attrib)
            else:
                assert same(v.get(O + "value"), want), (path, row, col, want, v.attrib)

        for col, want in enumerate(expected(money), 1):
            check(1, col, want)
        for col, want in enumerate(second(), 1):
            if col in (1, 2, 3, 6):
                v = cells[2, col]
                assert v.get(O + "value") == str(
                    {1: 903, 2: 904, 3: 905, 6: 906}[col] if source_cache else 0
                ), (path, col, v.attrib)
            else:
                check(2, col, want)
        assert cells[5, 1].get(O + "value") == str(907 if source_cache else 0)
        for row, col, values in [
            (13, 1, [["TRUE", "FALSE"], ["TRUE", "FALSE"]]),
            (16, 1, [[2, "#N/A"], [7, 3]]),
            (19, 1, [[64, 64]]),
        ]:
            for r, data in enumerate(values):
                for c, want in enumerate(data):
                    check(row + r, col + c, want)
        if not excel:
            assert (
                z.read("opaque/information.bin") == b"information source metadata\0\xff"
            )
    print("PASS raw information caches", Path(path).name, flush=True)


def verify(path, test_bin_dir, directory, money=2):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = Path(path)
    before = path.read_bytes()
    saved = directory / (path.name + "-recalculated.ods")
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
    for col, want in enumerate(second()):
        got = values[0][1][col]
        assert got["incomplete"] == (col in (0, 1, 2, 5)) and same(
            got.get("value", got["display"]), want
        ), (path, col, want, got)
    excel = path.suffix == ".xlsx"
    verify_caches(saved, money, source_cache=not excel, excel=excel)
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
