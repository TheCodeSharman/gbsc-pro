"""Saving a framing into a slot and getting it back, against a live unit.

A slot used to be a register dump replayed off the filesystem. It now records
the INPUTS to the calculation -- the framing, against the source it was tuned
for -- and the engine solves every register from them on the way back in.
docs/framing-presets.md

Writes flash and moves the picture, so it needs --preset-save and --source, and
puts the framing back however it ends.
"""

import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gbs_unit import (acquired_and_settled, framing_matches, framing_of,
                      framing_settled, fs_dir,
                      fs_read, get, get_json, mode_serv, press,
                      read_fields, read_settings,
                      reset_framing, wait_for, want_route)

# A slot is a named copy of the framing table, so what it holds is a file of its
# own rather than a record inside a shared one. docs/framing-presets.md
def slot_file(slot):
    return f"/slot-{slot}.txt"


# The slot this test uses, as the character /slot/set takes and the index naming
# its file. A is the first of them, so a bench unit that has never had a slot
# chosen is already on it.
SLOT_CHARACTER = "A"
SLOT_INDEX = 0

# The order /slot/set's character maps onto the index the file records, so a
# test can name a slot nothing has been stored in.
SLOT_CHARACTERS = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                   "abcdefghijklmnopqrstuvwxyz0123456789-._~()!*:,")

SAVE_COMMAND = "/uc?4"
LOAD_COMMAND = "/uc?3"

# A command is queued for loop(), so a 200 is not a command that has run.
SETTLE_SECONDS = 6.0


# How long /slot/set is given to answer. It is behind a free-heap gate and the
# margin OSCILLATES ACROSS THE FLOOR rather than sitting below it -- measured,
# six calls four seconds apart gave 200, timeout, 200, timeout, 200, 200 -- and
# a console client costs 0.5..1.2 KB of the few hundred bytes spare. So a 503
# is a window to wait out, not a verdict. docs/known-issues.md
SLOT_SET_SECONDS = 30.0


def select_slot(host, character):
    status, body = 0, ""
    deadline = time.monotonic() + SLOT_SET_SECONDS
    while time.monotonic() < deadline:
        status, body = get(host, f"/slot/set?slot={character}")
        if status == 200:
            time.sleep(1.0)
            return
        time.sleep(2.0)
    raise AssertionError(
        f"could not select slot {character} inside {SLOT_SET_SECONDS:.0f}s: "
        f"{status} {body!r}. A 503 naming low heap is the route's gate, and a "
        f"console or a web UI left attached is what spends the margin.")


def reset_to_default(host):
    """The framing back to the engine's own default, on the SCALING route.

    **A RESET TAKES THE STORED ROUTE WITH IT.** One /framing.txt record per
    source holds the framing, the shape and the route, so forgetting the record
    drops the source onto the default -- pass-through -- where there is no
    capture window and /geometry reports a framing of zeros. Everything in this
    file is about a framing, so the route is put back before one is read.
    """
    # **THE ROUTE IS RE-ANSWERED ON A LATER PASS, so reading it straight after
    # the reset is a race.** Asking the FRAMING instead settles it: a reset that
    # lands on a framing is a reset on the scaling route, because pass-through
    # reports a zero extent and `framing_settled()` refuses one. A reset that
    # answers None is one to put the route back under and take again -- the
    # engine drifted to pass-through seconds after a reset that had already
    # reported 237/688, which read as an empty slot load moving the picture.
    for _ in range(3):
        landed = reset_framing(host)
        if landed is not None:
            return landed
        scaled = want_route(host, False, tries=4, settle=25.0)
        assert scaled, f"the source would not scale, so it has no framing: {scaled}"
    return None


def tune_the_framing(host):
    """A framing well away from the default, so restoring it is visible."""
    for _ in range(5):
        press(host, "I", pixels=40)
        time.sleep(1.5)
    for _ in range(3):
        press(host, "-", pixels=30)
        time.sleep(1.5)
    tuned = wait_for(lambda: framing_settled(host), timeout=20.0)
    assert tuned is not None, "the framing never settled after the presses"
    return tuned


def records_in(host, slot):
    """One slot's records, as {source: [numbers]}, ignoring the file's comments.

    The framing file's own format -- `<lines>@<rate>/<width><hPol><vPol> =
    originH extentH originV extentV [shape] [route]` -- because a slot is a copy
    of it. The source key names the FRAME, so a 311-line count reads 312 here;
    the sync processor counts from zero and nothing downstream corrects twice.
    """
    text = fs_read(host, slot_file(slot)) or ""
    found = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, tail = line.partition("=")
        found[key.strip()] = [int(n) for n in tail.split()]
    return found


def spare_slot(host):
    """A slot holding no framing file, so an empty-slot test is testing that
    rather than whatever an earlier run left."""
    held = set(fs_dir(host) or [])
    for index, character in enumerate(SLOT_CHARACTERS):
        if slot_file(index).lstrip("/") not in {f.lstrip("/") for f in held}:
            return character
    pytest.skip("every slot holds a framing, so none is empty to test with")


@pytest.fixture
def framing_guard(host):
    """Put the framing and the selected slot back however the test ends, so a
    failure part way through does not leave the picture cropped or the unit on a
    slot nobody chose."""
    yield
    reset_to_default(host)
    select_slot(host, SLOT_CHARACTER)


def test_a_slot_gives_back_the_framing_it_was_given(host, source, preset_save,
                                                    framing_guard):
    """The whole point: tune a picture, keep it, and get it back later.

    The engine re-solves every register from the stored proportions, so what
    comes back is the same WINDOW rather than the same register values -- which
    is what lets a framing survive an output resolution change as well.
    """
    select_slot(host, SLOT_CHARACTER)
    reset_to_default(host)
    tuned = tune_the_framing(host)

    assert get(host, SAVE_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    default = reset_to_default(host)
    assert default is not None, "the framing never went back to its default"
    assert not framing_matches(default, tuned), (
        "the reset left the framing where the tuning put it, so nothing below "
        "can tell a restore from a reset that did nothing")

    assert get(host, LOAD_COMMAND)[0] == 200
    back = wait_for(
        lambda: framing_of(get_json(host, "/geometry")[1]) if framing_matches(
            framing_of(get_json(host, "/geometry")[1]), tuned) else None,
        timeout=SETTLE_SECONDS + 10.0)
    assert back is not None, (
        f"slot {SLOT_CHARACTER} did not restore the framing it was given: "
        f"wanted {tuned}, at {framing_of(get_json(host, '/geometry')[1])}")


def test_a_slot_records_the_framing_and_not_the_registers(host, source,
                                                          preset_save,
                                                          framing_guard):
    """What is stored is the proportions the engine holds, against the source it
    measured, so a reader can see what a slot means without decoding register
    values against a chip."""
    select_slot(host, SLOT_CHARACTER)
    reset_to_default(host)
    tune_the_framing(host)

    assert get(host, SAVE_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    at = get_json(host, "/geometry")[1]
    measured = read_fields(host, ["STATUS_SYNC_PROC_VTOTAL"])
    assert measured is not None

    stored = records_in(host, SLOT_INDEX)
    assert stored, (
        f"{slot_file(SLOT_INDEX)} holds no framing for slot {SLOT_CHARACTER}, "
        f"so the save stored nothing: {fs_dir(host)}")

    # The record for the source in force, not whichever the slot holds last:
    # a slot holds one per source. The key names the FRAME, which the sync
    # processor's count is one short of.
    #
    # **ONE LINE COUNT IS NOT ONE SOURCE KEY.** The key carries the sync width
    # and both polarities too, so the bench 312-line source has several
    # entries -- `312@50.08/695??` and `312@50.08/709++` were both held -- and
    # taking the first that counts 312 reads a record this solve did not write.
    # Nothing reports the key, so the proportions are what identify it.
    frame_lines = measured["STATUS_SYNC_PROC_VTOTAL"] + 1
    wanted = [at["poh"], at["peh"], at["pov"], at["pev"]]
    counted = {key: numbers for key, numbers in stored.items()
               if key.startswith(f"{frame_lines}@")}
    assert counted, (
        f"slot {SLOT_CHARACTER} has no record for the {frame_lines}-line "
        f"source the chip is counting: {sorted(stored)}")

    held = {key: numbers for key, numbers in counted.items()
            if numbers[:4] == wanted}
    assert held, (
        f"no record of the {frame_lines}-line source holds the proportions the "
        f"engine reports, {wanted}: "
        f"{ {k: v[:4] for k, v in counted.items()} }")

    for source_key in held:
        assert float(source_key.partition("@")[2].partition("/")[0]) > 0, (
            f"the record names no field rate: {source_key}")


def test_a_slot_holding_nothing_for_this_source_leaves_the_picture_alone(
        host, source, preset_save, framing_guard):
    """A load that finds nothing must do nothing. Loading an empty slot used to
    replay a register dump that did not exist, and there is no useful framing to
    invent in its place."""
    select_slot(host, spare_slot(host))
    before = reset_to_default(host)
    assert before is not None

    assert get(host, LOAD_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    after = framing_of(get_json(host, "/geometry")[1])
    assert framing_matches(after, before), (
        f"loading an empty slot moved the framing from {before} to {after}")


def test_a_slot_route_says_what_it_did(host, console, source, preset_save,
                                       framing_guard):
    """A silent no-op leaves someone pressing save and believing it worked.

    The routes used to save and load 432 register bytes; a user could tell they
    had done something because the picture changed. A save that lands on the
    framing already stored changes nothing visible, so the console is the only
    place it can be reported.
    """
    select_slot(host, SLOT_CHARACTER)
    settings_before = read_settings(host)
    assert settings_before is not None, "could not read the settings file whole"

    # One at a time. /uc? QUEUES a single command character for loop() to pick
    # up, so two requests in flight lose the first -- and a 200 says only that
    # the route was reached. CLAUDE.md, "HTTP answering does not mean the
    # firmware is running".
    # AFTER the slot selection and the settings read, both of which touch
    # flash: the unit resets the console client across one, so a socket opened
    # before them carries nothing and the routes below read as silent.
    console.reopen()

    for route, expected in ((SAVE_COMMAND, "slot save"), (LOAD_COMMAND, "slot load")):
        # The previous route re-detected the source, and /uc? is consumed from
        # loop() -- which spends two 6000 ms waits inside detection, so a
        # command sent there waits longer than any window worth opening.
        assert wait_for(lambda: acquired_and_settled(host), timeout=60.0), (
            f"the engine never settled before {route}, so a command sent here "
            "waits behind detection rather than being reported")
        console.reopen()
        assert get(host, route)[0] == 200, f"{route} did not answer"
        output = console.expect(expected)
        assert [line for line in output if expected in line], (
            f"{route} said nothing; expected a {expected!r} line, or a user "
            f"cannot tell a no-op from a save. Got: {output!r}")

    assert [f for f in (fs_dir(host) or []) if f.endswith("~")] == [], (
        f"a half-written temp file was left behind: {fs_dir(host)}")
    assert read_settings(host) == settings_before, (
        "a slot route changed the settings; the unit may now boot expecting "
        "something that was never saved")


# --- one slot, two sources ---------------------------------------------------

BENCH_MODE = "MODE X320 Y256 C256 F50"
BENCH_LINES = 311
OTHER_MODE = "MODE X640 Y480 C256 F60"
# The sync processor counts from zero, so a 525-line frame reads 524.
OTHER_LINES = 524

# What identifies which source the engine has solved for. The capturable region
# is a property of the source, so it says which one the solve ran against.
#
# **WAIT FOR IT TO CHANGE, never for it to hold still.** Neither this nor the
# framing moves until the engine has measured and re-solved, so "two reads
# agree" is satisfied by the state it is LEAVING -- and the previous source's
# window then reads as the new source's. Both mistakes were made here.
CAPTURABLE_FIELDS = ("ch", "cv")




def capturable(host):
    at = get_json(host, "/geometry")[1]
    return {name: at[name] for name in CAPTURABLE_FIELDS} if at else None


def change_source(where, host, command, lines, was=None):
    """Change the source mode and wait for the ENGINE to have solved for it.

    `was` is the capturable region before the change: with it, this waits for
    the region to move off that value, which is the only evidence the solve ran.
    Without it -- the first change of a run, where nothing is known -- it settles
    for the line count and a pause.
    """
    reply = mode_serv(where, command)
    assert reply.startswith("OK"), f"the source refused {command}: {reply}"

    counted = wait_for(
        lambda: (read_fields(host, ("STATUS_SYNC_PROC_VTOTAL",)) or {}).get(
            "STATUS_SYNC_PROC_VTOTAL") == lines or None,
        timeout=60.0, interval=2.0)
    assert counted, f"the source never settled on {lines} lines"

    if was is None:
        time.sleep(SETTLE_SECONDS)
        return capturable(host)

    region = wait_for(lambda: (lambda now: now if now and now != was else None)(
        capturable(host)), timeout=60.0, interval=2.0)
    assert region is not None, (
        f"the capturable region is still {was} after the source moved to "
        f"{lines} lines, so the engine has not solved for it and anything read "
        "here is the previous source's window")
    return region


@pytest.fixture
def bench_source(request, host):
    """Put the bench mode back however the test ends, and skip without a
    ModeServ to change the source with."""
    where = request.config.getoption("--modeserv")
    yield where
    change_source(where, host, BENCH_MODE, BENCH_LINES)



@pytest.mark.source_mode
def test_one_slot_holds_a_framing_for_each_source(host, source, preset_save,
                                                  bench_source, framing_guard):
    """A slot holds one record per source, and a source mode change picks the
    right one.

    This is what the register dumps did before it -- /preset_ntsc.A and eight
    more per slot -- except that those were keyed by the videoStandardInput
    classification and this is keyed by what the chip measured.
    docs/framing-presets.md
    """
    where = bench_source
    select_slot(host, SLOT_CHARACTER)

    bench = change_source(where, host, BENCH_MODE, BENCH_LINES)
    reset_to_default(host)
    framing_a = tune_the_framing(host)
    assert get(host, SAVE_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    other = change_source(where, host, OTHER_MODE, OTHER_LINES, was=bench)
    reset_to_default(host)
    framing_b = tune_the_framing(host)
    assert get(host, SAVE_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    held = records_in(host, SLOT_INDEX)
    assert len(held) >= 2, (
        f"slot {SLOT_CHARACTER} holds {len(held)} record(s), so the second "
        f"source replaced the first rather than joining it: {sorted(held)}")

    _restores(host, where, BENCH_MODE, BENCH_LINES, other, framing_a,
              "the bench source")
    _restores(host, where, OTHER_MODE, OTHER_LINES, bench, framing_b,
              "the 640x480 source")


def _restores(host, where, command, lines, was, wanted, named):
    change_source(where, host, command, lines, was=was)
    default = reset_to_default(host)
    assert default is not None, f"the framing never went back to default on {named}"
    assert not framing_matches(default, wanted), (
        f"the default framing on {named} is the tuned one, so nothing below can "
        "tell a restore from a reset that did nothing")

    assert get(host, LOAD_COMMAND)[0] == 200
    back = wait_for(
        lambda: framing_matches(framing_of(get_json(host, "/geometry")[1]), wanted)
                or None,
        timeout=SETTLE_SECONDS + 10.0)
    assert back, (
        f"slot {SLOT_CHARACTER} did not give {named} back its own framing: "
        f"wanted {wanted}, at {framing_of(get_json(host, '/geometry')[1])}")


def test_selecting_a_slot_restores_what_it_holds(host, source, preset_save,
                                                 framing_guard):
    """Choosing a slot IS loading it. A slot keeps the user's framing for every
    source it was stored against, so arriving on one has to put the picture
    where that slot left it -- without a second command nobody knows to send.
    """
    select_slot(host, SLOT_CHARACTER)
    reset_to_default(host)
    tuned = tune_the_framing(host)

    assert get(host, SAVE_COMMAND)[0] == 200
    time.sleep(SETTLE_SECONDS)

    default = reset_to_default(host)
    assert default is not None, "the framing never went back to its default"
    assert not framing_matches(default, tuned), (
        "the reset left the framing where the tuning put it, so nothing below "
        "can tell a restore from a reset that did nothing")

    select_slot(host, SLOT_CHARACTER)
    back = wait_for(
        lambda: framing_of(get_json(host, "/geometry")[1]) if framing_matches(
            framing_of(get_json(host, "/geometry")[1]), tuned) else None,
        timeout=SETTLE_SECONDS + 10.0)
    assert back is not None, (
        f"selecting slot {SLOT_CHARACTER} did not restore its framing: "
        f"wanted {tuned}, at {framing_of(get_json(host, '/geometry')[1])}")
