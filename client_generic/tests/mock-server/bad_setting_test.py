#!/usr/bin/env python3
"""A setting stored as the wrong kind of number doesn't crash the client (#666).

settings.json is read with boost::json, whose as_uint64() throws when the stored number can't
be represented exactly as one: too big for 64 bits, like 1e23 (which the parser can only keep
as a double), or not a whole number, like 120.5. Nothing caught that, so a bad value terminated
the client. The resume frame (settings.content.last_played_frame) is read as a uint64 on every
start. The client writes the same setting when it quits, and that write must replace the bad
value, or every start reads it again.

One fresh client per case, each set to resume the first dream at a bad frame. Each must stay up,
play that dream, quit cleanly and leave an integer in last_played_frame.

  huge        1e23, too big for any 64-bit integer
  fractional  120.5, not a whole number
  whole       120.0, a whole number written as a double; boost::json converts it, so this one
              is the control

    venv/bin/python bad_setting_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python bad_setting_test.py --app ... --case huge

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

import harness as h

CASES = {
    "huge": 1e23,
    "fractional": 120.5,
    "whole": 120.0,
}


def scenario(run, uuid):
    run.wait_until_ready()
    beat = run.wait_until_playing()
    h.check("Client is playing", True, f"{beat['current'][:8]} at frame {beat['frame']}")
    h.check("It resumed the saved dream", beat["current"] == uuid,
            f"expected {uuid[:8]}, got {beat['current'][:8]}")


def saved_frame(run):
    try:
        settings = json.loads((run.data / "settings.json").read_text())
        return settings["settings"]["content"].get("last_played_frame")
    except (OSError, ValueError, KeyError) as error:
        return f"unreadable: {error}"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--case", action="append", choices=list(CASES), help="run only these")
    parser.add_argument("--keep", action="store_true", help="keep the run folders even if they pass")
    args = parser.parse_args()
    h.resolve_media(args)
    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    dreams = h.load_dreams(args.media_dir, 3, 900)
    uuid = h.first_dream(args.media_dir, dreams)["uuid"]

    root = Path(tempfile.mkdtemp(prefix="infinidream-bad-setting-"))
    print(f"Run folder: {root}", flush=True)
    before = h.snapshot_real_installs()
    for name in args.case or list(CASES):
        value = CASES[name]
        print(f"\n== {name}: last_played_frame = {json.dumps(value)}", flush=True)
        run = h.Run(root / name, args.app, args.media_dir, screenshots=False,
                    content_settings=h.resume_settings(uuid, value))
        try:
            run.start()
            scenario(run, uuid)
        except h.Failed as failure:
            h.check(f"{name} scenario", False, str(failure))
        finally:
            run.stop()
        if run.client:
            h.verify_process(run)
            if run.quit_cleanly:
                frame = saved_frame(run)
                h.check("The bad value was replaced with an integer on quit",
                        isinstance(frame, int) and not isinstance(frame, bool), json.dumps(frame))
    h.check("Real installs were not changed",
            not [k for k, v in h.snapshot_real_installs().items() if before.get(k) != v])
    h.finish(root, args.keep)


if __name__ == "__main__":
    main()
