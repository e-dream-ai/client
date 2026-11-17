#!/usr/bin/env python3
"""A speed change also applies to the dream that's already preloaded (client#833).

The client preloads the next dream about 60 ms after each dream starts, and the preloaded clip
took its speed from the moment it was made. A speed change only reached the playing clip, so
the next dream played all the way through at the old speed, and only the one after it at the
new one. On a playlist of short dreams, such as Wankel Rotary Engine (193 frames each), that
happens on nearly every speed change: the picture slows down, speeds up for one dream, then
slows down again.

Serves the alpha-v1 set's Wankel Rotary Engine playlist and starts at 80 fps, as in the
original report. Lets the client download every dream so no rebuffer gets in the way. Then,
twice, it waits until a dream has started and the next has been preloaded, and changes the
speed: first down to speed 7 (16 fps), then up to speed 9 (32 fps). It checks that the
preloaded dream, and the one after it, take about as long as their frames at the new speed.

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import re
import sys
import tempfile
import time
from datetime import datetime

import harness as h

DEFAULT_SET = "alpha-v1"
WANKEL = "d9726526-b8f2-4221-a0bf-67fbacc01f4d"
START_FPS = 80.0
# (command, perceptual fps it sets): down a lot, then back up.
CHANGES = [("set_speed_7", 16.0), ("set_speed_9", 32.0)]
# How far a dream's time may be from its frames at the new speed. The bug is off by 2 to 5x,
# so this tolerance is wide enough for crossfades and a busy machine.
TOLERANCE = 0.35

MOVED = re.compile(r"moveToNextDream : (\S+) ")
PRELOADED = re.compile(r"Preloading initiated for (\S+)")
FINISHED = re.compile(r"Finished dream (\S+) \((\w+)\) at frame (\d+)/(\d+) after")
SPEED_SYNC = re.compile(r"state_sync.*playback_speed:([\d.]+)")
STAMP = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) - ")


def cached(run):
    folder = run.data / "content" / "mp4"
    return {p.stem for p in folder.glob("*.mp4")} if folder.exists() else set()


def stamp(log, pos):
    """Epoch seconds of the log line that holds offset pos."""
    line = log[log.rfind("\n", 0, pos) + 1:]
    return datetime.strptime(STAMP.match(line)[1], "%Y-%m-%d %H:%M:%S.%f").timestamp()


def finished(run, uuid, since):
    """(reason, frame, frames, seconds) for uuid, from its start (moveToNextDream) to its
    Finished line, both after log offset since. Not the Finished line's own "after", which
    starts when the dream before it is reported finished, partway through the crossfade."""
    log = run.log_text()
    start = next((m for m in MOVED.finditer(log, since) if m[1] == uuid), None)
    if not start:
        return None
    for m in FINISHED.finditer(log, start.end()):
        if m[1] == uuid:
            return m[2], int(m[3]), int(m[4]), stamp(log, m.start()) - stamp(log, start.start())
    return None


def activity(run, uuid):
    import server
    return float(server.snapshot_records(run.media_dir)[uuid].get("activityLevel") or 1.0)


def change_after_preload(run, command, fps):
    """Waits for a new dream to start and the next one to be preloaded, then sends command.
    Returns the preloaded dream and the log offset just before the command."""
    start = len(run.log_text())

    def preloaded():
        run.require_alive()
        log = run.log_text()[start:]
        moved = MOVED.search(log)
        if not moved:
            return None
        loaded = PRELOADED.search(log, moved.end())
        return loaded[1] if loaded else None

    # A dream at the slowest speed here lasts about 15 s, so allow for two of them.
    upcoming = h.wait_for("a dream to start and the next to be preloaded", preloaded, 45)
    time.sleep(0.5)  # well into the playing dream, with the next one ready
    mark = len(run.log_text())
    run.send(command)
    sync = h.wait_for(f"the client to report the new speed after {command}",
                      lambda: run.require_alive() or SPEED_SYNC.search(run.log_text(), mark), 10)
    h.check(f"{command} set the speed to {fps:g} fps", abs(float(sync[1]) - fps) < 0.5,
            f"state_sync says {sync[1]}")
    return upcoming, mark


def check_dream(run, uuid, fps, label, since):
    expected_fps = fps / activity(run, uuid)
    result = h.wait_for(f"{label} {uuid[:8]} to finish", lambda: run.require_alive()
                        or finished(run, uuid, since), 120)
    reason, frame, frames, seconds = result
    expected = frames / expected_fps
    ok = reason == "natural" and abs(seconds - expected) <= TOLERANCE * expected
    h.check(f"{label} {uuid[:8]} played at {fps:g} fps", ok,
            f"{reason} at frame {frame}/{frames} after {seconds:.1f} s; "
            f"{expected:.1f} s expected at {expected_fps:.1f} decode fps")


def scenario(run):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check("Client is playing", True)

    def all_cached():
        run.require_alive()
        return set(run.dreams) <= cached(run)

    h.wait_for(f"the client to download all {len(run.dreams)} dreams", all_cached, 180)
    h.check("Every dream is on the local disk", True, f"{len(run.dreams)} dreams")

    for command, fps in CHANGES:
        preloaded, mark = change_after_preload(run, command, fps)
        # The preloaded dream is the one that used to keep the old speed.
        check_dream(run, preloaded, fps, f"After {command}, preloaded dream", mark)
        # And the one after it, made after the change, as a control.
        after = h.wait_for("the dream after it to start", lambda: run.require_alive() or next(
            (m[1] for m in MOVED.finditer(run.log_text()[mark:]) if m[1] != preloaded), None), 60)
        check_dream(run, after, fps, f"After {command}, the dream after it", mark)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    if not args.set:
        args.set = DEFAULT_SET
    if not args.playlist:
        args.playlist = WANKEL
    h.resolve_media(args)

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-speed-preload-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, screenshots=False,
                player_settings={"perceptual_fps": START_FPS})
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
