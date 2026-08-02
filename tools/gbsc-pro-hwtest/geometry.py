#!/usr/bin/env python3
"""One-shot horizontal geometry snapshot, for pairing with a photograph.

    python3 tools/gbsc-pro-hwtest/geometry.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/geometry.py --host … --label "hscale 420"

Prints the input side, the output side, and what the three horizontal extents
imply about each other -- the scaler's output width, the memory blanking window
it lands in, and the display window actually sent to the TV. Those three can
disagree, and where they disagree is where edge artefacts come from.

Reads each segment in one /getregs burst, so the values in a snapshot are from
the same instant and can be compared against each other. Append --log to keep a
timestamped record next to the photographs.
"""

import argparse
import json
import time
import urllib.request


def burst(host, segment, first, last):
    url = f"http://{host}/getregs?s={segment:x}&from={first:02x}&to={last:02x}"
    with urllib.request.urlopen(url, timeout=8) as response:
        values = json.loads(response.read())["values"]
    return {first + i: int(values[2 * i : 2 * i + 2], 16) for i in range(len(values) // 2)}


def field(registers, register, offset, width):
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        raw |= registers[register + index] << (8 * index)
    return (raw >> offset) & ((1 << width) - 1)


def read_all(host):
    s0 = burst(host, 0, 0x00, 0x1C)
    s1 = burst(host, 1, 0x00, 0x2F)
    s3 = burst(host, 3, 0x00, 0x1F)
    s5 = burst(host, 5, 0x10, 0x4F)
    return {
        # source, as measured
        "STATUS_16": s0[0x16],
        "HPERIOD_IF": field(s0, 0x06, 0, 9),
        "VTOTAL": field(s0, 0x1B, 0, 11),
        "HLOW_LEN": field(s0, 0x19, 0, 12),
        # input side
        "PLLAD_MD": field(s5, 0x12, 0, 12),
        "IF_HSYNC_RST": field(s1, 0x0E, 0, 11),
        "IF_HB_SP2": field(s1, 0x1A, 0, 11),
        "IF_HB_ST2": field(s1, 0x18, 0, 11),
        "SP_RT_HS_SP": field(s5, 0x4B, 0, 12),
        # output side
        "VDS_HSYNC_RST": field(s3, 0x01, 0, 12),
        "VDS_HS_ST": field(s3, 0x0A, 0, 12),
        "VDS_HS_SP": field(s3, 0x0B, 4, 12),
        "VDS_HB_ST": field(s3, 0x04, 0, 12),
        "VDS_HB_SP": field(s3, 0x05, 4, 12),
        "VDS_DIS_HB_ST": field(s3, 0x10, 0, 12),
        "VDS_DIS_HB_SP": field(s3, 0x11, 4, 12),
        "VDS_HSCALE": field(s3, 0x16, 0, 10),
        "VDS_HSCALE_BYPS": field(s3, 0x00, 4, 1),
    }


def report(r, label=None):
    out = []
    add = out.append
    if label:
        add(f"=== {label}")
    add(f"    {time.strftime('%Y-%m-%d %H:%M:%S')}")

    add("\n  SOURCE (measured)")
    add(f"    STATUS_16 0x{r['STATUS_16']:02x}   VTOTAL {r['VTOTAL']}   "
        f"HPERIOD_IF {r['HPERIOD_IF']}{'  RAILED' if r['HPERIOD_IF'] in (0, 511) else ''}   "
        f"sync low {r['HLOW_LEN']} samples")

    line = r["IF_HSYNC_RST"] + 1
    sp2, st2 = r["IF_HB_SP2"], r["IF_HB_ST2"]
    capture = st2 - sp2
    wrapped = capture < 0
    if wrapped:
        capture += line
    add("\n  INPUT SIDE (IF units)")
    add(f"    PLLAD_MD {r['PLLAD_MD']}   IF_HSYNC_RST {r['IF_HSYNC_RST']} "
        f"(line = {line} units){'   [invariant: wants PLLAD_MD/2 = %d]' % (r['PLLAD_MD'] // 2)
           if r['IF_HSYNC_RST'] != r['PLLAD_MD'] // 2 else ''}")
    add(f"    capture {sp2} .. {st2}{'  (wrapped)' if wrapped else ''}   = {capture} units")
    add(f"    SP_RT_HS_SP {r['SP_RT_HS_SP']}   (must stay under PLLAD_MD)")

    htotal = r["VDS_HSYNC_RST"] + 1
    disp_sp, disp_st = r["VDS_DIS_HB_SP"], r["VDS_DIS_HB_ST"]
    mem_sp, mem_st = r["VDS_HB_SP"], r["VDS_HB_ST"]
    display = disp_st - disp_sp
    memory = mem_st - mem_sp
    scale = r["VDS_HSCALE"]
    magnify = None if r["VDS_HSCALE_BYPS"] else 1024 / scale if scale else None
    produced = capture * magnify if magnify else capture

    add("\n  OUTPUT SIDE (real output pixels)")
    add(f"    VDS_HSYNC_RST {r['VDS_HSYNC_RST']} (line = {htotal} px)   "
        f"sync {r['VDS_HS_ST']} .. {r['VDS_HS_SP']}")
    add(f"    memory  blanking active  {mem_sp} .. {mem_st}   = {memory} px")
    add(f"    display blanking active  {disp_sp} .. {disp_st}   = {display} px  "
        f"({display / htotal:.1%} of the line)")
    add(f"    VDS_HSCALE {scale}"
        + ("  BYPASSED (1:1)" if r["VDS_HSCALE_BYPS"] else f"  = x{magnify:.3f}"))

    add("\n  THE THREE EXTENTS, AND WHERE THEY DISAGREE")
    add(f"    scaler produces   {capture} capture units -> {produced:.0f} px")
    add(f"    memory window     {memory} px      -> {produced - memory:+.0f} px vs produced")
    add(f"    display window    {display} px      -> {produced - display:+.0f} px vs produced")
    add(f"    display vs memory  left edge {disp_sp - mem_sp:+d} px, "
        f"right edge {disp_st - mem_st:+d} px")
    if produced > display:
        add(f"    -> picture is WIDER than the display window: "
            f"{produced - display:.0f} px cropped")
    elif produced < display:
        add(f"    -> picture is NARROWER than the display window: "
            f"{display - produced:.0f} px of window with no picture in it")
    add("\n    Note the display window is complementary to display blanking by")
    add("    construction -- the same two registers -- so the picture cannot")
    add("    'extend into' it. Any overhang is against the memory window or the")
    add("    scaler's output width, which are set independently.")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--label", help="what was changed, for pairing with a photo")
    parser.add_argument("--log", help="append the snapshot to this file as well")
    args = parser.parse_args()

    text = report(read_all(args.host), args.label)
    print(text)
    if args.log:
        with open(args.log, "a") as handle:
            handle.write(text + "\n\n")


if __name__ == "__main__":
    main()
