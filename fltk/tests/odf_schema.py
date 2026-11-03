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
import subprocess
import tempfile
import zipfile
from lxml import etree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--schema-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--jing-jar', type=Path,
                    help='Use independent Jing validation; retains libxml2 diagnostics for comparison')
parser.add_argument('--java', default='java', help='Java executable for optional Jing validation')
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
temporary = tempfile.TemporaryDirectory(prefix='calligra-odf-schema-')
for path in sorted(files):
    record = {'file': str(path.resolve()), 'parts': []}
    identities = set()
    try:
        with zipfile.ZipFile(path) as archive:
            for name in ('content.xml', 'styles.xml'):
                if name not in archive.namelist():
                    if name == 'content.xml':
                        raise ValueError('Missing content.xml')
                    continue
                root = ET.fromstring(archive.read(name), ET.XMLParser(resolve_entities=False, no_network=True))
                for node in root.iter():
                    identity = node.get('{http://www.w3.org/XML/1998/namespace}id')
                    if identity:
                        if identity in identities:
                            raise ValueError('Duplicate XML identity across archive parts: ' + identity)
                        identities.add(identity)
                version = root.get('{urn:oasis:names:tc:opendocument:xmlns:office:1.0}version')
                if version not in ('1.0', '1.1', '1.2', '1.3', '1.4'):
                    raise ValueError('Missing/unknown OpenDocument version: ' + str(version))
                if version not in schemas:
                    schema = args.schema_dir / ('OpenDocument-v' + version + '-schema.rng')
                    hashes[version] = hashlib.sha256(schema.read_bytes()).hexdigest()
                    schemas[version] = ET.RelaxNG(ET.parse(str(schema), ET.XMLParser(resolve_entities=False, no_network=True)))
                passed = schemas[version].validate(root)
                errors = [] if passed else [str(e) for e in schemas[version].error_log]
                libxml2 = {'valid': passed, 'errors': errors}
                if args.jing_jar:
                    xml = Path(temporary.name) / name
                    xml.write_bytes(archive.read(name))
                    schema = args.schema_dir / ('OpenDocument-v' + version + '-schema.rng')
                    # The ODF schemas are not RELAX NG DTD-compatible. -i skips
                    # that additional ID/IDREF compatibility layer; XML identity
                    # uniqueness is checked separately across package parts.
                    run = subprocess.run([args.java, '-jar', str(args.jing_jar.resolve()),
                                          '-i', str(schema.resolve()), str(xml)],
                                         capture_output=True, text=True, timeout=60)
                    passed = run.returncode == 0
                    errors = [] if passed else [run.stdout + run.stderr]
                    if passed and not libxml2['valid']:
                        print('NOTE', path, name, 'Jing passes; libxml2 diagnostics retained')
                part = {'name': name, 'version': version, 'valid': passed, 'errors': errors}
                if args.jing_jar:
                    part.update(validator='Jing (-i); separate XML identity checks', libxml2=libxml2)
                record['parts'].append(part)
                if not passed:
                    print('FAIL', path, name, errors[0] if errors else 'schema rejected document')
                valid = valid and passed
    except (OSError, ValueError, zipfile.BadZipFile, ET.Error, subprocess.TimeoutExpired) as error:
        record['error'] = str(error)
        valid = False
        print('FAIL', path, str(error))
    results.append(record)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({'valid': valid, 'documents': results, 'schema_sha256': hashes}, indent=2) + '\n')
print(('PASS' if valid else 'FAIL'), len(results), 'ODF packages with version-matched content/styles schemas')
sys.exit(0 if valid else 1)
