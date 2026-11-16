#!/usr/bin/env python3
"""The buffering moon stays hidden on next between cached dreams (client#823).

Serves the server's dreams over an unlimited link and waits until the client has
downloaded every one. Then it sends next a number of times. Each next is a crossfade
into a dream on the local disk. The incoming decoder takes ~100 ms to fill its first
frames, so the crossfade is often held for a frame or two. That used to show the moon
for half a second, nearly every time. The client logs "Buffering moon shown" whenever
the moon starts to fade in, and no such line may follow the first next.

throttle_test.py checks the other side: the moon does show while the link is starved.
Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import re
import sys
import tempfile
import time

import harness as h

NEXTS = 8
NEXT_GAP = 4.0  # s between nexts: a 1 s crossfade, then a few seconds of the new dream


def cached(run):
    folder = run.data / "content" / "mp4"
    return {p.stem for p in folder.glob("*.mp4")} if folder.exists() else set()


def scenario(run):
    run.wait_until_ready()
    run.wait_until_playing()
    h.check("Client is playing", True)

    def all_cached():
        run.require_alive()
        return set(run.dreams) <= cached(run)

    h.wait_for(f"the client to download all {len(run.dreams)} dreams", all_cached, 180)
    h.check("Every dream is on the local disk", True, f"{len(run.dreams)} dreams")

    start = len(run.log_text())
    for _ in range(NEXTS):
        run.send("next")
        time.sleep(NEXT_GAP)
        run.require_alive()
    log = run.log_text()[start:]

    nexts = re.findall(r"Next dream \S+ is (not )?cached", log)
    h.check(f"Sent next {NEXTS} times, each to a cached dream",
            len(nexts) == NEXTS and not any(nexts),
            f"{len(nexts)} nexts, {sum(1 for n in nexts if n)} not cached")
    # How often the case behind #823 came up: a held crossfade used to show the moon.
    held = len(re.findall(r"Crossfade to \S+ held at", log))
    shown = log.count("Buffering moon shown")
    h.check("Buffering moon never shown on next between cached dreams", shown == 0,
            f"shown {shown} times; {held} of {len(nexts)} crossfades were held while the "
            "incoming dream filled its first frames")


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

    work = tempfile.mkdtemp(prefix="infinidream-moon-")
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
