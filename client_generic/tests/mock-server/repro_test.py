#!/usr/bin/env python3
"""Reproductions of client bugs found with the mock server (client#733), one fresh client per
case. Each case checks the right behavior, so it fails while the bug is there and stays as a
regression test once it's fixed. Mac only for now.

    venv/bin/python repro_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python repro_test.py --app ... --case previous-after-wrap
    venv/bin/python repro_test.py --list

Cases:
  seek-past-end        play_dream at a frame past the end must move on to another dream, not
                       sit on the last frame while "playing".
  seek-near-end        play_dream 10 frames before the end: the control for seek-past-end.
  previous-after-wrap  3 dreams, A next B next C next A next B: previous must go back to A. The
                       play history doubles as the back stack and is reset at the wrap.
  previous-two-dreams  2 dreams, A next B next A next B: previous must go back to A, not
                       restart B.
  switch-as-dream-ends play_dream an uncached dream over a slow link about 2 s before the
                       current dream ends, so the current one runs out of frames while the new
                       one buffers. The new dream must still fade in, not cut in: its crossfade
                       starts and then jumps to the end on the next frame ("Crossfade to
                       ... cut short at 2%: outgoing clip ... finished", #890).
"""
import argparse
import re
import sys
import tempfile
import time
from pathlib import Path

import harness as h

COMMAND_TIMEOUT = 15
MOVE_ON_TIMEOUT = 20  # for the client to leave a dream after seeking to or past its end


def current(run):
    run.require_alive()
    beats = run.heartbeats()
    return beats[-1] if beats else {}


def full(run, short):
    found = [u for u in run.dreams if u.startswith(short or "-")]
    return found[0] if len(found) == 1 else short


def wait_for_dream(run, description, accept, timeout=COMMAND_TIMEOUT):
    sent = time.time()
    def arrived():
        beat = current(run)
        uuid = full(run, beat.get("current"))
        return beat if beat and beat["time"] > sent and uuid in run.dreams and accept(uuid) else None
    return h.wait_for(description, arrived, timeout)


def names(run):
    return {u: "ABCDEFG"[i] for i, u in enumerate(run.dreams)}


def wait_until_all_cached(run):
    """Waits until every served dream is downloaded, so a natural end has somewhere to go
    (natural transitions only go to cached dreams, by design)."""
    mp4 = run.data / "content" / "mp4"
    h.wait_for("every dream to be cached",
               lambda: run.require_alive() or len(list(mp4.glob("*.mp4"))) >= len(run.dreams), 120)


def seek(run, frames_from_end):
    """play_dream the dream after the current one at frames_from_end from its end (negative is
    past it). The requested dream must appear, then the client must move on."""
    run.wait_until_ready()
    start = full(run, run.wait_until_playing()["current"])
    wait_until_all_cached(run)
    start = full(run, current(run).get("current"))
    target = next(u for u in run.dreams if u != start)
    frames = run.dream_frames[target]
    frame = frames - frames_from_end
    print(f"play_dream {target[:8]} ({frames} frames) at frame {frame}", flush=True)
    offset = len(run.log_text())
    run.send("play_dream", uuid=target, frameNumber=frame)
    # The log, not the 1 Hz heartbeat: a dream seeked near its end is on screen for under a second.
    logged = lambda text: run.require_alive() or text in run.log_text()[offset:]
    try:
        h.wait_for("the requested dream to start", lambda: logged(f"starting playback for {target}"),
                   COMMAND_TIMEOUT)
        h.check(f"play_dream at frame {frame} of {frames} showed the requested dream", True)
    except h.Failed:
        beat = current(run)
        why = "the decoder found no frames there" if logged(
            f"Decoder ended with no frames available for {target}") else "it never started"
        h.check(f"play_dream at frame {frame} of {frames} showed the requested dream", False,
                f"{why}; still on {beat.get('current', '?')[:8]}, the dream before")
        return
    try:
        h.wait_for("the client to move on", lambda: logged(f"Finished dream {target}"), MOVE_ON_TIMEOUT)
        h.check(f"Moved on after play_dream at frame {frame} of {frames}", True,
                f"now {current(run).get('current', '?')[:8]}")
    except h.Failed:
        beat = current(run)
        h.check(f"Moved on after play_dream at frame {frame} of {frames}", False,
                f"still on {beat.get('current', '?')[:8]} at frame {beat.get('frame')} after "
                f"{MOVE_ON_TIMEOUT} s, paused={beat.get('paused')} buffering={beat.get('buffering_pause')}")


def previous_after_nexts(run, nexts):
    """Sends next `nexts` times, then previous, and checks previous went back to the dream
    shown before the current one."""
    run.wait_until_ready()
    letter = names(run)
    order = [full(run, run.wait_until_playing()["current"])]
    for number in range(nexts):
        time.sleep(2)
        run.send("next")
        beat = wait_for_dream(run, f"next #{number + 1} to change the dream", lambda u: u != order[-1])
        order.append(full(run, beat["current"]))
    time.sleep(2)
    run.send("previous")
    try:
        beat = wait_for_dream(run, "previous to change the dream", lambda u: u != order[-1])
        went = full(run, beat["current"])
    except h.Failed:
        went = order[-1]
    path = " -> ".join(letter.get(u, u[:8]) for u in order)
    h.check("previous went back to the dream shown before", went == order[-2],
            f"{path}, previous -> {letter.get(went, went[:8])}"
            + ("" if went == order[-2] else f", expected {letter.get(order[-2])}"))


SWITCH_LEAD_SECONDS = 2.0  # how long before the current dream's end to send play_dream
SWITCH_SLOW_RATE = 8 * 1024  # bytes a second: the new dream's first frames take far longer


def switch_as_dream_ends(run):
    """With zero quota only the streamed first dream A is cached. Near A's end, slows the link
    and sends play_dream for uncached B, so A runs out of frames while B buffers. Then restores
    the link so B can start, and checks B's crossfade wasn't cut short."""
    run.wait_until_ready()
    a = full(run, run.wait_until_playing()["current"])
    b = next(u for u in run.dreams if u != a)
    frames = run.dream_frames[a]

    def frame_of(beat):
        return int(beat["frame"].split("/")[0])

    def rate_and_frame():
        beats = [x for x in run.heartbeats() if full(run, x.get("current")) == a and "frame" in x]
        if len(beats) < 3 or beats[-1]["time"] - beats[0]["time"] < 2:
            return None
        rate = (frame_of(beats[-1]) - frame_of(beats[0])) / (beats[-1]["time"] - beats[0]["time"])
        return (rate, beats[-1]) if rate > 0 else None
    rate, _ = h.wait_for(f"{a[:8]}'s frame rate", lambda: run.require_alive() or rate_and_frame(), 30)
    near = frames - int(SWITCH_LEAD_SECONDS * rate)
    print(f"{a[:8]} plays at {rate:.0f} frames/s; waiting for frame {near} of {frames}", flush=True)
    def near_end():
        found = run.require_alive() or rate_and_frame()
        return found and frame_of(found[1]) >= near
    h.wait_for(f"{a[:8]} to near its end", near_end, frames / rate + 10)

    offset = len(run.log_text())
    run.set_media_rate(SWITCH_SLOW_RATE)
    print(f"play_dream {b[:8]} (uncached) at {SWITCH_SLOW_RATE // 1024} KB/s", flush=True)
    run.send("play_dream", uuid=b)
    log = lambda: run.require_alive() or run.log_text()[offset:]
    ran_out = f"marking dream {a} as finished"
    started = f"starting playback for {b}"
    try:
        h.wait_for(f"{a[:8]} to run out of frames", lambda: ran_out in log() or started in log(),
                   SWITCH_LEAD_SECONDS + 10)
    finally:
        run.set_media_rate(0)
    h.wait_for(f"{b[:8]} to start", lambda: started in log(), COMMAND_TIMEOUT)
    text = log()
    h.check(f"{a[:8]} ran out of frames while {b[:8]} buffered (the setup)",
            0 <= text.find(ran_out) < text.find(started),
            "it did" if 0 <= text.find(ran_out) < text.find(started) else
            f"{b[:8]} was ready first, so this run didn't reproduce the case")
    time.sleep(3)  # for the crossfade to finish
    cut = re.search(r"Crossfade to " + re.escape(b) + r" cut short at (\d+)%", log())
    h.check(f"{b[:8]} faded in rather than cutting in", not cut,
            f"crossfade cut short at {cut[1]}% because {a[:8]} had finished" if cut else "")
    wait_for_dream(run, f"the client to settle on {b[:8]}", lambda u: u == b)


# name: (dreams served, scenario, extra server options)
CASES = {
    "seek-past-end": (3, lambda run: seek(run, -50), ()),
    "seek-near-end": (3, lambda run: seek(run, 10), ()),
    "previous-after-wrap": (3, lambda run: previous_after_nexts(run, 4), ()),
    "previous-two-dreams": (2, lambda run: previous_after_nexts(run, 3), ()),
    # Zero quota, so the background downloader leaves B uncached.
    "switch-as-dream-ends": (3, switch_as_dream_ends, ("--quota", "0")),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--case", action="append", choices=list(CASES), help="run only these")
    parser.add_argument("--list", action="store_true", help="list the cases")
    parser.add_argument("--keep", action="store_true", help="keep the run folders even if they pass")
    args = parser.parse_args()
    if args.list:
        print("\n".join(CASES))
        return
    if not args.app:
        parser.error("--app is required")
    h.resolve_media(args)
    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    root = Path(tempfile.mkdtemp(prefix="infinidream-repro-"))
    print(f"Run folder: {root}", flush=True)
    before = h.snapshot_real_installs()
    for name in args.case or list(CASES):
        dreams, body, server_args = CASES[name]
        print(f"\n== {name}", flush=True)
        # The previous-* cases count on exactly this many dreams, so the nexts wrap around.
        # --playlist serves the whole playlist instead.
        if h.PLAYLIST and name.startswith("previous-"):
            print(f"SKIP  {name}: needs exactly {dreams} dreams, so it can't run with --playlist",
                  flush=True)
            continue
        # A stuck frame is what seek-past-end looks for, so give the hang check room.
        run = h.Run(root / name, args.app, args.media_dir, screenshots=False,
                    max_stuck=MOVE_ON_TIMEOUT + 10, server_args=("--dreams", str(dreams), *server_args))
        try:
            run.start()
            body(run)
        except h.Failed as failure:
            h.check(f"{name} scenario", False, str(failure))
        finally:
            run.stop()
        if run.client:
            h.verify_process(run)
    h.check("Real installs were not changed",
            not [k for k, v in h.snapshot_real_installs().items() if before.get(k) != v])
    h.finish(root, args.keep)


if __name__ == "__main__":
    main()
