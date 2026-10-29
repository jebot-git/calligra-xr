#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent workbook date contexts: civil dates, raw serials, mixed formulas and save/reopen."""
import argparse
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from odf.opendocument import OpenDocumentSpreadsheet
from odf.table import CalculationSettings, NullDate, Table, TableCell, TableRow
from odf.text import P
from openpyxl import Workbook
from openpyxl.utils.datetime import CALENDAR_MAC_1904

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = dt.date(1899, 12, 30)
CUSTOM = dt.date(2000, 1, 1)
ISSUE = dt.date(2024, 2, 1)
SETTLEMENT = dt.date(2024, 5, 1)
COUPON = dt.date(2024, 2, 29)
FORMULAS = [
    '[.C1]-[.D1]', 'DATE(2024;5;1)', 'YEAR([.D1])',
    'YEARFRAC([.B1];"2024-05-01";0)',
    'ACCRINTM([.B1];[.D1];.1;[.A1];0)',
    'COUPNCD([.B1];DATE(2025;2;28);2;1)',
    '[.J1]-[.C1]', 'XNPV(.1;{-100;110};CHOOSE({1;2};[.B1];"2025-02-01"))',
    'AMORDEGRC([.A1];[.B1];"2024-12-31";100;1;.15;1)',
    'TBILLPRICE([.B1];"2024-05-01";.05)',
    'DAY(0)', 'MONTH(0)', 'YEAR(0)', 'DATE(1904;1;1)',
    'UNKNOWN_CONTEXT_FUNCTION()', 'IFERROR([.S1];77)',
    'COUPNCD([.B1];"2025-02-28";2;1)-DATE(2024;2;29)',
]


def expected(money=1000, epoch=CUSTOM):
    # Python's Gregorian calendar and the retained explicit financial equations.
    # AMORDEGRC's adjusted rate and recurrence deliberately use binary64.
    adjusted = .15 * 2.5
    first = math.floor(money * adjusted * (dt.date(2024, 12, 31) - ISSUE).days / 366 + .5)
    depreciation = math.floor((money - first) * adjusted + .5)
    return [money, ISSUE.isoformat(), SETTLEMENT.isoformat(),
            (SETTLEMENT - epoch).days, 0, (SETTLEMENT - epoch).days, 2024, .25,
            money * .025, COUPON.isoformat(), (COUPON - SETTLEMENT).days,
            -100 + 110 / 1.1 ** (366 / 365), depreciation,
            100 * (1 - .05 * 91 / 360), epoch.day, epoch.month, epoch.year,
            (dt.date(1904, 1, 1) - epoch).days, '#NAME?', 77, 0]


def produce(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    paths = []
    for epoch in (CUSTOM, DEFAULT, dt.date(1900, 1, 1), dt.date(1904, 1, 1),
                  dt.date(2100, 3, 1), dt.date(1, 1, 1), dt.date(9999, 12, 31)):
        doc = OpenDocumentSpreadsheet()
        settings = CalculationSettings(nullyear=1930, usewildcards=True)
        settings.addElement(NullDate(datevalue=epoch.isoformat()))
        doc.spreadsheet.addElement(settings)
        table = Table(name='DateContexts')
        doc.spreadsheet.addElement(table)
        row = TableRow()
        table.addElement(row)
        row.addElement(TableCell(valuetype='float', value=1000))
        for date in (ISSUE, SETTLEMENT):
            row.addElement(TableCell(valuetype='date', datevalue=date.isoformat()))
        row.addElement(TableCell(valuetype='float', value=(SETTLEMENT - epoch).days))
        for formula in FORMULAS:
            cell = TableCell(formula='of:=' + formula, valuetype='float', value=901)
            cell.addElement(P(text='901'))
            row.addElement(cell)
        path = directory / ('context-' + epoch.isoformat() + '.ods')
        doc.save(str(path))
        paths.append(path)
    for mac in (False, True):
        book = Workbook()
        sheet = book.active
        sheet.title = 'DateContexts'
        epoch = dt.date(1904, 1, 1) if mac else DEFAULT
        if mac:
            book.epoch = CALENDAR_MAC_1904
        sheet.cell(1, 1, 1000)
        for column, date in enumerate((ISSUE, SETTLEMENT), 2):
            sheet.cell(1, column, date)
            sheet.cell(1, column).number_format = 'yyyy-mm-dd'
        sheet.cell(1, 4, (SETTLEMENT - epoch).days)
        for column, formula in enumerate(FORMULAS, 5):
            formula = re.sub(r'\[\.([A-Z]+[0-9]+)\]', lambda m: m[1], formula).replace(';', ',')
            sheet.cell(1, column, '=' + formula)
        path = directory / ('context-1904.xlsx' if mac else 'context-1900.xlsx')
        book.save(path)
        with zipfile.ZipFile(path) as archive:
            parts = {name: archive.read(name) for name in archive.namelist()}
        ns = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
        root = ET.fromstring(parts['xl/worksheets/sheet1.xml'])
        for cell in root.iter(ns + 'c'):
            if cell.find(ns + 'f') is not None:
                value = cell.find(ns + 'v')
                if value is None:
                    value = ET.SubElement(cell, ns + 'v')
                value.text = '901'
        parts['xl/worksheets/sheet1.xml'] = ET.tostring(root, encoding='utf-8', xml_declaration=True)
        with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, value in parts.items():
                archive.writestr(name, value)
        paths.append(path)
    return paths


def verify(path, test_bin_dir, directory, money=1000):
    path = Path(path)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    before = path.read_bytes()
    if path.suffix == '.ods':
        with zipfile.ZipFile(path) as archive:
            content = ET.fromstring(archive.read('content.xml'))
            ns = '{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
            epoch = dt.date.fromisoformat(next(content.iter(ns + 'null-date')).get(ns + 'date-value'))
            original_parts = {name: archive.read(name) for name in archive.namelist()}
    else:
        epoch = dt.date(1904, 1, 1) if '1904' in path.stem else DEFAULT
        original_parts = None
    saved = directory / (path.stem + '-recalculated.ods')
    command = [str(Path(test_bin_dir).resolve() / 'calligra-native-financial-test'), '--document']
    env = {**os.environ, 'LD_LIBRARY_PATH': '/usr/local/lib/domed/bitmap'}
    rows = json.loads(subprocess.check_output([*command, str(path), str(saved)], text=True, env=env))
    want = expected(money, epoch)
    for column, (got, value) in enumerate(zip(rows[0], want), 1):
        if isinstance(value, str):
            assert got['display'] in (value, value + 'T00:00:00.000Z'), (path, column, got, value)
        else:
            assert got['kind'] == 1 and math.isclose(got['value'], value, rel_tol=3e-12, abs_tol=1e-10), (path, column, got, value)
        assert got['incomplete'] == (column in (19, 20)), (path, column, got)
    for column, date in ((2, ISSUE), (3, SETTLEMENT), (10, COUPON)):
        assert rows[0][column - 1]['value'] == (date - epoch).days
    office = '{urn:oasis:names:tc:opendocument:xmlns:office:1.0}'
    table = '{urn:oasis:names:tc:opendocument:xmlns:table:1.0}'
    with zipfile.ZipFile(saved) as archive:
        root = ET.fromstring(archive.read('content.xml'))
        cells = next(root.iter(table + 'table-row')).findall(table + 'table-cell')
        nulls = list(root.iter(table + 'null-date'))
        if epoch != DEFAULT:
            assert len(nulls) == 1 and nulls[0].get(table + 'date-value') == epoch.isoformat()
        for column, value in enumerate(want, 1):
            cell = cells[column - 1]
            if column in (19, 20):
                assert cell.get(office + 'value') == '901'
            elif column in (2, 3, 10):
                assert cell.get(office + 'value-type') == 'date'
                assert cell.get(office + 'date-value') in (value, value + 'T00:00:00.000Z')
            else:
                assert math.isclose(float(cell.get(office + 'value')), value, rel_tol=3e-12, abs_tol=1e-10)
        if original_parts:
            for name, value in original_parts.items():
                if name != 'content.xml':
                    assert archive.read(name) == value, name
    again = json.loads(subprocess.check_output([*command, str(saved)], text=True, env=env))
    assert again == rows, (path, 'saved model changed after reopen')
    assert path.read_bytes() == before
    (directory / (path.stem + '-report.json')).write_text(json.dumps(rows, indent=2) + '\n')
    print('PASS', path.name, 'mixed serial/civil dates, financial context, caches and reopened epoch', flush=True)
    return want


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--test-bin-dir', type=Path, default=ROOT / 'build-native/fltk')
    args = parser.parse_args()
    for path in produce(args.directory):
        verify(path, args.test_bin_dir, args.directory)
