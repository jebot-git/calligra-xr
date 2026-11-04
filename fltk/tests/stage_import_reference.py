#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent URI/subpackage/table-reference private-input corpus."""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
from threading import Thread
from urllib.parse import quote, unquote
from xml.sax.saxutils import quoteattr
import xml.etree.ElementTree as ET

from native_memory_archive import OFFICE, MANIFEST, STYLES, archive
from stage_fragment_codec_reference import DRAW, TEXT, MIME, frame, unpack

XLINK = "http://www.w3.org/1999/xlink"
SVG = b'<svg xmlns="http://www.w3.org/2000/svg" width="20" height="10"><rect id="glyph" width="20" height="10" fill="red"/></svg>'


def document(body, extras=()):
    content = f'''<office:document-content xmlns:office="{OFFICE}" xmlns:draw="{DRAW}" xmlns:text="{TEXT}" xmlns:xlink="{XLINK}" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2" office:version="1.2"><office:body><office:presentation><draw:page draw:name="Independent" draw:master-page-name="Default">{body}</draw:page></office:presentation></office:body></office:document-content>'''.encode()
    entries = [("mimetype", MIME), ("content.xml", content), ("styles.xml", STYLES)]
    entries += [(name, data) for name, data, media in extras]
    manifest = f'<manifest:manifest xmlns:manifest="{MANIFEST}" manifest:version="1.2"><manifest:file-entry manifest:full-path="/" manifest:media-type="{MIME.decode()}"/>'
    for name, data in entries[1:]:
        media = next((m for n, d, m in extras if n == name), "text/xml")
        manifest += f"<manifest:file-entry manifest:full-path={quoteattr(name)} manifest:media-type={quoteattr(media)}/>"
    manifest += "</manifest:manifest>"
    return archive(entries + [("META-INF/manifest.xml", manifest.encode())])


def picture(href, extra=""):
    return f'<draw:frame draw:name="Selected" xml:id="selected" svg:x="10pt" svg:y="10pt" svg:width="100pt" svg:height="60pt"><draw:image xlink:href={quoteattr(href)}/>{extra}</draw:frame>'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-bin", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    target = art / "destination.odp"
    target.write_bytes(
        document(
            '<draw:rect svg:width="200pt" svg:height="100pt"><text:p><text:a xlink:href="#%4EativePaste1">Untouched destination reference</text:a></text:p></draw:rect>'
        )
    )
    target_hash = hashlib.sha256(target.read_bytes()).hexdigest()
    accepted, rejected = [], []

    def invoke(name, source, success=True):
        path, output = art / (name + ".cstage"), art / (name + "-output.cstage")
        path.write_bytes(frame(source, [(0,)]))
        output.unlink(missing_ok=True)
        result = subprocess.run(
            [
                str(args.test_bin.resolve()),
                "--remap",
                str(target),
                str(path),
                str(output),
            ],
            capture_output=True,
            timeout=30,
        )
        (art / (name + ".log")).write_bytes(result.stdout + result.stderr)
        assert not any(
            x in result.stderr
            for x in (b"AddressSanitizer", b"LeakSanitizer", b"runtime error:")
        ), result.stderr
        assert hashlib.sha256(target.read_bytes()).hexdigest() == target_hash
        if not success:
            assert result.returncode == 1 and not output.exists(), (
                name,
                result.returncode,
                result.stderr,
            )
            rejected.append(name)
            return
        assert result.returncode == 0, (name, result.stderr)
        metadata = json.loads(result.stdout)
        paths, names, parts = unpack(output.read_bytes())
        assert paths == metadata["selection"] and len(names) == 1
        assert names[0].startswith("NativePaste")
        accepted.append(name)
        return parts

    odd = "Pictures/Č % #?.svg"
    for suffix in ("", "#glyph"):
        name = "encoded-path" if not suffix else "asset-fragment"
        parts = invoke(
            name,
            document(
                picture(quote(odd, safe="/") + suffix), [(odd, SVG, "image/svg+xml")]
            ),
        )
        root = ET.fromstring(parts["content.xml"])
        href = root.find(f".//{{{DRAW}}}image").attrib[f"{{{XLINK}}}href"]
        expected = "Objects/NativePaste2/" + odd
        assert href == quote(expected, safe="/") + suffix
        assert parts[expected] == SVG and odd not in parts

    embedded = f'<office:document-content xmlns:office="{OFFICE}" xmlns:text="{TEXT}" xmlns:draw="{DRAW}" xmlns:xlink="{XLINK}" office:version="1.2"><office:body><office:text><text:p><draw:frame><draw:image xlink:href="../Pictures/shared.svg"/></draw:frame></text:p></office:text></office:body></office:document-content>'.encode()
    extra = '<draw:object xlink:href="Object"/>'
    parts = invoke(
        "embedded-relative",
        document(
            picture("Pictures/shared.svg", extra),
            [
                ("Pictures/shared.svg", SVG, "image/svg+xml"),
                ("Object/", b"", "application/vnd.oasis.opendocument.text"),
                ("Object/content.xml", embedded, "text/xml"),
                (
                    "Object/owned.bin",
                    b"OWNED_EMBEDDED_BYTES\0\xff",
                    "application/octet-stream",
                ),
                ("unselected/secret", b"UNSELECTED_SECRET", "application/octet-stream"),
            ],
        ),
    )
    prefix = "Objects/NativePaste2/"
    assert parts[prefix + "Object/content.xml"] == embedded
    assert parts[prefix + "Object/owned.bin"] == b"OWNED_EMBEDDED_BYTES\0\xff"
    assert parts[prefix + "Pictures/shared.svg"] == SVG
    assert b"UNSELECTED_SECRET" not in b"".join(parts.values())
    embedded_root = ET.fromstring(parts[prefix + "Object/content.xml"])
    assert (
        embedded_root.find(f".//{{{DRAW}}}image").attrib[f"{{{XLINK}}}href"]
        == "../Pictures/shared.svg"
    )

    table = '<draw:frame draw:name="Selected" svg:width="200pt" svg:height="100pt"><draw:text-box><table:table table:name="T"><table:table-row><table:table-cell><text:p><text:a xlink:href="#T">Go</text:a></text:p></table:table-cell></table:table-row></table:table></draw:text-box></draw:frame>'
    parts = invoke("table-target", document(table))
    root = ET.fromstring(parts["content.xml"])
    table_name = root.find(
        ".//{urn:oasis:names:tc:opendocument:xmlns:table:1.0}table"
    ).attrib["{urn:oasis:names:tc:opendocument:xmlns:table:1.0}name"]
    assert (
        table_name.startswith("NativePaste")
        and root.find(f".//{{{TEXT}}}a").attrib[f"{{{XLINK}}}href"] == "#" + table_name
    )
    formula = table.replace(
        "<table:table-cell>",
        '<table:table-cell table:formula="of:=1" office:value-type="float" office:value="1">',
    ).replace('<text:a xlink:href="#T">Go</text:a>', "1")
    invoke("formula-needs-structural-rewrite", document(formula), success=False)
    invoke("missing-resource", document(picture("Pictures/missing.svg")), success=False)
    invoke("traversing-resource", document(picture("../../outside.svg")), success=False)
    invoke("encoded-traversal", document(picture("%2e%2e/outside.svg")), success=False)
    invoke(
        "directory-image",
        document(
            picture("Pictures/"),
            [
                ("Pictures/", b"", "image/svg+xml"),
                ("Pictures/secret", b"SECRET", "application/octet-stream"),
            ],
        ),
        success=False,
    )

    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(SVG)

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = Thread(target=server.serve_forever)
    thread.start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/never-fetch.svg"
        parts = invoke("external-no-fetch", document(picture(url)))
        root = ET.fromstring(parts["content.xml"])
        assert root.find(f".//{{{DRAW}}}image").attrib[f"{{{XLINK}}}href"] == url
        assert not requests and len(parts) == 4
    finally:
        server.shutdown()
        thread.join()
        server.server_close()
    (art / "results.json").write_text(
        json.dumps(
            dict(
                accepted=accepted,
                rejected=rejected,
                target_file_unchanged=True,
                independent_producer_and_parser=True,
                encoded_paths_and_fragments_exact=True,
                embedded_relative_links_and_owned_parts_exact=True,
                no_external_fetch=True,
                scope="Selected isolated-remap resource/reference cases; not destination merge/default isolation",
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        f"PASS isolated import: {len(accepted)} independent valid and {len(rejected)} explicit invalid/unfinished cases"
    )


if __name__ == "__main__":
    main()
