#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODF1.2 layer edits: scope-sensitive source, metadata and archive oracle."""

from pathlib import Path
import copy
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from stage_layers_producer import produce as layer_source, entries, O, S, T, D, V
from stage_objects_producer import normalized


def produce(directory):
    source = layer_source(directory)
    parts = entries(source)
    styles = ET.fromstring(parts["styles.xml"])
    layer = styles.find(".//" + D + "layer[@" + D + 'name="All"]')
    ET.SubElement(layer, V + "title").text = "All objects"
    ET.SubElement(layer, V + "desc").text = "Default layer notes"

    def default_elements(data, uri, names, scope):
        text = data.decode()
        prefix = re.search(r'xmlns:([A-Za-z0-9_]+)="' + re.escape(uri) + '"', text)[1]
        text = re.sub(
            r"(<|</)" + re.escape(prefix) + r":(" + names + r")(?=[ />])", r"\1\2", text
        )
        text = re.sub(
            r"<" + scope + r"(?=[ >])",
            lambda match: match[0] + ' xmlns="' + uri + '"',
            text,
        )
        return text.encode()

    # Default element namespaces still require prefixed attributes. Keep the
    # independent producer's attribute prefixes while exercising all three
    # native paths: drawing content, layer definitions and package metadata.
    parts["styles.xml"] = default_elements(
        ET.tostring(styles, encoding="utf-8", xml_declaration=True),
        D[1:-1],
        "layer-set|layer",
        "layer-set",
    )
    parts["content.xml"] = default_elements(
        parts["content.xml"], D[1:-1], "[A-Za-z0-9_-]+", "page"
    )
    parts["META-INF/manifest.xml"] = default_elements(
        parts["META-INF/manifest.xml"],
        "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0",
        "manifest|file-entry",
        "manifest",
    )

    target = source.with_name("layer-edit.odp")
    with zipfile.ZipFile(target, "w") as z:
        for name, data in parts.items():
            z.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    source.unlink()
    return target


def verify_saved(source, saved, case):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    root = ET.fromstring(before["content.xml"])
    styles = ET.fromstring(before["styles.xml"])
    page = root.find(".//" + D + "page")
    masters = styles.find(O + "master-styles")
    global_set = masters.find(D + "layer-set")
    editable = page.find(D + "rect[@" + D + 'name="Editable"]')
    if case == "repaired":
        third = root.findall(".//" + D + "page")[2]
        ET.SubElement(third.find(D + "layer-set"), D + "layer", {D + "name": "Missing"})
    elif case != "original":
        if case == "inherited":
            editable.attrib.pop(D + "layer")
        else:
            local = copy.deepcopy(global_set)
            page.insert(0, local)
            display = (
                "screen"
                if case
                in [
                    "screen-only",
                    "unlocked",
                    "moved",
                    "master-local",
                    "group-assigned",
                ]
                else "printer"
            )
            layer = ET.SubElement(
                local, D + "layer", {D + "name": "xy", D + "display": display}
            )
            ET.SubElement(layer, V + "title").text = "x"
            ET.SubElement(layer, V + "desc").text = "x\ny"
            editable.set(D + "layer", "xy")
            if case == "group-assigned":
                local.find(D + "layer[@" + D + 'name="HiddenLock"]').set(
                    D + "protected", "false"
                )
                for object in page.iter(D + "rect"):
                    if object.get(D + "name") in ["GroupVisible", "HiddenLocked"]:
                        object.set(D + "layer", "xy")
            if case in ["unlocked", "moved"]:
                local.find(D + "layer[@" + D + 'name="Locked"]').set(
                    D + "protected", "false"
                )
            if case == "moved":
                page.find(D + "rect[@" + D + 'name="Locked"]').set(
                    D + "transform", "matrix(1 0 0 1 0 0) translate(10pt 0pt)"
                )
            if case == "global-renamed":
                global_set.find(D + "layer[@" + D + 'name="Print"]').set(
                    D + "name", "xx"
                )
                master = masters.find(S + "master-page")
                master.find(D + "rect[@" + D + 'name="MasterPrint"]').set(
                    D + "layer", "xx"
                )
            if case == "master-local":
                master = masters.find(S + "master-page")
                local_master = copy.deepcopy(global_set)
                master.insert(0, local_master)
                local_master.find(D + "layer[@" + D + 'name="Screen"]').set(
                    D + "display", "none"
                )
    assert normalized(root) == normalized(ET.fromstring(after["content.xml"])), (
        "Unexpected object/reference/text/layer scope change",
        case,
    )
    assert normalized(styles) == normalized(ET.fromstring(after["styles.xml"])), (
        "Unexpected master/global/local metadata change",
        case,
    )
    for name in before:
        if name in ["content.xml", "styles.xml"]:
            continue
        if name == "META-INF/manifest.xml":
            assert normalized(ET.fromstring(before[name])) == normalized(
                ET.fromstring(after[name])
            ), case
        else:
            assert before[name] == after[name], (case, name)
    if case not in ["global-renamed", "master-local"]:
        assert before["styles.xml"] == after["styles.xml"], case


def verify(source, bin_dir, artifacts):
    artifacts = Path(artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-layer-edit-test"),
            "--document",
            str(source),
            str(artifacts / "model"),
        ],
        check=True,
    )
    for case in ["print-only", "global-renamed"]:
        verify_saved(source, artifacts / f"model-{case}.odp", case)
    (artifacts / "layer-edit.odp").write_bytes(source.read_bytes())
    print(
        "PASS independent producer layer creation/assignment/global rename, scoped references and metadata/archive oracle"
    )


def produce_missing(source, target):
    parts = entries(source)
    root = ET.fromstring(parts["content.xml"])
    third = root.findall(".//" + D + "page")[2]
    third.find(D + "rect").set(D + "layer", "Missing")
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    with zipfile.ZipFile(target, "w") as archive:
        for name, data in parts.items():
            archive.writestr(
                name,
                data,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    return Path(target)
