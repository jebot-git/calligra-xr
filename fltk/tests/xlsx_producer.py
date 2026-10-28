#!/usr/bin/env python3
"""Independent XLSX producer check; run with uv run --with openpyxl python ..."""
import datetime
from pathlib import Path
import argparse
import subprocess
import tempfile
from openpyxl import Workbook
from openpyxl.worksheet.formula import ArrayFormula
ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test-bin-dir',type=Path,default=ROOT/'build-native/fltk')
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'producer.xlsx'
    book = Workbook()
    first = book.active
    first.title = 'First'
    first['A1'] = 12
    first['A2'] = 30
    first['B1'] = '=SUM(A1,A2)'
    first['C1'] = 'Český 日本語'
    first['D1'] = datetime.datetime(2024, 1, 1, 12, 30)
    first['E1'] = datetime.time(12, 30)
    first['F1'] = True
    first['H1'] = ArrayFormula(ref='H1:I2', text='=MMULT({1,2;3,4},{5,6;7,8})')
    first['K1'] = '=SUM(H1:I2)'
    first['L1'] = ArrayFormula(ref='L1', text='=SUM(A1:A2*{2;3})')
    second = book.create_sheet('Other sheet')
    second['A1'] = '=First!B1*2'
    book.save(path)
    before = path.read_bytes()
    subprocess.run([str(args.test_bin_dir / 'calligra-fltk-xlsx-test'), '--external', str(path)], check=True)
    assert path.read_bytes() == before
