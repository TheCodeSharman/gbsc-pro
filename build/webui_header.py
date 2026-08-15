#!/usr/bin/env python3
"""Regenerate webui_html.h from webui.html -- the last link of the UI build.

The firmware serves the web UI from a gzipped PROGMEM array, not from the
filesystem -- see the `server.on("/")` handler, which sends `webui_html` with a
`Content-Encoding: gzip` header. Until 2026-08-05 the arduino-cli build did not
regenerate that array, so editing webui.html changed nothing on the device,
silently: the stale blob still compiled and still served. The /spiffs/ -> /fs/
rename hit exactly that.

**webui.html is itself generated.** The real source is public/src/index.ts:

    index.ts --tsc--> index.js --build.js--> webui.html --this--> webui_html.h

`npm run build` in public/ runs the whole chain, and this script covers only its
last step -- a Python equivalent of public/scripts/html2h.sh, which needs node
and xxd that the nix dev shell does not provide. Editing webui.html directly
works until someone runs the real build. See CLAUDE.md, "The web UI is generated
four times over".

`make -C build webui` regenerates; `make -C build webui-check` fails if the
header is out of date, which is the half that catches the mistake. It cannot
check the earlier links.

Deterministic on purpose: mtime is zeroed so identical input gives an identical
header, and a diff means the UI really changed. html2h.sh does not do this --
gzip stamps the current time -- so their outputs differ byte-wise for identical
input, which is why --check compares decompressed content instead.
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
