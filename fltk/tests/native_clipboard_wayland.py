#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Private native-Wayland multi-MIME transport and hostile-peer acceptance.

No desktop clipboard is read or changed. This verifies transport, not Stage
object serialization, native menu integration or ODF document semantics.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request


def bundle(formats):
    data = bytearray(b"CalligraClip\0\1\0\0" + struct.pack(">I", len(formats)))
    for mime, payload in formats:
        name = mime.encode("ascii")
        data += struct.pack(">IQ", len(name), len(payload)) + name + payload
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument(
        "--odf-fragment",
        type=Path,
        help="Also transfer an actual selected-object ODP payload verbatim",
    )
    parser.add_argument(
        "--stage-fragment",
        type=Path,
        help="Also transfer a real private Stage selection envelope verbatim",
    )
    args = parser.parse_args()
    if os.environ.get("CALLIGRA_CLIPBOARD_TEST_BUS") != "1":
        os.execvpe(
            "dbus-run-session",
            ["dbus-run-session", "--", sys.executable, *sys.argv],
            {**os.environ, "CALLIGRA_CLIPBOARD_TEST_BUS": "1"},
        )
    signal.pthread_sigmask(signal.SIG_SETMASK, [])

    def timed_out(signum, frame):
        raise TimeoutError("Private clipboard acceptance exceeded its deadline")

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(150)
    # Adopt the deliberate short-lived launcher's clipboard owner, so the test
    # can reap it explicitly after proving ownership survives launcher exit.
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
    art = args.artifacts.resolve()
    art.mkdir(parents=True, exist_ok=True)
    executable = args.bin_dir.resolve() / "calligra-wayland-clipboard"
    peer_executable = args.bin_dir.resolve() / "calligra-wayland-clipboard-peer"
    processes, logs, adopted = [], [], []
    host = None
    checks = []
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    with tempfile.TemporaryDirectory(prefix="calligra-clipboard-wayland-") as temp:
        base = Path(temp)
        env = {k: v for k, v in os.environ.items() if not k.startswith("DOME_")}
        env["LD_LIBRARY_PATH"] = "/usr/local/lib/domed/bitmap"
        for name in ("CONFIG", "DATA", "CACHE", "STATE", "RUNTIME"):
            path = base / name.lower()
            path.mkdir(mode=0o700)
            env[f"XDG_{name}_" + ("DIR" if name == "RUNTIME" else "HOME")] = str(path)
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
        for name in (
            "IME",
            "XR",
            "POLKIT",
            "KDECONNECT",
            "KWALLET",
            "TRAY",
            "XWAYLAND",
        ):
            env["DOME_START_" + name] = "0"
        host_socket = base / "host-socket"
        wrapper = base / "start-compositor"
        wrapper.write_text(
            "#!/usr/bin/python3\nimport os\nfrom pathlib import Path\nPath("
            + repr(str(host_socket))
            + ').write_text(os.environ["WAYLAND_DISPLAY"])\nos.execv("/usr/local/bin/dome-compositor",["dome-compositor","--backend","nested"])\n'
        )
        wrapper.chmod(0o700)

        def wait(test, timeout=25):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if host is not None and host.poll() is not None:
                    raise AssertionError(
                        "Private compositor exited; inspect " + str(art)
                    )
                try:
                    value = test()
                    if value:
                        return value
                except (OSError, ValueError, KeyError):
                    pass
                time.sleep(0.04)
            raise AssertionError(
                "Private clipboard wait timed out; inspect " + str(art)
            )

        def scene():
            with http.open(f"http://127.0.0.1:{port}/api/state", timeout=2) as response:
                return json.load(response)["scene"]

        def run(command, data=None, expected=0, target_env=None):
            result = subprocess.run(
                [str(x) for x in command],
                input=data,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env if target_env is None else target_env,
                timeout=9,
            )
            assert result.returncode == expected, (
                command,
                result.returncode,
                result.stderr.decode(errors="replace"),
            )
            return result

        def start(command, data=None):
            log = (art / f"process-{len(processes):02d}.log").open("wb")
            logs.append(log)
            process = subprocess.Popen(
                [str(x) for x in command],
                env=env,
                stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=log,
            )
            processes.append(process)
            if data is not None:
                process.stdin.write(data)
                process.stdin.close()
            return process

        def ready(process):
            assert select.select([process.stdout], [], [], 7)[0], "No clipboard READY"
            assert process.stdout.readline(128) == b"READY\n", (
                "Clipboard peer failed; inspect logs"
            )
            assert process.poll() is None
            return process

        def own(formats):
            return ready(start([executable, "--offer"], bundle(formats)))

        def types():
            return set(run([executable, "--types"]).stdout.decode().splitlines())

        def paste(mime):
            return run([executable, "--receive", mime]).stdout

        def external_paste(mime, primary=False):
            command = ["wl-paste", "--no-newline", "--type", mime]
            if primary:
                command.append("--primary")
            return run(command).stdout

        def external_copy(mime, payload, primary=False):
            command = ["wl-copy", "--foreground", "--type", mime]
            if primary:
                command.append("--primary")
            process = start(command, payload)

            def copied():
                assert process.poll() is None, "External clipboard owner exited"
                query = ["wl-paste", "--no-newline", "--type", mime]
                if primary:
                    query.append("--primary")
                result = subprocess.run(
                    query,
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=7,
                )
                return result.returncode == 0 and result.stdout == payload

            wait(copied)
            return process

        try:
            log = (art / "compositor.log").open("wb")
            logs.append(log)
            host = subprocess.Popen(
                ["cage", "-d", "--", str(wrapper)],
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
            env["WAYLAND_DISPLAY"] = match[1]
            assert "DISPLAY" not in env

            # Cage is a separate private compositor, with no ext-data-control
            # global. Failure must be explicit, rather than a text replacement.
            unsupported = run(
                [executable, "--types"],
                expected=1,
                target_env={**env, "WAYLAND_DISPLAY": host_socket.read_text()},
            )
            assert b"ext-data-control-v1" in unsupported.stderr
            checks.append("unavailable_protocol_explicit_failure")

            utf8 = "Native clipboard: λ, 日本語, 😀\n".encode()
            primary = "Private primary selection — unchanged".encode()
            external_copy("text/plain;charset=utf-8", primary, primary=True)
            external_copy("text/plain;charset=utf-8", utf8)
            assert paste("text/plain;charset=utf-8") == utf8
            checks.append("external_text_source_to_native_receiver")

            payload = bytes(range(256)) * 1027 + b"\0binary\0"
            big = bytes(range(256)) * 32768  # 8 MiB; substantially larger than a pipe.
            formats = [
                ("application/x-calligra-clipboard-test", payload),
                ("text/plain;charset=utf-8", utf8),
                ("application/x-calligra-empty", b""),
                ("application/octet-stream", big),
            ]
            if args.odf_fragment:
                formats.append(
                    (
                        "application/vnd.oasis.opendocument.presentation",
                        args.odf_fragment.read_bytes(),
                    )
                )
            if args.stage_fragment:
                formats.append(
                    (
                        "application/x-calligra-stage-objects",
                        args.stage_fragment.read_bytes(),
                    )
                )
            owner = own(formats)
            assert types() == {mime for mime, _ in formats}
            preferred = run(
                [
                    executable,
                    "--receive-preferred",
                    "image/png",
                    "text/plain;charset=utf-8",
                    formats[0][0],
                ]
            )
            assert preferred.stdout == bundle([("text/plain;charset=utf-8", utf8)])
            empty = run(
                [executable, "--receive-preferred", "application/x-calligra-empty"]
            )
            assert empty.stdout == bundle([("application/x-calligra-empty", b"")])
            absent = run(
                [executable, "--receive-preferred", "image/png", "image/jpeg"],
                expected=3,
            )
            assert absent.stdout == b""
            checks += [
                "one_connection_preferred_format_and_framing",
                "preferred_empty_payload",
                "preferred_absent_formats",
            ]
            for mime, value in formats:
                native = paste(mime)
                external = external_paste(mime)
                assert native == value
                assert external == value
                if mime == "application/x-calligra-stage-objects":
                    (art / "received-native.cstage").write_bytes(native)
                    (art / "received-external.cstage").write_bytes(external)
            assert external_paste("text/plain;charset=utf-8", primary=True) == primary
            missing = run([executable, "--receive", "image/png"], expected=3)
            assert missing.stdout == b""
            checks += [
                "simultaneous_mime_advertisement",
                "exact_binary_unicode_empty_large_payloads",
                "native_owner_to_external_receivers",
                "primary_selection_unchanged",
                "absent_mime_distinguished",
            ]

            bad = run([executable, "--offer"], b"broken bundle", expected=1)
            assert b"Invalid" in bad.stderr and paste(formats[0][0]) == payload
            bad_seat = run(
                [executable, "--offer", "--seat", "missing-seat"],
                bundle(formats),
                expected=1,
            )
            assert b"seat is unavailable" in bad_seat.stderr
            assert paste(formats[0][0]) == payload
            checks.append("invalid_bundle_and_missing_seat_preserve_selection")

            with ThreadPoolExecutor(max_workers=8) as executor:
                values = list(
                    executor.map(lambda _: paste("application/octet-stream"), range(8))
                )
            assert all(value == big for value in values)
            checks.append("eight_concurrent_exact_large_transfers")

            stalled = ready(
                start([peer_executable, "hold-receive", "application/octet-stream"])
            )
            start_time = time.monotonic()
            assert paste(formats[0][0]) == payload
            assert time.monotonic() - start_time < 3
            stalled.terminate()
            stalled.wait(timeout=4)
            assert owner.poll() is None and paste(formats[0][0]) == payload
            checks.append("stalled_or_closed_target_does_not_block_owner")

            replacement = own([("text/plain;charset=utf-8", b"replacement")])
            assert owner.wait(timeout=7) == 0
            assert paste("text/plain;charset=utf-8") == b"replacement"
            checks.append("replacement_retires_previous_owner")

            # The helper is launched by a process which exits after READY. It
            # must continue serving without any parent window/event loop.
            launcher_code = (
                "import subprocess,sys,json\n"
                "p=subprocess.Popen([sys.argv[1],'--offer'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=open(sys.argv[2],'wb'))\n"
                "p.stdin.write(sys.stdin.buffer.read());p.stdin.close()\n"
                "assert p.stdout.readline()==b'READY\\n'\n"
                "print(p.pid,flush=True)\n"
            )
            launcher = run(
                [
                    sys.executable,
                    "-c",
                    launcher_code,
                    executable,
                    art / "detached-owner.log",
                ],
                bundle(formats[:2]),
            )
            detached = int(launcher.stdout.strip())
            adopted.append(detached)
            assert replacement.wait(timeout=7) == 0
            assert paste(formats[0][0]) == payload
            own([("text/plain;charset=utf-8", b"retire detached owner")])
            wait(lambda: Path(f"/proc/{detached}/stat").read_text().split()[2] == "Z")
            assert os.waitpid(detached, 0)[1] == 0
            adopted.remove(detached)
            checks.append("clipboard_survives_launching_process_exit")

            for mode, diagnostic in [
                ("stall-source", b"timed out"),
                ("oversize-source", b"exceeds the transfer limit"),
            ]:
                peer = ready(start([peer_executable, mode]))
                start_time = time.monotonic()
                failed = run([executable, "--receive", formats[0][0]], expected=1)
                assert diagnostic in failed.stderr and failed.stdout == b""
                assert time.monotonic() - start_time < 8
                final_owner = own(
                    [("text/plain;charset=utf-8", b"recover after rejected source")]
                )
                assert peer.wait(timeout=7) == 0
            checks += ["stalled_source_deadline", "oversized_source_no_partial_output"]

            # A writer that never closes the private input must also time out
            # before it has any opportunity to replace the selection.
            log = (art / "incomplete-input.log").open("wb")
            logs.append(log)
            incomplete = subprocess.Popen(
                [str(executable), "--offer"],
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=log,
            )
            processes.append(incomplete)
            incomplete.stdin.write(b"CalligraClip")
            incomplete.stdin.flush()
            assert incomplete.wait(timeout=7) == 1
            assert b"timed out" in (art / "incomplete-input.log").read_bytes()
            assert paste("text/plain;charset=utf-8") == b"recover after rejected source"
            checks.append("incomplete_input_deadline_preserves_selection")

            os.killpg(host.pid, signal.SIGTERM)
            host.wait(timeout=4)
            assert final_owner.wait(timeout=7) == 1
            checks.append("compositor_disconnect_closes_owner")

            result = dict(
                native_wayland=True,
                isolated_compositor=True,
                desktop_clipboard_accessed=False,
                x_server_used=False,
                checks=checks,
                payload_sha256=hashlib.sha256(payload).hexdigest(),
                large_payload_bytes=len(big),
                executable=str(executable),
                executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                scope="transport only; does not prove Stage object-model or UI completion",
            )
            if args.odf_fragment:
                result["odf_fragment_sha256"] = hashlib.sha256(
                    args.odf_fragment.read_bytes()
                ).hexdigest()
                result["odf_fragment_exact_in_native_and_external_receivers"] = True
            if args.stage_fragment:
                result["stage_fragment_sha256"] = hashlib.sha256(
                    args.stage_fragment.read_bytes()
                ).hexdigest()
                result["stage_fragment_exact_in_native_and_external_receivers"] = True
            (art / "results.json").write_text(json.dumps(result, indent=2) + "\n")
            print(f"Native Wayland clipboard: {len(checks)} acceptance checks passed")
        finally:
            signal.alarm(0)
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
            for process in reversed(processes):
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
                for stream in (process.stdin, process.stdout):
                    if stream is not None:
                        stream.close()
            for pid in adopted:
                try:
                    os.kill(pid, signal.SIGTERM)
                    os.waitpid(pid, 0)
                except ProcessLookupError:
                    pass
            if host is not None and host.poll() is None:
                os.killpg(host.pid, signal.SIGTERM)
                try:
                    host.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    os.killpg(host.pid, signal.SIGKILL)
                    host.wait(timeout=4)
            for log in logs:
                log.close()


if __name__ == "__main__":
    main()
