#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Wayland Stage character/paragraph controls, typing and ODP history."""

import argparse
import json
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
import stage_text_format_producer as producer

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
            "Home": 102,
            "End": 107,
            "b": 48,
            "i": 23,
            "u": 22,
            "d": 32,
            "e": 18,
            "f": 33,
            "#": 4,
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
                    and data()["scene"]["rendered_shapes"] >= 4
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

        focused_field = 0

        def panel(label="Font…"):
            global focused_field
            menu_action(label)
            wait(lambda: data()["format_dialog"])
            focused_field = 0

        def field(index, value):
            global focused_field
            while focused_field != index:
                forward = focused_field < index
                key("Tab", () if forward else ("Shift_L",))
                focused_field += 1 if forward else -1
            key("a", ("Control_L",))
            for char in str(value):
                key(char, ("Shift_L",) if char == "#" else ())

        def apply(index):
            global focused_field
            while focused_field < index:
                key("Tab")
                focused_field += 1
            key("Space")
            focused_field = 0

        def editor_state():
            return data()["scene"]["editor"]

        def select(first, last):
            key("Home", ("Control_L",))
            for _ in range(first):
                key("Right")
            for _ in range(last - first):
                key("Right", ("Shift_L",))
            wait(lambda: editor_state()["selection"] == [first, last])

        def undo_clean():
            key("z", ("Control_L",))
            wait(lambda: not data()["modified"])
            assert editor_state()["selection"] == [1, 6]

        start()
        ex, ey, ew, eh = editor_state()["bounds"]
        click(ex + 60, ey + 20)
        select(1, 6)
        shot("initial-selection.png")
        # Real editor shortcuts must route to native history, preserving the
        # scalar selection rather than selecting UTF-8 byte offsets.
        key("i", ("Control_L",))
        wait(lambda: data()["modified"])
        undo_clean()
        assert data()["scene"]["can_redo"]
        panel()
        field(1, 24)
        key("Escape")
        wait(lambda: not data()["format_dialog"])
        assert not data()["modified"] and data()["scene"]["can_redo"]
        assert editor_state()["selection"] == [1, 6]
        panel()
        apply(9)
        wait(lambda: not data()["format_dialog"])
        assert not data()["modified"] and data()["scene"]["can_redo"]
        key("y", ("Control_L",))
        wait(lambda: data()["modified"])
        undo_clean()
        for shortcut in ("b", "u"):
            key(shortcut, ("Control_L",))
            wait(lambda: data()["modified"])
            undo_clean()
        panel()
        field(1, -1)
        apply(9)
        time.sleep(0.3)
        assert data()["format_dialog"] and not data()["modified"]
        shot("invalid-size.png")
        field(1, 24)
        field(6, "#ff0000")
        field(7, "#ffee00")
        shot("font-input.png")
        apply(9)
        wait(lambda: not data()["format_dialog"] and data()["modified"])
        assert editor_state()["selection"] == [1, 6]
        key("u", ("Control_L",))
        key("e", ("Control_L",))
        panel("Paragraph…")
        field(1, 7)
        field(2, 9)
        field(4, 1.8)
        field(5, 4)
        field(6, 5)
        shot("paragraph-input.png")
        apply(8)  # Pagination controls are inactive for slide text frames.
        wait(lambda: not data()["format_dialog"])
        assert editor_state()["selection"] == [1, 6]
        shot("formatted.png")
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        (art / "formatted.odp").write_bytes(doc.read_bytes())
        producer.verify_saved(original, doc, typed=False)
        (art / "formatted.odp").write_bytes(doc.read_bytes())
        select(0, 0)
        panel()
        field(1, 14)
        apply(9)
        wait(lambda: not data()["format_dialog"] and editor_state()["typing_format"])
        assert not data()["modified"]
        key("b", ("Control_L",))
        key("i", ("Control_L",))
        key("x")
        key("y")
        wait(lambda: editor_state()["text"] == "xy" + producer.TEXT)
        key("z", ("Control_L",))
        wait(lambda: not data()["modified"] and editor_state()["text"] == producer.TEXT)
        assert (
            editor_state()["selection"] == [0, 0]
            and not editor_state()["typing_format"]
        )
        key("y", ("Control_L",))
        wait(lambda: editor_state()["text"] == "xy" + producer.TEXT)
        assert editor_state()["selection"] == [2, 2]
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        (art / "native-saved.odp").write_bytes(doc.read_bytes())
        producer.verify_saved(original, doc)
        shot("typed.png")
        # Insert the same letter as the following source character. A longest
        # common-prefix diff would put the format and undo caret one too far on.
        select(0, 0)
        panel()
        field(1, 28)
        apply(9)
        wait(lambda: not data()["format_dialog"] and editor_state()["typing_format"])
        key("x")
        wait(lambda: editor_state()["text"] == "xxy" + producer.TEXT)
        assert editor_state()["selection"] == [1, 1]
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        repeated = art / "repeat-saved.odp"
        repeated.write_bytes(doc.read_bytes())
        producer.verify_repeated(art / "native-saved.odp", repeated)
        key("z", ("Control_L",))
        wait(lambda: editor_state()["text"] == "xy" + producer.TEXT)
        assert editor_state()["selection"] == [0, 0]
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        # Empty-frame paragraph defaults are a source edit with ordinary undo.
        click_world(350, 190)
        wait(lambda: data()["scene"]["selected_frame"] == 2)
        click(ex + 60, ey + 20)
        key("i", ("Control_L",))
        wait(lambda: data()["modified"])
        key("z", ("Control_L",))
        wait(lambda: not data()["modified"] and data()["scene"]["selected_frame"] == 2)
        assert editor_state()["text"] == "" and editor_state()["selection"] == [0, 0]
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
        assert not data()["modified"] and not data()["scene"]["can_undo"]
        assert editor_state()["text"] == "xy" + producer.TEXT
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    isolated_headless_wayland_host=True,
                    x_server_used=False,
                    universal_menu_font_and_paragraph=True,
                    keyboard_bold_italic_underline=True,
                    unicode_scalar_selection=True,
                    cancel_and_escape=True,
                    invalid_size_stays_in_panel=True,
                    unchanged_apply_preserves_redo=True,
                    paragraph_spacing_and_alignment=True,
                    future_typing_format=True,
                    merged_typing_history=True,
                    repeated_letter_exact_range=True,
                    undo_redo_restores_selection=True,
                    empty_frame_format_undo=True,
                    save_reopen=True,
                    raw_style_source_and_assets_preserved=True,
                    runtime_libraries=libraries,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland Stage formatting controls, Unicode selection, future typing/history and independent ODP save/reopen",
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
