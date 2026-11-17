#!/usr/bin/env python3
"""A playlist switch in the middle of an end-of-dream crossfade.

The client has two video pipelines. During a natural crossfade A -> B both are busy, so
play_playlist can't add a third dream C. It keeps whichever clip is more on screen and
crossfades from it to C over 1 s: before halfway it drops B (A jumps back to full); from
halfway it drops A (B jumps to full). It used to always keep A, and A, in its last seconds,
could run out before the 1 s fade ended, which cut to C. The client logs:
  "Playlist switch interrupted crossfade A -> B at N%: fading from outgoing|incoming"
  "Crossfade to C cut short at N%: outgoing clip A finished"

Serves 4 dreams d0..d3 with a TRANSITION_TIME s fade (the real default is 5 s). Each trial
switches to playlist 1 (d0 d1 d2 d3), seeks d1 to a few seconds before its end, waits for
its fade window to open (crossfade d1 -> d2), and sends play_playlist for playlist 2
(d3 d2 d1 d0) a fraction of the way into that fade. It reports what happened, and checks the
client kept the clip more on screen, no fade was cut short, it survives, ends up settled on
d3 and never went back in a crossfade.

Watch the window while it runs; each switch is announced just before it's sent.
Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import re
import sys
import tempfile
import time
from datetime import datetime

import harness as h
import server

DREAMS = 4
LEAD_SECONDS = 4.0  # seek this long before the fade window opens
SETTLE_SECONDS = 4.0  # after a switch, before looking at the result
LOG_TIME = r"\[(\S+ \S+) - \w+\]: "


def when(stamp):
    return datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def cached(run):
    folder = run.data / "content" / "mp4"
    return {p.stem for p in folder.glob("*.mp4")} if folder.exists() else set()


def latest_beat(run, since):
    beats = [b for b in run.heartbeats() if b["time"] > since and b.get("current", "-") != "-"]
    return beats[-1] if beats else None


def settle_on(run, uuid, timeout=30):
    """Waits for a heartbeat showing `uuid` playing with no crossfade running."""
    since = time.time()

    def settled():
        run.require_alive()
        beat = latest_beat(run, since)
        return beat if beat and beat["current"].startswith(uuid[:8]) and \
            beat["transitioning"] == "0" else None
    return h.wait_for(f"the client to settle on {uuid[:8]}", settled, timeout)


def frame_rate(run, uuid, since):
    """Frames a second the client plays `uuid` at, from its heartbeats since `since`."""
    def measured():
        run.require_alive()
        beats = [b for b in run.heartbeats() if b["time"] > since and
                 b.get("current", "-").startswith(uuid[:8]) and b["transitioning"] == "0"]
        return beats if len(beats) >= 4 and beats[-1]["time"] - beats[0]["time"] >= 3 else None
    beats = h.wait_for(f"a few heartbeats of {uuid[:8]}", measured, 30)
    f0, f1 = (int(b["frame"].split("/")[0]) for b in (beats[0], beats[-1]))
    rate = (f1 - f0) / (beats[-1]["time"] - beats[0]["time"])
    if rate <= 0:
        raise h.Failed(f"Couldn't measure the frame rate of {uuid[:8]}")
    return rate


def trial(run, fraction, fade, d, playlists, rate):
    a = d[1]
    run.send("play_playlist", uuid=playlists[0])
    settle_on(run, d[0])
    mark = len(run.log_text())
    seek = max(0, run.dream_frames[a] - int((fade + LEAD_SECONDS) * rate))
    run.send("play_dream", uuid=a, frameNumber=seek)

    pattern = re.compile(LOG_TIME + r"Fade out started for " + re.escape(a))

    def fade_opened():
        run.require_alive()
        return pattern.search(run.log_text()[mark:])
    opened = h.wait_for(f"{a[:8]}'s fade window to open", fade_opened, LEAD_SECONDS + 20)
    target = when(opened[1]) + fraction * fade
    print(f"      switching to playlist 2 at {fraction:.0%} of the {fade:g} s crossfade "
          f"{a[:8]} -> next", flush=True)
    time.sleep(max(0.0, target - time.time()))
    sent = time.time()
    run.send("play_playlist", uuid=playlists[1])
    time.sleep(SETTLE_SECONDS)
    beat = settle_on(run, d[3], timeout=20)

    log = run.log_text()[mark:]
    interrupted = re.search(LOG_TIME + r"Playlist switch interrupted crossfade (\S+) -> (\S+) "
                            r"at (\d+)%: fading from (\w+)", log)
    applied = re.search(LOG_TIME + r"Applying queued playlist change", log)
    cut = re.search(LOG_TIME + r"Crossfade to (\S+) cut short at (\d+)%: outgoing clip (\S+) "
                    r"finished", log)
    back = re.findall(r"Crossfade to \S+ went back from \d+% to \d+%", log)
    cancelled = re.findall(r"Cancelling transition[^\n]*", log)
    return {
        "fraction": fraction,
        "latency_ms": (when(applied[1]) - sent) * 1000 if applied else None,
        "interrupted": (interrupted[2], interrupted[3], int(interrupted[4]), interrupted[5])
        if interrupted else None,
        "cut": int(cut[3]) if cut else None,
        "back": back, "cancelled": cancelled, "settled": beat["current"],
    }


def scenario(run, fractions, fade):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check("Client is playing", True)

    d = list(run.dreams)
    if len(d) < DREAMS:
        raise h.Failed(f"Needs {DREAMS} dreams of at least 900 frames, got {len(d)}")

    def all_cached():
        run.require_alive()
        return set(d) <= cached(run)
    h.wait_for(f"the client to download all {len(d)} dreams", all_cached, 240)
    h.check("Every dream is on the local disk", True, f"{len(d)} dreams")

    playlists = [server.playlist_uuid(1), server.playlist_uuid(2)]
    if run.playlists[playlists[1]][0] != d[3]:
        raise h.Failed("Playlist 2 doesn't start with the last dream")
    names = {u: f"d{i}" for i, u in enumerate(d)}
    print("      dreams: " + ", ".join(f"d{i}={u[:8]}" for i, u in enumerate(d)), flush=True)

    run.send("play_playlist", uuid=playlists[0])
    since = settle_on(run, d[0])["time"]
    rate = frame_rate(run, d[0], since)
    print(f"      playing at {rate:.1f} frames a second", flush=True)

    results = [trial(run, f, fade, d, playlists, rate) for f in fractions]

    print("\n      fraction  switch landed at  faded from  cut short at", flush=True)
    for r in results:
        i = r["interrupted"]
        landed = f"{i[2]:>3}% of {names.get(i[0], i[0][:8])}->{names.get(i[1], i[1][:8])}" \
            if i else "  (no crossfade running)"
        kept = names.get(i[0] if i[3] == "outgoing" else i[1], "?") if i else "-"
        cut = f"{r['cut']}%" if r["cut"] is not None else "-"
        print(f"      {r['fraction']:>7.0%}   {landed:<17} {kept:<11} {cut}", flush=True)
    print(flush=True)

    hits = [r for r in results if r["interrupted"]]
    h.check("Every switch landed during a crossfade", len(hits) == len(results),
            f"{len(hits)} of {len(results)}")
    h.check("The interrupted crossfade was d1 -> d2",
            all(i[0] == d[1] and i[1] == d[2] for i in (r["interrupted"] for r in hits)),
            "; ".join(f"{names.get(r['interrupted'][0])}->{names.get(r['interrupted'][1])}"
                      for r in hits))
    h.check("Each switch faded from the clip more on screen",
            all((i[3] == "incoming") == (i[2] >= 50) for i in (r["interrupted"] for r in hits)
                if abs(i[2] - 50) > 1),  # the log rounds, so 50% itself could go either way
            "; ".join(f"{r['interrupted'][2]}%: {r['interrupted'][3]}" for r in hits))
    cuts = [f"{r['fraction']:.0%}: cut at {r['cut']}%" for r in results if r["cut"] is not None]
    h.check("No switch's crossfade was cut short", not cuts, "; ".join(cuts) or "none")
    h.check("Every switch ended settled on d3", all(r["settled"].startswith(d[3][:8]) for r in results))
    back = [b for r in results for b in r["back"]]
    h.check("No crossfade went back", not back, "; ".join(back) or "none")
    cancelled = [c for r in results for c in r["cancelled"]]
    h.check("No transition was cancelled", not cancelled, "; ".join(cancelled) or "none")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--transition-time", type=float, default=5.0,
                        help="the playlists' end-of-dream fade in seconds (default 5, the real default)")
    parser.add_argument("--fractions", default="0.1,0.3,0.4,0.6,0.7,0.9",
                        help="how far into the crossfade to switch, one trial each")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)
    fractions = [float(f) for f in args.fractions.split(",")]

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-playlist-interrupt-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, screenshots=False,
                server_args=("--dreams", str(DREAMS), "--transition-time", str(args.transition_time)))
    try:
        run.start()
        scenario(run, fractions, args.transition_time)
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
