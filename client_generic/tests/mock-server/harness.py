"""Shared parts of the mock server tests (client#733): launching the client against the mock
server, driving it over the socket, and the checks every test makes, including crash and
hang detection. Runs on Mac and Windows; crash reports, stack samples and screenshots are Mac
only.
"""
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from datetime import datetime
from pathlib import Path

import screens
import server

HERE = Path(__file__).resolve().parent
PORT = 8765
BASE = f"http://127.0.0.1:{PORT}"
API_KEY = "mock-server-test-key"
STORED_SESSION = "STORED_SESSION_MUST_NOT_BE_SENT"
STORED_KEY = "STORED_KEY_MUST_NOT_BE_SENT"
IS_MAC = sys.platform == "darwin"
REAL_INSTALLS = server.REAL_INSTALLS
CRASH_REPORTS = Path.home() / "Library" / "Logs" / "DiagnosticReports"

# Hang detection. Heartbeats come every second with INFINIDREAM_HEARTBEAT_SECONDS=1.
MAX_HEARTBEAT_GAP = 3.0  # seconds without a heartbeat
MAX_STUCK_SECONDS = 5.0  # seconds a playing clip's frame may stay the same

BEAT_LINE = re.compile(
    r"^\[(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) - info\]: Heartbeat (?P<fields>.*)$",
    re.MULTILINE)
FIELD = re.compile(r"(\w+)=(\S+)")


class Failed(Exception):
    pass


results = []


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
    if not IS_MAC:
        return False  # Not detected on Windows: a hang there may be a locked screen.
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


def parse_beats(text):
    """Heartbeats in a stretch of client log, as dicts of their fields plus 'time' (epoch seconds).

    Leaves out the last line, which the client may be partway through writing."""
    complete = text.rpartition("\n")[0]
    beats = []
    for match in BEAT_LINE.finditer(complete):
        # Fields after next= describe the next clip and reuse names like frame=, so prefix them.
        beat, prefix = {}, ""
        for key, value in FIELD.findall(match["fields"]):
            beat[prefix + key] = value
            if key == "next" and not prefix:
                prefix = "next_"
        beat["time"] = datetime.strptime(match["time"], "%Y-%m-%d %H:%M:%S.%f").timestamp()
        beats.append(beat)
    return beats


class Watchdog(threading.Thread):
    """Watches the client while a test runs: an unexpected exit, or a hang.

    A hang is no heartbeat for MAX_HEARTBEAT_GAP seconds, or the current clip's frame not
    moving for MAX_STUCK_SECONDS while the client says it is neither paused nor buffering.
    A locked screen stops rendering too, so it's reported as that, not as a hang. On a hang,
    the client's stacks are sampled once into the run folder."""

    def __init__(self, run, max_gap=MAX_HEARTBEAT_GAP, max_stuck=MAX_STUCK_SECONDS):
        super().__init__(daemon=True)
        self.test_run, self.max_gap, self.max_stuck = run, max_gap, max_stuck
        self.stopping = threading.Event()
        self.hang = None  # Description of the first hang seen.
        self.locked = False

    def stop(self):
        self.stopping.set()
        self.join(10)

    def check_once(self, now):
        run = self.test_run
        if not run.alive():
            return
        beats = run.heartbeats()
        if not beats:
            return  # Still starting up.
        last = beats[-1]
        gap = now - last["time"]
        problem = None
        if gap > self.max_gap:
            problem = f"no heartbeat for {gap:.1f} s"
        else:
            stuck_since = None
            for beat in reversed(beats):
                same = (beat.get("current") == last.get("current")
                        and beat.get("frame") == last.get("frame"))
                moving = (beat.get("paused") == "0" and beat.get("buffering_pause") == "0"
                          and beat.get("current", "-") != "-")
                if not (same and moving):
                    break
                stuck_since = beat["time"]
            if stuck_since is not None and last["time"] - stuck_since >= self.max_stuck:
                problem = (f"frame {last.get('frame')} of {last.get('current', '?')[:8]} stuck for "
                           f"{last['time'] - stuck_since:.0f} s while playing")
        if not problem:
            return
        if screen_locked():
            if not self.locked:
                print(f"NOTE  Screen is locked, which stops rendering ({problem})", flush=True)
            self.locked = True
            return
        self.hang = problem
        print(f"HANG  {problem}; sampling the client's stacks", flush=True)
        run.sample_stacks("hang")

    def run(self):  # threading.Thread entry point
        while not self.stopping.wait(0.5) and self.hang is None:
            try:
                self.check_once(time.time())
            except Exception as error:  # Never let the watchdog kill a test.
                print(f"Watchdog error: {error}", flush=True)


class Run:
    def __init__(self, work, app, media_dir, server_args=(), screenshots=True,
                 max_gap=MAX_HEARTBEAT_GAP, max_stuck=MAX_STUCK_SECONDS, content_settings=None):
        self.work = Path(work)
        self.content_settings = content_settings or {}  # Extra settings.content values.
        self.data = self.work / "data"
        self.event_path = self.work / "events.jsonl"
        self.client_output = self.work / "client-output.log"
        self.app = app
        self.media_dir = media_dir
        self.server = None
        self.client = None
        self.watcher = None
        self.watchdog = None
        self.marks = {}
        self.server_args = server_args
        self.screenshots = screenshots
        self.max_gap, self.max_stuck = max_gap, max_stuck
        self.started_at = None
        self.exit_code = None
        self.died_during_run = None  # Exit code if the client exited before the test stopped it.
        self.ignored_sigterm = False
        self.quit_cleanly = None  # Whether the client obeyed the test-only quit command.

    # What the client and server did.

    def events(self, kind=None):
        if not self.event_path.exists():
            return []
        events = [json.loads(line) for line in self.event_path.read_text().splitlines() if line.strip()]
        return [e for e in events if kind is None or e["kind"] == kind]

    def log_text(self):
        return "".join(p.read_text(errors="replace") for p in sorted((self.data / "Logs").glob("*.log"))) \
            if (self.data / "Logs").exists() else ""

    def heartbeats(self):
        # Read the log file, not the console output, which arrives in delayed chunks.
        return parse_beats(self.log_text())

    def alive(self):
        return self.client is not None and self.client.poll() is None

    def require_alive(self):
        """Raises Failed if the client has exited or hung; scenarios call it while waiting."""
        if not self.alive():
            raise Failed(f"Client exited during the run with {self.client.returncode}")
        if self.watchdog and self.watchdog.hang:
            raise Failed(f"Client hung: {self.watchdog.hang}")

    # Driving the client.

    def _control(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(f"{BASE}{path}", data=data, method="POST",
                                         headers={"Content-Type": "application/json"})
        return json.load(urllib.request.urlopen(request))

    def send(self, event, **fields):
        """Sends a remote-control command, such as next or play_dream with uuid=..."""
        sockets = self._control("/control/emit",
                                {"args": ["new_remote_control_event", {"event": event, **fields}]})["sockets"]
        if sockets != 1:
            raise Failed(f"Sent {event} to {sockets} sockets, expected 1")

    def set_media_rate(self, bytes_per_second):
        """Limits all media downloads together to this many bytes a second; 0 removes the limit."""
        self._control(f"/control/media-rate?bytes_per_second={int(bytes_per_second)}")

    def emit(self, args=None, raw=None):
        """Sends any Socket.IO event, or a raw text frame, to the client."""
        body = {"raw": raw} if raw is not None else {"args": args}
        sockets = self._control("/control/emit", body)["sockets"]
        if sockets != 1:
            raise Failed(f"Sent a socket message to {sockets} sockets, expected 1")

    # Starting and stopping.

    def start(self):
        self.data.mkdir(parents=True)
        # Stored credentials that must be ignored, because the server is plain http.
        # Without firsttimesetup, the Mac client hides the player view and playback never starts.
        (self.data / "settings.json").write_text(json.dumps({"settings": {
            "app": {"firsttimesetup": True},
            "content": {"server": BASE, "sealed_session": STORED_SESSION, "api_key": STORED_KEY,
                        **self.content_settings}}}))
        self.server = subprocess.Popen([
            sys.executable, str(HERE / "server.py"), "--media-dir", str(self.media_dir),
            "--port", str(PORT), "--event-log", str(self.event_path), *self.server_args,
            # Last, so they win over the test's own choice of dreams.
            *(("--playlist", PLAYLIST, "--dreams", "0", "--min-frames", "0") if PLAYLIST else ())])

        def healthy():
            if self.server.poll() is not None:
                raise Failed(f"Mock server exited with {self.server.returncode}")
            try:
                return json.load(urllib.request.urlopen(f"{BASE}/control/health", timeout=1))
            except OSError:
                return None

        health = wait_for("the server to start", healthy, 60)
        self.dreams, self.dream_fps = health["dreams"], health["fps"]
        self.dream_frames, self.playlists = health["frames"], health["playlists"]
        env = {**os.environ, "INFINIDREAM_DATA_DIR": str(self.data),
               "INFINIDREAM_API_KEY": API_KEY, "INFINIDREAM_HEARTBEAT_SECONDS": "1"}
        self.started_at = time.time()
        with open(self.client_output, "w") as output:
            self.client = subprocess.Popen([str(self.app)], env=env, stdout=output,
                                           stderr=subprocess.STDOUT)
        if self.screenshots and screens.can_capture():
            self.watcher = screens.ScreenWatcher(self.client.pid, self.work / "screens")
            self.watcher.start()
        self.watchdog = Watchdog(self, self.max_gap, self.max_stuck)
        self.watchdog.start()

    def wait_until_ready(self):
        """Waits for the client's hello and its socket connect on /remote-control."""
        def ready():
            self.require_alive()
            hello = any("/client/hello" in e["path"] for e in self.events("http"))
            connected = any(e["data"].startswith("40/remote-control") for e in self.events("ws_recv"))
            return hello and connected
        wait_for("the client's hello and socket connect", ready, 60)

    def wait_until_playing(self, timeout=120):
        """Waits for three heartbeats of a clip whose frames advance, and returns the last."""
        def playing():
            self.require_alive()
            beats = [b for b in self.heartbeats() if b.get("current", "-") != "-" and "frame" in b]
            if len(beats) >= 3 and beats[-1]["frame"] != beats[-3]["frame"]:
                return beats[-1]
            return None
        return wait_for("heartbeats with advancing frames", playing, timeout)

    def sample_stacks(self, label):
        """Saves a few seconds of the client's stacks, for a hang or a slow exit."""
        if not self.alive() or not IS_MAC:
            return None
        path = self.work / f"stacks-{label}.txt"
        try:
            subprocess.run(["sample", str(self.client.pid), "3", "-file", str(path)],
                           capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"Couldn't sample the client: {error}", flush=True)
            return None
        print(f"Stacks saved to {path}", flush=True)
        return path

    def stop(self, quit_timeout=15):
        """Stops the client and the server.

        Sends the test-only quit command first, the same path as the user quitting. If the
        client doesn't exit within quit_timeout, falls back to SIGTERM, then kills it after
        15 s more. Returns the client's exit code, as Popen reports it: negative for a signal."""
        if self.watchdog:
            self.watchdog.stop()
        if self.watcher:
            self.watcher.stop()
        if self.client:
            if not self.alive():
                self.died_during_run = self.client.returncode
            else:
                self.quit_cleanly = False
                try:
                    self.send("quit")
                    self.client.wait(quit_timeout)
                    self.quit_cleanly = self.client.returncode == 0
                except (Failed, OSError) as error:
                    print(f"Couldn't send quit: {error}", flush=True)
                except subprocess.TimeoutExpired:
                    print(f"Client didn't quit within {quit_timeout} s; sampling it and sending SIGTERM",
                          flush=True)
                    self.sample_stacks("ignored-quit")
            if self.alive():
                self.client.send_signal(signal.SIGTERM)
                try:
                    self.client.wait(15)
                except subprocess.TimeoutExpired:
                    print("Client ignored SIGTERM for 15 s; sampling and killing it", flush=True)
                    self.ignored_sigterm = True
                    self.sample_stacks("ignored-sigterm")
                    self.client.kill()
                    self.client.wait()
            self.exit_code = self.client.returncode
        if self.server:
            self.server.terminate()
            self.server.wait()
        return self.exit_code


# Command-line options every test shares.

DEFAULT_MEDIA_DIR = server.CLIENT_CACHE


# A real playlist of a snapshot set to serve instead of made-up playlists (--playlist).
PLAYLIST = None


def add_media_args(parser):
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--set", help="fixture set to serve (see fixtures.py), by name or path")
    group.add_argument("--media-dir", default=DEFAULT_MEDIA_DIR,
                       help="folder of <uuid>.mp4 to serve; defaults to the alpha client's cache")
    parser.add_argument("--playlist", help="serve only this real playlist of a snapshot --set, all "
                        "its dreams whatever their length, and start the client on it")


def first_dream(media_dir, dreams):
    """The record of the dream the client plays first: the start of the --playlist, or else
    the first dream the server lists."""
    if PLAYLIST:
        import server
        manifest = json.loads((Path(media_dir) / "manifest.json").read_text())
        order = next(p["uuids"] for p in manifest["playlists"] if p["id"] == PLAYLIST)
        records = server.snapshot_records(media_dir)
        return next(records[u] for u in order if u in records)
    return next(iter(dreams.values()))


def load_dreams(media_dir, count, min_frames, pick="smallest"):
    """The dreams a test's server will serve: server.load_dreams, or with --playlist up to count
    of that playlist's dreams of any length."""
    import server
    if PLAYLIST:
        dreams = server.load_dreams(media_dir, 0, 0, pick, PLAYLIST)
        return dict(list(dreams.items())[:count])
    return server.load_dreams(media_dir, count, min_frames, pick)


def resolve_media(args):
    """Points args.media_dir at the chosen fixture set, after checking its md5s."""
    global PLAYLIST
    PLAYLIST = getattr(args, "playlist", None)
    if PLAYLIST and not args.set:
        sys.exit("--playlist needs --set, a snapshot fixture set")
    if PLAYLIST:
        print(f"Serving only playlist {PLAYLIST}", flush=True)
    if not args.set:
        return
    import fixtures
    folder = fixtures.set_dir(args.set)
    problems = fixtures.verify(folder)
    if problems:
        sys.exit(f"Fixture set {folder} failed its check: {'; '.join(problems[:3])}")
    args.media_dir = str(folder)
    print(f"Serving fixture set {folder}", flush=True)


# Checks that apply to every run.

def describe_exit(code):
    if code is None:
        return "still running"
    if code >= 0xC0000000:  # A Windows exception, such as 0xC0000005 for an access violation.
        return f"crashed with exception 0x{code:08X}"
    if code < 0:
        try:
            return f"killed by {signal.Signals(-code).name}"
        except ValueError:
            return f"killed by signal {-code}"
    return f"exit status {code}"


def collect_crash_reports(run, wait=10):
    """Copies the crash reports macOS wrote for this run's client into the run folder.

    ReportCrash writes them a few seconds after the process dies, so this waits up to `wait`
    seconds when the exit looks like a crash. Returns a list of (path, summary)."""
    if not run.client:
        return []
    pid = run.client.pid
    deadline = time.time() + wait
    found = []
    while True:
        for path in CRASH_REPORTS.glob("infinidream*.ips"):
            if path.stat().st_mtime < run.started_at - 1 or any(path == f[0] for f in found):
                continue
            header, _, body = path.read_text(errors="replace").partition("\n")
            try:
                report = json.loads(body)
            except ValueError:
                continue
            if report.get("pid") != pid:
                continue
            copy = run.work / path.name
            shutil.copy(path, copy)
            found.append((path, summarize_crash(report)))
        if found or time.time() > deadline:
            return [(run.work / p.name, s) for p, s in found]
        time.sleep(1)


# The client's own signal handler. When a crash goes through it, the frames just below it are
# where the fault really happened; what's above is the handler's exit() going wrong (#769).
SIGNAL_HANDLER_FRAMES = ("signnal_handler", "_sigtramp")
NOISE_IMAGES = ("libsystem_", "libc++abi", "libobjc")


def _frame_text(frame, images):
    index = frame.get("imageIndex")
    image = images[index].get("name", "?") if index is not None and index < len(images) else "?"
    where = f" {frame['sourceFile']}:{frame['sourceLine']}" if frame.get("sourceFile") else ""
    return image, f"  {image}  {frame.get('symbol', '?')}{where}"


def summarize_crash(report, frames=8):
    """The exception, the original fault if the client's signal handler caught one, and the
    top of the crashed thread's stack, from an .ips report."""
    exception = report.get("exception", {})
    lines = [f"{exception.get('type', '?')} {exception.get('signal', '')}".strip()]
    for text in (report.get("asi") or {}).values():
        lines.append(" ".join(text))
    images = report.get("usedImages", [])
    try:
        for number, thread in enumerate(report.get("threads", [])):
            stack = thread.get("frames", [])
            caught = next((i for i, f in enumerate(stack)
                           if any(s in (f.get("symbol") or "") for s in SIGNAL_HANDLER_FRAMES)), None)
            if caught is None:
                continue
            below = [f for f in stack[caught + 1:]
                     if not any(s in (f.get("symbol") or "") for s in SIGNAL_HANDLER_FRAMES)]
            lines.append(f"original fault, caught by the client's signal handler (thread {number}):")
            lines += [_frame_text(f, images)[1] for f in below[:frames]]
            break
        thread = report["threads"][report["faultingThread"]]
        lines.append(f"crashed thread {report['faultingThread']} "
                     f"{thread.get('name') or thread.get('queue') or ''}".rstrip() + ":")
        shown = 0
        for frame in thread["frames"]:
            image, text = _frame_text(frame, images)
            if image.startswith(NOISE_IMAGES):
                continue
            lines.append(text)
            shown += 1
            if shown == frames:
                break
    except (KeyError, IndexError, TypeError):
        pass
    return "\n".join(lines)


def verify_process(run):
    """Crash and hang verdicts. Call after run.stop()."""
    if not run.client:
        return
    code = run.exit_code
    check("Client kept running until the test stopped it", run.died_during_run is None,
          describe_exit(run.died_during_run) if run.died_during_run is not None else "")
    if run.died_during_run is None:
        check("Client quit cleanly when asked", bool(run.quit_cleanly),
              describe_exit(code) if run.quit_cleanly else
              f"didn't quit; then {describe_exit(code)}" + (" after ignoring SIGTERM" if run.ignored_sigterm else ""))
        if run.quit_cleanly:
            leftovers = sorted(p.name for p in (run.data / "content").rglob("*.tmp"))
            check("No partial downloads left after quitting", not leftovers,
                  ", ".join(leftovers[:5]) + (f" and {len(leftovers) - 5} more" if len(leftovers) > 5 else ""))
    crashy = code is not None and code < 0 and code != -signal.SIGTERM
    reports = collect_crash_reports(run, wait=10 if crashy or run.died_during_run is not None else 2)
    check("No crash report", not reports, ", ".join(p.name for p, _ in reports))
    for path, summary in reports:
        print(f"Crash report {path}:\n{summary}", flush=True)
    swallowed = "RECEIVED SIGSEGV" in run.log_text()
    check("No crash hidden by the client's SIGSEGV handler", not swallowed,
          "the log says RECEIVED SIGSEGV" if swallowed else "")
    hang = run.watchdog.hang if run.watchdog else None
    check("No hang", hang is None, hang or "")


def verify_server(run, before, require_download=True):
    """What the server saw, credentials, downloads and real installs."""
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
    check("Finished downloads match their md5", (checked > 0 or not require_download) and not wrong,
          f"{checked} checked" + (f", wrong: {wrong}" if wrong else ""))

    check("Client's log is in the test folder", "Attaching Log" in run.log_text())
    changed = [k for k, v in snapshot_real_installs().items() if before.get(k) != v]
    check("Real installs were not changed", not changed, ", ".join(changed))


def finish(work, keep):
    """Prints the totals, removes the run folder if everything passed, and exits."""
    passed = bool(results) and all(results)
    print(f"\n{sum(results)} of {len(results)} checks passed", flush=True)
    if passed and not keep:
        shutil.rmtree(work)
    else:
        print(f"Artifacts kept in {work}", flush=True)
    sys.exit(0 if passed else 1)
