#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent typed information predicates, retained tests and raw formula metadata."""

import argparse
import ast
from fractions import Fraction
import json
import math
import os
from pathlib import Path
import random
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
T = "{urn:oasis:names:tc:opendocument:xmlns:table:1.0}"
O = "{urn:oasis:names:tc:opendocument:xmlns:office:1.0}"
ENV = {**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"}


def evaluate(binary, formulas):
    out = subprocess.check_output(
        [str(binary.resolve()), "--eval"],
        input="\n".join(formulas) + "\n",
        text=True,
        env=ENV,
    )
    values = [json.loads(line) for line in out.splitlines()]
    assert len(values) == len(formulas)
    return values


def matches(actual, expected):
    if actual["incomplete"]:
        return False
    if type(expected) is bool:
        return actual["kind"] == 2 and actual["display"] == (
            "TRUE" if expected else "FALSE"
        )
    if isinstance(expected, (float, int)):
        return actual["kind"] == 1 and actual.get("value") == expected
    if expected.startswith("#"):
        return actual["kind"] == 4 and actual["display"] == expected
    return actual["kind"] == 3 and actual["display"] == expected


def retained_units(binary):
    source = (ROOT / "sheets/tests/TestInformationFunctions.cpp").read_text()
    source = re.sub(
        r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
        lambda m: m[0] if m[0].startswith('"') else "",
        source,
    )
    cases = []
    for m in re.finditer(
        r'CHECK_EVAL\(("(?:\\.|[^"\\])*")\s*,\s*(.*?)\);', source, re.S
    ):
        formula = ast.literal_eval(m[1])
        if not re.match(
            r"^(?:ERROR.TYPE|ERRORTYPE|FORMULA|IS\w+|TYPE|N|NA|COM.SUN.STAR.SHEET.ADDIN.ANALYSIS.GETIS(?:EVEN|ODD))\(",
            formula,
        ):
            continue
        expected = m[2].strip()
        if expected.startswith("Value::error"):
            expected = {"NA": "#N/A", "VALUE": "#VALUE!", "DIV0": "#DIV/0!"}[
                re.search(r"error(\w+)\(", expected)[1]
            ]
        else:
            assert expected.startswith("Value(") and expected.endswith(")"), expected
            expected = expected[6:-1]
            expected = (
                expected == "true"
                if expected in ("true", "false")
                else ast.literal_eval(expected)
            )
        cases.append((formula.replace(";", ","), expected))
    assert len(cases) == 83
    values = evaluate(binary, [f for f, _ in cases])
    failures = []
    unavailable = []
    conventions = []
    for (formula, expected), actual in zip(cases, values):
        if formula == "ISREF(NA())":
            assert matches(actual, False), (formula, actual)
            conventions.append(dict(formula=formula, legacy=expected, native=False))
            continue
        if not matches(actual, expected):
            failures.append(dict(formula=formula, expected=expected, actual=actual))
    assert len(unavailable) == 0 and len(conventions) == 1
    assert not failures, failures
    return dict(
        cases=len(cases),
        agreeing=82,
        unavailable_INFO=unavailable,
        ISREF_error_convention=conventions,
    )


def host_values():
    version = (ROOT / "CMakeLists.txt").read_text()
    parts = [
        re.search(r"set\(RELEASE_SERVICE_VERSION_" + part + r' "([0-9]+)"\)', version)[
            1
        ]
        for part in ["MAJOR", "MINOR", "MICRO"]
    ]
    system = os.uname()
    return {
        "directory": str(Path.cwd()) + os.sep,
        "system": system.sysname,
        "osversion": f"{system.sysname} {system.release} ({system.machine})",
        "release": ".".join(parts),
        "recalc": "Automatic",
    }


def independent(binary):
    cases = []
    rng = random.Random(702934)
    numbers = [
        0.0,
        -0.0,
        1.0,
        -3.0,
        2.9,
        -2.9,
        math.nextafter(2.0, 0.0),
        math.nextafter(2.0, 3.0),
        float(2**53 - 1),
        float(2**53),
        1e308,
        5e-324,
    ]
    numbers += [rng.uniform(-1e6, 1e6) for _ in range(90)]
    for number in numbers:
        literal = format(number, ".17g")
        integer = math.trunc(Fraction.from_float(number))
        for fn, want in [
            ("ISEVEN", integer % 2 == 0),
            ("ISODD", integer % 2 != 0),
            ("TYPE", 1),
            ("ISNUM", True),
            ("ISNONTEXT", True),
            ("ISDATE", False),
            ("ISTIME", False),
        ]:
            cases.append((f"{fn}({literal})", want))
    for s in ["", "text", "2001-02-03", "PT4H", "é漢字", "3.9", "-2.9"]:
        literal = '"' + s + '"'
        for fn, want in [
            ("TYPE", 2),
            ("ISNUM", False),
            ("ISNONTEXT", False),
            ("ISNOTTEXT", False),
            ("ISDATE", False),
            ("ISTIME", False),
        ]:
            cases.append((f"{fn}({literal})", want))
        for fn in ["ISEVEN", "ISODD"]:
            try:
                want = (math.trunc(float(s)) % 2 == 0) == (fn == "ISEVEN")
            except ValueError:
                want = "#VALUE!"
            cases.append((f"{fn}({literal})", want))
    errors = ["#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A"]
    for code, error in enumerate(errors, 1):
        for fn, want in [
            ("ERRORTYPE", code),
            ("ERROR.TYPE", code),
            ("TYPE", 16),
            ("ISNONTEXT", True),
            ("ISDATE", False),
            ("ISTIME", False),
            ("ISEVEN", error),
            ("ISODD", error),
        ]:
            cases.append((f"{fn}({error})", want))
    for boolean in ["TRUE()", "FALSE()"]:
        for fn, want in [
            ("TYPE", 4),
            ("ISNUM", False),
            ("ISNONTEXT", True),
            ("ISEVEN", "#VALUE!"),
            ("ISODD", "#VALUE!"),
        ]:
            cases.append((f"{fn}({boolean})", want))
    for i in range(40):
        year, month, day = (
            rng.randint(2000, 2099),
            rng.randint(1, 12),
            rng.randint(1, 28),
        )
        hour, minute, second = rng.randrange(24), rng.randrange(60), rng.randrange(60)
        cases += [
            (f"ISDATE(DATE({year},{month},{day}))", True),
            (f"ISTIME(DATE({year},{month},{day}))", False),
            (f"ISTIME(TIME({hour},{minute},{second}))", True),
            (f"ISDATE(TIME({hour},{minute},{second}))", False),
            (f'TYPE({{{i},TRUE();"text",#N/A}})', 64),
        ]
    cases += [
        ("FORMULA(B1)", "=SUM(A19:A31)"),
        ("FORMULA(Sheet2!B1)", "=SUM(Sheet1!A19:Sheet1!A31)"),
        ("FORMULA(B2)", "#N/A"),
        ("ISFORMULA(B1)", True),
        ("ISFORMULA(B3)", False),
        ("ISFORMULA(B1:B3)", False),
        ("TYPE(Z999)", 1),
    ]
    cases += [(f'INFO("{key}")', value) for key, value in host_values().items()]
    cases += [
        ("FILENAME()", ""),
        ('INFO("numfile")', 3),
        ('TYPE(INFO("memused"))', 1),
        ('TYPE(INFO("totmem"))', 1),
        ('TYPE(INFO("memavail"))', 1),
    ]
    values = evaluate(binary, [f for f, _ in cases])
    failures = [
        dict(formula=f, expected=e, actual=a)
        for (f, e), a in zip(cases, values)
        if not matches(a, e)
    ]
    assert not failures, failures[:10]
    return dict(
        formulas=len(cases),
        parity_oracle="Fraction over exact binary64, truncate integer then modulo",
        typed_predicates=True,
    )


def unodf(s):
    if s.startswith("$"):
        s = s[1:]
    quote = False
    dot = -1
    for i, c in enumerate(s):
        if c == "'":
            quote = not quote
        elif c == "." and not quote:
            dot = i
    return s[1:] if dot == 0 else s[:dot] + "!" + s[dot + 1 :] if dot > 0 else s


def decode(formula):
    formula = "=" + formula.split(":=", 1)[1] if ":=" in formula else formula
    if not formula.startswith("="):
        formula = "=" + formula
    pieces = re.split(r'("(?:[^"\n]|"")*")', formula)
    for i in range(0, len(pieces), 2):
        pieces[i] = re.sub(
            r"\[([^\]]+)\]",
            lambda m: ":".join(unodf(s) for s in m[1].split(":")),
            pieces[i],
        )
        pieces[i] = "".join(
            "," if c == ";" else ";" if c == "|" else c for c in pieces[i]
        )
    return "".join(pieces)


class RawMetadata:
    def __init__(self, path):
        self.tables = list(ET.parse(path).getroot().iter(T + "table"))
        self.names = [e.get(T + "name") for e in self.tables]
        self.matrices = []
        for sheet, table in enumerate(self.tables):
            row = 1
            for element in table.findall(T + "table-row"):
                col = 1
                for cell in element:
                    if cell.tag not in (T + "table-cell", T + "covered-table-cell"):
                        continue
                    if cell.get(T + "number-matrix-rows-spanned"):
                        self.matrices.append(
                            (
                                sheet,
                                row,
                                col,
                                int(cell.get(T + "number-matrix-rows-spanned")),
                                int(cell.get(T + "number-matrix-columns-spanned", "1")),
                            )
                        )
                    col += int(cell.get(T + "number-columns-repeated", "1"))
                row += int(element.get(T + "number-rows-repeated", "1"))

    def owner(self, s, r, c):
        return next(
            (
                m
                for m in self.matrices
                if m[0] == s and m[1] <= r < m[1] + m[3] and m[2] <= c < m[2] + m[4]
            ),
            None,
        )

    def cell(self, s, r, c):
        row = 1
        for element in self.tables[s].findall(T + "table-row"):
            repeat = int(element.get(T + "number-rows-repeated", "1"))
            if row <= r < row + repeat:
                column = 1
                for cell in element:
                    if cell.tag not in (T + "table-cell", T + "covered-table-cell"):
                        continue
                    n = int(cell.get(T + "number-columns-repeated", "1"))
                    if column <= c < column + n:
                        return cell
                    column += n
                return None
            row += repeat
        return None

    def formula(self, record):
        match = re.fullmatch(r"of:=FORMULA\(\[([^\]]+)\]\)", record["formula"])
        assert match, record
        first = unodf(match[1].split(":")[0])
        target = re.fullmatch(r"(?:(.+)!)?\$?([A-Z]+)\$?([0-9]+)", first)
        assert target, record
        sheet = self.names.index(target[1].strip("'")) if target[1] else record["sheet"]
        row = int(target[3])
        column = 0
        for c in target[2]:
            column = column * 26 + ord(c) - 64
        caller = self.owner(record["sheet"], record["row"], record["column"])
        if caller:
            row += record["row"] - caller[1]
            column += record["column"] - caller[2]
        owner = self.owner(sheet, row, column)
        if owner:
            sheet, row, column = owner[:3]
        cell = self.cell(sheet, row, column)
        formula = cell.get(T + "formula") if cell is not None else None
        if not formula:
            return "#N/A"
        result = decode(formula)
        return "{" + result + "}" if owner else result


def retained_fods(binary):
    paths = sorted(
        (ROOT / "sheets/tests/data/fods/information/fods").glob("*.fods")
    ) + [ROOT / "sheets/tests/data/fods/spreadsheet/fods/errortype.fods"]
    before = [p.read_bytes() for p in paths]
    records = json.loads(
        subprocess.check_output(
            [str(binary.resolve()), "--information-fods", *map(str, paths)],
            text=True,
            env=ENV,
        )
    )
    allowed = (
        {(f, 1, r, 1) for f in ["iserr", "iserror"] for r in [2, 3, 4, 8, 9, 10]}
        | {("isref", 1, r, 1) for r in [4, 7]}
        | {("errortype", 1, r, 3) for r in [10, 15]}
    )
    unavailable = []
    conventions = []
    metadata = []
    failures = []
    agreements = 0
    raw = {}
    live_context = []
    host = host_values()
    for record in records:
        a = record["actual"]
        key = (
            Path(record["file"]).stem,
            record["sheet"],
            record["row"],
            record["column"],
        )
        if a["incomplete"]:
            assert key in allowed, record
            unavailable.append(record)
            continue
        if record["formula"].startswith("of:=INFO("):
            match = re.fullmatch(r'of:=INFO\("([^"\n]+)"\)', record["formula"])
            assert match, record
            category = match[1].lower()
            if category == "numfile":
                if record["file"] not in raw:
                    raw[record["file"]] = RawMetadata(record["file"])
                want = len(raw[record["file"]].tables)
            else:
                want = host[category]
            assert matches(a, want), (record, want)
            live_context.append(record)
        elif record["formula"].startswith("of:=FORMULA("):
            if record["file"] not in raw:
                raw[record["file"]] = RawMetadata(record["file"])
            want = raw[record["file"]].formula(record)
            assert matches(a, want), (record, want)
            if a["display"] != record["expected_text"] and not record["expected_error"]:
                metadata.append(record)
            else:
                agreements += 1
        elif key in {("type", 1, 2, 1), ("type", 1, 15, 1)}:
            expected = 4 if key[2] == 2 else 2
            assert a.get("value") == expected, record
            conventions.append(record)
        else:
            good = (
                a["kind"] == 4
                if record["expected_error"]
                else matches(a, record["expected_boolean"])
                if "expected_boolean" in record
                else matches(a, record["expected_value"])
                if "expected_value" in record
                else a["display"] == record["expected_text"]
            )
            if good:
                agreements += 1
            else:
                failures.append(record)
    assert (
        len(records) == 400
        and {
            (Path(r["file"]).stem, r["sheet"], r["row"], r["column"])
            for r in unavailable
        }
        == allowed
    )
    assert not failures, failures
    assert [p.read_bytes() for p in paths] == before
    return dict(
        cases=len(records),
        agreeing_caches=agreements,
        source_verified_metadata_presentations=metadata,
        live_context_values=live_context,
        type_conventions=conventions,
        unavailable=unavailable,
        source_unchanged=True,
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--test-bin", type=Path, required=True)
    p.add_argument("--fods-bin", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    result = dict(
        independent=independent(a.test_bin),
        retained_units=retained_units(a.test_bin),
        retained_fods=retained_fods(a.fods_bin),
    )
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(
        "PASS",
        result["independent"],
        result["retained_units"],
        {
            k: len(v) if isinstance(v, list) else v
            for k, v in result["retained_fods"].items()
        },
        flush=True,
    )
