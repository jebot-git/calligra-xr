#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual native Wayland caps/joins, single-coverage alpha and isolated authoring."""

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
import stage_stroke_producer as producer

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

        codes.update({str(n): n + 1 for n in range(1, 10)})
        codes.update({"0": 11, "b": 48, "c": 46, "d": 32, "e": 18, "f": 33})

        def panel_click(x, y):
            width, height = data()["window_size"]
            click((width - 660) / 2 + x, max(0, (height - 540) / 2) + y)

        def open_paint():
            menu_action("Fill and stroke…")
            wait(lambda: data()["format_dialog"])
            time.sleep(0.2)

        def text(x, y, value):
            panel_click(x, y)
            key("a", ("Control_L",))
            for c in value:
                key("3", ("Shift_L",)) if c == "#" else key(c)

        def picker(x, y, index, current):
            panel_click(x, y)
            for _ in range(abs(index - current)):
                key("Down" if index > current else "Up")
            key("Return")

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
                                    dict(
                                        world=[x, y],
                                        pixel=[
                                            math.floor(px) + dx,
                                            math.floor(py) + dy,
                                        ],
                                        got=got,
                                        expected=expected,
                                        window=w,
                                        viewport=data()["scene"]["viewport"],
                                    ),
                                    indent=2,
                                )
                            )
                            return None
                return True

            wait(captured)

        def save(case):
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            (art / ("native-" + case + ".odp")).write_bytes(doc.read_bytes())
            producer.verify_saved(original, doc, case, native=True)

        start()
        ink = producer.INK
        pixel(25, 60, (255, 255, 255))
        pixel(150, 60, ink)
        pixel(146, 51, (255, 255, 255), radius=0)
        pixel(271, 51, ink, radius=0)
        pixel(78, 122, ink, radius=0)
        pixel(168, 122, (255, 255, 255), radius=0)
        pixel(166, 124, ink, radius=0)
        pixel(256, 124, (255, 255, 255), radius=0)
        pixel(252, 124, ink, radius=0)
        pixel(342, 124, (255, 255, 255))
        pixel(338, 132, ink)
        pixel(120, 210, ink)
        pixel(111, 201, (255, 255, 255))
        pixel(201, 201, ink, radius=0)
        pixel(320, 210, (255, 255, 255))
        shot("original-strokes.png")
        click_world(190, 60)
        wait(lambda: selection() == [("LineRound", 0)])
        key("Right", ("Shift_L",))
        key("z", ("Control_L",))
        wait(lambda: not data()["modified"] and data()["scene"]["can_redo"])
        revision = data()["scene"]["revision"]
        open_paint()
        picker(540, 280, 3, 2)
        shot("draft-stroke.png")
        cancel()
        assert data()["scene"]["revision"] == revision and data()["scene"]["can_redo"]
        open_paint()
        apply()
        assert data()["scene"]["revision"] == revision and data()["scene"]["can_redo"]
        open_paint()
        picker(540, 280, 3, 2)
        apply()
        pixel(146, 51, ink, radius=0)
        pixel(25, 60, (255, 255, 255))
        save("cap")
        shot("screen-cap.png")
        key("z", ("Control_L",))
        pixel(146, 51, (255, 255, 255), radius=0)
        key("y", ("Control_L",))
        pixel(146, 51, ink, radius=0)
        assert not data()["modified"]
        key("z", ("Control_L",))
        click_world(45, 130)
        wait(lambda: selection() == [("CornerMiter", 0)])
        open_paint()
        picker(540, 330, 2, 1)
        apply()
        pixel(78, 122, (255, 255, 255), radius=0)
        pixel(76, 124, ink, radius=0)
        save("join")
        shot("screen-join.png")
        key("z", ("Control_L",))
        pixel(78, 122, ink, radius=0)
        click(55, 90)
        wait(lambda: data()["scene"]["page"] == 2)
        assert all(
            abs(a - b) < 1e-9
            for a, b in zip(item("NearLimit")["transform"], [0.7, 0, 0, 0.5, 250, 100])
        )
        pixel(316, 157, (255, 255, 255))
        pixel(31, 61, (255, 255, 255), radius=0)
        pixel(161, 61, (255, 255, 255), radius=0)
        click_world(85, 70)
        wait(lambda: selection() == [("Inherited", 0)])
        open_paint()
        picker(540, 280, 3, 2)
        picker(540, 330, 2, 3)
        apply()
        pixel(31, 61, ink, radius=0)
        pixel(161, 61, (255, 255, 255), radius=0)
        save("inherited")
        shot("screen-inherited.png")
        key("z", ("Control_L",))
        click_world(325, 70)
        wait(lambda: selection() == [("Protected", 0)])
        revision = data()["scene"]["revision"]
        open_paint()
        shot("protected-strokes.png")
        panel_click(540, 280)
        panel_click(540, 330)
        panel_click(585, 505)
        assert data()["format_dialog"] and data()["scene"]["revision"] == revision
        cancel()
        click(55, 110)
        wait(lambda: data()["scene"]["page"] == 3)
        assert all(
            abs(a - b) < 1e-9
            for a, b in zip(
                item("Affine")["transform"], [-1, 0.15, 0.3, 1.2, 292, 145.5]
            )
        )
        assert all(
            abs(a - b) < 1e-9
            for a, b in zip(item("Zigzag")["transform"], [1.7, 0, 0, 1.5, 15, 15])
        )
        pixel(249.5, 183, ink)
        pixel(231.6, 144.6, (255, 255, 255), radius=0)
        pixel(234.2, 146.7, ink, radius=0)
        pixel(55, 75, ink)
        pixel(142.5, 135.5, ink, radius=0)
        shot("crossing-alpha.png")
        click(55, 90)
        wait(lambda: data()["scene"]["page"] == 2)
        click_world(75, 150)
        wait(lambda: selection() == [("GroupedLine", 0)])
        menu_action("Select containing group")
        wait(lambda: selection() == [("GroupedLine", 1)])
        open_paint()
        picker(540, 280, 2, 1)
        picker(540, 330, 3, 1)
        apply()
        pixel(35, 150, ink)
        pixel(206, 144, (255, 255, 255), radius=0)
        pixel(195, 155, ink)
        save("group")
        shot("screen-group-strokes.png")
        w = native_window()
        api("/api/action", {"op": "desktop_resize", "id": w["id"], "w": 780, "h": 560})
        wait(lambda: data()["window_size"] == [780, 560])
        before = data()["scene"]["revision"]
        open_paint()
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
        producer.verify_saved(original, doc, "group", native=True)
        assert not data()["modified"] and not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    paint_panel_real_controls=True,
                    minimum_window_panel=True,
                    menu_reply_before_panel=True,
                    cancel_and_noop_preserve_redo=True,
                    protected_selection_disabled=True,
                    object_and_group_stroke=True,
                    stroke_caps=True,
                    stroke_joins=True,
                    affine_source_matrices=True,
                    reflected_join_pixels=True,
                    near_miter_limit_pixels=True,
                    zero_length_round_square=True,
                    single_coverage_translucent_ink=True,
                    inherited_and_shared_style_isolation=True,
                    one_command_history=True,
                    source_style_archive_oracle=True,
                    save_reopen=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland stroke caps/joins, zero-length strokes, single-coverage alpha, protected controls, style isolation/history and save/reopen",
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
