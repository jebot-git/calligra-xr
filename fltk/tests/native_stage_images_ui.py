#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland Stage image file chooser, embedded assets and native history."""

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
import stage_images_producer as producer

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
        for extension in ("png", "jpg", "gif", "bmp", "svg"):
            (art / ("input-image." + extension)).write_bytes(
                (doc.parent / ("image." + extension)).read_bytes()
            )
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
                cwd=doc.parent,
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

        codes.update({"i": 23, "m": 50, "g": 34, "e": 18, "p": 25, "n": 49, ".": 52})

        def inserted():
            return next((v for v in objects() if v["name"] == "NativeObject1"), None)

        def bounds(want):
            item = inserted()
            return item and all(
                math.isclose(a, b, abs_tol=1e-7) for a, b in zip(item["bounds"], want)
            )

        def chooser():
            menu_action("Image…")
            wait(lambda: data()["format_dialog"])
            dialog = wait(
                lambda: next(
                    (
                        w
                        for w in scene()["windows"]
                        if w.get("client_pid") == process.pid
                        and w.get("title") == "Insert image"
                    ),
                    None,
                )
            )
            (art / "chooser-window.json").write_text(
                json.dumps(dialog, indent=2) + "\n"
            )
            api("/api/action", {"op": "focus", "id": dialog["id"]})
            return dialog

        start()
        assert len(objects()) == 6
        shot("initial.png")
        chooser()
        shot("chooser-cancel.png")
        key("Escape")
        wait(lambda: not data()["format_dialog"])
        assert (
            inserted() is None
            and not data()["modified"]
            and not data()["scene"]["can_undo"]
        )
        chooser()
        key("a", ("Control_L",))
        for char in "image.png":
            key(char)
        shot("chooser-selected.png")
        key("Return")
        wait(
            lambda: (
                not data()["format_dialog"]
                and bounds([176, 105, 48, 30])
                and data()["modified"]
            )
        )
        wait(lambda: data()["scene"]["image_decodes"] >= 2)
        assert not data()["scene"]["image_errors"]
        assert not data()["scene"]["editor"]["active"]
        shot("inserted.png")
        key("z", ("Control_L",))
        wait(lambda: inserted() is None and not data()["modified"])
        chooser()
        key("Escape")
        wait(lambda: not data()["format_dialog"])
        assert data()["scene"]["can_redo"] and not data()["modified"]
        key("y", ("Control_L",))
        wait(lambda: bounds([176, 105, 48, 30]))
        click_world(200, 120)
        wait(lambda: data()["scene"]["selected_shape"] == 6)
        key("Right", ("Shift_L",))
        key("Right", ("Shift_L",))
        key("Down", ("Shift_L",))
        wait(lambda: bounds([196, 115, 48, 30]))
        key("Delete")
        wait(lambda: inserted() is None)
        key("z", ("Control_L",))
        wait(lambda: bounds([196, 115, 48, 30]))
        key("y", ("Control_L",))
        wait(lambda: inserted() is None)
        key("z", ("Control_L",))
        wait(lambda: bounds([196, 115, 48, 30]))
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        producer.verify_saved(original, doc, art / "input-image.png")
        (art / "native-saved.odp").write_bytes(doc.read_bytes())
        (art / "image-object.json").write_text(json.dumps(inserted(), indent=2) + "\n")
        shot("edited.png")
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
        wait(lambda: bounds([196, 115, 48, 30]) and not data()["modified"])
        assert not data()["scene"]["can_undo"]
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    universal_menu_image=True,
                    native_fltk_file_chooser=True,
                    chooser_cancel_preserves_history=True,
                    original_encoded_image_embedded=True,
                    actual_gpu_decode=True,
                    image_editor_disabled=True,
                    image_movement=True,
                    canvas_delete=True,
                    undo_redo=True,
                    save_reopen=True,
                    source_manifest_and_assets_preserved=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland Stage image chooser, embedding/GPU, movement/deletion/history and exact ODP assets/save/reopen",
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
