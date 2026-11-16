#!/usr/bin/env python3
"""Resuming from a pause, or speeding up, doesn't jump the timeline (client#747).

Lets the client download every dream, so no rebuffer gets in the way, then pauses and
resumes it a number of times. While paused the render loop keeps ticking but the player's
frame clock used to stand still, so the first tick after a resume saw the whole pause as
one long frame: it was clamped to 100 ms, the timeline jumped ahead by that much, and the
clip skipped a frame. The heartbeat counts those ticks (clamped_ticks) and the time they
lost beyond 100 ms (lost_ms). Each resume used to add one clamped tick and about the
length of the pause in lost_ms; now neither may grow.

A pause for buffering takes the same path, so this covers rebuffers too.

Then it switches between speed 1 (2 fps) and speed 9 (32 fps) a number of times. The clip
carries the part of a frame interval it has played so far from tick to tick, and that used
to be kept in seconds: up to half a second at 2 fps, which at 32 fps is up to 16 frames
owed at once. So going to 9 jumped ahead, discarding all but 4 of them. Now the clip keeps
its place within the frame across a change of rate, and no frame may be discarded.

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import sys
import tempfile
import time

import harness as h

CYCLES = 6
PAUSE_SECONDS = 1.5   # long enough to be well past the 100 ms clamp
PLAY_SECONDS = 2.5    # a couple of heartbeats of playing between pauses
# A busy machine can hitch a tick or two on its own; a jump on every resume is the bug.
MAX_CLAMPED = 1
# At speed 1 a frame lasts 0.5 s. A whole number of frames would land every switch at the same
# point within a frame, so add a different part of a frame each time, as a hand on a key would.
SLOW_SECONDS = 3.0
SLOW_OFFSETS = [0.05, 0.13, 0.21, 0.29, 0.37, 0.45]
FAST_SECONDS = 2.0


def cached(run):
    folder = run.data / "content" / "mp4"
    return {p.stem for p in folder.glob("*.mp4")} if folder.exists() else set()


def toggle(run, paused):
    run.send("pause")
    want = "1" if paused else "0"
    h.wait_for(f"the heartbeat to show paused={want}",
               lambda: run.require_alive() or run.heartbeats()[-1]["paused"] == want, 15)


def discarded_since(beats):
    """Frames discarded during these heartbeats, over every clip they show (the counter is
    per clip, so take each clip's growth separately)."""
    first, last = {}, {}
    for b in beats:
        for clip, prefix in (("current", ""), ("next", "next_")):
            uuid, count = b.get(clip), b.get(prefix + "discarded")
            if uuid and uuid != "-" and count is not None:
                first.setdefault(uuid, int(count))
                last[uuid] = int(count)
    return sum(last[u] - first[u] for u in last)


def speed_phase(run):
    start = len(run.log_text())
    for offset in SLOW_OFFSETS:
        run.send("set_speed_1")
        time.sleep(SLOW_SECONDS + offset)
        run.send("set_speed_9")
        time.sleep(FAST_SECONDS)
        run.require_alive()
    run.send("set_speed_1")  # a heartbeat that covers the last switch
    time.sleep(2)
    log = run.log_text()[start:]
    switches = log.count("Received WebSocket message: set_speed_9")
    h.check(f"Switched from speed 1 to 9 {len(SLOW_OFFSETS)} times", switches == len(SLOW_OFFSETS),
            f"{switches} logged")
    beats = h.parse_beats(log)
    discarded = discarded_since(beats)
    clamped = sum(int(b.get("clamped_ticks", 0)) for b in beats)
    h.check("No frame discarded on speeding up: the clip doesn't jump ahead", discarded == 0,
            f"{discarded} frames discarded over {len(SLOW_OFFSETS)} switches; {clamped} clamped ticks")


def scenario(run):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check("Client is playing", True)

    def all_cached():
        run.require_alive()
        return set(run.dreams) <= cached(run)

    h.wait_for(f"the client to download all {len(run.dreams)} dreams", all_cached, 180)
    h.check("Every dream is on the local disk", True, f"{len(run.dreams)} dreams")
    time.sleep(3)  # let any heartbeat that covers the last download pass

    start = len(run.log_text())
    for _ in range(CYCLES):
        toggle(run, paused=True)
        time.sleep(PAUSE_SECONDS)
        toggle(run, paused=False)
        time.sleep(PLAY_SECONDS)
    beats = h.parse_beats(run.log_text()[start:])

    resumes = run.log_text()[start:].count("Pause state changed to playing")
    h.check(f"Paused and resumed {CYCLES} times", resumes == CYCLES, f"{resumes} resumes logged")
    clamped = sum(int(b.get("clamped_ticks", 0)) for b in beats)
    lost = sum(float(b.get("lost_ms", 0)) for b in beats)
    discarded = [int(b["discarded"]) for b in beats if "discarded" in b]
    h.check("No tick clamped on resume: the timeline doesn't jump", clamped <= MAX_CLAMPED,
            f"{clamped} clamped ticks, {lost:.0f} ms lost over {CYCLES} resumes of "
            f"{PAUSE_SECONDS * 1000:.0f} ms; discarded counter "
            f"{discarded[0] if discarded else '-'} -> {discarded[-1] if discarded else '-'}")

    speed_phase(run)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-resume-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, screenshots=False)
    try:
        run.start()
        scenario(run)
    except h.Failed as failure:
        h.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
