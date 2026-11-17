#!/usr/bin/env python3
"""Switching to a playlist with nothing cached doesn't replay its first dream (client#835).

When a dream ends by itself, the client may only move on to a cached dream (#269). Right after
a switch to a new playlist, only the first dream is cached, because it was streamed. So the
client replayed it, again and again, until the background downloader had fetched the second.
The downloader made that worse: once it had nothing left to download, it slept 10 s, and a
playlist switch didn't wake it. Seen on the Wankel Rotary Engine playlist at 80 fps, where
each dream lasts 3 s: the first dream played four times.

Serves every dream of the alpha-v1 set and starts the client at 80 fps on the mock server's
one-dream playlist, which holds the set's largest dream, a Pink Floyd one, not in Wankel. It
waits for the client to cache that dream and for the downloader to report it has nothing left to
fetch, which is when it goes to sleep. Then it switches to the Wankel Rotary Engine playlist
(193-frame dreams), none of them cached, and checks that the first few dreams after the switch
are all different and in the playlist's order.
One fresh client per case:

  fast-link  no limit on media. The downloader must wake on the switch: it fetches the next
             dream long before the first one ends.
  slow-link  media limited to SLOW_RATE from the switch on, so the next dream can't be
             downloaded before the first ends. The client must stream it, not replay the first.

    venv/bin/python uncached_playlist_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python uncached_playlist_test.py --app ... --case slow-link

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import harness as h
import server

DEFAULT_SET = "alpha-v1"
WANKEL = "d9726526-b8f2-4221-a0bf-67fbacc01f4d"
SINGLE = server.playlist_uuid(3)  # the mock server's one-dream playlist
START_FPS = 80.0
DISTINCT = 4  # dreams that must start, all different, after the switch
SLOW_RATE = 1_500_000  # bytes a second: a Wankel dream (4-9 MB) takes 3-6 s to download

MOVED = re.compile(r"moveToNextDream : (\S+) ")
IDLE = "are already cached — nothing to download"

CASES = {
    "fast-link": {"rate": 0, "timeout": 60},
    "slow-link": {"rate": SLOW_RATE, "timeout": 120},
}


def starts_after(run, since):
    return [m[1] for m in MOVED.finditer(run.log_text(), since)]


def first_repeat(uuids):
    seen = set()
    for i, uuid in enumerate(uuids):
        if uuid in seen:
            return i
        seen.add(uuid)
    return None


def scenario(run, name, rate, timeout, order):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check(f"{name}: client is playing the one-dream playlist", True)
    # The downloader logs this once, when it finds nothing to fetch, just before it sleeps.
    h.wait_for("the downloader to go idle", lambda: run.require_alive() or IDLE in run.log_text(), 180)
    cached = {p.stem for p in (run.data / "content" / "mp4").glob("*.mp4")}
    h.check(f"{name}: downloader idle with no Wankel dream cached",
            not cached & set(order), f"{len(cached)} cached")

    if rate:
        run.set_media_rate(rate)
    mark = len(run.log_text())
    run.send("play_playlist", uuid=WANKEL)

    def enough():
        run.require_alive()
        starts = starts_after(run, mark)
        if first_repeat(starts) is not None or len(set(starts)) >= DISTINCT:
            return starts
        return None

    starts = h.wait_for(f"{DISTINCT} dreams to start after the switch", enough, timeout)
    repeat = first_repeat(starts)
    letters = {u: f"#{order.index(u) + 1}" if u in order else u[:8] for u in starts}
    path = " ".join(letters[u] for u in starts)
    h.check(f"{name}: no dream replayed after switching to an uncached playlist", repeat is None,
            f"started {path}" + (f"; {letters[starts[repeat]]} again" if repeat is not None else ""))
    expected = order[:len(starts)]
    h.check(f"{name}: the dreams play in the playlist's order", starts == expected,
            f"started {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--case", action="append", choices=list(CASES), help="run only these")
    parser.add_argument("--keep", action="store_true", help="keep the run folders even if they pass")
    args = parser.parse_args()
    if args.playlist:
        parser.error("this test chooses its own playlists")
    if not args.set:
        args.set = DEFAULT_SET
    h.resolve_media(args)
    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    manifest = json.loads((Path(args.media_dir) / "manifest.json").read_text())
    order = next(p["uuids"] for p in manifest["playlists"] if p["id"] == WANKEL)

    root = Path(tempfile.mkdtemp(prefix="infinidream-uncached-"))
    print(f"Run folder: {root}", flush=True)
    before = h.snapshot_real_installs()
    for name in args.case or list(CASES):
        case = CASES[name]
        print(f"\n== {name}", flush=True)
        # Serve every dream; start on the one-dream playlist, which holds the largest.
        run = h.Run(root / name, args.app, args.media_dir, screenshots=False,
                    player_settings={"perceptual_fps": START_FPS},
                    server_args=("--dreams", "0", "--min-frames", "0",
                                 "--default-playlist", SINGLE))
        try:
            run.start()
            scenario(run, name, case["rate"], case["timeout"], order)
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
