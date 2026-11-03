#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated native Wayland Stage movement, history and ODP persistence."""

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
import zipfile
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
import stage_move_producer as producer

http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
process = host = input_process = None


def wait(test, timeout=25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if host is not None and host.poll() is not None:
            raise AssertionError("Private Wayland host exited; inspect " + str(art))
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
                ["/usr/bin/cage", "--", str(wrapper)],
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
            wait(lambda: report.exists())
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
                )
            )
            wait(lambda: data()["scene"]["rendered_shapes"] >= 6)

        def rectangle():
            return next(
                v for v in data()["scene"]["objects"] if v["name"] == "Rectangle"
            )

        def check(x, y, modified=None, tolerance=1e-7):
            item = rectangle()
            a = item["transform"]
            return (
                abs(a[4] - x) < tolerance
                and abs(a[5] - y) < tolerance
                and (modified is None or data()["modified"] == modified)
            )

        def screen(x, y):
            scene = data()["scene"]
            vx, vy, vw, vh = scene["viewport"]
            scale = min((vw - 32) / scene["width"], (vh - 32) / scene["height"])
            offset = (
                vx + (vw - scene["width"] * scale) / 2,
                vy + (vh - scene["height"] * scale) / 2,
            )
            w = native_window()["desktop"]
            return (
                round(w["x"] + offset[0] + x * scale),
                round(w["y"] + offset[1] + y * scale),
                scale,
            )

        def drag_start(x, y):
            sx, sy, scale = screen(x, y)
            move(sx, sy)
            send({"button": 272, "state": 1})
            time.sleep(0.1)
            return sx, sy, scale

        def release():
            send({"button": 272, "state": 0})
            time.sleep(0.15)

        def shot(name):
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / name)

        start()
        wait(lambda: check(40, 70, False))
        shot("initial.png")
        # Moving a frame explicitly overrides master-supplied placement. Undo
        # must restore the absence of that flag and its exact source transform.
        drag_start(297, 145)
        release()
        key("Right")
        wait(lambda: data()["modified"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        import xml.etree.ElementTree as ET

        with zipfile.ZipFile(doc) as archive:
            root = ET.fromstring(archive.read("content.xml"))
        frame = root.find(
            ".//" + producer.DRAW + "frame[@" + producer.DRAW + 'name="GroupedText"]'
        )
        flag = (
            "{urn:oasis:names:tc:opendocument:xmlns:presentation:1.0}user-transformed"
        )
        assert frame.get(flag) == "true"
        (art / "frame-positioned.odp").write_bytes(doc.read_bytes())
        key("z", ("Control_L",))
        wait(lambda: data()["scene"]["can_redo"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        with zipfile.ZipFile(doc) as archive:
            root = ET.fromstring(archive.read("content.xml"))
        assert (
            root.find(
                ".//"
                + producer.DRAW
                + "frame[@"
                + producer.DRAW
                + 'name="GroupedText"]'
            ).get(flag)
            is None
        )
        # Master geometry is never editable; selecting/clicking is not a move.
        sx, sy, scale = drag_start(200, 10)
        move(sx + 35, sy + 25)
        release()
        assert check(40, 70, False)
        sx, sy, scale = drag_start(80, 95)
        release()
        assert check(40, 70, False) and not data()["scene"]["can_undo"]
        sx, sy, scale = drag_start(80, 95)
        move(sx + 60, sy + 32)
        release()
        dx, dy = 60 / scale, 32 / scale
        # wlroots absolute-pointer mapping can quantize an endpoint by one
        # output pixel. Bound that input error; subsequent nudges are exact
        # document-space operations and must match without this tolerance.
        wait(lambda: check(40 + dx, 70 + dy, True, tolerance=1.1 / scale))
        transform = rectangle()["transform"]
        dx, dy = transform[4] - 40, transform[5] - 70
        assert data()["scene"]["can_undo"]
        shot("dragged.png")
        key("z", ("Control_L",))
        wait(lambda: check(40, 70, False))
        assert data()["scene"]["can_redo"]
        # Escape while holding the button preserves the prior redo branch.
        sx, sy, _ = drag_start(80, 95)
        move(sx + 40, sy + 20)
        wait(lambda: data()["modified"])
        key("Escape")
        release()
        wait(lambda: check(40, 70, False))
        assert data()["scene"]["can_redo"]
        sx, sy, _ = drag_start(80, 95)
        move(sx + 40, sy + 20)
        move(sx, sy)
        release()
        wait(lambda: check(40, 70, False))
        assert data()["scene"]["can_redo"]
        # Leaving canvas focus cancels an in-progress body move too.
        sx, sy, _ = drag_start(80, 95)
        move(sx + 40, sy + 20)
        wait(lambda: data()["modified"])
        key("Tab")
        release()
        wait(lambda: check(40, 70, False))
        assert data()["scene"]["can_redo"]
        drag_start(80, 95)
        release()
        key("y", ("Control_L",))
        wait(lambda: check(40 + dx, 70 + dy, True))
        key("Right", ("Shift_L",))
        wait(lambda: check(50 + dx, 70 + dy, True))
        key("Down")
        wait(lambda: check(50 + dx, 71 + dy, True))
        key("z", ("Control_L",))
        wait(lambda: check(50 + dx, 70 + dy, True))
        key("y", ("Control_L",))
        wait(lambda: check(50 + dx, 71 + dy, True))
        shot("edited.png")
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, (dx + 10, dy + 1))
        (art / "native-saved.odp").write_bytes(doc.read_bytes())
        libraries = sorted(
            {
                line.split()[-1]
                for line in Path(f"/proc/{process.pid}/maps").read_text().splitlines()
                if any(v in line for v in ("libQt", "libfltk", "libgtk-"))
            }
        )
        assert not any("libQt" in p for p in libraries)
        assert any(
            p.startswith(("/usr/lib64/libfltk", "/lib64/libfltk")) for p in libraries
        )
        (art / "runtime-libraries.json").write_text(
            json.dumps(libraries, indent=2) + "\n"
        )
        (art / "movement.json").write_text(
            json.dumps(
                dict(delta=[dx + 10, dy + 1], final=rectangle(), scene=data()["scene"]),
                indent=2,
            )
            + "\n"
        )
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        report.unlink()
        start()
        wait(lambda: check(50 + dx, 71 + dy, False))
        assert not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    master_read_only=True,
                    click_no_op=True,
                    pointer_movement=True,
                    keyboard_nudges=True,
                    escape_cancel=True,
                    focus_cancel=True,
                    user_positioned_frame=True,
                    return_origin_no_op=True,
                    redo_preserved=True,
                    undo_redo=True,
                    save_reopen=True,
                    rich_source_preserved=True,
                    opaque_data=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland Stage move/nudges, master protection, no-op/cancel/history and exact source-preserving ODP save/reopen",
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
