#!/usr/bin/env python3
"""The most basic end-to-end test of the client against the mock server (client#733).

Launches the client in its own data folder, waits for it to sign in and play, sends
next and pause over the socket, then checks what the server saw and what the client did.
Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import screens

HERE = Path(__file__).resolve().parent
PORT = 8765
BASE = f"http://127.0.0.1:{PORT}"
API_KEY = "mock-server-test-key"
STORED_SESSION = "STORED_SESSION_MUST_NOT_BE_SENT"
STORED_KEY = "STORED_KEY_MUST_NOT_BE_SENT"
REAL_INSTALLS = [Path("/Users/Shared/infinidream.ai"), Path("/Users/Shared/infinidream.ai-stage")]
HEARTBEAT = re.compile(
    r"Heartbeat ticks=(?P<ticks>\d+) .*?timeline=(?P<timeline>[\d.]+) paused=(?P<paused>\d) "
    r"buffering_pause=(?P<buffering>\d) .*?current=(?P<current>\S+)(?: frame=(?P<frame>\d+)/\d+)?")


class Failed(Exception):
    pass


results = []
MAX_BLACK_SECONDS = 5  # Longer than a normal fade.
MAX_FROZEN_SECONDS = 5


def check(name, passed, detail=""):
    results.append(passed)
    print(f"{'PASS' if passed else 'FAIL'}  {name}{f' ({detail})' if detail else ''}", flush=True)


def wait_for(description, condition, timeout):
    """Polls until condition() returns something truthy, and returns it."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = condition()
        if value:
            return value
        time.sleep(0.25)
    raise Failed(f"Timed out after {timeout} s waiting for {description}")


def screen_locked():
    return b"CGSSessionScreenIsLocked" in subprocess.check_output(["ioreg", "-n", "Root", "-d1"])


def snapshot_real_installs():
    """What a test run must not change. Only meaningful with no other client running."""
    state = {}
    for root in REAL_INSTALLS:
        for path in [root / "settings.json", *sorted((root / "Logs").glob("*.log"))]:
            if path.exists():
                stat = path.stat()
                state[str(path)] = (stat.st_mtime, stat.st_size)
        mp4s = root / "content" / "mp4"
        if mp4s.exists():
            state[str(mp4s)] = sorted(p.name for p in mp4s.iterdir())
    return state


class Run:
    def __init__(self, work, app, media_dir):
        self.work = Path(work)
        self.data = self.work / "data"
        self.event_path = self.work / "events.jsonl"
        self.client_output = self.work / "client-output.log"
        self.app = app
        self.media_dir = media_dir
        self.server = None
        self.client = None
        self.watcher = None
        self.marks = {}

    def events(self, kind=None):
        if not self.event_path.exists():
            return []
        events = [json.loads(line) for line in self.event_path.read_text().splitlines() if line.strip()]
        return [e for e in events if kind is None or e["kind"] == kind]

    def log_text(self):
        return "".join(p.read_text(errors="replace") for p in sorted((self.data / "Logs").glob("*.log"))) \
            if (self.data / "Logs").exists() else ""

    def heartbeats(self):
        # Read the log file, not the console output, which arrives in delayed chunks. The
        # client may be partway through writing the last line, so leave that out.
        complete = self.log_text().rpartition("\n")[0]
        return [m.groupdict() for m in HEARTBEAT.finditer(complete)]

    def send(self, event):
        request = urllib.request.Request(f"{BASE}/control/send/{event}", method="POST")
        sockets = json.load(urllib.request.urlopen(request))["sockets"]
        if sockets != 1:
            raise Failed(f"Sent {event} to {sockets} sockets, expected 1")

    def start(self):
        self.data.mkdir(parents=True)
        # Stored credentials that must be ignored, because the server is plain http.
        # Without firsttimesetup, the Mac client hides the player view and playback never starts.
        (self.data / "settings.json").write_text(json.dumps({"settings": {
            "app": {"firsttimesetup": True},
            "content": {"server": BASE, "sealed_session": STORED_SESSION, "api_key": STORED_KEY}}}))
        self.server = subprocess.Popen([
            sys.executable, str(HERE / "server.py"), "--media-dir", str(self.media_dir),
            "--port", str(PORT), "--event-log", str(self.event_path)])

        def healthy():
            try:
                return json.load(urllib.request.urlopen(f"{BASE}/control/health", timeout=1))
            except OSError:
                return None

        self.dreams = wait_for("the server to start", healthy, 60)["dreams"]
        env = {**os.environ, "INFINIDREAM_DATA_DIR": str(self.data),
               "INFINIDREAM_API_KEY": API_KEY, "INFINIDREAM_HEARTBEAT_SECONDS": "1"}
        with open(self.client_output, "w") as output:
            self.client = subprocess.Popen([str(self.app)], env=env, stdout=output,
                                           stderr=subprocess.STDOUT)
        if screens.can_capture():
            self.watcher = screens.ScreenWatcher(self.client.pid, self.work / "screens")
            self.watcher.start()

    def alive(self):
        return self.client.poll() is None

    def stop(self):
        exit_code = None
        if self.watcher:
            self.watcher.stop()
        if self.client and self.alive():
            self.client.send_signal(signal.SIGTERM)
            try:
                exit_code = self.client.wait(15)
            except subprocess.TimeoutExpired:
                print("Client ignored SIGTERM for 15 s; killing it", flush=True)
                self.client.kill()
                self.client.wait()
        if self.server:
            self.server.terminate()
            self.server.wait()
        return exit_code


def scenario(run):
    def require_alive():
        if not run.alive():
            raise Failed(f"Client exited with {run.client.returncode}")

    def hello_and_socket():
        require_alive()
        hello = any("/client/hello" in e["path"] for e in run.events("http"))
        connected = any(e["data"].startswith("40/remote-control") for e in run.events("ws_recv"))
        return hello and connected

    wait_for("the client's hello and socket connect", hello_and_socket, 60)
    check("Client said hello over http and connected over ws", True)

    def playing():
        require_alive()
        beats = run.heartbeats()
        playing_beats = [b for b in beats if b["frame"] and b["current"] != "-"]
        return len(playing_beats) >= 3 and int(playing_beats[-1]["frame"]) != int(playing_beats[-3]["frame"])

    wait_for("heartbeats with advancing frames", playing, 120)
    current = run.heartbeats()[-1]["current"]
    check("Client is playing: heartbeat frames advance", True, f"current {current[:8]}")
    check("Current dream is one the server offered", current in run.dreams)
    run.marks["playing"] = time.time()
    time.sleep(6)
    current = run.heartbeats()[-1]["current"]

    run.send("next")
    def moved_on():
        require_alive()
        beat = run.heartbeats()[-1]
        return beat["current"] != current and beat["current"] != "-"
    wait_for("next to change the current dream", moved_on, 15)
    check("next changed the current dream", "Received WebSocket message: next" in run.log_text(),
          f"from {current[:8]} to {run.heartbeats()[-1]['current'][:8]}")

    run.marks["pause_sent"] = time.time()
    run.send("pause")
    wait_for("pause to show in the heartbeat", lambda: run.heartbeats()[-1]["paused"] == "1", 15)
    time.sleep(8)  # Long enough for several screenshots of the settled picture.
    beats = run.heartbeats()[-3:]
    frozen = len({b["frame"] for b in beats}) == 1 and all(b["paused"] == "1" for b in beats)
    check("pause stopped the frames, and heartbeats continued", frozen,
          "frames " + ", ".join(str(b["frame"]) for b in beats))

    run.marks["resume_sent"] = time.time()
    run.send("pause")
    wait_for("the second pause to resume", lambda: run.heartbeats()[-1]["paused"] == "0", 15)
    resumed_at = run.heartbeats()[-1]["frame"]
    wait_for("frames to advance after resuming",
             lambda: run.heartbeats()[-1]["frame"] != resumed_at, 15)
    check("Second pause resumed playback", True)
    time.sleep(6)
    run.marks["end"] = time.time()
    require_alive()


def verify(run, before):
    http = run.events("http")
    socket_opens = run.events("ws_open")

    sent = json.dumps(http + socket_opens)
    check("Stored login was never sent", STORED_SESSION not in sent and STORED_KEY not in sent)
    api_calls = [e for e in http if e["path"].startswith("/api/")]
    check("Every API call used the env key",
          bool(api_calls) and all(e["api_key"] == API_KEY for e in api_calls),
          f"{len(api_calls)} calls")
    check("Socket connected with the env key",
          bool(socket_opens) and all(e["api_key"] == API_KEY for e in socket_opens))
    check("Stored settings were left alone", all(
        s in (run.data / "settings.json").read_text() for s in (STORED_SESSION, STORED_KEY)))

    unexpected = [e for e in http if e["status"] >= 400]
    check("Server returned no errors", not unexpected, json.dumps(unexpected[:3]) if unexpected else "")
    unhandled = run.events("unhandled")
    check("Client asked for nothing the server lacks", not unhandled,
          ", ".join(sorted({e["path"] for e in unhandled})))
    media = [e for e in http if e["path"].startswith("/media/")]
    check("Client downloaded media over http", bool(media), f"{len(media)} requests")

    records = {}
    for path in (run.data / "content" / "json").rglob("*.json"):
        try:
            for dream in json.loads(path.read_text()).get("data", {}).get("dreams", []):
                records[dream["uuid"]] = dream
        except (ValueError, AttributeError):
            pass
    cached = list((run.data / "content" / "mp4").glob("*.mp4"))
    wrong = [p.name for p in cached if p.stem in records
             and hashlib.md5(p.read_bytes()).hexdigest() != records[p.stem]["md5"]]
    checked = sum(p.stem in records for p in cached)
    check("Finished downloads match their md5", checked > 0 and not wrong,
          f"{checked} checked" + (f", wrong: {wrong}" if wrong else ""))

    check("Client's log is in the test folder", "Attaching Log" in run.log_text())
    changed = [k for k, v in snapshot_real_installs().items() if before.get(k) != v]
    check("Real installs were not changed", not changed, ", ".join(changed))


def verify_screen(run):
    """Checks what reached the screen, independently of what the client logged."""
    if not run.watcher:
        print("SKIP  Screen checks: this terminal has no Screen Recording permission "
              "(System Settings > Privacy & Security)", flush=True)
        return
    watcher = run.watcher
    brightness = [round(s.brightness) for s in watcher.samples]
    print(f"Screenshots: {len(brightness)} taken, {watcher.failures} failed, "
          f"brightness {min(brightness, default=0)}-{max(brightness, default=0)}", flush=True)
    check("Client's window showed a picture",
          any(not screens.is_black(s) for s in watcher.samples))

    marks = run.marks
    if "end" not in marks:
        return  # The scenario failed before playback finished, so nothing to compare.
    playing = (watcher.between(marks["playing"], marks["pause_sent"])
               + watcher.between(marks["resume_sent"] + 1, marks["end"]))
    # Each stretch of playback is judged on its own, so the pause doesn't count as frozen.
    before_pause = watcher.between(marks["playing"], marks["pause_sent"])
    after_pause = watcher.between(marks["resume_sent"] + 1, marks["end"])
    black = max(screens.longest_black_run(before_pause), screens.longest_black_run(after_pause))
    frozen = max(screens.longest_frozen_run(before_pause), screens.longest_frozen_run(after_pause))
    check("No long black stretch during playback", bool(playing) and black <= MAX_BLACK_SECONDS,
          f"{len(playing)} screenshots, longest {black:.1f} s")
    check("Picture kept changing during playback", len(playing) >= 5 and frozen <= MAX_FROZEN_SECONDS,
          f"longest unchanged {frozen:.1f} s")

    # Allow time for the pause to take effect and for any on-screen message to settle.
    paused = watcher.between(marks["pause_sent"] + 3, marks["resume_sent"])
    biggest = max((screens.difference(a, b) for a, b in zip(paused, paused[1:])), default=None)
    check("Picture held still while paused",
          len(paused) >= 2 and biggest < screens.CHANGE_THRESHOLD,
          f"{len(paused)} screenshots, biggest change {biggest if biggest is None else round(biggest, 2)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    parser.add_argument("--media-dir", default="/Users/Shared/infinidream.ai/content/mp4")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()

    if screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-e2e-")
    print(f"Run folder: {work}", flush=True)
    before = snapshot_real_installs()
    run = Run(work, args.app, args.media_dir)
    try:
        run.start()
        scenario(run)
    except Failed as e:
        check("Scenario", False, str(e))
    finally:
        exit_code = run.stop()
    if run.client:
        print(f"Client exit code after SIGTERM: {exit_code}", flush=True)
        verify(run, before)
        verify_screen(run)

    passed = bool(results) and all(results)
    print(f"\n{sum(results)} of {len(results)} checks passed", flush=True)
    if passed and not args.keep:
        shutil.rmtree(work)
    else:
        print(f"Artifacts kept in {work}", flush=True)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
