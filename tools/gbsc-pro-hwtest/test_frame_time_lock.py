"""The frame time lock's option has to reach the lock at runtime.

Frame time lock is the only thing on the board that steers the output field
rate towards the source's, so whether it is running is a question about the
picture. The option that selects it is read in `loop()` behind eight further
conditions, and until the gate said which one was shut, turning the option on
and seeing nothing happen was indistinguishable from the lock being useless --
which is the reading a session spent an evening on.
"""
import time

import pytest

from gbs_unit import get


# The heartbeat's fifth character carries the option bits, and frame time lock
# is bit 1 of it. Read rather than assumed: /sc?W toggles, so a test that does
# not know where it started cannot put it back.
HEARTBEAT_OPTIONS_INDEX = 4
FRAME_TIME_LOCK_BIT = 1 << 1

LOCK_LINE = "frame time lock:"
OPTION_OFF = "frame time lock: the option is off"
RUNNING = "frame time lock: running"


def _frame_time_lock_selected(console, seconds=3.0):
    """Whether the option is on, off the heartbeat rather than the flash: /sc?W
    changes RAM only, so /preferencesv2.txt answers about the last save."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if console.status:
            latest = console.status[-1]
            if len(latest) > HEARTBEAT_OPTIONS_INDEX:
                return bool(ord(latest[HEARTBEAT_OPTIONS_INDEX]) & FRAME_TIME_LOCK_BIT)
        time.sleep(0.25)
    pytest.skip("no heartbeat in 3 s, so the option's state cannot be read")


def _select_frame_time_lock(host, console, wanted):
    """Put the option where the test wants it. /sc?W is the RAM-only toggle, so
    a run leaves the saved preferences exactly as it found them."""
    if _frame_time_lock_selected(console) != wanted:
        get(host, "/sc?W")
        time.sleep(1.0)
    assert _frame_time_lock_selected(console) == wanted, (
        f"/sc?W did not put the option {'on' if wanted else 'off'}"
    )


def _wait_for_lock_line(console, seconds=12.0):
    """The gate's own report of what it is doing, or None."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        for line in reversed(console.lines):
            if LOCK_LINE in line:
                return line[line.index(LOCK_LINE):].strip()
        time.sleep(0.5)
    return None


def test_the_option_reaches_the_lock_at_runtime(host, console):
    """Toggling at runtime has to reach the lock, in both directions.

    The failure this guards is a dead option: the flag was read by a gate no
    console line described and by an arming function whose body was empty, so
    turning it on changed one character of the heartbeat and nothing a session
    could observe. What the gate reports while the option is ON is whatever it
    is doing -- running, or the one condition standing in the way -- and the
    requirement here is only that it is no longer "the option is off".
    """
    started_on = _frame_time_lock_selected(console)
    try:
        _select_frame_time_lock(host, console, False)
        console.drain()
        _select_frame_time_lock(host, console, True)

        live = _wait_for_lock_line(console)
        assert live is not None, (
            "the option went on and the lock's gate said nothing. An option "
            "that reaches no gate is not a setting"
        )
        assert live != OPTION_OFF, (
            f"the option went on and the gate still reads {live!r}"
        )

        console.drain()
        _select_frame_time_lock(host, console, False)

        stopped = _wait_for_lock_line(console)
        assert stopped == OPTION_OFF, (
            f"the option went off and the gate said {stopped!r}, so the lock "
            "did not stop steering when the user switched it off"
        )
    finally:
        _select_frame_time_lock(host, console, started_on)


def test_the_lock_runs_on_a_locked_source(host, console, source):
    """Switched on, the lock has to reach the source -- not merely open a gate.

    This could not pass at all until the coast window was placed again: the
    loop asked a held flag whether the video was bypassing the scaler, the flag
    read pass-through on a unit that was scaling, and the coast window that
    FrameSync::init() needs was never written. The gate reported
    `not armed: no coast window` for as long as anyone watched.

    Frame time lock is the only thing on the board that steers the output field
    rate towards the source's, so an option that opens its gate and never
    corrects leaves a beat between the two nothing walks back.
    """
    started_on = _frame_time_lock_selected(console)
    try:
        _select_frame_time_lock(host, console, False)
        console.drain()
        _select_frame_time_lock(host, console, True)

        live = _wait_for_lock_line(console, seconds=30.0)
        assert live == RUNNING, (
            f"the lock reports {live!r} on a locked source. It corrects on its "
            "own interval once armed, so anything else here is a condition it "
            "is waiting on rather than a pause"
        )
    finally:
        _select_frame_time_lock(host, console, started_on)
