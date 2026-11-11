#!/usr/bin/env python3
"""Drives the client through the common remote-control commands and checks each one's effect
in the heartbeat (client#733): next five times, previous, play_dream, play_dream at a frame,
forward and backward, then quit. The nexts run over a slow link, so some of them start a dream
that is still downloading, and each must add its dream to the play history (#771). Takes
about a minute and a half. Mac only for now.

    venv/bin/python commands_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
"""
import argparse
import sys
import tempfile
import time

import harness as h

# forward and backward skip this far in the video's own time (CPlayer::SkipForward(±10)): at
# a file's 30 fps that's 300 frames, which at the default 20 fps playback is 15 s on screen.
SKIP_SECONDS = 10
SEEK_FRAME = 300
COMMAND_TIMEOUT = 15
# Total media rate until the nexts are done: enough to play the dream on screen, too little
# to finish downloading the rest of the playlist, so some nexts take the not-cached path.
SLOW_LINK = 600_000
NOT_CACHED = "Next dream is not cached, will try loading"


def frame_of(beat):
    return int(beat["frame"].split("/")[0])


def latest(run):
    run.require_alive()
    beats = run.heartbeats()
    return beats[-1] if beats else None


def wait_for_dream(run, description, accept):
    """Waits for a heartbeat after now whose current dream passes accept(uuid)."""
    sent = time.time()
    def arrived():
        beat = latest(run)
        if beat and beat["time"] > sent and beat.get("current", "-") != "-" and accept(beat["current"]):
            return beat
        return None
    return h.wait_for(description, arrived, COMMAND_TIMEOUT)


def playback_rate(run):
    """Frames a second the current clip is advancing: the median of the last few steps."""
    beats = run.heartbeats()[-5:]
    steps = sorted((frame_of(b) - frame_of(a)) / (b["time"] - a["time"])
                   for a, b in zip(beats, beats[1:])
                   if a.get("current") == b.get("current") and b["time"] > a["time"])
    return steps[len(steps) // 2] if steps else 20.0


def check_skip(run, command, seconds):
    """Sends forward or backward and checks the frame jumped by about that many seconds of
    the video's own time."""
    time.sleep(4)  # Steady heartbeats to measure the rate from.
    before, rate = latest(run), playback_rate(run)
    file_fps = run.dream_fps[before["current"]]
    expected = seconds * file_fps
    run.send(command)

    def jumped():
        beat = latest(run)
        if not beat or beat["time"] <= before["time"] or beat["current"] != before["current"]:
            return None
        jump = frame_of(beat) - frame_of(before) - rate * (beat["time"] - before["time"])
        return (beat, jump) if abs(jump) > abs(expected) / 2 else None

    try:
        beat, jump = h.wait_for(f"{command} to move the frame", jumped, COMMAND_TIMEOUT)
    except h.Failed:
        h.check(f"{command} skipped about {seconds:+d} s", False,
                f"frame {frame_of(before)} -> {frame_of(latest(run))}, no jump seen")
        return
    h.check(f"{command} skipped about {seconds:+d} s", abs(jump - expected) <= abs(expected) * 0.2,
            f"frame {frame_of(before)} -> {frame_of(beat)}: {jump:+.0f} frames, "
            f"{jump / file_fps:+.1f} s at the file's {file_fps:.0f} fps")


def scenario(run):
    run.wait_until_ready()
    first = run.wait_until_playing()["current"]
    h.check("Client is playing", True, first[:8])

    # With 3 dreams, 5 nexts go round the playlist more than once. The link is slow
    # throughout, so some nexts reach a dream that is still downloading: the path that
    # left dreams out of the play history (#771).
    order, stuck, unrecorded = [first], [], []
    next_logs = ""
    for number in range(1, 6):
        time.sleep(2)
        now = latest(run)["current"]
        log_before = len(run.log_text())
        run.send("next")
        try:
            beat = wait_for_dream(run, f"next #{number} to change the dream", lambda u: u != now)
            order.append(beat["current"])
        except h.Failed:
            run.require_alive()
            stuck.append(number)
            order.append(latest(run)["current"])
        # The history is updated before the new dream starts, so it's in the log by now.
        logged = run.log_text()[log_before:]
        next_logs += logged
        if f"Added to history: {order[-1]}" not in logged:
            unrecorded.append(number)
    run.set_media_rate(0)
    h.check("next changed the dream 5 times in a row", not stuck,
            " -> ".join(u[:8] for u in order) + (f"; next #{', #'.join(map(str, stuck))} stayed on the same dream"
                                                 if stuck else ""))
    h.check("Every next added its dream to the play history", not unrecorded,
            f"no 'Added to history' after next #{', #'.join(map(str, unrecorded))}" if unrecorded else "")
    streamed = next_logs.count(NOT_CACHED)
    h.check("At least one next started a dream that was still downloading", streamed > 0,
            f"{streamed} of 5" if streamed else
            f"none did, so the not-cached path wasn't tested; lower SLOW_LINK ({SLOW_LINK} B/s)")
    h.check("Every dream played is one the server offered", all(u in run.dreams for u in order))

    time.sleep(2)
    current = latest(run)["current"]
    earlier = next(u for u in reversed(order) if u != current)
    run.send("previous")
    beat = wait_for_dream(run, "previous to change the dream", lambda u: u != current)
    h.check("previous went back to the last different dream", beat["current"] == earlier,
            f"now {beat['current'][:8]}, expected {earlier[:8]}")

    target = next(u for u in run.dreams if u != latest(run)["current"])
    time.sleep(2)
    run.send("play_dream", uuid=target)
    beat = wait_for_dream(run, "play_dream to switch dreams", lambda u: u == target)
    h.check("play_dream switched to the requested dream", True, target[:8])

    other = next(u for u in run.dreams if u != target)
    time.sleep(2)
    run.send("play_dream", uuid=other, frameNumber=SEEK_FRAME)
    beat = wait_for_dream(run, "play_dream with frameNumber to switch dreams", lambda u: u == other)
    # The first heartbeat comes up to a second after the seek, at up to ~20 frames a second.
    h.check(f"play_dream with frameNumber started near frame {SEEK_FRAME}",
            SEEK_FRAME - 5 <= frame_of(beat) <= SEEK_FRAME + 60, f"first heartbeat at frame {frame_of(beat)}")

    check_skip(run, "forward", SKIP_SECONDS)
    check_skip(run, "backward", -SKIP_SECONDS)
    time.sleep(3)
    run.require_alive()


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

    work = tempfile.mkdtemp(prefix="infinidream-commands-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, server_args=("--media-rate", str(SLOW_LINK)),
                screenshots=False)
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
