#!/usr/bin/env python3
"""A playlist switch's crossfade doesn't snap back at the old dream's end (client#826).

play_playlist starts a 1 s crossfade into the new playlist's first dream. If the old dream
reaches its end-of-dream fade window (its last 5 s) while that crossfade is running, the
client used to start its normal end-of-dream crossfade on top of it, which restarted the
fade: the new dream faded in partway, jumped back to the old one and faded in again. The
client logs that as "Crossfade to ... went back".

Lets the client download every dream. Then, a few times, it seeks the middle dream to about
SEEK_SECONDS before its end with play_dream, and times play_playlist to land just before its
fade window opens. It checks that the old dream's fade window
really did open during each switch's crossfade, and that no crossfade went back.

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import re
import sys
import tempfile
import time

import harness as h
import server

TRIALS = 3
DEFAULT_FADE_SECONDS = 5.0  # the client's end-of-dream fade when a playlist sets none
LEAD_SECONDS = 0.5   # switch this long before the fade window opens; the crossfade lasts 1 s
OVERLAP_SECONDS = 1.0
SEEK_SECONDS = 10.0  # play_dream this far before the end, so a trial doesn't wait out a dream


def cached(run):
    folder = run.data / "content" / "mp4"
    return {p.stem for p in folder.glob("*.mp4")} if folder.exists() else set()


def frames(beat):
    done, total = beat["frame"].split("/")
    return int(done), int(total)


def fade_seconds(run):
    """The end-of-dream fade the current playlist asks for (the mock server's say 2 s)."""
    times = re.findall(r"Playlist loops: \d+, transition time: ([\d.]+)", run.log_text())
    return float(times[-1]) if times else DEFAULT_FADE_SECONDS


def steady_beats(run, since=0.0):
    """The heartbeats of the current dream since `since` (epoch seconds), back to the last
    crossfade."""
    beats = [b for b in run.heartbeats() if b.get("current", "-") != "-" and b["time"] > since]
    run_of = []
    for b in reversed(beats):
        if b["current"] != beats[-1]["current"] or b["transitioning"] != "0":
            break
        run_of.insert(0, b)
    return run_of


def time_switch(run, since):
    """Waits until the current dream will open its fade window in LEAD_SECONDS and returns
    its uuid; None if it's already too late. A heartbeat's frame is only good to a frame, so
    the rate is measured over every heartbeat of the dream, and measured again a few seconds
    before the switch, so the last guess looks only that far ahead."""
    while True:
        def measurable():
            run.require_alive()
            beats = steady_beats(run, since)
            return beats if len(beats) >= 3 and beats[-1]["time"] - beats[0]["time"] >= 2 else None

        beats = h.wait_for("a few heartbeats of one dream", measurable, 30)
        (f0, _), (f1, total) = frames(beats[0]), frames(beats[-1])
        rate = (f1 - f0) / (beats[-1]["time"] - beats[0]["time"])
        if rate <= 0:
            return None
        fade_opens = beats[-1]["time"] + (total - f1) / rate - fade_seconds(run)
        wait = fade_opens - LEAD_SECONDS - time.time()
        if wait < 0.2:
            return None
        if wait <= 3:
            time.sleep(wait)
            return beats[-1]["current"]
        time.sleep(wait - 2.5)


def scenario(run):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check("Client is playing", True)

    def all_cached():
        run.require_alive()
        return set(run.dreams) <= cached(run)

    h.wait_for(f"the client to download all {len(run.dreams)} dreams", all_cached, 180)
    h.check("Every dream is on the local disk", True, f"{len(run.dreams)} dreams")

    uuids = list(run.dreams)
    if len(uuids) < 3:
        raise h.Failed("Needs at least 3 dreams, so the seeked one starts neither playlist")
    # Neither playlist starts with the middle dream: one starts with uuids[0], the other uuids[-1].
    middle = uuids[len(uuids) // 2]
    playlists = [server.playlist_uuid(1), server.playlist_uuid(2)]
    beats = h.wait_for("a few heartbeats of one dream",
                       lambda: (lambda bs: bs if len(bs) >= 3 else None)(steady_beats(run)), 30)
    (f0, _), (f1, _) = frames(beats[0]), frames(beats[-1])
    rate = (f1 - f0) / max(beats[-1]["time"] - beats[0]["time"], 1)
    seek = max(0, run.dream_frames[middle] - int(SEEK_SECONDS * rate))
    start = len(run.log_text())
    overlaps, attempts = 0, 0
    while overlaps < TRIALS and attempts < TRIALS * 4:
        attempts += 1
        sent = time.time()
        run.send("play_dream", uuid=middle, frameNumber=seek)
        old = time_switch(run, sent + 1)
        if old != middle:
            continue
        target = playlists[attempts % 2]
        mark = len(run.log_text())
        run.send("play_playlist", uuid=target)
        time.sleep(fade_seconds(run) + 3)  # let the old dream finish and the next one settle
        run.require_alive()
        log = run.log_text()[mark:]
        applied = re.search(r"\[(\S+ \S+) - info\]: Applying queued playlist change", log)
        faded = re.search(r"\[(\S+ \S+) - info\]: Fade out started for " + re.escape(old), log)
        if applied and faded:
            gap = (h.datetime.strptime(faded[1], "%Y-%m-%d %H:%M:%S.%f")
                   - h.datetime.strptime(applied[1], "%Y-%m-%d %H:%M:%S.%f")).total_seconds()
            hit = 0 <= gap <= OVERLAP_SECONDS
            overlaps += hit
            print(f"      switch {attempts}: old dream's fade window opened {gap * 1000:.0f} ms "
                  f"{'into' if gap >= 0 else 'before'} the playlist crossfade"
                  f"{'' if hit else ' (missed)'}", flush=True)
        else:
            print(f"      switch {attempts}: old dream's fade window didn't open (missed)", flush=True)
    log = run.log_text()[start:]

    h.check(f"Old dream's fade window opened during a playlist crossfade {TRIALS} times",
            overlaps >= TRIALS, f"{overlaps} of {attempts} switches")
    back = re.findall(r"Crossfade to \S+ went back from \d+% to \d+%", log)
    h.check("No crossfade went back", not back, "; ".join(back) or "none")


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

    work = tempfile.mkdtemp(prefix="infinidream-playlist-fade-")
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
