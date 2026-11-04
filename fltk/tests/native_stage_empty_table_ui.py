#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated native Wayland empty-table GPU regression; no desktop input."""

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

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--fixture", type=Path, required=True)
parser.add_argument("--artifacts", type=Path, required=True)
parser.add_argument("--before-binary", type=Path)
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
host = None
client = None


def wait(test, timeout=25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if host is not None and host.poll() is not None:
            raise AssertionError("Private Wayland host exited")
        try:
            value = test()
            if value:
                return value
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(0.06)
    raise AssertionError("Private empty-table fixture timed out: " + str(art))


with tempfile.TemporaryDirectory(prefix="calligra-empty-table-") as temporary:
    base = Path(temporary)
    env = {k: v for k, v in os.environ.items() if not k.startswith("DOME_")}
    env["LD_LIBRARY_PATH"] = "/usr/local/lib/domed/bitmap"
    for name in ("CONFIG", "DATA", "CACHE", "STATE", "RUNTIME"):
        path = base / name.lower()
        path.mkdir(mode=0o700)
        env["XDG_" + name + ("_DIR" if name == "RUNTIME" else "_HOME")] = str(path)
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
        RUST_LOG="warn",
    )
    for name in ("IME", "XR", "POLKIT", "KDECONNECT", "KWALLET", "TRAY", "XWAYLAND"):
        env["DOME_START_" + name] = "0"
    wrapper = base / "start-compositor"
    wrapper.write_text(
        '#!/usr/bin/python3\nimport os\nos.execv("/usr/local/bin/dome-compositor",["dome-compositor","--backend","nested"])\n'
    )
    wrapper.chmod(0o700)
    try:
        with (art / "compositor.log").open("w") as log:
            host = subprocess.Popen(
                ["/usr/bin/cage", "-d", "--", str(wrapper)],
                env=env,
                stdout=log,
                stderr=log,
                start_new_session=True,
            )

        def scene():
            with http.open(f"http://127.0.0.1:{port}/api/state", timeout=2) as response:
                return json.load(response)["scene"]

        wait(lambda: scene()["source"] == "wayland")
        match = wait(
            lambda: re.search(
                r"Dome Wayland socket: (\S+)", (art / "compositor.log").read_text()
            )
        )
        native = {**env, "WAYLAND_DISPLAY": match[1], "FLTK_BACKEND": "wayland"}
        records = []
        cases = [("after", args.binary.resolve())]
        if args.before_binary:
            cases.insert(0, ("before", args.before_binary.resolve()))
        for label, binary in cases:
            report = art / (label + "-report.json")
            capture = art / (label + ".ppm")
            with (art / (label + ".log")).open("w") as log:
                client = subprocess.Popen(
                    [
                        str(binary),
                        str(args.fixture.resolve()),
                        "--report",
                        str(report),
                        "--capture",
                        str(capture),
                        "--capture-delay-ms",
                        "1400",
                        "--quit-after-ms",
                        "3000",
                    ],
                    env=native,
                    stdout=log,
                    stderr=log,
                )
                wait(lambda: capture.exists() and Path(str(capture) + ".json").exists())
                client.wait(timeout=30)
            assert client.returncode == 0, (label, client.returncode)
            text = (art / (label + ".log")).read_text()
            assert (
                "ERROR: AddressSanitizer" not in text and "runtime error:" not in text
            )
            metadata = json.loads(Path(str(capture) + ".json").read_text())
            assert metadata["wayland"] and metadata["gpu_widgets"]
            data = json.loads(report.read_text())
            current = data["scene"]
            assert current["render_error"] == "" and not data["modified"]
            image = Image.open(capture).convert("RGB")
            image.save(art / (label + ".png"))
            blue = sum(
                1
                for r, g, b in image.get_flattened_data()
                if r < 110 and g < 135 and 140 < b < 200 and b - r > 40
            )
            record = {
                "build": label,
                "table_cells": len(current["table_cells"]),
                "table_paints": current["table_paints"],
                "blue_pixels": blue,
                "native_wayland": True,
                "gpu": True,
            }
            if label == "after":
                assert (
                    record["table_cells"] == 4
                    and record["table_paints"] >= 4
                    and blue > 100
                ), record
            else:
                assert record["table_cells"] == 0 and blue == 0, record
            records.append(record)
            client = None
        (art / "results.json").write_text(
            json.dumps(
                {
                    "cases": records,
                    "isolated_headless_wayland": True,
                    "x_server_used": False,
                    "desktop_input_sent": False,
                },
                indent=2,
            )
            + "\n"
        )
        print(
            "PASS native Wayland empty-table GPU cells, paint and border pixels",
            records,
        )
    finally:
        if client is not None and client.poll() is None:
            client.terminate()
            client.wait(timeout=10)
        if host is not None and host.poll() is None:
            os.killpg(host.pid, signal.SIGTERM)
            try:
                host.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(host.pid, signal.SIGKILL)
                host.wait()
