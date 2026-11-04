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

## Stage selected-object representation

`application/x-calligra-stage-objects` is a private, versioned selection
representation, separate from the helper's multi-MIME process bundle. Public
`application/vnd.oasis.opendocument.presentation` data remains a standalone ODP
without private selection attributes. Neither representation is an integrated
Stage clipboard command yet.

The private representation uses unsigned, big-endian fields:

| Field | Size | Meaning |
| --- | --- | --- |
| Magic/version | 8 bytes | `43 53 74 61 67 65 00 01` (`CStage`, NUL, version 1) |
| Archive size | 8 bytes | Exact byte length of the final ODP ZIP |
| Selection count | 4 bytes | Number of selected object roots |
| Path depth, per root | 2 bytes | Number of following element-child indices |
| Path indices, per root | 4 bytes each | Zero-based element-child ranks below the sole `draw:page` |
| Archive | Declared size | Complete standalone selected-object ODP |

Whitespace, comments and other non-element nodes do not consume child ranks.
The metadata order retains the primary selection; the ODP retains source paint
order. Context-only ancestor groups are distinct from selected groups. Duplicate
or ancestor/descendant selections are invalid in a wire representation. The
native copying API normalizes overlapping source selections before encoding.

The complete envelope is capped at 64 MiB. There must be 1–100,000 roots, each
path has depth 1–256, and their combined path components are capped at one
million. Child indices and actual path traversal have separate one-million
bounds. Truncated, future-version, overlapping, out-of-page, nonselectable and
trailing-data inputs fail explicitly. ZIP/XML and dependency-closure checks
apply independently after validating envelope metadata.

Both encoder and decoder re-extract the explicitly selected dependency closure.
Unselected source objects, outer metadata and unused assets are excluded; plain
text is derived from selected source paragraphs in source order. No caller-
provided text fallback is serialized. This canonicalization is not destination
import: default/style/font/layer isolation and collision-safe identity/reference
rewriting remain required before objects can be pasted into another document.

The helper's 64 MiB aggregate limit still applies when offering public ODP,
private selection and text together. A producer must validate the complete
bundle before publishing or committing a prepared cut.
