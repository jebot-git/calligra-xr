#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland connector creation/reconnection/route controls and exact history."""

import argparse
import json
import math
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import stage_connector_authoring_producer as producer
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--bin-dir", type=Path, required=True)
parser.add_argument("--test-bin-dir", type=Path, required=True)
parser.add_argument("--artifacts", type=Path, required=True)
parser.add_argument("--minimum-window", action="store_true")
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
                {
                    "op": "desktop_resize",
                    "id": w["id"],
                    "w": 780 if args.minimum_window else 1100,
                    "h": 560 if args.minimum_window else 620,
                },
            )
            api("/api/action", {"op": "desktop_move", "id": w["id"], "x": 60, "y": 50})
            api("/api/action", {"op": "focus", "id": w["id"]})
            wait(
                lambda: (
                    data().get("window_size")
                    == ([780, 560] if args.minimum_window else [1100, 620])
                    and not data()["scene"]["render_error"]
                    and data()["scene"]["rendered_shapes"] >= 4
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
            _vx, _vy, vw, vh = s["viewport"]
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

        codes.update({str(n): n + 1 for n in range(1, 10)})
        codes.update({"0": 11, "b": 48, "c": 46, "d": 32, "e": 18, "f": 33})

        def panel_click(x, y):
            width, height = data()["window_size"]
            click((width - 660) / 2 + x, max(0, (height - 540) / 2) + y)

        choice_focus = [0]

        def open_paint():
            menu_action("Fill and stroke…")
            wait(lambda: data()["format_dialog"])
            choice_focus[0] = 0  # Panel initially focuses Fill.
            time.sleep(0.2)

        def text(x, y, value):
            panel_click(x, y)
            key("a", ("Control_L",))
            for c in value:
                key("3", ("Shift_L",)) if c == "#" else key(c)

        def picker(x, y, index, current):
            # A pointer-opened popup can close on release before the arrow
            # events arrive. FLTK Choice opens from the keyboard with Space;
            # arrows on a closed choice instead navigate the surrounding group.
            # Focus order is fixed here; image opacity is inactive in this
            # image-free fixture. Never send Return to a closed popup, where
            # it would activate the panel's default Apply button.
            target = {(540, 280): 8, (540, 330): 9}[(x, y)]
            while choice_focus[0] != target:
                if choice_focus[0] < target:
                    key("Tab")
                    choice_focus[0] += 1
                else:
                    key("Tab", ("Shift_L",))
                    choice_focus[0] -= 1
            key("Space")
            for _ in range(abs(index - current)):
                key("Down" if index > current else "Up")
            key("Return")
            assert data()["format_dialog"], "Choice selection applied the panel early"

        def apply():
            panel_click(585, 505)
            wait(lambda: not data()["format_dialog"])

        def cancel():
            key("Escape")
            wait(lambda: not data()["format_dialog"])

        def shot(name):
            time.sleep(0.25)
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / name)

        def pixel(x, y, expected, radius=1):
            # At minimum size a four-point shaft is only about three pixels wide;
            # the center sample is interior, while its neighbors cross antialiasing.
            if args.minimum_window:
                radius = 0
            # Compositor captures include the cursor; keep it off the document.
            move(12, 12)

            def captured():
                im = (
                    Image.open(art / "live.ppm")
                    .transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                    .convert("RGB")
                )
                px, py = local(x, y)
                w = native_window()["desktop"]
                px += w["x"]
                py += w["y"]
                for dx in range(-radius, radius + 1):
                    for dy in range(-radius, radius + 1):
                        got = im.getpixel((math.floor(px) + dx, math.floor(py) + dy))
                        if not all(abs(a - b) <= 2 for a, b in zip(got, expected)):
                            im.save(art / "last-pixel-failure.png")
                            (art / "last-pixel-failure.json").write_text(
                                json.dumps(
                                    {
                                        "world": [x, y],
                                        "pixel": [
                                            math.floor(px) + dx,
                                            math.floor(py) + dy,
                                        ],
                                        "got": got,
                                        "expected": expected,
                                        "window": w,
                                        "viewport": data()["scene"]["viewport"],
                                    },
                                    indent=2,
                                )
                            )
                            return None
                return True

            wait(captured)

        def save(case, expected=None):
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            target = art / ("native-" + case + ".odp")
            target.write_bytes(doc.read_bytes())
            producer.verify_saved(original, target, expected)

        def open_connector(create=False):
            menu_action("Connector…", group=create)
            wait(lambda: data()["format_dialog"])
            time.sleep(0.15)

        def pick(desired, current):
            key("Space")
            for _ in range(abs(desired - current)):
                key("Down" if desired > current else "Up")
            key("Return")
            assert data()["format_dialog"]

        def connector_click(x, y):
            w, h = data()["window_size"]
            click(max(0, (w - 740) / 2) + x, max(0, (h - 520) / 2) + y)

        def accept():
            connector_click(665, 485)
            wait(lambda: not data()["format_dialog"])

        def field(value):
            key("a", ("Control_L",))
            for c in value:
                key(c)

        def thin_ink(x, y):
            move(12, 12)

            def captured():
                im = (
                    Image.open(art / "live.ppm")
                    .transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                    .convert("RGB")
                )
                px, py = local(x, y)
                w = native_window()["desktop"]
                px, py = math.floor(px + w["x"]), math.floor(py + w["y"])
                return any(
                    max(im.getpixel((px + dx, py + dy))) < 210
                    for dx in range(-1, 2)
                    for dy in range(-1, 2)
                )

            wait(captured)

        codes.update({".": 52, "-": 12})
        start()
        assert data()["scene"]["multisample_samples"] >= 4
        click_world(50, 70)
        wait(lambda: selection() == [("EditA", 0)])
        original_names = set(names())
        revision = data()["scene"]["revision"]
        open_connector(True)
        shot("creation-controls.png")
        key("Escape")
        wait(lambda: not data()["format_dialog"])
        assert data()["scene"]["revision"] == revision and not data()["modified"]
        open_connector(True)
        pick(3, 0)  # Curve.
        key("Tab")  # Start object; inventory includes the group and its leaves.
        pick(11, 0)  # AuthorA.
        key("Tab")
        pick(2, 0)  # Right standard port.
        key("Tab")  # Attached X/Y fields are disabled.
        pick(12, 0)  # AuthorB.
        key("Tab")
        pick(4, 0)  # Left standard port.
        shot("attached-controls.png")
        accept()
        wait(lambda: len(set(names()) - original_names) == 1)
        created = next(iter(set(names()) - original_names))
        wait(lambda: selection() == [(created, 0)])
        thin_ink(185, 190)
        save("attached", "attached")
        shot("created-attached.png")
        key("z", ("Control_L",))
        wait(lambda: selection() == [("EditA", 0)])
        save("undo-created")
        key("y", ("Control_L",))
        wait(lambda: selection() == [(created, 0)])
        save("redo-created", "attached")
        revision = data()["scene"]["revision"]
        open_connector()
        accept()
        assert data()["scene"]["revision"] == revision and not data()["modified"]
        menu_action("Position and size…")
        wait(lambda: data()["format_dialog"])
        accept()
        assert data()["scene"]["revision"] == revision and not data()["modified"]
        open_connector()
        pick(1, 3)  # Straight.
        key("Tab")
        pick(0, 11)  # Detach start; retain displayed point before editing.
        key("Tab")
        field("28.22222222222222")  # 80 pt.
        key("Tab")
        field("31.75")  # 90 pt.
        key("Tab")
        pick(0, 12)  # Detach end.
        key("Tab")
        field("123.47222222222223")  # 350 pt.
        key("Tab")
        field("31.75")
        accept()
        thin_ink(215, 90)
        save("free", "free")
        shot("free-endpoints.png")
        key("z", ("Control_L",))
        save("undo-free", "attached")
        key("y", ("Control_L",))
        save("redo-free", "free")
        revision = data()["scene"]["revision"]
        open_connector()
        pick(0, 1)
        connector_click(260, 340)
        field("1.763888888888889")  # +5pt signed intermediate offset.
        shot("skew-controls.png")
        accept()
        save("skew", "skew")
        shot("authored-skew.png")
        key("z", ("Control_L",))
        save("undo-skew", "free")
        # The empty paragraph inserted with the connector is editable through
        # the existing native text area and retains its centered label style.
        box = data()["scene"]["editor"]["bounds"]
        click(box[0] + 15, box[1] + 15)
        key("a")
        key("b")
        key("c")
        wait(lambda: data()["modified"])
        save("label", "label")
        shot("connector-label.png")
        libraries = sorted(
            {
                line.split()[-1]
                for line in Path(f"/proc/{process.pid}/maps").read_text().splitlines()
                if ".so" in line and "/" in line.split()[-1]
            }
        )
        assert not any("libQt" in p or "libgtk-" in p for p in libraries)
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        report.unlink()
        start()
        wait(lambda: item(created)["text"] == "abc")
        shot("reopened-connector.png")
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                {
                    "native_wayland": True,
                    "multisampled_one_point_strokes": True,
                    "isolated_host": True,
                    "creation_and_target_glue_choices": True,
                    "free_endpoint_coordinates": True,
                    "route_and_skew_controls": True,
                    "label_editing_and_reopen": True,
                    "cancel_noop_and_selection_history": True,
                    "source_style_id_archive_oracle": True,
                    "minimum_window": args.minimum_window,
                    "runtime_libraries": libraries,
                },
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland connector create/reconnect/free/skew/label controls, source/archive/selection history and reopen",
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
