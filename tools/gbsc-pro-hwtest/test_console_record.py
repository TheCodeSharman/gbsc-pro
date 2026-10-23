"""The console recorder, against a fake link. No hardware.

It exists because nothing on the board retains the console: a fault provoked at
the bench with no capture attached cannot be read afterwards, which is how one
session's unrepeatable event was lost.
"""

import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import console_record


def stepping(step=0.1):
    """A clock that advances a tick per reading, so a run is bounded."""
    at = [0.0]

    def now():
        at[0] += step
        return at[0]

    return now


def link(*script):
    """A recv that plays `script`, raising Quiet for ever once spent.

    An exception in the script is raised rather than returned, so a test can
    script a quiet moment or a dropped link.
    """
    said = list(script)

    def recv():
        step = said.pop(0) if said else console_record.Quiet()
        if isinstance(step, BaseException):
            raise step
        return step

    return recv, lambda: None


def test_lines_are_timestamped_from_the_start_of_the_run():
    out = io.StringIO()
    kept = console_record.record(
        "unit", out, run_s=2, now=stepping(),
        connect=lambda host: link("first", "second"), pause=lambda _: None)

    assert kept == 2
    body = out.getvalue()
    assert "=== recording unit ===" in body
    assert "first" in body and "second" in body


def test_a_quiet_link_is_not_a_dropped_one():
    """A recv with nothing to say must not end the recording -- the console is
    silent for most of a run, and treating that as a drop reconnects for ever."""
    out = io.StringIO()
    taken = []

    def connect(host):
        taken.append(host)
        return link(console_record.Quiet(), "after the quiet")

    console_record.record("unit", out, run_s=2, now=stepping(),
                          connect=connect, pause=lambda _: None)

    assert "after the quiet" in out.getvalue()
    assert taken == ["unit"]


def test_a_dropped_link_is_taken_again():
    """A restart drops the socket, and a recording that ended there would stop
    part way through the thing it was started for."""
    out = io.StringIO()
    links = []

    def connect(host):
        links.append(host)
        if len(links) == 1:
            return link("before the drop", EOFError())
        return link("after the drop")

    console_record.record("unit", out, run_s=2, now=stepping(),
                          connect=connect, pause=lambda _: None)

    assert len(links) == 2
    assert "before the drop" in out.getvalue()
    assert "after the drop" in out.getvalue()
