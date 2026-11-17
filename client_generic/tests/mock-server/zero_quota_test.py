#!/usr/bin/env python3
"""With no quota and nothing cached, the client still moves on when a dream ends (#666).

When a dream ends by itself, the client only moves to a cached dream (by design, #269), and with
no quota the background downloader caches nothing. #666 reported that this busy-looped: after
the first dream ended, every frame asked for the next dream and found nothing. The client
streams the first dream whatever its quota, and since #725 a streamed dream that downloads in
full is added to the cache, so it should always have that one to go back to. This checks that
it does, and doesn't need #666's fallback of streaming from the end of a dream.

Starts a fresh client with zero quota, set to resume the first dream at its last frame, which
is how #666 was first seen, so the dream ends as soon as it starts. Checks:
- the client was told it has no quota, and fetched no dream but the one it streamed;
- the streamed dream was cached;
- once it ended, another dream (here, the same one again) started playing;
- the wait in between asked for a next dream at most about once a second (#714), not every
  frame.

    venv/bin/python zero_quota_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import re
import sys
import tempfile
import time

import harness as h

STARTED = re.compile(r"starting playback(?: anyway)? for (\S+)")
NO_CACHED = "Preflight : no cached dreams available and canStream=false"
FINISH_TIMEOUT = 60
CACHE_TIMEOUT = 60
MOVE_ON_TIMEOUT = 30
MAX_TRIES_PER_SECOND = 2  # 1 a second (#714), with some room; every frame would be 20 or more


def scenario(run, uuid, frames):
    run.wait_until_ready()
    log = run.log_text
    seen = lambda text: run.require_alive() or text in log()

    zero = re.search(r"remaining quota : (-?\d+)", log())
    h.check("Client was told it has no quota", bool(zero) and int(zero[1]) == 0,
            f"remaining quota {zero[1]}" if zero else "no handshake line")

    starts = lambda: [m[1] for m in STARTED.finditer(log())]
    h.wait_for(f"{uuid[:8]} to start", lambda: run.require_alive() or uuid in starts(),
               FINISH_TIMEOUT)
    first_start = next(m.end() for m in STARTED.finditer(log()) if m[1] == uuid)
    ended = time.time()
    # The next dream starts buffering during the end-of-dream crossfade, so its start is logged
    # before "Finished dream" for this one.
    h.wait_for(f"{uuid[:8]} to finish", lambda: seen(f"Finished dream {uuid}"), FINISH_TIMEOUT)
    h.check(f"Resumed at its last frame, {uuid[:8]} ended", True)

    mp4 = run.data / "content" / "mp4" / f"{uuid}.mp4"
    try:
        h.wait_for("the streamed dream to be cached", lambda: run.require_alive() or mp4.exists(),
                   CACHE_TIMEOUT)
        h.check("The streamed dream was cached", True)
    except h.Failed:
        h.check("The streamed dream was cached", False, f"no {mp4.name} after {CACHE_TIMEOUT} s")

    def next_start():
        run.require_alive()
        found = [m[1] for m in STARTED.finditer(log(), first_start)]
        return found[0] if found else None

    try:
        nxt = h.wait_for("another dream to start", next_start, MOVE_ON_TIMEOUT)
        waited = time.time() - ended
        h.check("Another dream started after it ended", True,
                f"{nxt[:8]}{' (the same one)' if nxt == uuid else ''} after {waited:.1f} s")
    except h.Failed:
        beat = run.heartbeats()[-1] if run.heartbeats() else {}
        waited = time.time() - ended
        h.check("Another dream started after it ended", False,
                f"still on {beat.get('current', '?')[:8]} at frame {beat.get('frame')} "
                f"{MOVE_ON_TIMEOUT} s after it ended")
    # Count from the first start: every try after that is the client waiting to move on.
    finished_at = first_start

    tries = log().count(NO_CACHED, finished_at)
    h.check("It didn't busy-loop while waiting",
            tries <= MAX_TRIES_PER_SECOND * max(waited, 1) + 2,
            f"{tries} 'no cached dreams' warnings in {waited:.0f} s")

    others = sorted({e["path"].split("/")[-1][:8] for e in run.events("http")
                     if e["path"].startswith("/media/") and not e["path"].endswith(uuid)})
    h.check("No other dream was downloaded", not others, ", ".join(others))


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

    dreams = h.load_dreams(args.media_dir, 3, 900)
    first = h.first_dream(args.media_dir, dreams)
    uuid, frames = first["uuid"], int(first["frames"])
    print(f"Resuming {uuid[:8]} ({frames} frames) at its last frame, with zero quota", flush=True)

    work = tempfile.mkdtemp(prefix="infinidream-zero-quota-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    # The client holds the ended dream's last frame while it waits, which isn't a hang.
    run = h.Run(work, args.app, args.media_dir, screenshots=False,
                max_stuck=FINISH_TIMEOUT + CACHE_TIMEOUT + MOVE_ON_TIMEOUT,
                server_args=("--quota", "0"),
                content_settings=h.resume_settings(uuid, frames - 1))
    try:
        run.start()
        scenario(run, uuid, frames)
    except h.Failed as failure:
        h.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before, require_download=False)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
