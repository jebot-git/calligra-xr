#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland Stage direct resize/rotate/endpoints and source-preserving history."""

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
import stage_geometry_producer as producer

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

        def objects():
            return data()["scene"]["objects"]

        def menu_action(label):
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
            action = re.search(
                r"\((\d+), \{[^{}]*'label': <'" + caption + r"'>", layout
            )
            assert action, layout
            subprocess.run(
                [
                    *command,
                    "--method",
                    "com.canonical.dbusmenu.Event",
                    action[1],
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

        def shot(name):
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / name)

        codes.update({str(i): (11 if i == 0 else i + 1) for i in range(10)})
        codes.update({"-": 12, ".": 52, "Space": 57})

        def item():
            return next(v for v in objects() if v["name"] == "GroupedText")

        def geometry(want):
            g = item()["geometry"]
            values = [*g["origin"], g["width"], g["height"], g["rotation"]]
            return all(math.isclose(a, b, abs_tol=1e-7) for a, b in zip(values, want))

        def item(name="GroupedText"):
            for index, value in enumerate(objects()):
                if value["name"] == name:
                    return {**value, "index": index}
            raise AssertionError(name)

        def geometry(name="GroupedText"):
            return item(name)["geometry"]

        def check(expected, name="GroupedText", tolerance=1e-7):
            g = geometry(name)
            got = [*g["origin"], g["width"], g["height"], g["rotation"]]
            return all(
                abs(math.remainder(a - b, 360) if i == 4 else a - b) <= tolerance
                for i, (a, b) in enumerate(zip(got, expected))
            )

        def screen(x, y):
            state = data()["scene"]
            vx, vy, vw, vh = state["viewport"]
            scale = min((vw - 32) / state["width"], (vh - 32) / state["height"])
            w = native_window()["desktop"]
            return (
                round(w["x"] + vx + (vw - state["width"] * scale) / 2 + x * scale),
                round(w["y"] + vy + (vh - state["height"] * scale) / 2 + y * scale),
                scale,
            )

        def begin(kind):
            handle = next(
                h for h in data()["scene"]["transform_handles"] if h["kind"] == kind
            )
            sx, sy, scale = screen(handle["x"], handle["y"])
            move(sx, sy)
            time.sleep(0.15)
            desktop = native_window()["desktop"]
            delivered = data()["scene"].get("event_pointer")
            desired = [sx - desktop["x"], sy - desktop["y"]]
            diagnostic = {
                "requested_global": [sx, sy],
                "expected_client": desired,
                "delivered_client": delivered,
                "capture_extent": list(Image.open(art / "live.ppm").size),
            }
            with (art / "pointer-probes.jsonl").open("a") as stream:
                stream.write(json.dumps(diagnostic) + "\n")
            assert delivered is not None
            assert max(abs(a - b) for a, b in zip(delivered, desired)) <= 1
            send({"button": 272, "state": 1})
            time.sleep(0.1)
            return sx, sy, scale

        def release():
            send({"button": 272, "state": 0})
            time.sleep(0.15)

        def shift(pressed):
            send({"key": codes["Shift_L"], "state": int(pressed)})
            time.sleep(0.06)

        def clean_undo():
            key("z", ("Control_L",))
            wait(lambda: check(initial) and not data()["modified"])
            assert data()["scene"]["can_redo"]

        start()
        initial = [320, 40, 180, 60, 90]
        click_world(290, 130)
        wait(lambda: data()["scene"]["selected_shape"] == 2)
        assert check(initial)
        assert {h["kind"] for h in data()["scene"]["transform_handles"]} == set(
            range(9)
        )
        shot("handles.png")
        begin(0)
        release()
        assert (
            check(initial)
            and not data()["modified"]
            and not data()["scene"]["can_undo"]
        )
        sx, sy, scale = begin(0)
        move(sx + 30, sy - 24)
        (art / "debug-input-state.json").write_text(
            json.dumps(api("/api/state"), indent=2) + "\n"
        )
        shot("debug-first-drag.png")
        release()
        dx, dy = 30 / scale, -24 / scale
        resized = [320 + dx, 40 + dy, 180 - dy, 60 + dx, 90]
        wait(lambda: check(resized, tolerance=1.1 / scale) and data()["modified"])
        shot("resized.png")
        clean_undo()
        # Cancel, returning to the start, and losing canvas focus all retain
        # the original XML/history and the existing redo branch.
        for cancel in ["Escape", "return", "Tab"]:
            sx, sy, _ = begin(0)
            move(sx + 30, sy - 24)
            wait(lambda: data()["modified"])
            if cancel == "return":
                move(sx, sy)
            else:
                key(cancel)
            release()
            wait(lambda: check(initial) and not data()["modified"])
            assert data()["scene"]["can_redo"]
        sx, sy, _ = begin(4)
        shift(True)
        move(sx - 8, sy + 12)
        release()
        shift(False)
        wait(lambda: data()["modified"])
        g = geometry()
        assert math.isclose(g["width"] / g["height"], 3, abs_tol=1e-7)
        clean_undo()
        sx, sy, scale = begin(0)
        move(sx + 30, sy - 24)
        release()
        wait(lambda: check(resized, tolerance=1.1 / scale) and data()["modified"])
        g = geometry()
        center = [g["origin"][0] - g["height"] / 2, g["origin"][1] + g["width"] / 2]
        cx, cy, _ = screen(*center)
        sx, sy, _ = begin(8)
        shift(True)
        move(cx - (sy - cy), cy + (sx - cx))
        release()
        shift(False)
        final = [
            resized[0] - resized[3] / 2 + resized[2] / 2,
            resized[1] + resized[2] / 2 + resized[3] / 2,
            resized[2],
            resized[3],
            180,
        ]
        wait(lambda: check(final, tolerance=2.5 / scale))
        assert abs(math.remainder(geometry()["rotation"] - 180, 360)) < 1e-7
        assert data()["scene"]["overflow_frames"] == 0
        shot("rotated.png")
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        edits = {"GroupedText": final}
        producer.verify_saved(original, doc, edits, tolerance=2.5 / scale)
        (art / "edits.json").write_text(
            json.dumps(
                dict(
                    edits=edits,
                    tolerance=2.5 / scale,
                    input_pixel_tolerance=2.5,
                    scale=scale,
                ),
                indent=2,
            )
            + "\n"
        )
        # Image resize honors its current aspect ratio. Crossing both axes
        # produces a reflection, and Escape restores its exact saved frame.
        image = item("Image")
        bx, by, bw, bh = image["bounds"]
        click_world(bx + bw / 2, by + bh / 2)
        wait(lambda: data()["scene"]["selected_shape"] == image["index"])
        image_initial = [180, 70, 45, 35, 0]
        assert check(image_initial, "Image")
        sx, sy, _ = begin(4)
        shift(True)
        move(sx + 30, sy + 10)
        wait(lambda: data()["modified"])
        assert math.isclose(
            geometry("Image")["width"] / geometry("Image")["height"],
            image_initial[2] / image_initial[3],
            abs_tol=1e-7,
        )
        key("Escape")
        release()
        shift(False)
        wait(lambda: not data()["modified"] and check(image_initial, "Image"))
        sx, sy, scale = begin(4)
        move(
            sx - round(2 * image_initial[2] * scale),
            sy - round(2 * image_initial[3] * scale),
        )
        wait(
            lambda: (
                data()["modified"]
                and item("Image")["transform"][0] < -0.99
                and item("Image")["transform"][3] < -0.99
            )
        )
        shot("image-mirrored.png")
        release()
        mirror = {
            **edits,
            "Image": [
                180,
                70,
                abs(45 - round(90 * scale) / scale),
                abs(35 - round(70 * scale) / scale),
                180,
            ],
        }
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, mirror, tolerance=2.5 / scale)
        (art / "mirror-saved.odp").write_bytes(doc.read_bytes())
        (art / "mirror-edits.json").write_text(
            json.dumps(dict(edits=mirror, tolerance=2.5 / scale), indent=2) + "\n"
        )
        key("z", ("Control_L",))
        wait(lambda: data()["modified"] and check(image_initial, "Image"))
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        assert data()["scene"]["can_redo"]
        click_world(120, 220)
        wait(lambda: data()["scene"]["selected_shape"] == item("Line")["index"])
        assert {h["kind"] for h in data()["scene"]["transform_handles"]} == {9, 10}
        line_initial = [20, 220, 200, 0, 0]
        assert check(line_initial, "Line")
        sx, sy, _ = begin(10)
        move(sx - 32, sy + 12)
        wait(lambda: data()["modified"])
        assert all(
            abs(a - b) < 1e-7
            for a, b in zip(geometry("Line")["origin"], line_initial[:2])
        )
        assert geometry("Line")["width"] < line_initial[2]
        shot("line-endpoint.png")
        key("Escape")
        release()
        wait(lambda: not data()["modified"] and check(line_initial, "Line"))
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, edits, tolerance=2.5 / scale)
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
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        report.unlink()
        start()
        wait(lambda: check(final, tolerance=2.5 / scale) and not data()["modified"])
        assert not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    resize_and_rotation_handles=True,
                    no_op_and_cancel_preserve_redo=True,
                    focus_loss_cancel=True,
                    shift_aspect_ratio=True,
                    center_rotation_snaps_15_degrees=True,
                    image_resize_and_mirror=True,
                    line_endpoints=True,
                    one_gesture_undo=True,
                    save_reopen=True,
                    independent_input_and_source_oracle=True,
                    input_tolerance_pixels=2.5,
                    unrelated_source_and_assets_preserved=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland affine resize/rotation/image/line handles, constrained drag/cancel/history and independent ODP source/save/reopen",
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
