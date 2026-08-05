#!/usr/bin/env python3
"""Regenerate webui_html.h from webui.html.

The firmware serves the web UI from a gzipped PROGMEM array, not from the
filesystem -- see the `server.on("/")` handler, which sends `webui_html` with a
`Content-Encoding: gzip` header. That array lives in a checked-in generated
header, and until 2026-08-05 nothing regenerated it: editing webui.html changed
nothing on the device, silently, because the stale blob still built and still
served. The /spiffs/ -> /fs/ rename hit exactly that.

So this exists to make the two impossible to disagree about. `make -C build
webui` regenerates, and `make -C build webui-check` fails if the header is out
of date, which is the half that catches the mistake.

Deterministic on purpose: mtime is zeroed so identical input gives an identical
header, and a diff means the UI really changed.
"""

import argparse
import gzip
import io
import re
import sys
from pathlib import Path

PER_LINE = 12  # bytes per line, matching the existing header's layout


def compress(html: bytes) -> bytes:
    """gzip the UI, deterministically.

    mtime=0 keeps the output byte-identical for identical input; the default
    stamps the current time and would make every regeneration a diff.
    """
    buffer = io.BytesIO()
    with gzip.GzipFile(
        filename="webui.html", mode="wb", fileobj=buffer, compresslevel=9, mtime=0
    ) as f:
        f.write(html)
    return buffer.getvalue()


def render(blob: bytes, uncompressed_len: int) -> str:
    lines = []
    for start in range(0, len(blob), PER_LINE):
        chunk = blob[start : start + PER_LINE]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk))
    return (
        "const uint8_t webui_html[] PROGMEM = {\n"
        + ",\n".join(lines)
        + "\n};\n"
        + f"const unsigned int webui_html_len = {uncompressed_len};\n"
    )


def current_blob(header: str) -> bytes:
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", header))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit 1 if the header does not match the source",
    )
    args = parser.parse_args()

    html = args.source.read_bytes()
    blob = compress(html)
    generated = render(blob, len(blob))

    if not args.check:
        args.header.write_text(generated)
        print(
            f"{args.header.name}: {len(html)} bytes of HTML -> {len(blob)} gzipped"
        )
        return 0

    # --check compares what the header *decompresses to*, not its bytes. A
    # different zlib build can emit a different stream for the same input, and
    # failing on that would be a false alarm about a UI that is actually current.
    if not args.header.exists():
        print(f"{args.header} does not exist", file=sys.stderr)
        return 1
    try:
        shipped = gzip.decompress(current_blob(args.header.read_text()))
    except Exception as e:  # noqa: BLE001 - an unreadable header is out of date
        print(f"{args.header} could not be decompressed: {e}", file=sys.stderr)
        return 1

    if shipped == html:
        print(f"{args.header.name} is up to date with {args.source.name}")
        return 0

    print(
        f"{args.header.name} is STALE: it serves {len(shipped)} bytes but "
        f"{args.source.name} is {len(html)}. The device would run the old UI. "
        f"Run: make -C build webui",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
