"""Client for a running gbs-control unit: HTTP control surface and the status
WebSocket. See docs/gbs-control-debug-interface.md for the surface itself."""

import json
import re
import threading
import time
import urllib.error
import urllib.request

import websocket  # websocket-client, from the repo's nix dev shell


def get(host, path, timeout=5):
    """GET, returning (status, body). A dead host or an HTTP error is a return
    value, not an exception; status 0 means the request never completed."""
    try:
        with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except (urllib.error.URLError, OSError) as e:
        return 0, str(e)


def get_json(host, path, timeout=5):
    """GET returning (status, parsed-or-None)."""
    status, body = get(host, path, timeout)
    try:
        return status, json.loads(body)
    except ValueError:
        return status, None


class Console:
    """The unit's status WebSocket, created once per session because each client
    costs heap. The server accepts five (WEBSOCKETS_SERVER_CLIENT_MAX).

    Frames starting '#' are status (preset, slot, options); everything else is
    terminal text, i.e. whatever SerialM broadcast.
    """

    def __init__(self, host, timeout=5):
        self.lines = []
        self.status = []
        self._buf = ""
        self._lock = threading.Lock()
        self._stop = False
        self.ws = websocket.create_connection(
            f"ws://{host}:81/", subprotocols=["arduino"], timeout=timeout
        )
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _pump(self):
        while not self._stop:
            try:
                frame = self.ws.recv()
            except Exception:
                return
            if isinstance(frame, bytes):
                frame = frame.decode("utf-8", "replace")
            with self._lock:
                if frame.startswith("#"):
                    self.status.append(frame)
                    continue
                self._buf += frame
                while "\n" in self._buf:
                    line, self._buf = self._buf.split("\n", 1)
                    self.lines.append(line.rstrip("\r"))

    def drain(self):
        """Forget anything received so far, so collect() sees only what follows."""
        with self._lock:
            self.lines.clear()

    def collect(self, seconds=2.5):
        """Wait, then return the lines received since the last drain()."""
        time.sleep(seconds)
        with self._lock:
            return list(self.lines)

    def close(self):
        self._stop = True
        try:
            self.ws.close()
        except Exception:
            pass


# --- registers --------------------------------------------------------------


def read_reg(host, segment, register):
    """One register byte via /getreg, or None if the request did not succeed."""
    status, payload = get_json(host, f"/getreg?s={segment:x}&r={register:02x}")
    if status != 200 or not payload:
        return None
    try:
        return int(payload["value"], 16)
    except (KeyError, ValueError):
        return None


def write_reg(host, segment, register, value):
    """Write one register byte. Returns the parsed /setreg payload, or None."""
    status, payload = get_json(
        host, f"/setreg?s={segment:x}&r={register:02x}&v={value:02x}"
    )
    return payload if status == 200 else None


def read_word(host, segment, low_register, mask):
    """A little-endian register pair, masked to the field's bit width. The TV5725
    spreads a multi-bit field across consecutive bytes low-first."""
    low = read_reg(host, segment, low_register)
    high = read_reg(host, segment, low_register + 1)
    if low is None or high is None:
        return None
    return (low | (high << 8)) & mask


# --- console output ---------------------------------------------------------

_TIMING_ROW = re.compile(r"^(\S[^:]*?)\s*:\s*(\d+)(?:\s+(\d+))?\s*$")


def parse_timings(lines):
    """The 'label : value [value]' rows printVideoTimings() emits, as
    {label: (value, ...)}. Lines that are not timing rows are ignored."""
    rows = {}
    for line in lines:
        match = _TIMING_ROW.match(line)
        if match:
            rows[match.group(1).strip()] = tuple(
                int(g) for g in match.groups()[1:] if g is not None
            )
    return rows


# --- filesystem -------------------------------------------------------------


def spiffs_dir(host, attempts=3):
    """SPIFFS listing, or None. Retried because /spiffs/dir calls delay(1) inside
    an async handler and intermittently drops the first request after WebSocket
    traffic — upstream behaviour, unrelated to what these tests cover."""
    for attempt in range(attempts):
        status, payload = get_json(host, "/spiffs/dir", timeout=15)
        if status == 200 and payload is not None:
            return payload
        if attempt + 1 < attempts:
            time.sleep(2)
    return None


def spiffs_read(host, path, timeout=15):
    """Fetch a file off the unit as text, or None."""
    status, body = get(host, f"/spiffs/download?f={path}", timeout=timeout)
    return body if status == 200 else None
