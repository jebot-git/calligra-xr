#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated native Wayland Sheets input, calculation, history and persistence."""

import argparse
import importlib
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
parser.add_argument(
    "--suite",
    choices=(
        "criteria",
        "order",
        "moments",
        "paired",
        "distribution",
        "inference",
        "regression",
        "log_regression",
        "database",
    ),
    required=True,
)
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
producer = importlib.import_module(args.suite + "_producer")
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
                and "calligra-sheets" in w.get("app_id", "").lower()
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

        sources = producer.produce(base / "fixtures")
        for source in sources:
            producer.verify(source, args.test_bin_dir, art / "producer")
        doc = sources[0]
        report = art / (args.suite + ".json")
        capture = art / (args.suite + ".ppm")
        with zipfile.ZipFile(doc) as z:
            original = {name: z.read(name) for name in z.namelist()}
        env.update(
            WAYLAND_DISPLAY=str(base / "runtime" / native_socket),
            FLTK_BACKEND="wayland",
        )

        def start():
            global process
            log = (art / (args.suite + ".log")).open("a")
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
            wait(lambda: report.exists())
            w = wait(native_window)
            api(
                "/api/action",
                {"op": "desktop_resize", "id": w["id"], "w": 1100, "h": 600},
            )
            api("/api/action", {"op": "desktop_move", "id": w["id"], "x": 60, "y": 50})
            api("/api/action", {"op": "focus", "id": w["id"]})
            wait(
                lambda: (
                    native_window()["desktop"]["x"] == 60
                    and native_window()["desktop"]["y"] == 50
                )
            )
            time.sleep(0.6)
            move(5, 5)

        def data():
            return json.loads(report.read_text())

        def first():
            move(5, 5)
            click(230, 140)
            wait(lambda: data()["cell"]["column"] >= 2 and data()["cell"]["row"] == 1)
            for _ in range(data()["cell"]["column"] - 1):
                key("Left")
            wait(lambda: data()["cell"]["column"] == 1 and data()["cell"]["row"] == 1)

        def cells(money):
            first()
            for col, want in enumerate(producer.expected(money), 1):

                def matches():
                    cell = data()["cell"]
                    if cell["column"] != col or cell["row"] != 1:
                        return False
                    if isinstance(want, str):
                        return cell["value"] == want
                    try:
                        value = float(cell["value"])
                        painted = float(cell.get("painted_value", "nan"))
                        return math.isclose(
                            value, want, rel_tol=3e-10, abs_tol=1e-10
                        ) and math.isclose(painted, value, rel_tol=1e-3, abs_tol=1e-15)
                    except ValueError:
                        return False

                try:
                    wait(matches)
                except AssertionError:
                    raise AssertionError(
                        f"Column {col}: expected {want}; actual {data()}"
                    )
                if col < len(producer.expected(money)):
                    key("Right")

        start()
        cells(2)
        (art / "initial-scene.json").write_text(json.dumps(scene(), indent=2) + "\n")
        Image.open(art / "live.ppm").transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(
            art / "initial.png"
        )
        libraries = sorted(
            {
                line.split()[-1]
                for line in Path(f"/proc/{process.pid}/maps").read_text().splitlines()
                if any(v in line for v in ("libQt", "libfltk", "libgtk-"))
            }
        )
        (art / "runtime-libraries.json").write_text(
            json.dumps(libraries, indent=2) + "\n"
        )
        assert not any("libQt" in p for p in libraries), libraries
        assert any(
            p.startswith(("/usr/lib64/libfltk", "/lib64/libfltk")) for p in libraries
        ), libraries
        first()
        click(410, 74)
        key("a", ("Control_L",))
        key("8")
        key("Return")
        cells(8)
        key("z", ("Control_L",))
        cells(2)
        key("y", ("Control_L",))
        cells(8)
        first()
        Image.open(art / "live.ppm").transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(
            art / (args.suite + "-edited.png")
        )
        key("s", ("Control_L",))
        wait(lambda: not data()["modified"])
        if hasattr(producer, "verify_caches"):
            producer.verify_caches(doc, money=8)
        (art / "native-saved.ods").write_bytes(doc.read_bytes())
        producer.verify(doc, args.test_bin_dir, art / "saved", money=8)
        with zipfile.ZipFile(doc) as z:
            for name, value in original.items():
                if name != "content.xml":
                    assert z.read(name) == value, name
        previous = process.pid
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        wait(
            lambda: not any(w.get("client_pid") == previous for w in scene()["windows"])
        )
        report.unlink()
        start()
        cells(8)
        key("q", ("Control_L",))
        assert process.wait(timeout=10) == 0
        (art / "results.json").write_text(
            json.dumps(
                {
                    "native_wayland": True,
                    "isolated_headless_wayland_host": True,
                    "x_server_used": False,
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
            "PASS isolated native Wayland input, all cells, editing/history, recalculation, raw caches, save/reopen and opaque data; no X server",
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
