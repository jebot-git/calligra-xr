#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODS/XLSX producers with stale conditional caches and opaque data."""

import argparse
import json
import math
import os
from pathlib import Path
import datetime as dt
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableCell, TableColumn, TableRow
from odf.text import P
from openpyxl import Workbook

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("COUNTIF", "SUMIF", "AVERAGEIF", "COUNTIFS", "SUMIFS", "AVERAGEIFS")
FORMULAS = [
    'COUNTIF([.A1:.B1];">2")',
    'SUMIF([.A1:.B1];">2")',
    'AVERAGEIF([.A1:.B1];">2")',
    'COUNTIFS([.A1:.B1];">2";[.A1:.B1];"<7")',
    'SUMIFS([.A1:.B1];[.A1:.B1];">2";[.A1:.B1];"<7")',
    'AVERAGEIFS([.A1:.B1];[.A1:.B1];">2";[.A1:.B1];"<7")',
    'SUMIF([.C1:.D1];"r*";[.A1])',
    'COUNTIF([.C1:.D1];"r*")',
    'COUNTIF([.C1:.D1];"ed")',
    'COUNTIF([.E1:.F1];"")',
    'SUMIF([.A1:.B1];">0";[.A1])',
    "NATIVE_UNAVAILABLE_CRITERION()",
    "IFERROR([.R1];77)",
    'SUMIFS([.A1:.B1];[.A1:.A2];">0")',
    'COUNTIF([.A1:.B1];{">0";">2"})',
]
OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"


def expected(money=2, profile="wildcards"):
    values = [money, 4]
    selected = [value for value in values if value > 2]
    both = [value for value in values if 2 < value < 7]
    # These independent producer fixtures use a star search against Red/blue.
    # Regex r* and literal r* do not match either whole cell; wildcard r* does.
    star = 1 if profile == "wildcards" else 0
    substring = 1 if profile == "substring" else 0
    return [
        money,
        4,
        "Red",
        "blue",
        "",
        "",
        len(selected),
        sum(selected),
        sum(selected) / len(selected),
        len(both),
        sum(both),
        sum(both) / len(both),
        money * star,
        star,
        substring,
        2,
        sum(values),
        "#NAME?",
        77,
        "#VALUE!",
        2,
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    from odf.table import CalculationSettings, NullDate

    paths = []
    for profile in ["wildcards", "regex", "literal", "substring"]:
        doc = OpenDocumentSpreadsheet()
        settings = CalculationSettings()
        settings.setAttrNS(TABLE[1:-1], "case-sensitive", "true")
        settings.setAttrNS(
            TABLE[1:-1],
            "use-regular-expressions",
            "true" if profile == "regex" else "false",
        )
        settings.setAttrNS(
            TABLE[1:-1], "use-wildcards", "true" if profile == "wildcards" else "false"
        )
        settings.setAttrNS(
            TABLE[1:-1],
            "search-criteria-must-apply-to-whole-cell",
            "false" if profile == "substring" else "true",
        )
        settings.addElement(
            NullDate(datevalue="1904-01-01" if profile == "regex" else "1899-12-30")
        )
        doc.spreadsheet.addElement(settings)
        sheet = Table(name="Conditional aggregates")
        doc.spreadsheet.addElement(sheet)
        sheet.addElement(TableColumn(numbercolumnsrepeated=21))
        row = TableRow()
        sheet.addElement(row)
        for cell in [
            TableCell(valuetype="float", value=2),
            TableCell(valuetype="float", value=4),
            TableCell(valuetype="string"),
            TableCell(valuetype="string"),
            TableCell(),
            TableCell(formula='of:=""', valuetype="string"),
        ]:
            row.addElement(cell)
        row.childNodes[2].addElement(P(text="Red"))
        row.childNodes[3].addElement(P(text="blue"))
        for formula in FORMULAS:
            cell = TableCell(formula="of:=" + formula, valuetype="float", value=901)
            cell.addElement(P(text="901"))
            row.addElement(cell)
        dated = TableRow()
        sheet.addElement(dated)
        dated.addElement(TableCell(valuetype="date", datevalue="1904-01-02"))
        for formula in ['COUNTIF([.A2];">=1904-01-02")', 'COUNTIF([.A2];"<1/3/1904")']:
            dated.addElement(
                TableCell(formula="of:=" + formula, valuetype="float", value=901)
            )
        path = directory / (
            "criteria.ods" if profile == "wildcards" else "criteria-" + profile + ".ods"
        )
        doc.save(str(path))
        with zipfile.ZipFile(path, "a") as archive:
            archive.writestr(
                "opaque/criteria.bin", b"unchanged criteria producer data\x00\xff"
            )
        paths.append(path)
    book = Workbook()
    ws = book.active
    ws.title = "Conditional aggregates"
    for column, value in enumerate([2, 4, "Red", "blue", None, '=""'], 1):
        ws.cell(1, column, value)
    import re

    for column, formula in enumerate(FORMULAS, 7):
        formula = re.sub(
            r"\[\.([A-Z]+[0-9]+)(?::\.([A-Z]+[0-9]+))?\]",
            lambda m: m[1] + (":" + m[2] if m[2] else ""),
            formula,
        ).replace(";", ",")
        ws.cell(1, column, "=" + formula)
    ws.cell(2, 1, dt.datetime(1904, 1, 2))
    ws.cell(2, 1).number_format = "yyyy-mm-dd"
    ws.cell(2, 2, '=COUNTIF(A2,">=1904-01-02")')
    ws.cell(2, 3, '=COUNTIF(A2,"<1/3/1904")')
    path = directory / "criteria.xlsx"
    book.save(path)
    paths.append(path)
    from openpyxl.utils.datetime import CALENDAR_MAC_1904

    book.epoch = CALENDAR_MAC_1904
    path = directory / "criteria-1904.xlsx"
    book.save(path)
    paths.append(path)
    return paths


def verify(path, test_bin_dir, directory, money=2):
    path, directory = Path(path), Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    saved = directory / (path.name + "-recalculated.ods")
    original = path.read_bytes()
    output = subprocess.check_output(
        [
            str(Path(test_bin_dir).resolve() / "calligra-native-criteria-test"),
            "--document",
            str(path),
            str(saved),
        ],
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    profile = next(
        (name for name in ("regex", "literal", "substring") if name in path.stem),
        "wildcards",
    )
    rows = json.loads(output)
    for column, (got, want) in enumerate(zip(rows[0][0], expected(money, profile)), 1):
        if isinstance(want, str):
            assert got["display"] == want, (path, column, got, want)
        else:
            assert got["kind"] == 1 and math.isclose(
                got["value"], want, rel_tol=2e-12, abs_tol=1e-12
            ), (path, column, got, want)
        assert got["incomplete"] == (column in (18, 19)), (column, got)
    assert len(rows[0][0]) == 21
    epoch = (
        dt.date(1904, 1, 1)
        if profile == "regex" or "1904" in path.stem
        else dt.date(1899, 12, 30)
    )
    assert rows[0][1][0]["value"] == (dt.date(1904, 1, 2) - epoch).days, (
        path,
        rows[0][1][0],
    )
    for column in [1, 2]:
        assert (
            rows[0][1][column]["value"] == 1 and not rows[0][1][column]["incomplete"]
        ), (path, column, rows[0][1][column])
    with zipfile.ZipFile(saved) as archive:
        cells = next(
            ET.fromstring(archive.read("content.xml")).iter(TABLE + "table-row")
        ).findall(TABLE + "table-cell")
        dated = list(
            ET.fromstring(archive.read("content.xml")).iter(TABLE + "table-row")
        )[1].findall(TABLE + "table-cell")
        for column in (1, 2):
            assert dated[column].get(OFFICE + "value") == "1", (path, column)
        for column, want in enumerate(expected(money, profile), 1):
            if column in (18, 19):
                if path.suffix == ".ods":
                    assert cells[column - 1].get(OFFICE + "value") == "901"
            elif isinstance(want, (int, float)):
                assert math.isclose(
                    float(cells[column - 1].get(OFFICE + "value")),
                    want,
                    rel_tol=2e-12,
                    abs_tol=1e-12,
                ), column
        if path.suffix == ".ods":
            assert (
                archive.read("opaque/criteria.bin")
                == b"unchanged criteria producer data\x00\xff"
            )
    assert path.read_bytes() == original
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    print(
        "PASS",
        path.name,
        "all six conditional functions, calculated caches and unsupported-cache preservation",
        flush=True,
    )
    return expected(money, profile)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
