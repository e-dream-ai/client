#!/usr/bin/env python3
"""The most basic end-to-end test of the client against the mock server (client#733).

Launches the client in its own data folder, waits for it to sign in and play, sends
next and pause over the socket, then checks what the server saw and what the client did.
Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import sys
import tempfile
import time

import harness as h
import screens

MAX_BLACK_SECONDS = 5  # Longer than a normal fade.
MAX_FROZEN_SECONDS = 5


def frame_of(beat):
    return int(beat["frame"].split("/")[0])


def scenario(run):
    run.wait_until_ready()
    h.check("Client said hello over http and connected over ws", True)

    current = run.wait_until_playing()["current"]
    h.check("Client is playing: heartbeat frames advance", True, f"current {current[:8]}")
    h.check("Current dream is one the server offered", current in run.dreams)
    run.marks["playing"] = time.time()
    time.sleep(6)
    current = run.heartbeats()[-1]["current"]

    run.send("next")
    def moved_on():
        run.require_alive()
        beat = run.heartbeats()[-1]
        return beat["current"] != current and beat["current"] != "-"
    h.wait_for("next to change the current dream", moved_on, 15)
    h.check("next changed the current dream", "Received WebSocket message: next" in run.log_text(),
            f"from {current[:8]} to {run.heartbeats()[-1]['current'][:8]}")

    run.marks["pause_sent"] = time.time()
    run.send("pause")
    h.wait_for("pause to show in the heartbeat", lambda: run.heartbeats()[-1]["paused"] == "1", 15)
    time.sleep(8)  # Long enough for several screenshots of the settled picture.
    beats = run.heartbeats()[-3:]
    frozen = len({b["frame"] for b in beats}) == 1 and all(b["paused"] == "1" for b in beats)
    h.check("pause stopped the frames, and heartbeats continued", frozen,
            "frames " + ", ".join(str(frame_of(b)) for b in beats))

    run.marks["resume_sent"] = time.time()
    run.send("pause")
    h.wait_for("the second pause to resume", lambda: run.heartbeats()[-1]["paused"] == "0", 15)
    resumed_at = run.heartbeats()[-1]["frame"]
    h.wait_for("frames to advance after resuming",
               lambda: run.heartbeats()[-1]["frame"] != resumed_at, 15)
    h.check("Second pause resumed playback", True)
    time.sleep(6)
    run.marks["end"] = time.time()
    run.require_alive()


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
    h.check("Client's window showed a picture",
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
    h.check("No long black stretch during playback", bool(playing) and black <= MAX_BLACK_SECONDS,
          f"{len(playing)} screenshots, longest {black:.1f} s")
    h.check("Picture kept changing during playback", len(playing) >= 5 and frozen <= MAX_FROZEN_SECONDS,
          f"longest unchanged {frozen:.1f} s")

    # Allow time for the pause to take effect and for any on-screen message to settle.
    paused = watcher.between(marks["pause_sent"] + 3, marks["resume_sent"])
    biggest = max((screens.difference(a, b) for a, b in zip(paused, paused[1:])), default=None)
    h.check("Picture held still while paused",
          len(paused) >= 2 and biggest < screens.CHANGE_THRESHOLD,
          f"{len(paused)} screenshots, biggest change {biggest if biggest is None else round(biggest, 2)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-e2e-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir)
    try:
        run.start()
        scenario(run)
    except h.Failed as e:
        h.check("Scenario", False, str(e))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before)
        verify_screen(run)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
