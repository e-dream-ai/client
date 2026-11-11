#!/usr/bin/env python3
"""Throttled end-to-end test of the client against the mock server (client#733).

Serves the largest videos over a rate-limited link and changes the rate during the run:
enough for the first dream, then well below it, then enough again. Checks that the client
plays smoothly, rebuffers when starved, recovers, and keeps its render loop running
throughout. Takes about 45 seconds. Mac only for now.
"""
import argparse
import json
import re
import shutil
import sys
import tempfile
import time
import urllib.request
from datetime import datetime

import e2e_test as e
import server

# The client plays dreams at 20 fps by default, whatever the file's own rate.
PLAYBACK_FPS = 20
GOOD_RATE = 2.5   # times the first dream's playback rate: room for prefetching beside it
STARVED_RATE = 0.6  # times the first dream's playback rate: below what it needs
MAX_HEARTBEAT_GAP = 3.0  # seconds; heartbeats come every second
DROP_FRACTION = 0.4  # lower the rate before this share of the first dream can have arrived
MIN_GOOD_SECONDS = 3  # but play at least this long at the good rate first

BEAT = re.compile(
    r"\[(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) - info\]: Heartbeat ticks=(?P<ticks>\d+) "
    r"tick_ms=[\d.]+ avg_tick_ms=[\d.]+ max_tick_ms=(?P<max_tick>[\d.]+) late_ticks=(?P<late>\d+) "
    r"clamped_ticks=(?P<clamped>\d+) lost_ms=(?P<lost>\d+) .*?paused=(?P<paused>\d) "
    r"buffering_pause=(?P<buffering>\d) .*?current=(?P<current>\S+) frame=(?P<frame>\d+)/\d+ "
    r"queue=(?P<queue>\d+) (?P<state>\w+) discarded=(?P<discarded>\d+) short=(?P<short>\d+) "
    r"rebuffers=(?P<rebuffers>\d+)")


def beats(run):
    complete = run.log_text().rpartition("\n")[0]
    found = []
    for match in BEAT.finditer(complete):
        beat = match.groupdict()
        beat["time"] = datetime.strptime(beat["time"], "%Y-%m-%d %H:%M:%S.%f").timestamp()
        found.append(beat)
    return found


def set_rate(bytes_per_second):
    request = urllib.request.Request(
        f"{e.BASE}/control/media-rate?bytes_per_second={int(bytes_per_second)}", method="POST")
    urllib.request.urlopen(request).read()
    print(f"Media limited to {bytes_per_second / 1e3:.0f} KB/s", flush=True)


def rebuffers(beat_list):
    """Rebuffers so far, summed over every clip seen as current."""
    latest = {}
    for beat in beat_list:
        latest[beat["current"]] = int(beat["rebuffers"])
    return sum(latest.values())


def scenario(run, need, download_seconds):
    started_at = time.time()

    def playing():
        found = beats(run)
        return len(found) >= 3 and len({b["frame"] for b in found[-3:]}) == 3

    e.wait_for("the client to start playing", playing, 120)
    first = beats(run)[-1]["current"]
    e.check("Client is playing the first dream", True, first[:8])

    # Starve the client while most of the first dream is still to come. At GOOD_RATE the
    # whole file would take download_seconds even with the link to itself, so by
    # DROP_FRACTION of that time at most that fraction of it can have arrived. Waiting
    # longer lets a fast link finish the file, and then nothing can make the client rebuffer.
    drop_at = started_at + DROP_FRACTION * download_seconds
    time.sleep(max(MIN_GOOD_SECONDS, drop_at - time.time()))
    so_far = beats(run)
    e.check(f"No rebuffering at {GOOD_RATE}x the dream's rate", rebuffers(so_far) == 0,
            f"{rebuffers(so_far)} rebuffers")

    set_rate(need * STARVED_RATE)
    starved_at = time.time()
    e.wait_for("the client to rebuffer", lambda: rebuffers(beats(run)) > 0, 120)
    e.check(f"Client rebuffered at {STARVED_RATE}x the dream's rate", True,
            f"{time.time() - starved_at:.0f} s after the drop")
    time.sleep(15)

    set_rate(need * GOOD_RATE)
    restored_at = time.time()

    def recovered():
        recent = [b for b in beats(run) if b["time"] > restored_at][-5:]
        return len(recent) == 5 and all(
            b["state"] == "playing" and b["paused"] == "0" for b in recent) and \
            len({b["frame"] for b in recent}) == 5

    e.wait_for("playback to recover", recovered, 90)
    e.check("Playback recovered after the rate went back up", True,
            f"{time.time() - restored_at:.0f} s after the change")


def verify_timing(run):
    found = beats(run)
    gaps = [b["time"] - a["time"] for a, b in zip(found, found[1:])]
    longest = max(gaps, default=0)
    e.check("Heartbeat kept coming while starved", bool(gaps) and longest < MAX_HEARTBEAT_GAP,
            f"longest gap {longest:.1f} s")
    # Reported, not checked: client#747 makes discards and lost time track the rebuffers.
    clips = {}
    for beat in found:
        clips[beat["current"]] = beat
    print(f"Rebuffers {rebuffers(found)}, discarded "
          f"{sum(int(b['discarded']) for b in clips.values())}, late ticks "
          f"{sum(int(b['late']) for b in found)}, clamped ticks "
          f"{sum(int(b['clamped']) for b in found)}, longest tick "
          f"{max((float(b['max_tick']) for b in found), default=0):.0f} ms", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    parser.add_argument("--media-dir", default="/Users/Shared/infinidream.ai/content/mp4")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()

    if e.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    # The server lists dreams in this order, and the client starts with the first one.
    dreams = server.load_dreams(args.media_dir, 3, 900, "largest")
    first = next(iter(dreams.values()))
    need = int(first["size"]) / (first["frames"] / PLAYBACK_FPS)
    download_seconds = int(first["size"]) / (need * GOOD_RATE)
    print(f"First dream {first['uuid'][:8]} needs {need / 1e3:.0f} KB/s; at {GOOD_RATE}x it "
          f"downloads in {download_seconds:.0f} s", flush=True)

    # Screenshots stall the render loop briefly, which would show up as late ticks.
    e.screens.can_capture = lambda: False
    work = tempfile.mkdtemp(prefix="infinidream-throttle-")
    print(f"Run folder: {work}", flush=True)
    before = e.snapshot_real_installs()
    run = e.Run(work, args.app, args.media_dir, server_args=(
        "--pick", "largest", "--media-rate", str(int(need * GOOD_RATE))))
    try:
        run.start()
        scenario(run, need, download_seconds)
    except e.Failed as failure:
        e.check("Scenario", False, str(failure))
    finally:
        exit_code = run.stop()
    if run.client:
        print(f"Client exit code after SIGTERM: {exit_code}", flush=True)
        # The run can end before every download finishes, so don't require one.
        e.verify(run, before, require_download=False)
        verify_timing(run)

    passed = bool(e.results) and all(e.results)
    print(f"\n{sum(e.results)} of {len(e.results)} checks passed", flush=True)
    if passed and not args.keep:
        shutil.rmtree(work)
    else:
        print(f"Artifacts kept in {work}", flush=True)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
