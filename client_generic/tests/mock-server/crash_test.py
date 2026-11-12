#!/usr/bin/env python3
"""Sends malformed remote-control messages and checks the client survives them (client#733).

The real backend validates what it sends, but the client shouldn't trust the server. Each case
starts a fresh client, waits until it plays, sends one message over the /remote-control
socket, and then checks that the client is still running and still playing, with the usual
crash and hang verdicts. A case that crashes the client can't share a client with the next,
so each takes about 30 seconds. Mac only for now.

    venv/bin/python crash_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python crash_test.py --app ... --case no-event --case frame-number-string
"""
import argparse
import sys
import tempfile
import time
from pathlib import Path

import harness as h

EVENT = "new_remote_control_event"
WATCH_SECONDS = 8  # How long the client must keep playing after the message.


def cases(dreams):
    """name -> (what it sends, {"args": [...]} or {"raw": "..."})."""
    some_dream = dreams[0]
    return {
        # A control: well-formed, unknown to the client, which should log it and carry on.
        "unknown-event": ("an event name the client doesn't know",
                          {"args": [EVENT, {"event": "no_such_event"}]}),
        "no-payload": ("the event with no payload at all", {"args": [EVENT]}),
        "not-object": ("a string where an object belongs", {"args": [EVENT, "next"]}),
        "no-event": ("an object with no 'event' field", {"args": [EVENT, {"uuid": some_dream}]}),
        "event-not-string": ("'event' as a number", {"args": [EVENT, {"event": 5}]}),
        "play-dream-no-uuid": ("play_dream with no uuid", {"args": [EVENT, {"event": "play_dream"}]}),
        "play-playlist-no-uuid": ("play_playlist with no uuid",
                                  {"args": [EVENT, {"event": "play_playlist"}]}),
        "frame-number-string": ("play_dream with frameNumber as a string",
                                {"args": [EVENT, {"event": "play_dream", "uuid": some_dream,
                                                  "frameNumber": "100"}]}),
    }


def run_case(name, description, message, app, media_dir, root, before):
    print(f"\n=== {name}: {description}", flush=True)
    start = len(h.results)
    run = h.Run(root / name, app, media_dir, screenshots=False)
    try:
        run.start()
        run.wait_until_ready()
        run.wait_until_playing()
        run.emit(**message)
        sent_at = time.time()
        time.sleep(WATCH_SECONDS)
        run.require_alive()
        beats = [b for b in run.heartbeats() if b["time"] > sent_at]
        frames = [(b.get("current"), b.get("frame")) for b in beats]
        h.check("Client kept playing after the message",
                len(beats) >= WATCH_SECONDS - 2 and len(set(frames)) > 1,
                f"{len(beats)} heartbeats since, {len(set(frames))} different frames")
    except h.Failed as failure:
        h.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before, require_download=False)
    return all(h.results[start:])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--case", action="append", help="run only this case; can repeat")
    parser.add_argument("--list", action="store_true", help="list the cases and exit")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)

    if args.list:
        for name, (description, _) in cases(["<uuid>"]).items():
            print(f"{name:24} {description}")
        return
    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    # The server picks the same dreams every time from the same folder, so ask it once.
    import server
    dreams = list(h.load_dreams(args.media_dir, 3, 900))
    chosen = cases(dreams)
    unknown = [c for c in args.case or [] if c not in chosen]
    if unknown:
        sys.exit(f"Unknown case {', '.join(unknown)}; --list shows them")

    work = tempfile.mkdtemp(prefix="infinidream-crash-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    outcome = {}
    for name in args.case or chosen:
        description, message = chosen[name]
        outcome[name] = run_case(name, description, message, args.app, args.media_dir,
                                 Path(work), before)

    print("\nSummary:", flush=True)
    for name, passed in outcome.items():
        print(f"  {'PASS' if passed else 'FAIL'}  {name}", flush=True)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
