#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent typed database fixtures, named ranges and raw saved ODS caches."""

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
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import (
    Table,
    TableCell,
    TableColumn,
    TableRow,
    NamedExpressions,
    NamedExpression,
    DatabaseRanges,
    DatabaseRange,
)
from odf.text import P
from openpyxl import Workbook
from openpyxl.workbook.defined_name import DefinedName
from database_reference import aggregate

OFFICE = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
TABLE = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
CRIT = "[.$H$12:.$H$14]"
FORMULAS = [
    f'{name}(Records;"Score";{CRIT})'
    for name in ("DSUM", "DAVERAGE", "DCOUNT", "DCOUNTA")
] + [
    'DGET(Records;"Score";{"Name"|"beta"})',
    *[
        f'{name}(Records;"Score";{CRIT})'
        for name in ("DMAX", "DMIN", "DPRODUCT", "DSTDEV", "DSTDEVP", "DVAR", "DVARP")
    ],
    f"DCOUNT(Records;;{CRIT})",
    f"DCOUNTA(Records;0;{CRIT})",
    f"DSUM(Records;N(COLUMN([.A1]))+1;{CRIT})",
    "DSUM(Records;2;UnavailableCriteria)",
    "IFERROR([.R1];77)",
    "DSUM(Records;2;[.$M$12:.$M$13])",
    'DGET(Records;"Name";{"Name"|"beta"})',
]
UNION = "of:=[$Database.$H$12:.$H$13]~[$Database.$I$12:.$J$13]"


def records(money=2):
    return [
        ["alpha", money, "A", True],
        ["beta", 4, "B", False],
        ["ALPHA", 6, "A", True],
        ["gamma", 8, "B", False],
        ["alpha", 10, "C", True],
        ["delta", None, "A", False],
    ]


def scalar(v):
    return str(v) if isinstance(v, str) else float(v)


def expected(money=2, excel=False):
    selected = [r for r in records(money) if r[2] == "A"]
    values = (
        [
            scalar(aggregate(n, selected, 1))
            for n in ("DSUM", "DAVERAGE", "DCOUNT", "DCOUNTA")
        ]
        + [4]
        + [
            scalar(aggregate(n, selected, 1))
            for n in ("DMAX", "DMIN", "DPRODUCT", "DSTDEV", "DSTDEVP", "DVAR", "DVARP")
        ]
    )
    return [
        money,
        4,
        *values,
        3,
        3,
        money + 6,
        "#PARSE!",
        77,
        sum(r[1] for r in records(money) if r[1] is not None and r[1] >= 5),
        "beta",
    ]


def matrices(money):
    return [
        (10, 0, [[2, 3]]),
        (12, 0, [[money + 6, 2, "#VALUE!"]]),
        (14, 0, [[money + 6], [2], ["#N/A"]]),
        (18, 0, [[2, 3], [3, 0]]),
    ]


def cell(value=None, formula=None):
    if isinstance(value, bool):
        out = TableCell(valuetype="boolean", booleanvalue="true" if value else "false")
    elif isinstance(value, str):
        out = TableCell(valuetype="string")
        out.addElement(P(text=value))
    elif value is not None:
        out = TableCell(valuetype="float", value=value)
        out.addElement(P(text=str(value)))
    else:
        out = TableCell()
    if formula:
        out.setAttribute("formula", "of:=" + formula)
    return out


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = OpenDocumentSpreadsheet()
    sheet = Table(name="Database")
    sheet.addElement(TableColumn(numbercolumnsrepeated=21))
    doc.spreadsheet.addElement(sheet)
    grid = {(0, 0): cell(2), (0, 1): cell(4)}
    for c, f in enumerate(FORMULAS, 2):
        grid[0, c] = cell(901, f)
    for c, (f, value) in enumerate(
        [
            ('DSUM(Records;2;{"Score"|">0,005"})', 903),
            ("IFERROR([.A2];77)", 904),
            ("N(IFERROR(NATIVE_MISSING();5))", 905),
            ("[.Z999]", 906),
            ("ISBLANK([.Z999])", False),
            ("COUNT([.Z999])", 906),
            ("DSUM(Records;2;[.$I$12:.$J$13])", 906),
            ("DSUM(Records;2;[.$I$12:.$J$14])", 906),
            ('DCOUNT(Records;2;{"Score"|""})', 906),
        ]
    ):
        grid[1, c] = cell(value, f)
    for c, h in enumerate(["Name", "Score", "Group", "Flag"]):
        grid[2, c] = cell(h)
    for r, values in enumerate(records(), 3):
        for c, v in enumerate(values):
            grid[r, c] = cell(901, "[.A1]") if (r, c) == (3, 1) else cell(v)
    for r, c, value in [
        (11, 7, "Group"),
        (12, 7, "A"),
        (11, 8, "Score"),
        (11, 9, "Score"),
        (12, 8, ">2"),
        (12, 9, "<8"),
        (11, 12, "Calculated"),
        (12, 12, None),
        (12, 15, 5),
    ]:
        grid[r, c] = cell(value, "[.$B4]>=Threshold" if (r, c) == (12, 12) else None)
    for r, c, f, nr, nc in [
        (10, 0, f'DCOUNT(Records;{{"Score";"Flag"}};{CRIT})', 1, 2),
        (12, 0, f'DSUM(Records;{{"Score";"Flag";"missing"}};{CRIT})', 1, 3),
        (14, 0, f"DSUM(Records;{{2|4}};{CRIT})", 3, 1),
        (18, 0, f"DCOUNT(Records;{{2;4|0;1}};{CRIT})", 2, 2),
    ]:
        for rr in range(r, r + nr):
            for cc in range(c, c + nc):
                grid[rr, cc] = cell(901)
        grid[r, c].setAttribute("formula", "of:=" + f)
        grid[r, c].setAttribute("numbermatrixrowsspanned", nr)
        grid[r, c].setAttribute("numbermatrixcolumnsspanned", nc)
    for r in range(20):
        row = TableRow()
        sheet.addElement(row)
        for c in range(max((cc + 1 for rr, cc in grid if rr == r), default=1)):
            row.addElement(grid.get((r, c), TableCell()))
    names = NamedExpressions()
    names.addElement(
        NamedExpression(
            name="UnavailableCriteria",
            basecelladdress="$Database.$H$12",
            expression=UNION,
        )
    )
    names.addElement(
        NamedExpression(
            name="Threshold", basecelladdress="$Database.$P$13", expression="of:=5"
        )
    )
    doc.spreadsheet.addElement(names)
    ranges = DatabaseRanges()
    ranges.addElement(
        DatabaseRange(name="Records", targetrangeaddress="$Database.$A$3:.$D$9")
    )
    doc.spreadsheet.addElement(ranges)
    ods = directory / "database.ods"
    doc.save(str(ods))
    with zipfile.ZipFile(ods, "a") as z:
        z.writestr("opaque/database.bin", b"database metadata preserved\x00\xff")
    book = Workbook()
    ws = book.active
    ws.title = "Database"
    ws.append([2, 4])

    def excel_formula(f):
        f = re.sub(r"\[\.([^]:]+):\.([^]]+)\]", r"\1:\2", f)
        f = re.sub(r"\[\.([^]]+)\]", r"\1", f)
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
        return "=" + "".join(out)

    for c, f in enumerate(FORMULAS, 3):
        ws.cell(1, c, excel_formula(f))
    for c, f in enumerate(
        [
            'DSUM(Records;2;{"Score"|">0,005"})',
            "IFERROR([.A2];77)",
            "N(IFERROR(NATIVE_MISSING();5))",
            "[.Z999]",
            "ISBLANK([.Z999])",
            "COUNT([.Z999])",
            "DSUM(Records;2;[.$I$12:.$J$13])",
            "DSUM(Records;2;[.$I$12:.$J$14])",
            'DCOUNT(Records;2;{"Score"|""})',
        ],
        1,
    ):
        ws.cell(2, c, excel_formula(f))
    for c, h in enumerate(["Name", "Score", "Group", "Flag"], 1):
        ws.cell(3, c, h)
    for r, values in enumerate(records(), 4):
        for c, v in enumerate(values, 1):
            ws.cell(r, c, "=A1" if (r, c) == (4, 2) else v)
    for row, col, v in [
        (12, 8, "Group"),
        (13, 8, "A"),
        (12, 9, "Score"),
        (12, 10, "Score"),
        (13, 9, ">2"),
        (13, 10, "<8"),
        (12, 13, "Calculated"),
        (13, 13, "=$B4>=Threshold"),
        (13, 16, 5),
    ]:
        ws.cell(row, col, v)
    book.defined_names.add(DefinedName("Records", attr_text="'Database'!$A$3:$D$9"))
    book.defined_names.add(DefinedName("Threshold", attr_text="5"))
    book.defined_names.add(
        DefinedName(
            "UnavailableCriteria",
            attr_text="'Database'!$H$12:$H$13,'Database'!$I$12:$J$13",
        )
    )
    ws.row_dimensions[16].height = 25  # Explicit formatting-only row after a gap.
    xlsx = directory / "database.xlsx"
    book.save(xlsx)
    return [ods, xlsx]


def same(got, want):
    return (
        got == want
        if isinstance(want, str)
        else math.isclose(float(got), want, rel_tol=3e-12, abs_tol=5e-324)
    )


def verify_caches(path, money=2, source_cache=True, excel=False):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read("content.xml"))
        table = root.find(".//" + TABLE + "table")
        rows = table.findall(TABLE + "table-row")
        for c, want in enumerate(expected(money, excel)):
            v = rows[0].findall(TABLE + "table-cell")[c]
            if c in (17, 18):
                if source_cache:
                    assert v.get(OFFICE + "value") == "901", (path, c, v.attrib)
            else:
                assert same(
                    v.get(
                        OFFICE + ("string-value" if isinstance(want, str) else "value")
                    ),
                    want,
                ), (path, c, want, v.attrib)
        for c, want in enumerate([903, 904, 905, 0, "true", 0, 10, 10, 5]):
            v = rows[1].findall(TABLE + "table-cell")[c]
            if c < 3:
                if source_cache:
                    assert v.get(OFFICE + "value") == str(want), (path, c, v.attrib)
            elif c == 4:
                assert v.get(OFFICE + "boolean-value") == "true", (path, c, v.attrib)
            else:
                assert same(v.get(OFFICE + "value"), want), (path, c, v.attrib)
        if excel:
            expressions = [
                e
                for e in root.iter(TABLE + "named-expression")
                if e.get(TABLE + "name") == "UnavailableCriteria"
            ]
            assert len(expressions) == 1
            expression = expressions[0].get(TABLE + "expression")
            assert expression.startswith("of:="), expression
            assert (
                "]~[" in expression
                and expression.count("[") == 2
                and expression.count("]") == 2
            ), expression
            for definition in list(root.iter(TABLE + "named-range")) + list(
                root.iter(TABLE + "named-expression")
            ):
                assert definition.get(TABLE + "base-cell-address"), definition.attrib
        # The criterion is a formula relative to the first database data row; its
        # stored cell, the database metadata and unsupported union stay intact.
        if not excel:
            assert (
                root.find(".//" + TABLE + "database-range").get(
                    TABLE + "target-range-address"
                )
                == "$Database.$A$3:.$D$9"
            )
            union = [
                e
                for e in root.iter(TABLE + "named-expression")
                if e.get(TABLE + "name") == "UnavailableCriteria"
            ]
            assert len(union) == 1 and union[0].get(TABLE + "expression") == UNION
            threshold = [
                e
                for e in root.iter(TABLE + "named-expression")
                if e.get(TABLE + "name") == "Threshold"
            ]
            assert (
                len(threshold) == 1
                and threshold[0].get(TABLE + "expression") == "of:=5"
            )
            for r, c, values in matrices(money):
                for rr, line in enumerate(values):
                    for cc, want in enumerate(line):
                        v = rows[r + rr].findall(TABLE + "table-cell")[c + cc]
                        assert same(
                            v.get(
                                OFFICE
                                + ("string-value" if isinstance(want, str) else "value")
                            ),
                            want,
                        ), (path, r + rr, c + cc, want, v.attrib)
            assert (
                z.read("opaque/database.bin") == b"database metadata preserved\x00\xff"
            )
    print("PASS raw database caches", Path(path).name, flush=True)


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
    for c, (got, want) in enumerate(
        zip(rows[0][0], expected(money, excel), strict=True)
    ):
        assert same(got.get("value", got["display"]), want), (path, c, want, got)
        assert got["incomplete"] == (c in (17, 18)), (path, c, got)
    for c, want in enumerate(["#VALUE!", 77, 5, 0, "TRUE", 0, 10, 10, 5]):
        got = rows[0][1][c]
        assert same(got.get("value", got["display"]), want), (path, c, want, got)
        assert got["incomplete"] == (c < 3), (path, c, got)
    verify_caches(saved, money, source_cache=not excel, excel=excel)
    assert path.read_bytes() == before
    (directory / (path.name + "-report.json")).write_text(
        json.dumps(rows, indent=2) + "\n"
    )
    return expected(money, excel)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--test-bin-dir", type=Path, required=True)
    a = p.parse_args()
    for path in produce(a.directory):
        verify(path, a.test_bin_dir, a.directory / "verified")
