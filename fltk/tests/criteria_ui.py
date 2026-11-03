#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Conditional-aggregate keyboard editing, recalculation, cache persistence and reopen."""

import argparse
import json
import os
import re
from pathlib import Path
import signal
import socket
import urllib.request
import subprocess
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET
from Xlib import X, XK, display
from Xlib.error import XError
from Xlib.ext import xtest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wayland", action="store_true")
parser.add_argument(
    "--suite", choices=("criteria", "order", "moments"), default="criteria"
)
parser.add_argument("--bin-dir", type=Path, default=ROOT / "build-native/fltk")
parser.add_argument(
    "--artifacts", type=Path, default=ROOT / "build-native/ui-artifacts"
)
parser.add_argument("--test-bin-dir", type=Path, default=ROOT / "build-native/fltk")
args = parser.parse_args()
if not os.environ.get("CALLIGRA_UI_BUS"):
    os.execvpe(
        "dbus-run-session",
        ["dbus-run-session", "--", sys.executable, *sys.argv],
        {**os.environ, "CALLIGRA_UI_BUS": "1"},
    )
signal.pthread_sigmask(signal.SIG_SETMASK, [])
art = args.artifacts.resolve()
art.mkdir(parents=True, exist_ok=True)


def wait(test, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            v = test()
            if v:
                return v
        except (OSError, ValueError, KeyError, XError):
            pass
        time.sleep(0.06)
    raise AssertionError("Timed out; inspect " + str(art))


with tempfile.TemporaryDirectory(prefix="calligra-ui-") as temp:
    base = Path(temp)
    env = {**os.environ, "FLTK_BACKEND": "x11", "NO_AT_BRIDGE": "1"}
    for name in ("CONFIG", "DATA", "CACHE", "STATE", "RUNTIME"):
        p = base / name.lower()
        p.mkdir(mode=0o700)
        env[f"XDG_{name}_" + ("DIR" if name == "RUNTIME" else "HOME")] = str(p)
    for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
        env.pop(key, None)
    rd, wr = os.pipe()
    xvfb = subprocess.Popen(
        [
            "Xvfb",
            "-displayfd",
            str(wr),
            "-screen",
            "0",
            "1400x1000x24",
            "-nolisten",
            "tcp",
        ],
        pass_fds=(wr,),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    os.close(wr)
    with os.fdopen(rd) as stream:
        env["DISPLAY"] = ":" + stream.readline().strip()
    xd = display.Display(env["DISPLAY"])
    process = None
    compositor = None
    if args.wayland:
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        env.update(
            DOME_PREVIEW_PORT=str(port),
            DOME_RENDERER="gles",
            DOME_START_IME="0",
            DOME_LOGIN_SESSION="0",
            DOME_START_XR="0",
            DOME_START_POLKIT="0",
            DOME_START_KDECONNECT="0",
            DOME_START_TRAY="0",
            RUST_LOG="warn",
            LIBGL_ALWAYS_SOFTWARE="1",
        )
        with (art / "compositor.log").open("w") as log:
            compositor = subprocess.Popen(
                ["/usr/local/bin/dome-compositor", "--backend", "nested"],
                env=env,
                stdout=log,
                stderr=log,
            )

        def scene():
            with urllib.request.urlopen(
                f"http://127.0.0.1:{port}/api/state", timeout=1
            ) as response:
                return json.load(response)["scene"]

        wait(lambda: scene()["source"] == "wayland")
        socket_name = wait(
            lambda: re.search(
                r"Dome Wayland socket: (\S+)", (art / "compositor.log").read_text()
            )
        )[1]
        env.update(WAYLAND_DISPLAY=socket_name, FLTK_BACKEND="wayland")
        env.pop("DISPLAY", None)

        def action(payload):
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/action",
                data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"},
            )
            with urllib.request.urlopen(request, timeout=2) as response:
                response.read()

        def native_window():
            return next(
                (
                    w
                    for w in scene()["windows"]
                    if "calligra-sheets" in w.get("app_id", "").lower()
                    and w.get("client_pid") == process.pid
                ),
                None,
            )

    def key(name, modifiers=()):
        mods = [xd.keysym_to_keycode(XK.string_to_keysym(m)) for m in modifiers]
        for code in mods:
            xtest.fake_input(xd, X.KeyPress, code)
            xd.sync()
            time.sleep(0.02)
        code = xd.keysym_to_keycode(XK.string_to_keysym(name))
        assert code, name
        xtest.fake_input(xd, X.KeyPress, code)
        xd.sync()
        time.sleep(0.02)
        xtest.fake_input(xd, X.KeyRelease, code)
        for code in reversed(mods):
            xtest.fake_input(xd, X.KeyRelease, code)
        xd.sync()
        time.sleep(0.05)

    def text(value):
        for c in value:
            choices = xd.keysym_to_keycodes(ord(c))
            code, index = next((code, index) for code, index in choices if index < 2)
            shift = xd.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
            if index:
                xtest.fake_input(xd, X.KeyPress, shift)
            xtest.fake_input(xd, X.KeyPress, code)
            xtest.fake_input(xd, X.KeyRelease, code)
            if index:
                xtest.fake_input(xd, X.KeyRelease, shift)
            xd.sync()
            time.sleep(0.025)

    def window(app):
        if args.wayland:
            native = native_window()
            if not native:
                return None
            action({"op": "desktop_resize", "id": native["id"], "w": 1100, "h": 680})
            action({"op": "desktop_move", "id": native["id"], "x": 60, "y": 50})
            action({"op": "focus", "id": native["id"]})
            wait(lambda: scene()["focused"] == native["id"])
            wait(
                lambda: native_window()["desktop"]["x"] == 60
                and native_window()["desktop"]["y"] == 50
            )
            time.sleep(0.5)
            return max(
                (
                    w
                    for w in xd.screen().root.query_tree().children
                    if w.get_attributes().map_state == X.IsViewable
                ),
                key=lambda w: w.get_geometry().width * w.get_geometry().height,
            )
        for w in xd.screen().root.query_tree().children:
            if "Calligra " + app.title() in str(w.get_wm_name()):
                return w

    def click(w, x, y):
        w.configure(stack_mode=X.Above)
        w.set_input_focus(X.RevertToParent, X.CurrentTime)
        point = xd.screen().root.translate_coords(w, 0, 0)
        xd.sync()
        time.sleep(0.07)
        if args.wayland:
            native = native_window()
            action({"op": "focus", "id": native["id"]})
            box = native["desktop"]
            x += int(box["x"])
            y += int(box["y"])
        xtest.fake_input(xd, X.MotionNotify, x=point.x + x, y=point.y + y)
        xd.sync()
        time.sleep(0.15)
        xtest.fake_input(xd, X.ButtonPress, 1)
        xd.sync()
        time.sleep(0.05)
        xtest.fake_input(xd, X.ButtonRelease, 1)
        xd.sync()
        time.sleep(0.2)

    try:
        import math
        from PIL import Image
        import importlib

        producer = importlib.import_module(args.suite + "_producer")
        produce, verify, expected = producer.produce, producer.verify, producer.expected

        sources = produce(base / "fixtures")
        for source in sources:
            verify(source, args.test_bin_dir, art / "producer")
        doc = sources[0]
        report = art / (args.suite + ".json")
        capture = art / (args.suite + ".ppm")
        with zipfile.ZipFile(doc) as z:
            original = {name: z.read(name) for name in z.namelist()}
        with (art / (args.suite + ".log")).open("w") as log:
            process = subprocess.Popen(
                [
                    str(args.bin_dir.resolve() / "calligra-sheets-fltk"),
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

            def data():
                return json.loads(report.read_text())

            wait(lambda: report.exists())
            host = wait(lambda: window("sheets"))
            if args.wayland:
                (art / "initial-scene.json").write_text(
                    json.dumps(scene(), indent=2) + "\n"
                )
            time.sleep(0.3)
            g = host.get_geometry()
            snap = host.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xFFFFFFFF)
            Image.frombytes("RGB", (g.width, g.height), snap.data, "raw", "BGRX").save(
                art / "initial.png"
            )

            def first():
                # Move into another real cell before returning to A1; this also
                # confirms pointer entry/focus after a Wayland client is reopened.
                if args.wayland:
                    xtest.fake_input(xd, X.MotionNotify, x=5, y=5)
                    xd.sync()
                    time.sleep(0.1)
                click(host, 230, 140)
                wait(
                    lambda: data()["cell"]["column"] >= 2 and data()["cell"]["row"] == 1
                )
                for _ in range(data()["cell"]["column"] - 1):
                    key("Left")
                wait(
                    lambda: data()["cell"]["column"] == 1 and data()["cell"]["row"] == 1
                )

            def cells(cash):
                first()
                for column, value in enumerate(expected(cash), 1):

                    def matches():
                        cell = data()["cell"]
                        if cell["column"] != column or cell["row"] != 1:
                            return False
                        if isinstance(value, str):
                            return cell["value"] == value
                        try:
                            actual = float(cell["value"])
                            painted = float(cell.get("painted_value", "nan"))
                            return math.isclose(
                                actual, value, rel_tol=3e-10, abs_tol=1e-10
                            ) and math.isclose(
                                painted, actual, rel_tol=1e-3, abs_tol=1e-15
                            )
                        except ValueError:
                            return False

                    try:
                        wait(matches)
                    except AssertionError:
                        raise AssertionError(
                            f"Column {column}: expected {value}; actual {data()}"
                        )
                    if column < len(expected(cash)):
                        key("Right")

            cells(2)
            libraries = sorted(
                {
                    line.split()[-1]
                    for line in Path(f"/proc/{process.pid}/maps")
                    .read_text()
                    .splitlines()
                    if any(token in line for token in ("libQt", "libfltk", "libgtk-"))
                }
            )
            assert not any("libQt" in library for library in libraries), libraries
            assert any(
                library.startswith(("/usr/lib64/libfltk", "/lib64/libfltk"))
                for library in libraries
            ), libraries
            first()
            click(host, 410, 74)
            key("a", ("Control_L",))
            text("8")
            key("Return")
            cells(8)
            key("z", ("Control_L",))
            cells(2)
            key("y", ("Control_L",))
            cells(8)
            first()
            time.sleep(0.15)
            g = host.get_geometry()
            snap = host.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xFFFFFFFF)
            Image.frombytes("RGB", (g.width, g.height), snap.data, "raw", "BGRX").save(
                art / (args.suite + "-edited.png")
            )
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            if hasattr(producer, "verify_caches"):
                producer.verify_caches(doc, money=8)
                (art / "native-saved.ods").write_bytes(doc.read_bytes())
            verify(doc, args.test_bin_dir, art / "saved", money=8)
            with zipfile.ZipFile(doc) as saved:
                for name, value in original.items():
                    if name != "content.xml":
                        assert saved.read(name) == value, name
            wait(lambda: capture.exists())
            previous_pid = process.pid
            key("q", ("Control_L",))
            assert process.wait(timeout=10) == 0
            if args.wayland:
                wait(
                    lambda: not any(
                        w.get("client_pid") == previous_pid for w in scene()["windows"]
                    )
                )
                xtest.fake_input(xd, X.MotionNotify, x=5, y=5)
                xd.sync()
                time.sleep(0.3)
            report.unlink()
            process = subprocess.Popen(
                [
                    str(args.bin_dir.resolve() / "calligra-sheets-fltk"),
                    str(doc),
                    "--report",
                    str(report),
                ],
                env=env,
                stdout=log,
                stderr=log,
            )
            wait(lambda: report.exists())
            host = wait(lambda: window("sheets"))
            cells(8)
            key("q", ("Control_L",))
            assert process.wait(timeout=10) == 0
            (art / "results.json").write_text(
                json.dumps(
                    {
                        "native_wayland": args.wayland,
                        "keyboard_editing": True,
                        "native_recalculation": True,
                        "undo_redo": True,
                        "saved_caches": True,
                        "save_reopen": True,
                        "opaque_data": True,
                        "runtime_libraries": libraries,
                    },
                    indent=2,
                )
                + "\n"
            )
            print(
                "PASS "
                + args.suite
                + " keyboard editing, recalculation, undo/redo, native save/cache retention and reopen",
                flush=True,
            )
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait()
        if compositor and compositor.poll() is None:
            compositor.terminate()
            compositor.wait(timeout=10)
        xd.close()
        xvfb.terminate()
        xvfb.wait(timeout=5)
