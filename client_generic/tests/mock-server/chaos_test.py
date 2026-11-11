#!/usr/bin/env python3
"""Stress test (client#733): sends many remote-control commands quickly and at random, while
changing the media rate every few seconds, then checks the client survived and recovers.

Commands include next, previous, forward, backward, pause, speed changes, HUD toggles,
play_dream (any served dream, often at a random frame or near the end of the clip) and
play_playlist (the mock server's playlists, one of which has a single dream). Like, dislike,
report and web are left out: the first three need server endpoints the mock lacks, and web
opens a browser.

The run is random but replayable: --seed picks the same commands, gaps and rates again.
Every action is written to actions.jsonl in the run folder. Mac only for now.

    venv/bin/python chaos_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python chaos_test.py --app ... --seed 1234 --seconds 120
"""
import argparse
import json
import random
import sys
import tempfile
import time
from collections import Counter

import harness as h

DREAMS = 4
RECOVERY_TIMEOUT = 60
COMMAND_TIMEOUT = 20

# Media rates in bytes a second; 0 is no limit. The low ones starve the client.
RATES = [0, 0, 5_000_000, 2_000_000, 1_000_000, 500_000, 200_000, 50_000, 10_000]
RATE_SECONDS = (2.0, 6.0)  # how long each rate lasts

# (command, weight). play_dream and play_playlist are filled in with random arguments.
COMMANDS = [
    ("next", 10), ("previous", 6), ("forward", 6), ("backward", 6), ("pause", 4),
    ("play_dream", 8), ("play_playlist", 4),
    ("shuffle", 1), ("repeat", 1), ("playback_slower", 2), ("playback_faster", 2),
    ("set_speed", 2), ("brighter", 1), ("darker", 1), ("help", 1), ("status", 1),
    ("credit", 1), ("reset_playlist", 1),
]


def frame_for(rng, frames):
    """A frame to start a play_dream at, or None for no frameNumber."""
    kind = rng.choices(["none", "start", "random", "near_end", "past_end"], [3, 1, 4, 2, 1])[0]
    if kind == "none":
        return None
    if kind == "start":
        return 0
    if kind == "random":
        return rng.randrange(frames)
    if kind == "near_end":
        return rng.randrange(max(0, frames - 40), frames)
    return frames + rng.randrange(1, 100)


def make_command(rng, run):
    name = rng.choices([c for c, _ in COMMANDS], [w for _, w in COMMANDS])[0]
    if name == "play_dream":
        uuid = rng.choice(run.dreams)
        fields = {"uuid": uuid}
        frame = frame_for(rng, run.dream_frames[uuid])
        if frame is not None:
            fields["frameNumber"] = frame
        return name, fields
    if name == "play_playlist":
        return name, {"uuid": rng.choice(list(run.playlists))}
    if name == "set_speed":
        return f"set_speed_{rng.randint(1, 9)}", {}
    return name, {}


def gaps(rng):
    """Yields the pauses between commands: mostly short, some bursts, some calm stretches."""
    while True:
        kind = rng.choices(["quick", "burst", "calm"], [7, 2, 1])[0]
        if kind == "quick":
            yield rng.expovariate(1 / 0.4)
        elif kind == "burst":
            for _ in range(rng.randint(3, 6)):
                yield 0.05
        else:
            yield rng.uniform(2, 4)


class Actions:
    """Writes every action to actions.jsonl, for reading alongside the client log."""

    def __init__(self, path, started):
        self.file, self.started = open(path, "w", buffering=1), started

    def log(self, kind, **fields):
        self.file.write(json.dumps({"t": round(time.time() - self.started, 3), "kind": kind,
                                    **fields}) + "\n")


def chaos(run, rng, seconds, actions):
    counts, rates = Counter(), []
    gap = gaps(rng)
    end = time.time() + seconds
    next_rate = time.time()
    while time.time() < end:
        run.require_alive()
        if time.time() >= next_rate:
            rate = rng.choice(RATES)
            run.set_media_rate(rate)
            rates.append(rate)
            actions.log("media_rate", bytes_per_second=rate)
            next_rate = time.time() + rng.uniform(*RATE_SECONDS)
        name, fields = make_command(rng, run)
        run.send(name, **fields)
        counts[name] += 1
        actions.log("send", event=name, **fields)
        time.sleep(next(gap))
    return counts, rates


def latest(run):
    run.require_alive()
    beats = run.heartbeats()
    return beats[-1] if beats else None


def recover(run, actions):
    """Lifts the rate limit, unpauses, returns to the default playlist and checks that
    playback and next work again."""
    run.set_media_rate(0)
    actions.log("media_rate", bytes_per_second=0)
    time.sleep(2)
    beat = latest(run)
    if beat and beat.get("paused") == "1":
        run.send("pause")
        actions.log("send", event="pause")

    default = next(iter(run.playlists))
    run.send("play_playlist", uuid=default)
    actions.log("send", event="play_playlist", uuid=default)
    sent = time.time()

    def playing():
        beats = [b for b in run.heartbeats()[-4:] if b["time"] > sent]
        if (len(beats) >= 3 and all(b.get("paused") == "0" and b.get("current", "-") != "-"
                                    for b in beats)
                and beats[-1]["current"] == beats[-3]["current"]
                and beats[-1]["frame"] != beats[-3]["frame"]):
            return beats[-1]
        run.require_alive()
        return None

    try:
        beat = h.wait_for("playback to resume", playing, RECOVERY_TIMEOUT)
        h.check("Playback resumed after the chaos", True,
                f"{beat['current'][:8]} at frame {beat['frame']} after {time.time() - sent:.0f} s")
    except h.Failed:
        last = latest(run) or {}
        h.check("Playback resumed after the chaos", False,
                f"last heartbeat: current={last.get('current')} frame={last.get('frame')} "
                f"paused={last.get('paused')} buffering={last.get('buffering_pause')}")
        return

    time.sleep(2)
    now = latest(run)["current"]
    run.send("next")
    actions.log("send", event="next")
    sent = time.time()

    def changed():
        beat = latest(run)
        return beat if beat and beat["time"] > sent and beat.get("current", "-") not in ("-", now) else None

    try:
        beat = h.wait_for("next to change the dream", changed, COMMAND_TIMEOUT)
        h.check("next still works after the chaos", True, f"{now[:8]} -> {beat['current'][:8]}")
    except h.Failed:
        h.check("next still works after the chaos", False, f"stayed on {now[:8]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.add_argument("--seconds", type=float, default=60, help="how long to send commands")
    parser.add_argument("--seed", type=int, help="replay a run; random if not given")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)
    seed = args.seed if args.seed is not None else random.randrange(1_000_000)
    rng = random.Random(seed)

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-chaos-")
    print(f"Run folder: {work}", flush=True)
    print(f"Seed {seed} (replay with --seed {seed})", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, server_args=("--dreams", str(DREAMS)),
                screenshots=False)
    actions = None
    try:
        run.start()
        actions = Actions(f"{work}/actions.jsonl", run.started_at)
        actions.log("seed", seed=seed)
        run.wait_until_ready()
        first = run.wait_until_playing()["current"]
        h.check("Client is playing", True, first[:8])

        counts, rates = chaos(run, rng, args.seconds, actions)
        h.check(f"Client survived {sum(counts.values())} commands and {len(rates)} rate changes "
                f"in {args.seconds:.0f} s", True,
                ", ".join(f"{name} {n}" for name, n in counts.most_common()))
        recover(run, actions)

        played = {b["current"] for b in run.heartbeats() if b.get("current", "-") != "-"}
        h.check("Every dream played is one the server offered", played <= set(run.dreams),
                ", ".join(u[:8] for u in played - set(run.dreams)))
    except h.Failed as failure:
        h.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before)
    if not all(h.results):
        print(f"Replay with --seed {seed}", flush=True)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
