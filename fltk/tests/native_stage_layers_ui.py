#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland ODF layer visibility/protection, protected editing and exact history."""

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
import stage_layers_producer as producer

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

        def shot(name, samples):
            # Report publication can precede the compositor's next captured
            # resized/updated frame. Require the authored pixel oracle in that
            # captured frame, rather than sampling a stale preview surface.
            def captured():
                image = (
                    Image.open(art / "live.ppm")
                    .transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                    .convert("RGB")
                )
                image.save(art / name)
                try:
                    for x, y, color in samples:
                        pixel(image, x, y, color)
                except AssertionError:
                    return None
                return image

            return wait(captured)

        def pixel(image, x, y, expected):
            px, py = local(x, y)
            w = native_window()["desktop"]
            got = image.getpixel((round(w["x"] + px), round(w["y"] + py)))
            assert all(abs(a - b) <= 2 for a, b in zip(got, expected)), (
                x,
                y,
                got,
                expected,
            )

        def selected(name, depth=0):
            return selection() == [(name, depth)]

        def error_closed():
            wait(lambda: data().get("modal_dialog", False))
            key("Return")
            wait(lambda: not data().get("modal_dialog", False))

        start()
        wait(lambda: len(names()) == 4 and data()["scene"]["missing_glyphs"] == 0)
        assert set(names()) == {"Editable", "ScreenOnly", "Locked", "GroupVisible"}
        assert (
            item("Locked")["layer_protected"] and not item("Locked")["object_editable"]
        )
        screen = shot(
            "screen-global.png",
            [
                (107, 91, (204, 48, 48)),
                (207, 91, (34, 85, 136)),
                (307, 91, (255, 255, 255)),
                (207, 161, (224, 160, 32)),
                (350, 10, (34, 85, 136)),
            ],
        )
        pixel(screen, 107, 91, (204, 48, 48))
        pixel(screen, 207, 91, (34, 85, 136))
        pixel(screen, 307, 91, (255, 255, 255))
        pixel(screen, 107, 161, (255, 255, 255))
        pixel(screen, 207, 161, (224, 160, 32))
        pixel(screen, 307, 231, (255, 255, 255))
        pixel(screen, 350, 10, (34, 85, 136))
        click_world(250, 70)  # Print-only geometry cannot be selected on screen.
        wait(lambda: not selection())
        click_world(50, 70)
        wait(lambda: selected("Editable"))
        key("Right", ("Shift_L",))
        wait(lambda: data()["modified"])
        for _ in range(5):
            key("Down")
        for _ in range(6):
            key("z", ("Control_L",))
        wait(lambda: not data()["modified"] and data()["scene"]["can_redo"])
        revision = data()["scene"]["revision"]
        # Visible protected objects remain selectable for inspection/copy, but
        # expose neither editing nor transform handles.
        click_world(150, 135)
        wait(lambda: selected("Locked"))
        assert not data()["scene"]["editor"]["active"]
        assert not data()["scene"]["transform_handles"]
        menu_action("Delete object")
        error_closed()
        assert data()["scene"]["revision"] == revision and data()["scene"]["can_redo"]
        assert not data()["modified"] and "Locked" in names()
        # The group has a hidden protected nested member. A group selection
        # must not bypass its protection through deletion, resize or movement.
        click_world(250, 135)
        wait(lambda: selected("GroupVisible"))
        menu_action("Select containing group")
        wait(lambda: selected("GroupVisible", 1))
        assert not data()["scene"]["editor"]["active"]
        assert not data()["scene"]["transform_handles"]
        menu_action("Delete object")
        error_closed()
        assert data()["scene"]["revision"] == revision and data()["scene"]["can_redo"]
        key("Escape")
        click_world(50, 70)
        wait(lambda: selected("Editable"))
        for _ in range(6):
            key("y", ("Control_L",))
        wait(lambda: data()["modified"])
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, True)
        (art / "native-moved.odp").write_bytes(doc.read_bytes())
        # Use the real slide browser to inspect the local set's inverted modes.
        click(55, 94)
        wait(lambda: data()["scene"]["page"] == 2)
        assert set(names()) == {"LocalScreen", "LocalDefault"}
        screen = shot(
            "screen-local.png",
            [
                (107, 101, (204, 48, 48)),
                (207, 101, (255, 255, 255)),
                (307, 101, (224, 160, 32)),
            ],
        )
        pixel(screen, 107, 101, (204, 48, 48))
        pixel(screen, 207, 101, (255, 255, 255))
        pixel(screen, 307, 101, (224, 160, 32))
        assert item("LocalDefault")["object_editable"]
        click(55, 110)
        wait(lambda: data()["scene"]["page"] == 3)
        assert set(names()) == {"EmptyLocal"}
        screen = shot(
            "screen-empty-master.png",
            [(350, 10, (34, 85, 136)), (107, 91, (204, 48, 48))],
        )
        pixel(screen, 350, 10, (34, 85, 136))
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
        assert not data()["scene"]["can_undo"] and not data()["modified"]
        assert item("Editable")["bounds"][:2] == [50, 55]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    global_and_local_screen_pixels=True,
                    print_only_not_selectable=True,
                    protected_object_editor_and_handles_disabled=True,
                    hidden_protected_group_guard=True,
                    menu_error_reply_and_modal=True,
                    rejected_commands_preserve_redo=True,
                    save_reopen=True,
                    independent_raw_source_oracle=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland layer pixels, protected object/group controls, menu guards/history and exact source save/reopen",
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
