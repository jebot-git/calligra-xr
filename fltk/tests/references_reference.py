#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ordered-area oracle with ODS save/reopen and cache checks."""

import argparse
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import (
    Table,
    TableColumn,
    TableRow,
    TableCell,
    NamedExpressions,
    NamedExpression,
)
from odf.text import P


def run(binary, directory, schema_dir=None):
    directory.mkdir(parents=True, exist_ok=False)
    rng = random.Random(431792)
    doc = OpenDocumentSpreadsheet()
    grids = []
    for sheet in range(2):
        grids.append(
            [
                [
                    rng.choice([None, "", "text", True, False, rng.randint(-100, 100)])
                    for _ in range(6)
                ]
                for _ in range(12)
            ]
        )
    formulas = []
    names = NamedExpressions()
    for case in range(100):
        areas = []
        for i in range(rng.randint(2, 6)):
            sheet = rng.randrange(2)
            top, left = rng.randrange(12), rng.randrange(6)
            bottom, right = rng.randrange(top, 12), rng.randrange(left, 6)
            areas.append((sheet, top, left, bottom, right))
        if case % 2 == 0:
            areas.append(areas[0])  # Duplicates remain part of the sequence.

        def reference(a):
            sheet, top, left, bottom, right = a
            return f"[$'Data {sheet}'.${chr(65 + left)}${top + 1}:.${chr(65 + right)}${bottom + 1}]"

        union = "~".join(map(reference, areas))
        name = f"Sequence_{case}"
        names.addElement(
            NamedExpression(
                name=name, expression="of:=" + union, basecelladdress="$'Results'.$A$1"
            )
        )
        sequence = [
            grids[s][r][c]
            for s, t, l, b, rt in areas
            for r in range(t, b + 1)
            for c in range(l, rt + 1)
        ]
        numbers = [v for v in sequence if type(v) is int]

        def aggregate(function):
            if function == "COUNT":
                return len(numbers)
            if function == "COUNTA":
                return sum(v is not None for v in sequence)
            if function == "COUNTBLANK":
                return sum(v is None or v == "" for v in sequence)
            if function == "SUM":
                return sum(numbers)
            if function == "MIN":
                return min(numbers, default=0)
            if function == "MAX":
                return max(numbers, default=0)
            if function == "AVERAGE":
                return float(Fraction(sum(numbers), len(numbers)))
            if function == "VARP":
                mean = Fraction(sum(numbers), len(numbers))
                return float(
                    sum((Fraction(v) - mean) ** 2 for v in numbers) / len(numbers)
                )

        if not numbers:
            continue
        for function in (
            "SUM",
            "COUNT",
            "COUNTA",
            "COUNTBLANK",
            "MIN",
            "MAX",
            "AVERAGE",
            "VARP",
        ):
            formulas.append((f"{function}({name})", aggregate(function)))
            formulas.append((f"{function}({union})", aggregate(function)))
        formulas.append((f"AREAS({name})", len(areas)))
        for i, a in enumerate(areas):
            s, t, l, b, rt = a
            values = [
                grids[s][r][c]
                for r in range(t, b + 1)
                for c in range(l, rt + 1)
                if type(grids[s][r][c]) is int
            ]
            formulas.append((f"SUM(INDEX({name};0;0;{i + 1}))", sum(values)))
            formulas.append((f"ROWS(INDEX({name};0;0;{i + 1}))", b - t + 1))
            formulas.append((f"COLUMNS(INDEX({name};0;0;{i + 1}))", rt - l + 1))
    table = Table(name="Results")
    table.addElement(TableColumn())
    for formula, want in formulas:
        row = TableRow()
        cell = TableCell(valuetype="float", value=987654321, formula="of:=" + formula)
        cell.addElement(P(text="987654321"))
        row.addElement(cell)
        table.addElement(row)
    doc.spreadsheet.addElement(table)
    for s, grid in enumerate(grids):
        table = Table(name=f"Data {s}")
        table.addElement(TableColumn(numbercolumnsrepeated=6))
        for data in grid:
            row = TableRow()
            for value in data:
                if value is None:
                    cell = TableCell()
                elif type(value) is bool:
                    cell = TableCell(
                        valuetype="boolean", booleanvalue=str(value).lower()
                    )
                elif type(value) is int:
                    cell = TableCell(valuetype="float", value=value)
                else:
                    cell = TableCell(valuetype="string", stringvalue=value)
                if value is not None:
                    cell.addElement(P(text=str(value)))
                row.addElement(cell)
            table.addElement(row)
        doc.spreadsheet.addElement(table)
    doc.spreadsheet.addElement(names)
    source = directory / "references.ods"
    doc.save(str(source))

    def validate(name, paths):
        if schema_dir:
            subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).with_name("odf_schema.py")),
                    "--schema-dir",
                    str(schema_dir),
                    "--output",
                    str(directory / (name + "-schema.json")),
                    *map(str, paths),
                ],
                check=True,
            )

    validate("source", [source])
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    env = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}
    for input_name, output_name in [
        ("references.ods", "saved.ods"),
        ("saved.ods", "reopened.ods"),
    ]:
        output = json.loads(
            subprocess.check_output(
                [
                    str(binary.resolve()),
                    "--document",
                    str(directory / input_name),
                    str(directory / output_name),
                ],
                text=True,
                env=env,
            )
        )
        assert len(output[0]) == len(formulas)
        for i, (formula, want) in enumerate(formulas):
            actual = output[0][i][0]
            assert (
                not actual["incomplete"]
                and "value" in actual
                and math.isclose(actual["value"], want, rel_tol=2e-12, abs_tol=1e-12)
            ), (formula, want, actual)
        with zipfile.ZipFile(directory / output_name) as z:
            root = ET.fromstring(z.read("content.xml"))
            t = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
            o = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
            result = root.find(".//" + t + "table")
            for row, (formula, want) in zip(
                result.findall(t + "table-row"), formulas, strict=True
            ):
                cell = row.find(t + "table-cell")
                assert math.isclose(
                    float(cell.get(o + "value")), want, rel_tol=2e-12, abs_tol=1e-12
                ), (formula, want, cell.attrib)
    validate("exported", [directory / "saved.ods", directory / "reopened.ods"])
    assert hashlib.sha256(source.read_bytes()).hexdigest() == before
    record = dict(
        formulas=len(formulas),
        oracle="Python ordered area enumeration, exact Fraction moments",
        save_reopen=True,
        raw_caches=True,
        source_unchanged=True,
    )
    (directory / "results.json").write_text(json.dumps(record, indent=2) + "\n")
    print("PASS", record, flush=True)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--test-bin", type=Path, required=True)
    p.add_argument("--schema-dir", type=Path)
    a = p.parse_args()
    run(a.test_bin, a.directory, a.schema_dir)
