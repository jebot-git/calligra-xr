# Clipboard protocol

`ext-data-control-v1.xml` is the unmodified protocol from wayland-protocols
1.49, installed at `/usr/share/wayland-protocols/staging/ext-data-control/`.
Its copyright and permission notice are retained inside the XML.
Upstream: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/ext-data-control/ext-data-control-v1.xml

The helper uses version 1 for the regular clipboard only. It does not request
or read primary-selection data. A compositor must explicitly expose this
privileged protocol; DomeD currently does so. An unavailable protocol is a
reported failure, without replacing the clipboard with a text substitute.

The foreground owner accepts a bounded private binary bundle on standard input,
acknowledges compositor synchronization with `READY`, closes standard output and
serves each advertised MIME type until replacement. The caller must retain/reap
the process and close its input; process ownership is independent of any window.
No data is written to disk. Receivers buffer a complete transfer before writing
it to standard output. The private bundle framing is a process transport, not an
ODF clipboard format or a claim of office-application interoperability.

Input/output and each outstanding source transfer have five-second deadlines.
The owner allows up to 16 concurrent nonblocking transfers; one stalled target
cannot block another target. Bundle payloads total at most 64 MiB, with up to
16 unique MIME formats. A receiving payload is also capped at 64 MiB. The
compositor's MIME inventory and seat count are bounded separately.

`--seat NAME` chooses a Wayland seat. With no explicit name, exactly one live
seat is required; ambiguous multi-seat clipboards are rejected.
