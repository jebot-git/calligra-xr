#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Stage object clipboard under isolated native Wayland; no desktop input."""

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
parser.add_argument(
    "--early-dismiss-alerts",
    action="store_true",
    help="Diagnostic: dismiss before mapping; exposes a system-FLTK Wayland leak",
)
parser.add_argument("--debug-wayland", action="store_true")
parser.add_argument("--trace-preload")
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
import select
import struct
import zipfile
from lxml import etree as ET

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
            "c": 46,
            "v": 47,
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

        bins = args.bin_dir.resolve()
        helper = bins / "calligra-wayland-clipboard"
        if not helper.is_file():
            helper = bins.parent / "libexec/calligra-fltk/calligra-wayland-clipboard"
        assert helper.is_file(), helper

        documents = art / "fixtures"
        subprocess.run(
            [
                str(args.test_bin_dir.resolve() / "calligra-native-stage-paste-test"),
                "--documents",
                str(documents),
            ],
            check=True,
        )
        doc = art / "editing.odp"
        doc.write_bytes((documents / "source.odp").read_bytes())
        report = art / "stage.json"
        capture = art / "stage.ppm"
        env.update(
            WAYLAND_DISPLAY=str(base / "runtime" / native_socket),
            FLTK_BACKEND="wayland",
        )

        def data():
            return json.loads(report.read_text())

        def count():
            return sum(not o["master"] for o in data()["scene"]["objects"])

        def start():
            global process
            report.unlink(missing_ok=True)
            log = (art / "stage.log").open("a")
            process = subprocess.Popen(
                [
                    str(bins / "calligra-stage-fltk"),
                    str(doc),
                    "--report",
                    str(report),
                    "--capture",
                    str(capture),
                ],
                env={
                    **env,
                    **({"WAYLAND_DEBUG": "client"} if args.debug_wayland else {}),
                    **(
                        {"LD_PRELOAD": args.trace_preload} if args.trace_preload else {}
                    ),
                },
                stdout=log,
                stderr=log,
            )
            wait(lambda: report.exists(), timeout=60)
            w = wait(native_window)
            api(
                "/api/action",
                {"op": "desktop_resize", "id": w["id"], "w": 1100, "h": 700},
            )
            api("/api/action", {"op": "desktop_move", "id": w["id"], "x": 60, "y": 50})
            api("/api/action", {"op": "focus", "id": w["id"]})
            wait(
                lambda: (
                    data().get("window_size") == [1100, 700]
                    and not data()["scene"]["render_error"]
                )
            )

        def dismiss_alert():
            if args.early_dismiss_alerts:
                key("Return")
                return

            # Operate on an exposed native dialog. Status is written before
            # FLTK creates/maps its alert; an immediate Return can reach an
            # unmapped Wayland window and exercises a separate toolkit bug.
            def alert_window():
                return next(
                    (
                        w
                        for w in scene()["windows"]
                        if process
                        and w.get("client_pid") == process.pid
                        and w.get("id") != native_window()["id"]
                        and w.get("desktop", {}).get("w", 0) > 0
                    ),
                    None,
                )

            wait(alert_window)
            time.sleep(0.25)
            key("Return")
            wait(lambda: alert_window() is None)

        def close():
            global process
            key("q", ("Control_L",))
            assert process.wait(timeout=10) == 0
            process = None

        def receive(mime):
            r = subprocess.run(
                [str(helper), "--receive", mime],
                env=env,
                capture_output=True,
                timeout=10,
            )
            assert r.returncode == 0, r.stderr
            return r.stdout

        def offer(mime, payload):
            name = mime.encode()
            wire = (
                b"CalligraClip\0\1\0\0"
                + struct.pack(">I", 1)
                + struct.pack(">IQ", len(name), len(payload))
                + name
                + payload
            )
            log = (art / "external-owner.log").open("ab")
            p = subprocess.Popen(
                [str(helper), "--offer"],
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=log,
            )
            p.stdin.write(wire)
            p.stdin.close()
            assert select.select([p.stdout], [], [], 10)[0]
            assert p.stdout.readline() == b"READY\n"
            owners.append(p)
            return p

        def save(label):
            key("s", ("Control_L",))
            wait(lambda: not data()["modified"])
            (art / (label + ".odp")).write_bytes(doc.read_bytes())
            time.sleep(0.18)
            Image.open(art / "live.ppm").transpose(
                Image.Transpose.FLIP_TOP_BOTTOM
            ).save(art / (label + ".png"))

        def content():
            with zipfile.ZipFile(doc) as z:
                return z.read("content.xml")

        def body(value):
            def freeze(n):
                return (
                    n.tag,
                    sorted(n.attrib.items()),
                    n.text,
                    [(freeze(c), c.tail) for c in n],
                )

            root = ET.fromstring(value)
            # Cutting a selected leaf retains its now-empty context groups and
            # imported definitions. Undo must restore the exact pasted source.
            groups = list(
                root.iter("{urn:oasis:names:tc:opendocument:xmlns:drawing:1.0}g")
            )
            for group in reversed(groups):
                if not len(group) and not (group.text or "").strip():
                    group.getparent().remove(group)
            return freeze(
                root.find("{urn:oasis:names:tc:opendocument:xmlns:office:1.0}body")
            )

        owners = []

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

        start()
        click_world(250, 35)
        wait(lambda: len(data()["scene"]["selected_objects"]) == 1)
        selected = data()["scene"]["selected_shape"]
        assert data()["scene"]["objects"][selected]["name"] == "X", data()["scene"]
        key("c", ("Control_L",))
        wait(lambda: data()["status_message"] == "Objects copied")
        copied = receive("application/x-calligra-stage-objects")
        public = receive("application/vnd.oasis.opendocument.presentation")
        assert b"PK" == public[:2] and copied[:6] == b"CStage"
        assert "Child" in receive("text/plain;charset=utf-8").decode()
        (art / "copied.cstage").write_bytes(copied)
        (art / "copied.odp").write_bytes(public)
        assert not data()["modified"]
        close()
        assert receive("application/x-calligra-stage-objects") == copied
        doc.write_bytes((documents / "destination-before.odp").read_bytes())
        before = content()
        start()
        click_world(500, 280)
        initial = count()
        key("v", ("Control_L",))
        wait(lambda: data()["status_message"] == "Objects pasted")
        wait(lambda: count() == initial + 1 and data()["scene"]["table_cells"])
        assert not data()["scene"]["render_error"]
        save("native-pasted")
        inserted = content()
        key("z", ("Control_L",))
        wait(lambda: count() == initial)
        save("native-undo")
        assert content() == before
        key("y", ("Control_L",))
        wait(lambda: count() == initial + 1)
        save("native-redo")
        assert content() == inserted
        menu_action("Cut")
        wait(lambda: data()["status_message"] == "Objects cut")
        assert count() == initial
        save("native-cut")
        cut_content = content()
        assert body(cut_content) == body(before)
        key("z", ("Control_L",))
        wait(lambda: count() == initial + 1)
        save("native-cut-undo")
        assert content() == inserted
        key("y", ("Control_L",))
        wait(lambda: count() == initial)
        save("native-cut-redo")
        assert content() == cut_content
        assert receive("application/x-calligra-stage-objects")
        offer("text/plain;charset=utf-8", "Native plain Ω\nSecond line".encode())
        menu_action("Paste")
        wait(lambda: data()["status_message"] == "Objects pasted")
        assert count() == initial + 1
        assert any("Native plain Ω" in o["text"] for o in data()["scene"]["objects"])
        save("native-plain")
        key("z", ("Control_L",))
        wait(lambda: count() == initial)
        save("native-plain-undo")
        assert content() == cut_content
        offer("application/vnd.oasis.opendocument.presentation", public)
        key("v", ("Control_L",))
        wait(lambda: data()["status_message"] == "Objects pasted")
        save("native-public")
        assert count() == initial + 1
        wait(lambda: data()["scene"]["table_cells"])
        stable = content()
        original_count = count()
        delayed = offer("application/vnd.oasis.opendocument.presentation", public)
        os.kill(delayed.pid, signal.SIGSTOP)
        key("v", ("Control_L",))
        wait(lambda: data()["status_message"] == "Receiving clipboard objects…")
        click_world(500, 280)
        os.kill(delayed.pid, signal.SIGCONT)
        wait(
            lambda: (
                "selection changed while the clipboard was pending"
                in data()["status_message"]
            )
        )
        assert count() == original_count and not data()["modified"]
        dismiss_alert()
        time.sleep(0.2)
        save("native-stale-selection")
        assert content() == stable
        # Start another delayed receive, then change the document itself.
        click_world(250, 35)
        delayed = offer("application/vnd.oasis.opendocument.presentation", public)
        os.kill(delayed.pid, signal.SIGSTOP)
        key("v", ("Control_L",))
        wait(lambda: data()["status_message"] == "Receiving clipboard objects…")
        key("Right")
        wait(lambda: data()["modified"])
        os.kill(delayed.pid, signal.SIGCONT)
        wait(
            lambda: (
                "selection changed while the clipboard was pending"
                in data()["status_message"]
            )
        )
        assert count() == original_count
        dismiss_alert()
        time.sleep(0.2)
        key("z", ("Control_L",))
        wait(lambda: not data()["modified"])
        save("native-stale-revision-undo")
        assert content() == stable and data()["scene"]["can_redo"]
        close()
        start()
        assert (
            count() == initial + 1
            and not data()["modified"]
            and not data()["scene"]["can_undo"]
        )
        close()
        metadata = json.loads(Path(str(capture) + ".json").read_text())
        assert metadata["wayland"] and metadata["gpu_widgets"]
        log = (art / "stage.log").read_text()
        assert "ERROR: AddressSanitizer" not in log and "runtime error:" not in log
        (art / "results.json").write_text(
            json.dumps(
                dict(
                    native_wayland=True,
                    exposed_alerts=not args.early_dismiss_alerts,
                    isolated_headless_wayland_host=True,
                    desktop_clipboard_accessed=False,
                    keyboard_copy_paste=True,
                    universal_menu_cut_paste=True,
                    private_and_public_odp=True,
                    plain_unicode_text=True,
                    owner_survives_source_application_exit=True,
                    receiving_history_exact=True,
                    cut_undo_redo=True,
                    paste_undo_redo=True,
                    save_reopen=True,
                    delayed_selection_and_revision_rejected=True,
                    source_tables_rendered_after_paste=True,
                ),
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS actual Stage clipboard keyboard/menu, cross-process ownership, native/public/text paste, exact history and save/reopen",
            flush=True,
        )
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        for owner in locals().get("owners", []):
            if owner.poll() is None:
                owner.send_signal(signal.SIGCONT)
                owner.terminate()
                owner.wait(timeout=10)
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
