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

# The arithmetic and the headroom constants live in geometry_math, so that this
# tool and the code that *chooses* registers cannot come to different conclusions
# about the same hardware. Why HEADROOM_WARN_PX is a warn floor rather than a
# measured boundary is documented there.
import geometry_math as gm

HEADROOM_WARN_PX = gm.HEADROOM_WARN_PX


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


def sample_hperiod(host, reads=8):
    """HPERIOD_IF several times, with the firmware's own validity rule.

    A single read is not trustworthy: the counter rails to 0 or 511 and stays
    there, perfectly steady, so stability is not a validity test. The firmware
    takes eight reads and abandons the batch if any two differ by more than 3;
    this reports the spread instead of hiding it, so a noisy reading is visible
    as noise rather than being quietly averaged into a plausible number.
    """
    values = []
    for _ in range(reads):
        values.append(field(burst(host, 0, 0x06, 0x07), 0x06, 0, 9))
    values.sort()
    median = values[len(values) // 2]
    spread = values[-1] - values[0]
    if median in (0, 511):
        state = "RAILED"
    elif spread > 3:
        state = "UNSETTLED"
    else:
        state = "ok"
    return {"median": median, "spread": spread, "state": state, "n": reads}


def read_all(host, hperiod_reads=8):
    hperiod = sample_hperiod(host, hperiod_reads)
    s0 = burst(host, 0, 0x00, 0x1C)
    s1 = burst(host, 1, 0x00, 0x2F)
    s3 = burst(host, 3, 0x00, 0x1F)
    s5 = burst(host, 5, 0x10, 0x4F)
    return {
        # source, as measured
        "STATUS_16": s0[0x16],
        "HPERIOD_IF": hperiod["median"],
        "HPERIOD_SAMPLE": hperiod,
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
    hp = r["HPERIOD_SAMPLE"]
    add(f"    STATUS_16 0x{r['STATUS_16']:02x}   VTOTAL {r['VTOTAL']}   "
        f"HPERIOD_IF {hp['median']} +/-{hp['spread']} over {hp['n']} reads"
        f"{'  ' + hp['state'].upper() if hp['state'] != 'ok' else ''}   "
        f"sync low {r['HLOW_LEN']} samples")
    if hp["state"] == "RAILED":
        add("      -> the counter is railed, not measuring. /uc?h brings it back.")
    elif hp["state"] == "UNSETTLED":
        add("      -> spread > 3, still settling. Wait a few seconds and re-read.")

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
    magnify = gm.magnification(scale, r["VDS_HSCALE_BYPS"])
    # magnify is None only for a dropped read; fall back to 1:1 so the rest of
    # the report still prints, and say so loudly further down.
    produced = gm.produced_px(capture, scale, r["VDS_HSCALE_BYPS"])
    if produced is None:
        produced = capture

    add("\n  OUTPUT SIDE (real output pixels)")
    add(f"    VDS_HSYNC_RST {r['VDS_HSYNC_RST']} (line = {htotal} px)   "
        f"sync {r['VDS_HS_ST']} .. {r['VDS_HS_SP']}")
    add(f"    memory  blanking active  {mem_sp} .. {mem_st}   = {memory} px")
    add(f"    display blanking active  {disp_sp} .. {disp_st}   = {display} px  "
        f"({display / htotal:.1%} of the line)")
    # A dropped or truncated read shows up as 0, which is not a legal divisor.
    # This print is what a bench session trusts, so say so rather than crash.
    if r["VDS_HSCALE_BYPS"]:
        scale_note = "  BYPASSED (1:1)"
    elif magnify:
        scale_note = f"  = x{magnify:.3f}"
    else:
        scale_note = "  !! reads 0 -- dropped read or cleared register, magnification UNKNOWN"
    add(f"    VDS_HSCALE {scale}{scale_note}")

    add("\n  THE THREE EXTENTS, AND WHERE THEY DISAGREE")
    if magnify is None and not r["VDS_HSCALE_BYPS"]:
        add("    (VDS_HSCALE unreadable -- the figures below assume 1:1 and are NOT")
        add("     the real output width. Re-read before trusting any of this.)")
    add(f"    scaler produces   {capture} capture units -> {produced:.2f} px")
    add(f"    memory window     {memory} px      -> {produced - memory:+.2f} px vs produced")
    add(f"    display window    {display} px      -> {produced - display:+.2f} px vs produced")

    headroom = gm.headroom_px(memory, produced)
    add(f"    headroom          {headroom:+.2f} px   (memory window - produced)")
    if headroom < HEADROOM_WARN_PX:
        safe = gm.smallest_safe_hscale(capture, memory) if magnify else None
        if headroom < 0:
            add(f"    !! OVERFLOW: the product exceeds the memory window by "
                f"{-headroom:.2f} px per line.")
        else:
            add(f"    !! THIN HEADROOM: only {headroom:.2f} px of slack, under the "
                f"{HEADROOM_WARN_PX} px floor.")
        add("       Moving comb bands follow. Fitting is not enough -- the scaler")
        add("       must finish reading the line from memory before the line ends,")
        add("       so it needs slack. How much is not known: the requirement is")
        add("       not monotonic in HSCALE and the corruption has stable bands,")
        add("       so the floor is set well clear rather than at a boundary.")
        if safe:
            add(f"       smallest safe VDS_HSCALE for this capture is {safe} "
                f"(-> {capture * 1024 / safe:.2f} px, "
                f"{memory - capture * 1024 / safe:.2f} px headroom).")
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
