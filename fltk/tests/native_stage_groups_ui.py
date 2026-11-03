#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland multi-object grouping, affine gestures, history and source preservation."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--bin-dir", type=Path, required=True)
parser.add_argument("--test-bin-dir", type=Path, required=True)
parser.add_argument("--artifacts", type=Path, required=True)
args = parser.parse_args()
if os.environ.get("CALLIGRA_NATIVE_UI_BUS") != "1":
    os.execvpe(
        "dbus-run-session",
        ["dbus-run-session", "--", sys.executable, *sys.argv],
        {**os.environ, "CALLIGRA_NATIVE_UI_BUS": "1"},
    )
signal.pthread_sigmask(signal.SIG_SETMASK, [])
art = args.artifacts.resolve()
art.mkdir(parents=True, exist_ok=True)
import stage_groups_producer as producer

http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
process = host = input_process = None


def wait(test, timeout=25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if host is not None and host.poll() is not None:
            raise AssertionError("Private Wayland host exited; inspect " + str(art))
        if process is not None and process.poll() is not None:
            raise AssertionError(
                f"Native Stage exited with {process.returncode}; inspect {art}"
            )
        try:
            result = test()
            if result:
                return result
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(0.06)
    raise AssertionError("Native Wayland fixture timed out; inspect " + str(art))


with tempfile.TemporaryDirectory(prefix="calligra-native-ui-") as temp:
    base = Path(temp)
    env = {k: v for k, v in os.environ.items() if not k.startswith("DOME_")}
    # The desktop can carry an opt-in /opt/fltk-vr toolkit search path.
    # Acceptance tests must use the system FLTK selected by the native build.
    env["LD_LIBRARY_PATH"] = "/usr/local/lib/domed/bitmap"
    for name in ("CONFIG", "DATA", "CACHE", "STATE", "RUNTIME"):
        p = base / name.lower()
        p.mkdir(mode=0o700)
        env[f"XDG_{name}_" + ("DIR" if name == "RUNTIME" else "HOME")] = str(p)
    for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        env.pop(name, None)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env.update(
        WLR_BACKENDS="headless",
        WLR_HEADLESS_OUTPUTS="1",
        WLR_RENDERER="pixman",
        WLR_LIBINPUT_NO_DEVICES="1",
        LIBGL_ALWAYS_SOFTWARE="1",
        DOME_RENDERER="gles",
        DOME_LOGIN_SESSION="0",
        DOME_SESSION_RESTORE="0",
        DOME_APPEARANCE_SERVICE="0",
        DOME_PREVIEW_PORT=str(port),
        DOME_RENDER_CAPTURE=str(art / "live.ppm"),
        RUST_LOG="warn",
    )
    for name in ("IME", "XR", "POLKIT", "KDECONNECT", "KWALLET", "TRAY", "XWAYLAND"):
        env["DOME_START_" + name] = "0"
    host_socket = base / "host-socket"
    wrapper = base / "start-compositor"
    wrapper.write_text(
        "#!/usr/bin/python3\nimport os\nfrom pathlib import Path\nPath("
        + repr(str(host_socket))
        + ').write_text(os.environ["WAYLAND_DISPLAY"])\nos.execv("/usr/local/bin/dome-compositor",["dome-compositor","--backend","nested"])\n'
    )
    wrapper.chmod(0o700)

    def api(path, payload=None):
        with http.open(
            urllib.request.Request(
                f"http://127.0.0.1:{port}" + path,
                data=None if payload is None else json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"},
            ),
            timeout=2,
        ) as response:
            return json.load(response)

    def scene():
        return api("/api/state")["scene"]

    def native_window():
        return next(
            (
                w
                for w in scene()["windows"]
                if process
                and w.get("client_pid") == process.pid
                and "calligra-stage" in w.get("app_id", "").lower()
            ),
            None,
        )

    try:
        with (art / "compositor.log").open("w") as log:
            host = subprocess.Popen(
                ["/usr/bin/cage", "-d", "--", str(wrapper)],
                env=env,
                stdout=log,
                stderr=log,
                start_new_session=True,
            )
        wait(lambda: host_socket.exists())
        wait(lambda: scene()["source"] == "wayland")
        match = wait(
            lambda: re.search(
                r"Dome Wayland socket: (\S+)", (art / "compositor.log").read_text()
            )
        )
        native_socket = match[1]
        input_log = (art / "input.log").open("w")
        input_process = subprocess.Popen(
            [
                str(args.test_bin_dir.resolve() / "calligra-wayland-test-input"),
                str(base / "runtime" / host_socket.read_text()),
            ],
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=input_log,
            text=True,
            bufsize=1,
        )
        assert input_process.stdout.readline().strip() == "ready", (
            "Private native input helper failed"
        )

        def send(payload):
            input_process.stdin.write(json.dumps(payload) + "\n")
            input_process.stdin.flush()
            assert input_process.stdout.readline().strip() == "ok"

        def move(x, y):
            size = wait(lambda: Image.open(art / "live.ppm").size)
            send({"move": [int(x), int(y)], "extent": list(size)})
            time.sleep(0.12)

        codes = {
            "Left": 105,
            "Right": 106,
            "Down": 108,
            "Delete": 111,
            "x": 45,
            "Up": 103,
            "Escape": 1,
            "Tab": 15,
            "Shift_L": 42,
            "Return": 28,
            "Control_L": 29,
            "q": 16,
            "s": 31,
            "a": 30,
            "z": 44,
            "y": 21,
            "8": 9,
            "g": 34,
        }

        def key(name, mods=()):
            for item in (*mods, name):
                send({"key": codes[item], "state": 1})
                time.sleep(0.025)
            for item in (name, *reversed(mods)):
                send({"key": codes[item], "state": 0})
                time.sleep(0.025)
            time.sleep(0.07)

        def click(x, y):
            w = native_window()
            api("/api/action", {"op": "focus", "id": w["id"]})
            box = w["desktop"]
            move(box["x"] + x, box["y"] + y)
            send({"button": 272, "state": 1})
            time.sleep(0.06)
            send({"button": 272, "state": 0})
            time.sleep(0.2)

        doc = producer.produce(base / "fixtures")
        producer.verify(doc, args.test_bin_dir, art / "producer")
        original = art / "original.odp"
        original.write_bytes(doc.read_bytes())
        report = art / "stage.json"
        capture = art / "stage.ppm"
        env.update(
            WAYLAND_DISPLAY=str(base / "runtime" / native_socket),
            FLTK_BACKEND="wayland",
        )

        def data():
            return json.loads(report.read_text())

        def start():
            global process
            log = (art / "stage.log").open("a")
            process = subprocess.Popen(
                [
                    str(args.bin_dir.resolve() / "calligra-stage-fltk"),
                    str(doc),
                    "--report",
                    str(report),
                    "--capture",
                    str(capture),
                ],
                env=env,
                stdout=log,
                stderr=log,
            )
            (art / "application-pid.txt").write_text(str(process.pid) + "\n")
            # Instrumented first-frame/font initialization can be slow on a
            # loaded host. An exited process still fails immediately above.
            wait(lambda: report.exists(), timeout=60)
            w = wait(native_window)
            api(
                "/api/action",
                {"op": "desktop_resize", "id": w["id"], "w": 1100, "h": 740},
            )
            api("/api/action", {"op": "desktop_move", "id": w["id"], "x": 60, "y": 50})
            api("/api/action", {"op": "focus", "id": w["id"]})
            wait(
                lambda: (
                    data().get("window_size") == [1100, 740]
                    and not data()["scene"]["render_error"]
                    and data()["scene"]["rendered_shapes"] >= 6
                )
            )

        def menu_action(label, group=False):
            address = data()["desktop_menu"]
            assert address["attached"]
            command = [
                "gdbus",
                "call",
                "--session",
                "--dest",
                address["service"],
                "--object-path",
                address["path"],
            ]
            layout = subprocess.check_output(
                [
                    *command,
                    "--method",
                    "com.canonical.dbusmenu.GetLayout",
                    "0",
                    "3",
                    "[]",
                ],
                text=True,
            )
            caption = "".join("_?" + re.escape(c) for c in label)
            actions = re.findall(
                r"\((\d+), \{[^{}]*'label': <'" + caption + r"'>", layout
            )
            assert actions and (not group or len(actions) == 2), layout
            action = actions[1 if group else 0]
            subprocess.run(
                [
                    *command,
                    "--method",
                    "com.canonical.dbusmenu.Event",
                    action,
                    "clicked",
                    "<0>",
                    "0",
                ],
                check=True,
                stdout=subprocess.DEVNULL,
            )

        def click_world(x, y):
            s = data()["scene"]
            vx, vy, vw, vh = s["viewport"]
            scale = min((vw - 32) / s["width"], (vh - 32) / s["height"])
            click(
                vx + (vw - s["width"] * scale) / 2 + x * scale,
                vy + (vh - s["height"] * scale) / 2 + y * scale,
            )

        def item(name):
            return next(
                {**v, "index": i}
                for i, v in enumerate(data()["scene"]["objects"])
                if v["name"] == name
            )

        def selection():
            values = data()["scene"]["objects"]
            return [
                (values[r["shape"]]["name"], r["ancestors"])
                for r in data()["scene"]["selected_objects"]
            ]

        def group_selected():
            return len(selection()) == 1 and selection()[0][1] == 1

        def names():
            return [v["name"] for v in data()["scene"]["objects"] if not v["master"]]

        def scale():
            s = data()["scene"]
            vx, vy, vw, vh = s["viewport"]
            return min((vw - 32) / s["width"], (vh - 32) / s["height"])

        def local(x, y):
            s = data()["scene"]
            vx, vy, vw, vh = s["viewport"]
            zoom = scale()
            return (
                vx + (vw - s["width"] * zoom) / 2 + x * zoom,
                vy + (vh - s["height"] * zoom) / 2 + y * zoom,
            )

        def modified_click(x, y, mods=()):
            for mod in mods:
                send({"key": codes[mod], "state": 1})
            click_world(x, y)
            for mod in reversed(mods):
                send({"key": codes[mod], "state": 0})

        def drag(x, y, dx, dy, cancel=None, mods=()):
            w = native_window()
            api("/api/action", {"op": "focus", "id": w["id"]})
            px, py = local(x, y)
            px += w["desktop"]["x"]
            py += w["desktop"]["y"]
            move(px, py)
            for mod in mods:
                send({"key": codes[mod], "state": 1})
            send({"button": 272, "state": 1})
            time.sleep(0.08)
            move(px + dx, py + dy)
            if cancel == "return":
                move(px, py)
            elif cancel:
                key(cancel)
            send({"button": 272, "state": 0})
            for mod in reversed(mods):
                send({"key": codes[mod], "state": 0})
            time.sleep(0.18)

        authored = {"A": (40, 60, 180, 120), "B": (120, 70, 140, 130)}

        def pose(action, tolerance=1e-6):
            a, b, c, d, e, f = action
            for name, (x, y, w, h) in authored.items():
                corners = [
                    (a * xx + c * yy + e, b * xx + d * yy + f)
                    for xx, yy in [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
                ]
                left = min(p[0] for p in corners)
                top = min(p[1] for p in corners)
                expected = [
                    left,
                    top,
                    max(p[0] for p in corners) - left,
                    max(p[1] for p in corners) - top,
                ]
                if any(
                    abs(got - want) > tolerance
                    for got, want in zip(item(name)["bounds"], expected)
                ):
                    return False
            return True

        identity = [1, 0, 0, 1, 0, 0]

        def save_case(name, action=None, linear_tolerance=1e-8):
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            target = art / (name + ".odp")
            target.write_bytes(doc.read_bytes())
            producer.verify_saved(
                original, target, True, action, 2.5 / scale(), linear_tolerance
            )
            if action is not None:
                (art / (name + "-action.json")).write_text(
                    json.dumps(
                        dict(
                            action=action,
                            tolerance=2.5 / scale(),
                            linear_tolerance=linear_tolerance,
                        ),
                        indent=2,
                    )
                    + "\n"
                )

        def restore_group():
            key("z", ("Control_L",))
            wait(lambda: group_selected() and pose(identity))
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            producer.verify_saved(original, doc, True)

        def shot(name):
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / name)

        start()
        wait(lambda: pose(identity))
        click_world(50, 80)
        wait(lambda: selection() == [("A", 0)])
        modified_click(230, 120, ("Shift_L",))
        wait(lambda: set(selection()) == {("A", 0), ("B", 0)})
        assert not data()["scene"]["editor"]["active"]
        modified_click(50, 80, ("Shift_L",))
        wait(lambda: selection() == [("B", 0)])
        modified_click(50, 80, ("Shift_L",))
        wait(lambda: set(selection()) == {("A", 0), ("B", 0)})
        menu_action("Bring to front")
        wait(lambda: names() == ["C", "Y", "N1", "N2", "X", "Spare", "Last", "B", "A"])
        assert set(selection()) == {("A", 0), ("B", 0)}
        key("z", ("Control_L",))
        wait(
            lambda: set(selection()) == {("A", 0), ("B", 0)} and not data()["modified"]
        )
        key("g", ("Control_L",))
        wait(lambda: group_selected() and data()["modified"])
        assert (
            not data()["scene"]["editor"]["active"]
            and len(data()["scene"]["transform_handles"]) == 9
        )
        save_case("native-grouped")
        shot("group-selection.png")
        for label, expected in [
            ("Bring to front", ["C", "Y", "N1", "N2", "X", "Spare", "Last", "B", "A"]),
            ("Send to back", ["B", "A", "C", "Y", "N1", "N2", "X", "Spare", "Last"]),
        ]:
            menu_action(label)
            wait(lambda: names() == expected and group_selected())
            key("z", ("Control_L",))
            wait(lambda: group_selected() and not data()["modified"])
        # Select containing group expands only the primary object, retaining
        # the other group selection when wrapping existing groups together.
        modified_click(180, 50, ("Control_L",))
        wait(lambda: selection() == [("X", 0)])
        menu_action("Select containing group")
        wait(lambda: selection() == [("X", 1)])
        modified_click(50, 80, ("Shift_L",))
        wait(lambda: set(selection()) == {("A", 0), ("X", 1)})
        menu_action("Select containing group")
        wait(lambda: set(selection()) == {("A", 1), ("X", 1)})
        key("g", ("Control_L",))
        wait(lambda: len(selection()) == 1 and selection()[0] == ("Y", 2))
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        nested = art / "native-nested.odp"
        nested.write_bytes(doc.read_bytes())
        producer.verify_nested(art / "native-grouped.odp", nested)
        key("z", ("Control_L",))
        wait(lambda: set(selection()) == {("A", 1), ("X", 1)})
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, True)
        modified_click(50, 80, ("Control_L",))
        wait(lambda: selection() == [("A", 0)])
        menu_action("Select containing group")
        wait(group_selected)
        # A single keyboard nudge moves every member and undoes in one step.
        key("Right", ("Shift_L",))
        wait(lambda: pose([1, 0, 0, 1, 10, 0]) and data()["modified"])
        key("z", ("Control_L",))
        wait(lambda: pose(identity) and not data()["modified"])
        zoom = scale()
        dx, dy = 24, -16
        moved = [1, 0, 0, 1, dx / zoom, dy / zoom]
        drag(230, 120, dx, dy)
        wait(lambda: pose(moved, 1.1 / zoom))
        save_case("native-moved", moved)
        restore_group()
        assert data()["scene"]["can_redo"]
        for cancel in ["Escape", "return", "Tab"]:
            drag(230, 120, 30, -12, cancel=cancel)
            wait(
                lambda: (
                    pose(identity)
                    and not data()["modified"]
                    and not data()["scene"]["dragging"]
                )
            )
            assert data()["scene"]["can_redo"]
        key("y", ("Control_L",))
        wait(lambda: pose(moved, 1.1 / zoom))
        key("z", ("Control_L",))
        wait(lambda: pose(identity) and not data()["modified"])
        # Resize a group by scaling its members, retaining source frame sizes.
        dx = round(44 * zoom)
        sx = 1 + dx / (220 * zoom)
        resized = [sx, 0, 0, 1, 40 * (1 - sx), 0]
        drag(260, 130, dx, 0)
        wait(lambda: pose(resized, 2.5 / zoom))
        save_case("native-resized", resized, 1.1 / (zoom * 220))
        restore_group()
        # Shift locks a quarter-turn around the authored selection center.
        radius = 70 + 24 / zoom
        px, py = local(150, 130 - radius)
        ex, ey = local(150 + radius, 130)
        rotated = [0, 1, -1, 0, 280, -20]
        drag(150, 130 - radius, round(ex - px), round(ey - py), mods=("Shift_L",))
        wait(lambda: pose(rotated, 2.5 / zoom))
        save_case("native-rotated", rotated)
        restore_group()
        dx = -round(440 * zoom)
        sx = 1 + dx / (220 * zoom)
        mirrored = [sx, 0, 0, 1, 40 * (1 - sx), 0]
        drag(260, 130, dx, 0)
        wait(lambda: pose(mirrored, 2.5 / zoom))
        save_case("native-mirrored", mirrored, 1.1 / (zoom * 220))
        shot("group-mirrored.png")
        restore_group()
        # Empty selection and Select all are view operations, with no document
        # mutation, and Undo restores the complete previous object selection.
        click_world(390, 225)
        wait(lambda: not selection())
        assert (
            not data()["scene"]["editor"]["active"]
            and data()["scene"]["selected_shape"] == -1
        )
        key("a", ("Control_L",))
        wait(lambda: len(selection()) == 5)
        key("Escape")
        wait(lambda: not selection())
        key("z", ("Control_L",))
        wait(lambda: set(selection()) == {("A", 0), ("B", 0)})
        key("y", ("Control_L",))
        wait(lambda: group_selected() and not data()["modified"])
        key("g", ("Control_L", "Shift_L"))
        wait(lambda: set(selection()) == {("A", 0), ("B", 0)})
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, False)
        (art / "native-ungrouped.odp").write_bytes(doc.read_bytes())
        key("z", ("Control_L",))
        wait(lambda: group_selected() and data()["modified"])
        key("y", ("Control_L",))
        wait(
            lambda: set(selection()) == {("A", 0), ("B", 0)} and not data()["modified"]
        )
        # Explicit group selection deletes the whole group, including nested
        # shapes, and undo restores group identity plus the exact document.
        modified_click(180, 50, ("Control_L",))
        wait(lambda: selection() == [("X", 0)])
        menu_action("Select containing group")
        wait(lambda: selection() == [("X", 1)])
        key("Delete")
        wait(lambda: all(n not in names() for n in ["X", "Y", "N1", "N2"]))
        key("z", ("Control_L",))
        wait(lambda: selection() == [("X", 1)] and not data()["modified"])
        modified_click(50, 80, ("Control_L",))
        wait(lambda: selection() == [("A", 0)])
        assert data()["scene"]["editor"]["active"]
        revision = data()["scene"]["revision"]
        menu_action("Delete object")
        wait(lambda: data()["modal_dialog"])
        assert "referenced" in data()["status_message"]
        shot("reference-guard-dialog.png")
        key("Return")
        wait(lambda: not data()["modal_dialog"])
        assert (
            data()["scene"]["revision"] == revision
            and not data()["modified"]
            and data()["scene"]["can_redo"]
        )
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, False)
        libraries = sorted(
            {
                line.split()[-1]
                for line in Path(f"/proc/{process.pid}/maps").read_text().splitlines()
                if any(v in line for v in ("libQt", "libgtk-", "libfltk"))
            }
        )
        assert not any("libQt" in p or "libgtk-" in p for p in libraries)
        assert any(
            p.startswith(("/usr/lib64/libfltk", "/lib64/libfltk")) for p in libraries
        )
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        report.unlink()
        start()
        wait(lambda: pose(identity) and not data()["modified"])
        assert not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    shift_toggle_and_ctrl_leaf=True,
                    group_and_ungroup_shortcuts=True,
                    group_selection_editor_guard=True,
                    group_move_and_nudge=True,
                    group_resize_rotate_mirror=True,
                    cancel_focus_loss_and_return_preserve_redo=True,
                    empty_and_select_all=True,
                    complete_selection_undo_redo=True,
                    whole_group_delete_undo=True,
                    multi_and_group_ordering=True,
                    grouping_existing_groups=True,
                    external_reference_delete_guard=True,
                    save_reopen=True,
                    independent_source_and_gesture_oracles=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland multi/group selection, affine gestures, grouping/ungrouping, history/deletion guards and independent ODP save/reopen",
            flush=True,
        )
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        if input_process and input_process.poll() is None:
            input_process.stdin.close()
            input_process.wait(timeout=5)
        if host and host.poll() is None:
            os.killpg(host.pid, signal.SIGTERM)
            try:
                host.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(host.pid, signal.SIGKILL)
                host.wait()
