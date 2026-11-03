#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent ODP grouping/ungrouping fixture and structural source oracle."""

from pathlib import Path
import os
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from stage_order_producer import produce as stacking_source, D, V, normalized, entries


def produce(directory):
    source = stacking_source(directory)
    parts = entries(source)
    root = ET.fromstring(parts["content.xml"])
    spare = root.find(".//" + D + "rect[@" + D + 'name="Spare"]')
    for name, value in [
        ("x", "130pt"),
        ("y", "130pt"),
        ("width", "80pt"),
        ("height", "70pt"),
    ]:
        spare.set(V + name, value)
    parts["content.xml"] = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    target = source.with_name("grouping.odp")
    with zipfile.ZipFile(target, "w") as archive:
        for name, value in parts.items():
            archive.writestr(
                name,
                value,
                compress_type=zipfile.ZIP_STORED
                if name == "mimetype"
                else zipfile.ZIP_DEFLATED,
            )
    source.unlink()
    return target


def verify_saved(
    source, saved, grouped, action=None, tolerance=1e-7, linear_tolerance=1e-8
):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name != "content.xml":
            assert before[name] == after[name], name
    a, b = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    first, second = a.find(".//" + D + "page"), b.find(".//" + D + "page")
    named_before = {n.get(D + "name"): n for n in first}
    named_after = {n.get(D + "name"): n for n in second if n.get(D + "name")}
    if grouped:
        wrappers = [n for n in second if n.tag == D + "g" and not n.get(D + "name")]
        assert len(wrappers) == 1
        wrapper = wrappers[0]
        assert wrapper.attrib == {D + "z-index": "4"}
        assert [n.get(D + "name") for n in wrapper] == ["A", "B"]
        assert [n.get(D + "z-index") for n in wrapper] == ["20", "4"]
        for node in wrapper:
            named_after[node.get(D + "name")] = node
        expected = ["C", "Group", "Spare", "Opaque", None, "Last"]
        for node in second:
            assert node.get(D + "z-index") == str(expected.index(node.get(D + "name")))
    else:
        assert not any(n.tag == D + "g" and not n.get(D + "name") for n in second)
        expected = ["C", "Group", "Spare", "Opaque", "B", "A", "Last"]
        for node in second:
            assert node.get(D + "z-index") == str(expected.index(node.get(D + "name")))
    assert named_before.keys() == named_after.keys()
    if action is not None:
        for name in ["A", "B"]:
            old, new = named_before[name], named_after[name]
            matrix = re.fullmatch(
                r"matrix\(([-+\d.eE ]+)\) translate\(([-+\d.eE]+)pt ([-+\d.eE]+)pt\)",
                new.get(D + "transform", ""),
            )
            assert matrix, new.attrib
            linear = list(map(float, matrix[1].split()))
            assert len(linear) == 6 and linear[4:] == [0, 0]
            actual = linear[:4] + [float(matrix[2]), float(matrix[3])]
            for i, (got, want) in enumerate(zip(actual, action)):
                assert abs(got - want) <= (linear_tolerance if i < 4 else tolerance), (
                    name,
                    i,
                    got,
                    want,
                )
            if D + "transform" in old.attrib:
                new.set(D + "transform", old.get(D + "transform"))
            else:
                new.attrib.pop(D + "transform")

    # Reconstruct original root source order solely for comparison. Only the
    # new group wrapper, object placement and parent stacking indices may differ.
    # Existing groups, rich source, geometry, IDs and navigation stay exact.
    for node in list(second):
        second.remove(node)
    for name, old in named_before.items():
        node = named_after[name]
        if D + "z-index" in old.attrib:
            node.set(D + "z-index", old.get(D + "z-index"))
        else:
            node.attrib.pop(D + "z-index", None)
        second.append(node)
    assert normalized(a) == normalized(b), (
        "Changed unrelated XML, groups, geometry, styled text, IDs, links or navigation"
    )
    print(
        "PASS independent group structure, authored stacking, exact geometry/text/IDs/navigation and archive source",
        flush=True,
    )


def verify_nested(source, saved):
    before, after = entries(source), entries(saved)
    assert before.keys() == after.keys()
    for name in before:
        if name != "content.xml":
            assert before[name] == after[name], name
    a, b = ET.fromstring(before["content.xml"]), ET.fromstring(after["content.xml"])
    first, second = a.find(".//" + D + "page"), b.find(".//" + D + "page")
    wrappers = [n for n in second if n.tag == D + "g" and not n.get(D + "name")]
    assert len(wrappers) == 1
    wrapper = wrappers[0]
    assert wrapper.attrib == {D + "z-index": "3"}
    assert [n.tag for n in wrapper] == [D + "g", D + "g"]
    assert [n.get(D + "name") for n in wrapper] == [None, "Group"]
    assert [n.get(D + "z-index") for n in wrapper] == ["4", "1"]
    objects = {n.get(D + "name"): n for n in second if n != wrapper}
    objects.update({n.get(D + "name"): n for n in wrapper})
    expected = ["C", "Spare", "Opaque", None, "Last"]
    for node in second:
        assert node.get(D + "z-index") == str(expected.index(node.get(D + "name")))
    for node in list(second):
        second.remove(node)
    for old in first:
        node = objects[old.get(D + "name")]
        if D + "z-index" in old.attrib:
            node.set(D + "z-index", old.get(D + "z-index"))
        else:
            node.attrib.pop(D + "z-index", None)
        second.append(node)
    assert normalized(a) == normalized(b), (
        "Nested grouping changed existing groups/source/geometry/navigation"
    )
    print(
        "PASS independent grouping of existing groups and exact original group/source/archive preservation",
        flush=True,
    )


def verify(source, bin_dir, artifacts):
    art = Path(artifacts)
    art.mkdir(parents=True, exist_ok=True)
    if source.resolve() != (art / "grouping.odp").resolve():
        (art / "grouping.odp").write_bytes(source.read_bytes())
    grouped, ungrouped = art / "model-grouped.odp", art / "model-ungrouped.odp"
    run = subprocess.run(
        [
            str(Path(bin_dir).resolve() / "calligra-native-stage-groups-test"),
            "--document",
            str(source),
            str(grouped),
            str(ungrouped),
        ],
        env={**os.environ, "LD_LIBRARY_PATH": "/usr/local/lib/domed/bitmap"},
        capture_output=True,
        text=True,
    )
    (art / "model.log").write_text(run.stdout + run.stderr)
    run.check_returncode()
    verify_saved(source, grouped, True)
    verify_saved(source, ungrouped, False)
    return grouped, ungrouped


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("artifacts", type=Path)
    p.add_argument("--bin-dir", type=Path, required=True)
    a = p.parse_args()
    source = produce(a.artifacts)
    verify(source, a.bin_dir, a.artifacts)
