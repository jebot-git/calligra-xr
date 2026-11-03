#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODS/XLSX producers with stale statistics caches and opaque data."""

import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import Table, TableCell, TableColumn, TableRow
from odf.text import P
from openpyxl import Workbook

ROOT = Path(__file__).resolve().parents[2]
NAMES = (
    "AVERAGEA",
    "AVEDEV",
    "DEVSQ",
    "DEVSQA",
    "GEOMEAN",
    "HARMEAN",
    "STDEVA",
    "STDEVPA",
    "VARA",
    "VARPA",
)
FORMULAS = [f"{name}([.A1:.F1])" for name in NAMES] + [
    "[.G1]*2",
    "NATIVE_UNAVAILABLE_STATISTIC()",
    "IFERROR([.R1];77)",
    "GEOMEAN(0)",
    'AVERAGEA({[.A1];[.B1]|TRUE();"text"})',
]
OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"


def expected(money=2):
    plain = [money, 4]
    full = [money, 4, 1, 0, 0]
    mean = statistics.mean(plain)
    m2 = sum((x - statistics.mean(full)) ** 2 for x in full)
    return [
        money,
        4,
        "TRUE",
        "text",
        "",
        "",
        statistics.mean(full),
        statistics.mean(abs(x - mean) for x in plain),
        sum((x - mean) ** 2 for x in plain),
        m2,
        statistics.geometric_mean(plain),
        statistics.harmonic_mean(plain),
        statistics.stdev(full),
        statistics.pstdev(full),
        statistics.variance(full),
        statistics.pvariance(full),
        statistics.mean(full) * 2,
        "#NAME?",
        77,
        "#NUM!",
        statistics.mean([money, 4, 1, 0]),
    ]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Descriptive statistics")
    doc.spreadsheet.addElement(sheet)
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    row = TableRow()
    sheet.addElement(row)
    for cell in [
        TableCell(valuetype="float", value=2),
        TableCell(valuetype="float", value=4),
        TableCell(valuetype="boolean", booleanvalue="true"),
        TableCell(valuetype="string"),
        TableCell(),
        TableCell(formula='of:=""', valuetype="string"),
    ]:
        row.addElement(cell)
    row.childNodes[3].addElement(P(text="text"))
    for formula in FORMULAS:
        cell = TableCell(formula="of:=" + formula, valuetype="float", value=901)
        cell.addElement(P(text="901"))
        row.addElement(cell)
    ods = directory / "statistics.ods"
    doc.save(str(ods))
    with zipfile.ZipFile(ods, "a") as archive:
        archive.writestr(
            "opaque/statistics.bin", b"unchanged statistics producer data\x00\xff"
        )
    book = Workbook()
    ws = book.active
    ws.title = "Descriptive statistics"
    for column, value in enumerate([2, 4, True, "text", None, '=""'], 1):
        ws.cell(1, column, value)
    import re

    for column, formula in enumerate(FORMULAS, 7):
        # The last formula uses the retained nonstandard expression-array
        # extension, so use a portable scalar expression in the XLSX producer.
        if column == 21:
            formula = 'AVERAGEA(A1,B1,TRUE(),"text")'
        else:
            formula = re.sub(
                r"\[\.([A-Z]+[0-9]+)(?::\.([A-Z]+[0-9]+))?\]",
                lambda m: m[1] + (":" + m[2] if m[2] else ""),
                formula,
            ).replace(";", ",")
        ws.cell(1, column, "=" + formula)
    xlsx = directory / "statistics.xlsx"
    book.save(xlsx)
    return [ods, xlsx]


def verify(path, test_bin_dir, directory, money=2):
    path, directory = Path(path), Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    saved = directory / (path.name + "-recalculated.ods")
    original = path.read_bytes()
    output = subprocess.check_output(
        [
            str(Path(test_bin_dir).resolve() / "calligra-native-statistics-test"),
            "--document",
            str(path),
            str(saved),
        ],
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    rows = json.loads(output)
    for column, (got, want) in enumerate(zip(rows[0][0], expected(money)), 1):
        if isinstance(want, str):
            assert got["display"] == want, (path, column, got, want)
        else:
            assert got["kind"] == 1 and math.isclose(
                got["value"], want, rel_tol=2e-12, abs_tol=1e-12
            ), (path, column, got, want)
        assert got["incomplete"] == (column in (18, 19)), (column, got)
    assert len(rows[0][0]) == 21
    with zipfile.ZipFile(saved) as archive:
        cells = next(
            ET.fromstring(archive.read("content.xml")).iter(TABLE + "table-row")
        ).findall(TABLE + "table-cell")
        for column, want in enumerate(expected(money), 1):
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
                archive.read("opaque/statistics.bin")
                == b"unchanged statistics producer data\x00\xff"
            )
    assert path.read_bytes() == original
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    print(
        "PASS",
        path.name,
        "all ten descriptive functions, calculated caches and unsupported-cache preservation",
        flush=True,
    )
    return expected(money)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--test-bin-dir", type=Path, required=True)
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory / "verified")
