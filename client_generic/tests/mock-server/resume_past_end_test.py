#!/usr/bin/env python3
"""A saved resume position past the end of its dream plays that dream from the start (#666).

The client saves the frame it was on when it quits (settings.content.last_played_frame) and
seeks there on the next start. A stale, hand-edited or wrapped value can be past the dream's
end; the client only rejected values beyond 24 hours at 60 fps. A seek past the end finds no
frames, so the dream was marked finished on arrival and never shown. #777 clamps play_dream
seeks, but the resume at startup doesn't go through it.

Starts a fresh client set to resume the first dream at three times its length, and checks it
plays that dream from near its start instead.

    venv/bin/python resume_past_end_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"

Mac only for now. The client opens a window, and the screen must stay unlocked.
"""
import argparse
import sys
import tempfile

import harness as h

PAST_END_FACTOR = 3  # resume at this many times the dream's length, well under the 24 h bound
PLAY_TIMEOUT = 60
# Frames into the dream that still count as "from the start" after a few heartbeats.
NEAR_START = 300


def scenario(run, uuid, frames, resume_at):
    run.wait_until_ready()
    no_frames = f"Decoder ended with no frames available for {uuid}"

    def playing_resumed():
        run.require_alive()
        beats = [b for b in run.heartbeats() if b.get("current") == uuid and "frame" in b]
        if len(beats) >= 3 and beats[-1]["frame"] != beats[-3]["frame"]:
            return beats[-1]
        # Give up early once the client has moved on from the dream.
        later = [b for b in run.heartbeats() if b.get("current", "-") not in ("-", uuid)]
        if later:
            raise h.Failed(f"moved on to {later[-1]['current'][:8]} without playing it")
        return None

    try:
        beat = h.wait_for("the resumed dream to play", playing_resumed, PLAY_TIMEOUT)
        frame = int(beat["frame"].split("/")[0])
        h.check(f"Resuming {uuid[:8]} at frame {resume_at} of {frames} played it", True,
                f"at frame {beat['frame']}")
        h.check("It played from the start", frame < NEAR_START, f"at frame {beat['frame']}")
    except h.Failed as failure:
        why = "the decoder found no frames there" if no_frames in run.log_text() else str(failure)
        h.check(f"Resuming {uuid[:8]} at frame {resume_at} of {frames} played it", False, why)
    h.check("The decoder found frames to play", no_frames not in run.log_text())


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
    resume_at = frames * PAST_END_FACTOR
    print(f"Resuming {uuid[:8]} ({frames} frames) at frame {resume_at}", flush=True)

    work = tempfile.mkdtemp(prefix="infinidream-resume-past-end-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, screenshots=False,
                content_settings=h.resume_settings(uuid, resume_at))
    try:
        run.start()
        scenario(run, uuid, frames, resume_at)
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
