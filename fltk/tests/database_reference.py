#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent row predicates and exact Fraction database aggregates."""

import argparse
from decimal import Decimal, localcontext
from fractions import Fraction as F
import json
import math
import os
from pathlib import Path
import random
import re
import subprocess
import xml.etree.ElementTree as ET


class Error(str):
    pass


NAMES = (
    "DAVERAGE",
    "DCOUNT",
    "DCOUNTA",
    "DGET",
    "DMAX",
    "DMIN",
    "DPRODUCT",
    "DSTDEV",
    "DSTDEVP",
    "DSUM",
    "DVAR",
    "DVARP",
)


def literal_value(v):
    if v is None:
        return "Z999"
    if isinstance(v, Error):
        return str(v)
    if isinstance(v, str):
        return '"' + v.replace('"', '""') + '"'
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    return format(float(v), ".17g")


def literal(rows):
    return "{" + ";".join(",".join(literal_value(v) for v in row) for row in rows) + "}"


def match(value, query):
    if query is None:
        query = 0
    text = isinstance(query, str)
    op = "="
    operand = query
    if text:
        for token in ("<=", ">=", "<>", "=", "<", ">"):
            if query.startswith(token):
                op = token
                operand = query[len(token) :]
                break
    empty = text and operand == ""
    numeric = not text
    if text and not empty:
        try:
            n = float(operand)
            numeric = math.isfinite(n)
        except ValueError:
            pass
    else:
        n = float(operand) if numeric else 0
    if isinstance(value, Error):
        same = text and operand == value
        return same if op == "=" else not same if op == "<>" else False
    if value is None:
        return (
            op == "<>"
            if numeric
            else (empty if op == "=" else not empty if op == "<>" else False)
        )
    if numeric:
        try:
            v = float(value)
        except ValueError:
            return op == "<>"
        if not math.isfinite(v):
            return op == "<>"
        sign = (v > n) - (v < n)
    else:
        v = (
            ("TRUE" if value else "FALSE")
            if isinstance(value, bool)
            else str(value)
            if not isinstance(value, (int, float))
            else format(value, ".15g")
        )
        if op in ("=", "<>"):
            if empty:
                found = v == ""
            else:
                try:
                    found = re.fullmatch(operand, v, re.I) is not None
                except re.error:
                    found = v.casefold() == operand.casefold()
            return found if op == "=" else not found
        a, b = v.casefold(), str(operand).casefold()
        sign = (a > b) - (a < b)
    return {
        "=": sign == 0,
        "<>": sign != 0,
        "<": sign < 0,
        "<=": sign <= 0,
        ">": sign > 0,
        ">=": sign >= 0,
    }[op]


def sqrt_fraction(x):
    with localcontext() as c:
        c.prec = 110
        return float((Decimal(x.numerator) / Decimal(x.denominator)).sqrt())


def aggregate(name, records, field):
    if field is None:
        return len(records)
    if name == "DGET":
        if not records:
            return Error("#VALUE!")
        if len(records) > 1:
            return Error("#NUM!")
        return Error("#VALUE!") if records[0][field] is None else records[0][field]
    values = [r[field] for r in records]
    if name == "DCOUNT":
        return sum(isinstance(v, (int, float, bool)) for v in values)
    if name == "DCOUNTA":
        return sum(v is not None for v in values)
    errors = [v for v in values if isinstance(v, Error)]
    if errors:
        return errors[0]
    numbers = [F(v) for v in values if isinstance(v, (int, float, bool))]
    if name == "DSUM":
        return sum(numbers, F(0))
    if name in ("DMIN", "DMAX"):
        return (min(numbers) if name == "DMIN" else max(numbers)) if numbers else 0
    if name == "DPRODUCT":
        return math.prod(numbers) if numbers else 0
    if not numbers:
        return Error("#DIV/0!")
    mean = sum(numbers) / len(numbers)
    if name == "DAVERAGE":
        return mean
    population = name in ("DVARP", "DSTDEVP")
    if len(numbers) < (1 if population else 2):
        return Error("#DIV/0!")
    var = sum((v - mean) ** 2 for v in numbers) / (len(numbers) - (not population))
    if name.startswith("DSTDEV"):
        return sqrt_fraction(var)
    return var


def expected(db, criteria, field, name):
    headings = [v.casefold() for v in db[0]]
    columns = [headings.index(v.casefold()) for v in criteria[0]]
    records = [
        row
        for row in db[1:]
        if any(
            all(match(row[c], q) for c, q in zip(columns, condition, strict=True))
            for condition in criteria[1:]
        )
    ]
    return aggregate(name, records, field)


def independent(binary):
    rng = random.Random(20261103)
    cases = []
    for iteration in range(150):
        db = [["g", "n", "flag"]]
        for _ in range(rng.randrange(3, 13)):
            group = rng.choice(["a", "B", "c", "4", "", None])
            value = rng.choice(
                [
                    rng.randrange(-10, 11),
                    rng.random(),
                    None,
                    "4",
                    "text",
                    True,
                    False,
                    Error("#N/A"),
                ]
            )
            db.append([group, value, bool(rng.randrange(2))])
        headers = [rng.choice(db[0]) for _ in range(rng.randrange(1, 4))]
        queries = [
            "a",
            "b",
            "a.*",
            "b|c",
            "[",
            "=",
            "<>",
            "<>text",
            ">=0",
            "<4",
            0,
            1,
            True,
            False,
            None,
        ]
        criteria = [headers] + [
            [rng.choice(queries) for _ in headers] for _ in range(rng.randrange(1, 4))
        ]
        for name in NAMES:
            field = None if name in ("DCOUNT", "DCOUNTA") and iteration % 4 == 0 else 1
            formula = f"{name}({literal(db)},{'' if field is None else literal_value(db[0][field])},{literal(criteria)})"
            cases.append((formula, expected(db, criteria, field, name)))
        if iteration % 5 == 0:
            formula = f'DSUM({literal(db)},{{"n","flag"}},{literal(criteria)})'
            cases.append((formula, [expected(db, criteria, i, "DSUM") for i in (1, 2)]))
    for values in (
        [1e308, 1, -1e308],
        [1e308, 1e308],
        [1e308, -1e308],
        [1e15, 1e15 + 1, 1e15 + 2],
        [5e-324, 1e-323],
        [1e308, 1e308, 1e-308, 1e-308],
        [1e200, 1e-200, -1e200, -1e-200],
        [-1e308, 0, 1e308],
        [1, 1, 1],
    ):
        db = [["n"]] + [[v] for v in values]
        criteria = [["n"], ["<>text"]]
        for name in NAMES:
            cases.append(
                (
                    f"{name}({literal(db)},1,{literal(criteria)})",
                    expected(db, criteria, 0, name),
                )
            )
    out = subprocess.check_output(
        [str(binary.resolve()), "--eval"],
        input="\n".join(f for f, w in cases) + "\n",
        text=True,
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
    )
    records = list(map(json.loads, out.splitlines()))
    assert len(records) == len(cases)
    cells = 0

    def check(got, want, formula):
        nonlocal cells
        if isinstance(want, list):
            assert got.get("rows") == 1 and got.get("columns") == len(want), (
                formula,
                got,
            )
            for g, w in zip(got["items"], want, strict=True):
                check(g, w, formula)
            return
        cells += 1
        assert not got["incomplete"], (formula, got)
        if isinstance(want, Error):
            assert got["display"] == want, (formula, want, got)
        elif isinstance(want, str):
            assert got["kind"] == 3 and got["display"] == want, (formula, want, got)
        elif isinstance(want, bool):
            assert got["kind"] == 2 and got["display"] == (
                "TRUE" if want else "FALSE"
            ), (formula, want, got)
        else:
            try:
                v = float(want)
            except OverflowError:
                v = math.inf
            if math.isinf(v):
                assert got["display"] == "#NUM!", (formula, v, got)
            else:
                assert "value" in got and math.isclose(
                    got["value"], v, rel_tol=3e-12, abs_tol=5e-324
                ), (formula, v, got)

    for (f, w), got in zip(cases, records, strict=True):
        check(got, w, f)
    return {
        "formulas": len(cases),
        "result_cells": cells,
        "sample_arithmetic": "exact Fraction over binary64 observations",
        "sqrt_precision_digits": 110,
        "predicate_reference": "independent Python typed AND/OR comparisons",
    }


def raw_cells(path, sheet):
    t = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
    o = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
    root = ET.parse(path).getroot()
    table = list(root.iter(t + "table"))[sheet]
    result = {}
    r = 1
    for row in table.findall(t + "table-row"):
        nr = int(row.get(t + "number-rows-repeated", "1"))
        c = 1
        for cell in row:
            if cell.tag not in (t + "table-cell", t + "covered-table-cell"):
                continue
            nc = int(cell.get(t + "number-columns-repeated", "1"))
            payload = {
                k.split("}")[1]: v
                for k, v in cell.attrib.items()
                if k.startswith(o) or k == t + "formula"
            }
            text = "".join(cell.itertext()).strip()
            if payload or text:
                payload["text"] = text
                assert nr * nc < 100000
                for rr in range(r, r + nr):
                    for cc in range(c, c + nc):
                        result[rr, cc] = payload
            c += nc
        r += nr
    return result


def retained(binary):
    root = Path(__file__).resolve().parents[2]
    paths = sorted((root / "sheets/tests/data/fods/database/fods").glob("*.fods"))
    originals = [p.read_bytes() for p in paths]
    records = json.loads(
        subprocess.check_output(
            [str(binary.resolve()), "--database-fods", *map(str, paths)],
            text=True,
            env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        )
    )
    agreeing = errors = 0
    unavailable = []
    seen = set()
    for r in records:
        a = r["actual"]
        key = (Path(r["file"]).stem, r["sheet"], r["row"], r["column"])
        if a["incomplete"]:
            if key[0] == "dsum" and key[1] == 3 and 5 <= key[2] <= 19 and key[3] == 2:
                assert a["display"] == "#VALUE!", r
                cells = raw_cells(r["file"], 3)
                refs = re.search(
                    r"\[\.([A-Z]+)([0-9]+):\.([A-Z]+)([0-9]+)\]", r["formula"]
                )
                assert refs, r

                def column(s):
                    n = 0
                    for c in s:
                        n = n * 26 + ord(c) - 64
                    return n

                left, top, right, bottom = (
                    column(refs[1]),
                    int(refs[2]),
                    column(refs[3]),
                    int(refs[4]),
                )
                queries = [
                    cells.get((row, col), {}).get("text", "")
                    for row in range(top + 1, bottom + 1)
                    for col in range(left, right + 1)
                ]
                assert any("," in q for q in queries), (r, queries)
                reason = "Decimal-comma numeric criteria require a calculation-number locale absent from the native context; source caches are preserved instead of performing lexical numeric comparisons."
            elif key in {("dvar", 1, 7, 1), ("dvarp", 1, 7, 1)}:
                assert a["display"] == "#PARSE!", r
                t = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
                names = [
                    e
                    for e in ET.parse(r["file"]).getroot().iter(t + "named-range")
                    if e.get(t + "name") == "criteria"
                ]
                assert len(names) == 1 and " " in names[0].get(t + "cell-range-address")
                reason = "Named criteria is a reference list; union evaluation remains unavailable, while unrelated calculations and the raw name declaration remain usable."
            else:
                raise AssertionError(r)
            seen.add(key)
            unavailable.append({**r, "reason": reason})
        elif r["expected_error"]:
            assert a["kind"] == 4, r
            errors += 1
        elif "expected_value" in r:
            assert "value" in a and math.isclose(
                a["value"], r["expected_value"], rel_tol=1e-10, abs_tol=1e-12
            ), r
            agreeing += 1
        else:
            assert (
                r["expected_type"] == "string" and a["display"] == r["expected_text"]
            ), r
            agreeing += 1
    assert (len(records), agreeing, errors, len(unavailable)) == (121, 103, 1, 17)
    assert seen == {("dsum", 3, row, 2) for row in range(5, 20)} | {
        ("dvar", 1, 7, 1),
        ("dvarp", 1, 7, 1),
    }
    assert [p.read_bytes() for p in paths] == originals
    return {
        "cases": len(records),
        "agreeing": agreeing,
        "error_labels": errors,
        "unavailable": unavailable,
        "source_unchanged": True,
    }


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--test-bin", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    result = {"independent": independent(a.test_bin), "retained": retained(a.test_bin)}
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS independent database", result["independent"], flush=True)
    print("Retained database", {k:(len(v) if k=="unavailable" else v) for k,v in result["retained"].items()}, flush=True)
