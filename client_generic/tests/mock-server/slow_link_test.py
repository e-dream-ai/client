#!/usr/bin/env python3
"""Slow-link test of adaptive playback speed and crossfades (client#770).

Serves the largest videos over a rate-limited link and checks that the client, instead of
stopping and starting:

1. at 0.6x the first dream's rate, slows playback below 100% and keeps it moving, with few
   or no rebuffers;
2. at 0.15x, slows below 25%, the floor before it was lowered to 10%;
3. on that link, switching to an uncached dream holds the crossfade while the new dream
   buffers, instead of snapping back to the old dream and fading in again;
4. returns to full speed when the rate goes back up.

Serves the alpha-v1 fixture set by default (its two long Pink Floyd dreams). Takes about three
minutes. Runs on Mac and Windows.
"""
import argparse
import re
import sys
import tempfile
import time

import harness as e

# The client plays dreams at 20 fps by default, whatever the file's own rate.
PLAYBACK_FPS = 20
GOOD_RATE = 2.5     # times the first dream's playback rate
SLOW_RATE = 0.6     # slow enough to need slowing, fast enough to stay well above the floor
FLOOR_RATE = 0.15   # under the old 25% floor, over the 10% one
OLD_FLOOR = 0.25
DROP_FRACTION = 0.4  # lower the rate before this share of the first dream can have arrived
MIN_GOOD_SECONDS = 3
MAX_GOOD_SECONDS = 12
MAX_SLOW_REBUFFERS = 2  # once slowed at SLOW_RATE; before client#770 there were about 20
SETTLE_SECONDS = 15
CROSSFADE_TRIES = 3


def speed(beat):
    return float(beat.get("speed", "1"))


def first_dream_beats(run, uuid, since=0.0):
    return [b for b in run.heartbeats() if b.get("current") == uuid and b["time"] >= since]


def rebuffers_of(run, uuid):
    found = first_dream_beats(run, uuid)
    return int(found[-1]["rebuffers"]) if found else 0


def scenario(run, need, download_seconds, others, frames_of):
    started_at = time.time()
    first = run.wait_until_playing()["current"]
    e.check("Client is playing the first dream", True, first[:8])

    # Drop the rate while most of the first dream is still to come (see throttle_test.py),
    # and soon: every second at the good rate adds buffer the slow phase has to drain.
    drop_at = started_at + min(DROP_FRACTION * download_seconds, MAX_GOOD_SECONDS)
    time.sleep(max(MIN_GOOD_SECONDS, drop_at - time.time()))

    # 1. Slow, not stop.
    run.set_media_rate(need * SLOW_RATE)
    dropped_at = time.time()
    print(f"Media limited to {SLOW_RATE}x the dream's rate", flush=True)

    def slowed(limit):
        return lambda: any(speed(b) < limit for b in first_dream_beats(run, first, dropped_at))

    e.wait_for(f"playback to slow at {SLOW_RATE}x", slowed(0.95), 90)
    slowed_at = time.time()
    e.check(f"Playback slowed below 100% at {SLOW_RATE}x the dream's rate", True,
            f"{slowed_at - dropped_at:.0f} s after the drop")
    rebuffers_before = rebuffers_of(run, first)
    time.sleep(SETTLE_SECONDS)
    window = first_dream_beats(run, first, slowed_at)
    frames = [int(b["frame"].split("/")[0]) for b in window]
    added = rebuffers_of(run, first) - rebuffers_before
    e.check(f"At most {MAX_SLOW_REBUFFERS} rebuffers while slowed", added <= MAX_SLOW_REBUFFERS,
            f"{added} in {SETTLE_SECONDS} s, speeds "
            f"{min(map(speed, window), default=1):.2f}-{max(map(speed, window), default=1):.2f}")
    e.check("Frames kept advancing while slowed", len(frames) >= 2 and frames[-1] > frames[0],
            f"frame {frames[0] if frames else '?'} to {frames[-1] if frames else '?'}")

    # 2. Below the old floor.
    run.set_media_rate(need * FLOOR_RATE)
    floor_at = time.time()
    print(f"Media limited to {FLOOR_RATE}x the dream's rate", flush=True)
    try:
        e.wait_for(f"playback to slow below {OLD_FLOOR:.0%}",
                   lambda: any(speed(b) < OLD_FLOOR - 0.05
                               for b in first_dream_beats(run, first, floor_at)), 60)
        lowest = min(speed(b) for b in first_dream_beats(run, first, floor_at))
        e.check(f"Playback slowed below the old {OLD_FLOOR:.0%} floor", True, f"down to {lowest:.2f}x")
    except e.Failed:
        beats = first_dream_beats(run, first, floor_at)
        e.check(f"Playback slowed below the old {OLD_FLOOR:.0%} floor", False,
                f"lowest {min(map(speed, beats), default=1):.2f}x in 60 s")

    # 3. Crossfade into another dream on the slow link. Start it from a frame well inside,
    # which hasn't downloaded yet, so the new dream starts on a starved link; its start may
    # already be there from the background downloader. Alternate dreams for a few tries.
    held = []
    for attempt in range(CROSSFADE_TRIES):
        target = others[0] if attempt % 2 == 0 else first
        frame = frames_of[target] * (attempt + 2) // (CROSSFADE_TRIES + 3)
        print(f"play_dream {target[:8]} at frame {frame} on the slow link", flush=True)
        run.send("play_dream", uuid=target, frameNumber=frame)
        try:
            e.wait_for(f"{target[:8]} to finish fading in",
                       lambda t=target: (lambda b: b and b[-1].get("current") == t
                                         and b[-1].get("transitioning") == "0")(run.heartbeats()),
                       180)
        except e.Failed as failure:
            e.check(f"Crossfade into {target[:8]} finished", False, str(failure))
            break
        if re.search(rf"Crossfade to {target} held at", run.log_text()):
            held.append(target)
            break
    e.check("A crossfade was held while its new dream buffered", bool(held),
            f"into {held[0][:8]}" if held else "the new dream never ran dry mid-fade; "
            "the scenario didn't exercise the hold")
    backs = re.findall(r"Crossfade to (\S+) went back from (\d+)% to (\d+)%", run.log_text())
    e.check("No crossfade went back", not backs,
            "; ".join(f"{u[:8]} {a}% -> {b}%" for u, a, b in backs) or "")

    # 4. Back to full speed.
    run.set_media_rate(need * GOOD_RATE)
    restored_at = time.time()

    def full_speed():
        recent = [b for b in run.heartbeats() if b["time"] > restored_at][-3:]
        return len(recent) == 3 and all(speed(b) >= 1.0 for b in recent)

    e.wait_for("playback to return to full speed", full_speed, 120)
    e.check("Playback returned to full speed after the rate went back up", True,
            f"{time.time() - restored_at:.0f} s after the change")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    e.add_media_args(parser)
    # Its two Pink Floyd dreams are 11-12k frames, so neither ends during the test.
    parser.set_defaults(set="alpha-v1")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    e.resolve_media(args)

    if e.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    # The two largest: a long first dream that stays on screen through the slow phases, and
    # another to switch to.
    dreams = e.load_dreams(args.media_dir, 2, 900, "largest")
    first = e.first_dream(args.media_dir, dreams)
    others = [uuid for uuid in dreams if uuid != first["uuid"]]
    # The client plays a dream at its perceptual rate divided by the dream's activity level.
    playback_fps = PLAYBACK_FPS / float(first.get("activityLevel") or 1)
    need = int(first["size"]) / (first["frames"] / playback_fps)
    download_seconds = int(first["size"]) / (need * GOOD_RATE)
    print(f"First dream {first['uuid'][:8]} needs {need / 1e3:.0f} KB/s", flush=True)

    work = tempfile.mkdtemp(prefix="infinidream-slowlink-")
    print(f"Run folder: {work}", flush=True)
    before = e.snapshot_real_installs()
    # Slowed playback advances frames slowly, and screenshots stall the render loop.
    run = e.Run(work, args.app, args.media_dir, server_args=(
        "--pick", "largest", "--dreams", "2", "--media-rate", str(int(need * GOOD_RATE))),
        screenshots=False, max_stuck=30)
    try:
        run.start()
        scenario(run, need, download_seconds, others, {u: d["frames"] for u, d in dreams.items()})
    except e.Failed as failure:
        e.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        e.verify_process(run)
        e.verify_server(run, before, require_download=False)
    e.finish(work, args.keep)


if __name__ == "__main__":
    main()
