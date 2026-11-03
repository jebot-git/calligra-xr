#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland native layer authoring, scoped references, assignment and exact history."""

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
import stage_layer_edit_producer as producer

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
            "Home": 102,
            "End": 107,
            "Space": 57,
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
                {"op": "desktop_resize", "id": w["id"], "w": 1100, "h": 620},
            )
            api("/api/action", {"op": "desktop_move", "id": w["id"], "x": 60, "y": 50})
            api("/api/action", {"op": "focus", "id": w["id"]})
            wait(
                lambda: (
                    data().get("window_size") == [1100, 620]
                    and not data()["scene"]["render_error"]
                    and data()["scene"]["rendered_shapes"] >= 5
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

        def panel_click(x, y):
            width, height = data()["window_size"]
            click((width - 760) / 2 + x, (height - 520) / 2 + y)

        def open_layers():
            menu_action("Layers...")
            wait(lambda: data()["format_dialog"])
            time.sleep(0.25)

        def picker(x, y, index, current=0):
            # FLTK's choice popup retains its current item; Home does not
            # reset that item here. Move from the independently known value.
            panel_click(x, y)
            for _ in range(abs(index - current)):
                key("Down" if index > current else "Up")
            key("Return")

        def row(index):
            panel_click(100, 108)
            key("Home")
            for _ in range(index):
                key("Down")

        def text(x, y, letters):
            panel_click(x, y)
            key("a", ("Control_L",))
            for char in letters:
                key("Return" if char == "\n" else char)

        def apply():
            panel_click(690, 485)
            wait(lambda: not data()["format_dialog"])

        def save_case(case):
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            (art / ("native-" + case + ".odp")).write_bytes(doc.read_bytes())
            producer.verify_saved(original, doc, case)

        def shot(name):
            time.sleep(0.25)
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / name)

        def new_print_layer():
            panel_click(75, 341)
            text(530, 109, "xy")
            text(530, 219, "x")
            text(530, 258, "x\ny")
            # Renaming and assignment together must use the new layer name.
            picker(480, 369, 8)
            panel_click(675, 369)
            picker(480, 146, 2)

        start()
        wait(lambda: len(names()) == 4 and data()["scene"]["missing_glyphs"] == 0)
        click_world(50, 70)
        wait(lambda: selection() == [("Editable", 0)])
        key("Right", ("Shift_L",))
        key("z", ("Control_L",))
        wait(lambda: not data()["modified"] and data()["scene"]["can_redo"])
        baseline_revision = data()["scene"]["revision"]
        open_layers()
        new_print_layer()
        shot("draft-layer.png")
        assert (
            data()["scene"]["revision"] == baseline_revision
            and data()["scene"]["can_redo"]
            and not data()["modified"]
        )
        key("Escape")
        wait(lambda: not data()["format_dialog"])
        assert (
            data()["scene"]["revision"] == baseline_revision
            and data()["scene"]["can_redo"]
        )
        producer.verify_saved(original, doc, "original")
        open_layers()
        new_print_layer()
        apply()
        wait(lambda: "Editable" not in names() and data()["modified"])
        assert (
            not data()["scene"]["selected_objects"] and not data()["scene"]["can_redo"]
        )
        save_case("print-only")
        shot("screen-print-only.png")
        key("z", ("Control_L",))
        wait(lambda: "Editable" in names())
        assert selection() == [("Editable", 0)] and not data()["scene"]["can_undo"]
        key("y", ("Control_L",))
        wait(lambda: "Editable" not in names())
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        # Global rename affects the unshadowed master, while all page-local
        # definitions and the other master's own set retain their old names.
        open_layers()
        picker(170, 62, 0, current=1)
        row(2)
        text(530, 109, "xx")
        shot("global-rename-panel.png")
        apply()
        save_case("global-renamed")
        key("z", ("Control_L",))
        wait(lambda: data()["scene"]["can_redo"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, "print-only")
        # Edit the local layer to screen-only; this makes the selection visible
        # again without inventing a new selected object.
        open_layers()
        row(6)
        picker(480, 146, 1, current=2)
        apply()
        wait(lambda: "Editable" in names())
        assert not data()["scene"]["selected_objects"]
        save_case("screen-only")
        shot("screen-screen-only.png")
        before = data()["scene"]["revision"]
        open_layers()
        row(6)
        panel_click(195, 341)
        apply()
        assert data()["scene"]["revision"] == before
        # Layer protection can be intentionally changed, after which ordinary
        # native movement is available. Undo restores the protection itself.
        open_layers()
        row(4)
        panel_click(350, 182)
        apply()
        click_world(150, 135)
        wait(lambda: selection() == [("Locked", 0)])
        assert (
            item("Locked")["object_editable"] and not item("Locked")["layer_protected"]
        )
        save_case("unlocked")
        key("Right", ("Shift_L",))
        wait(lambda: data()["modified"])
        save_case("moved")
        key("z", ("Control_L",))
        key("z", ("Control_L",))
        wait(lambda: item("Locked")["layer_protected"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, "screen-only")
        # A master-local display change affects its two consumers, with the
        # third slide's own master left intact.
        open_layers()
        picker(170, 62, 2, current=1)
        panel_click(500, 62)
        row(1)
        picker(480, 146, 3, current=1)
        apply()
        wait(lambda: data()["scene"]["master_shapes"] == 0)
        save_case("master-local")
        shot("screen-master-local.png")
        click(55, 110)
        wait(lambda: data()["scene"]["page"] == 3)
        assert data()["scene"]["master_shapes"] == 1
        click(55, 78)
        wait(lambda: data()["scene"]["page"] == 1)
        key("z", ("Control_L",))
        wait(lambda: data()["scene"]["master_shapes"] == 1)
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, "screen-only")
        # Group assignment covers its hidden source members, while protection
        # still applies until the relevant layer is intentionally unlocked.
        click_world(250, 135)
        wait(lambda: selection() == [("GroupVisible", 0)])
        menu_action("Select containing group")
        wait(lambda: selection() == [("GroupVisible", 1)])
        open_layers()
        picker(480, 369, 8)
        panel_click(675, 369)
        row(5)
        panel_click(350, 182)
        picker(480, 369, 8)
        panel_click(675, 369)
        apply()
        wait(lambda: "HiddenLocked" in names())
        assert selection() == [("GroupVisible", 1)]
        save_case("group-assigned")
        shot("screen-group-assigned.png")
        key("z", ("Control_L",))
        wait(lambda: "HiddenLocked" not in names())
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, "screen-only")
        click_world(50, 70)
        wait(lambda: selection() == [("Editable", 0)])
        open_layers()
        panel_click(660, 62)
        assert data()["format_dialog"]  # Used local-only layer prevents inheritance.
        picker(480, 369, 1)
        panel_click(675, 369)
        panel_click(660, 62)
        apply()
        save_case("inherited")
        shot("screen-inherited.png")
        # All controls, including Apply, must fit the application's minimum
        # 780x560 client size without relying on a larger desktop.
        w = native_window()
        api("/api/action", {"op": "desktop_resize", "id": w["id"], "w": 780, "h": 560})
        wait(lambda: data()["window_size"] == [780, 560])
        before = data()["scene"]["revision"]
        open_layers()
        shot("minimum-window-panel.png")
        apply()
        assert data()["scene"]["revision"] == before and not data()["modified"]
        libraries = sorted(
            {
                line.split()[-1]
                for line in Path(f"/proc/{process.pid}/maps").read_text().splitlines()
                if ".so" in line and "/" in line.split()[-1]
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
        assert not data()["modified"] and not data()["scene"]["can_undo"]
        producer.verify_saved(original, doc, "inherited")
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        # Explicit repair defines a missing name in this page's empty local
        # set. It must not add a global definition or rewrite other scopes.
        missing_source = producer.produce_missing(original, art / "missing-source.odp")
        doc = base / "missing.odp"
        doc.write_bytes(missing_source.read_bytes())
        report.unlink()
        start()
        click(55, 110)
        wait(lambda: data()["scene"]["page"] == 3)
        assert any(
            "Missing drawing layer" in value for value in data()["scene"]["unsupported"]
        )
        open_layers()
        panel_click(135, 404)
        apply()
        wait(lambda: not data()["scene"]["unsupported"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(missing_source, doc, "repaired")
        (art / "native-repaired.odp").write_bytes(doc.read_bytes())
        shot("screen-repaired.png")
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        report.unlink()
        start()
        click(55, 110)
        wait(lambda: data()["scene"]["page"] == 3)
        assert not data()["scene"]["unsupported"] and not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    layer_panel_real_controls=True,
                    minimum_window_panel=True,
                    menu_reply_before_panel=True,
                    cancel_preserves_source_and_redo=True,
                    create_rename_and_assign=True,
                    scoped_global_rename=True,
                    screen_print_modes=True,
                    local_unlock_and_movement=True,
                    master_local_scope=True,
                    used_layer_delete_guard=True,
                    inheritance_and_reassignment=True,
                    group_assignment_with_hidden_members=True,
                    explicit_missing_layer_repair=True,
                    one_command_selection_history=True,
                    independent_source_metadata_archive_oracle=True,
                    save_reopen=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland layer authoring/scopes/rename/assignment/protection, transactional panel/history and independent ODP archive oracle",
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
