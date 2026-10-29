#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate saved ODF content/styles with local, version-matched OASIS schemas.

Use uv run --no-project --with lxml python fltk/tests/odf_schema.py
--schema-dir /path/to/schemas --output results.json /path/to/documents.
Schema files are named OpenDocument-v1.2-schema.rng, etc. Obtain the official
schemas separately; this validator does not load network resources.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile
from lxml import etree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--schema-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('inputs', nargs='+', type=Path)
args = parser.parse_args()
files = set()
for path in args.inputs:
    if path.is_dir():
        files.update(p for p in path.rglob('*') if p.suffix.lower() in ('.odt', '.ods', '.odp'))
    else:
        files.add(path)
if not files:
    raise SystemExit('No OpenDocument packages were selected')
schemas, hashes = {}, {}
results = []
valid = True
for path in sorted(files):
    record = {'file': str(path.resolve()), 'parts': []}
    try:
        with zipfile.ZipFile(path) as archive:
            for name in ('content.xml', 'styles.xml'):
                if name not in archive.namelist():
                    if name == 'content.xml':
                        raise ValueError('Missing content.xml')
                    continue
                root = ET.fromstring(archive.read(name), ET.XMLParser(resolve_entities=False, no_network=True))
                version = root.get('{urn:oasis:names:tc:opendocument:xmlns:office:1.0}version')
                if version not in ('1.0', '1.1', '1.2', '1.3', '1.4'):
                    raise ValueError('Missing/unknown OpenDocument version: ' + str(version))
                if version not in schemas:
                    schema = args.schema_dir / ('OpenDocument-v' + version + '-schema.rng')
                    hashes[version] = hashlib.sha256(schema.read_bytes()).hexdigest()
                    schemas[version] = ET.RelaxNG(ET.parse(str(schema), ET.XMLParser(resolve_entities=False, no_network=True)))
                passed = schemas[version].validate(root)
                errors = [] if passed else [str(e) for e in schemas[version].error_log]
                record['parts'].append({'name': name, 'version': version, 'valid': passed, 'errors': errors})
                if not passed:
                    print('FAIL', path, name, errors[0] if errors else 'schema rejected document')
                valid = valid and passed
    except (OSError, ValueError, zipfile.BadZipFile, ET.Error) as error:
        record['error'] = str(error)
        valid = False
        print('FAIL', path, str(error))
    results.append(record)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({'valid': valid, 'documents': results, 'schema_sha256': hashes}, indent=2) + '\n')
print(('PASS' if valid else 'FAIL'), len(results), 'ODF packages with version-matched content/styles schemas')
sys.exit(0 if valid else 1)
