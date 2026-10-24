"""What register differs between acquisitions whose best ADC phase differs?

The ADC's optimum moves across the whole field between acquisitions of one
unchanged mode. The chosen PA_SP_S does not predict it, the scan's dither band
does not predict it, and no test-bus selector tracks it. This stops guessing
which register it is and asks all 1536.

Per trial: re-acquire, dump every register, then sweep the ADC phase and find
the best. Afterwards the dumps are compared, grouped by how far apart the
optima were.

Binary outcome. If some register differs in step with the optimum, the engine
wrote it and the phase is derivable. If the dumps are byte-identical across
acquisitions whose optima are a third of the field apart, the cause is not in
the register space -- nothing on the chip can observe it, no derivation can
exist, and no search can score it.
"""
import sys
import time
import urllib.request

sys.path.insert(0, '/home/msharman/Projects/gbsc-pro/tools/gbsc-pro-hwtest')
import numpy as np

import snapdiff
import wedge
from hdmi_capture import BLACK, device, frames, luma

HOST = '192.168.88.108'
MODESERV = '192.168.88.10'
MODE = "MODE X1280 Y720 C256 F60"
OTHER = "MODE X800 Y600 C256 F60"
WIDTH, HEIGHT = 1920, 1080
CLIP = 10
ACQUIRE_S = 22
TRIALS = int(sys.argv[1]) if len(sys.argv) > 1 else 6
PHASE_STEP = 2

import socket


def modeserv(command):
    link = socket.create_connection((MODESERV, 6502), timeout=10)
    link.sendall((command + "\n").encode())
    out = b""
    while True:
        chunk = link.recv(4096)
        if not chunk:
            break
        out += chunk
    link.close()
    return out.decode(errors="replace").strip()


def get(path):
    return urllib.request.urlopen(f"http://{HOST}{path}", timeout=10).read().decode()


def lit_box(grey):
    c = np.where(grey.mean(axis=0) > BLACK)[0]
    r = np.where(grey.mean(axis=1) > BLACK)[0]
    return int(c[0]), int(c[-1]), int(r[0]), int(r[-1])


dev = device()
phases = list(range(0, 32, PHASE_STEP))
trials = []
for trial in range(TRIALS):
    get("/phase?clear")
    modeserv(OTHER)
    time.sleep(ACQUIRE_S)
    modeserv(MODE)
    modeserv("PATTERN PM5544")
    modeserv("ANIM OFF")
    time.sleep(ACQUIRE_S)

    dump = snapdiff.capture(HOST)
    anchor = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=20))
    try:
        box = wedge.locate(anchor[0], lit_box(anchor[0]))
    except AssertionError as why:
        print(f"trial {trial + 1}: no grating, skipped -- {why}")
        continue
    scores = []
    for phase in phases:
        get(f"/phase?adc={phase}")
        time.sleep(0.7)
        greys = luma(frames(CLIP, dev, WIDTH, HEIGHT, warmup=6))
        scores.append(wedge.score(greys, box)[0])
    best = phases[int(np.argmax(scores))]
    trials.append((best, dump, max(scores), min(scores), box))
    print(f"trial {trial + 1}: best phase {best:2d}  peak {max(scores):5.1f}  "
          f"trough {min(scores):5.1f}  box x{box[0]}")

get("/phase?clear")

if len(trials) < 2:
    print("not enough trials")
    sys.exit(0)

print("\nbest phases:", [t[0] for t in trials])
# The pair furthest apart on the field, which is where a cause must show.
worst = max(((i, j) for i in range(len(trials)) for j in range(i + 1, len(trials))),
            key=lambda p: min(abs(trials[p[0]][0] - trials[p[1]][0]),
                              32 - abs(trials[p[0]][0] - trials[p[1]][0])))
a, b = worst
print(f"\ncomparing trial {a + 1} (best {trials[a][0]}) with "
      f"trial {b + 1} (best {trials[b][0]})")

register_map = snapdiff.load_map()
# diff() prints its own report and returns nothing.
snapdiff.diff(trials[a][1], trials[b][1], register_map, include_volatile=False)

print("\nAND EVERY PAIR, so a register that moves WITH the optimum is told from "
      "one that merely moved once:")
for i in range(len(trials)):
    for j in range(i + 1, len(trials)):
        gap = min(abs(trials[i][0] - trials[j][0]),
                  32 - abs(trials[i][0] - trials[j][0]))
        print(f"\n  trials {i + 1}-{j + 1}: phase gap {gap:2d}")
        snapdiff.diff(trials[i][1], trials[j][1], register_map,
                      include_volatile=False)

print("\n**A REGISTER THAT DIFFERS IS NOT A REGISTER THAT CAUSES.** Measured: "
      "the pair whose optima were 16 apart differed in PA_SP_S alone, and "
      "holding PA_SP_S across six values moved the optimum by about one step "
      "per step -- far too little to be it. Hold whatever this names and "
      "measure its causal slope before believing it.")
