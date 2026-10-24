#!/usr/bin/env python3
"""Certify that the engine did not re-solve while a measurement was taken.

    with resolve_watch.attached(host) as watch:
        ...take the readings...
    print(watch.summary())          # "console quiet", or what landed

**A HELD PHASE IS NOT A CERTIFICATE.** `/phase?adc=`/`?sp=` stop the engine
re-choosing a phase and stop nothing else: an arm takes the sync pad away,
which re-places the sink's own window, and the re-install re-latches the ADC
PLL. So a run that crossed one compares two states however firmly both phases
were held, and the drift reads as the phase's own instability -- two readings of
one held `PA_SP_S` bracketing a six-minute run gave achievable peaks of 60.5
and 76.6 with their optima 24 steps apart. docs/known-issues.md

The console is the only place the arm is visible, and it is cheap: a steady
source says nothing at all between solves, so silence over a run IS the
certificate.
"""

import threading
import time

import console_record

# The console lines that mean a measurement spans two states, and which part
# moved. `measured:` is deliberately absent -- the engine measures a settled
# source without disturbing it, and only the arm above it costs anything.
DISTURBANCES = (
    ("measure: start,", "arm"),
    ("sync pad:", "pad"),
    ("installing: divider", "divider"),
    ("phase scan:", "phase"),
    ("sampling phase:", "phase"),
)


def disturbance(line):
    """Which part of the engine moved, or None for a line that cost nothing."""
    for prefix, part in DISTURBANCES:
        if line.startswith(prefix):
            return part
    return None


class Watch:
    """What the console said during a run, reduced to what it invalidates."""

    def __init__(self):
        self._seen = []
        self._lock = threading.Lock()

    @classmethod
    def over(cls, lines):
        """The watch a run would have produced from `lines`, for a test."""
        watch = cls()
        for line in lines:
            watch.offer(line)
        return watch

    def offer(self, line):
        part = disturbance(line.strip())
        if part is not None:
            with self._lock:
                self._seen.append((time.monotonic(), part, line.strip()))

    def quiet(self):
        with self._lock:
            return not self._seen

    def parts(self):
        with self._lock:
            return [part for _, part, _ in self._seen]

    def lines(self):
        with self._lock:
            return [line for _, _, line in self._seen]

    def summary(self):
        parts = self.parts()
        if not parts:
            return "console quiet"
        counted = {}
        for part in parts:
            counted[part] = counted.get(part, 0) + 1
        return "RUN VOID -- " + ", ".join(
            f"{count}x {part}" for part, count in sorted(counted.items()))


class attached:
    """A Watch fed by the live console for the life of the block.

    The link is the recorder's, so a drop mid-run reconnects rather than
    ending the certificate silently -- a run whose watcher died reads as quiet,
    which is the one failure this must not have.
    """

    def __init__(self, host, connect=console_record._connect):
        self.host = host
        self.watch = Watch()
        self._connect = connect
        self._stop = threading.Event()
        self._thread = None

    def __enter__(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self.watch

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join(timeout=5)
        return False

    def _run(self):
        while not self._stop.is_set():
            try:
                recv, close = self._connect(self.host)
            except Exception:
                self._stop.wait(2)
                continue
            try:
                while not self._stop.is_set():
                    try:
                        self.watch.offer(recv())
                    except console_record.Quiet:
                        continue
                    except Exception:
                        break
            finally:
                try:
                    close()
                except Exception:
                    pass
