#!/usr/bin/env python3
"""Navigation stress test (client#733): chaos_test.py limited to moving around, over a link
whose rate changes every 1 to 4 seconds. Sends next (right arrow), previous (left arrow),
forward and backward (10 s), play_playlist, play_dream (often at a random frame or near
the end of the clip) and playback_faster and playback_slower, quickly and at random, then checks the client recovers. Half the
commands come as a burst: the same command 1 to 10 times, 50 ms apart, like a held-down
arrow key. Takes the same options as chaos_test.py, including --seed to replay a run. Mac only for now.

    venv/bin/python navigation_chaos_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
"""
import chaos_test

COMMANDS = [
    ("next", 10), ("previous", 8), ("forward", 6), ("backward", 6),
    ("play_playlist", 4), ("play_dream", 6), ("playback_faster", 4), ("playback_slower", 4),
]
RATE_SECONDS = (1.0, 4.0)
BURST_CHANCE = 0.5     # chance that a command is sent as a burst
BURST_SIZE = (1, 10)   # sends in a burst, counting the first
BURST_GAP = 0.05       # seconds between the sends in a burst

pick_command, pick_gaps = chaos_test.make_command, chaos_test.gaps
burst = {"command": None, "left": 0}  # the command being repeated, and sends still to go


def make_command(rng, run, commands=COMMANDS):
    if burst["left"]:
        burst["left"] -= 1
        return burst["command"]
    command = pick_command(rng, run, commands)
    if rng.random() < BURST_CHANCE:
        burst["command"], burst["left"] = command, rng.randint(*BURST_SIZE) - 1
    return command


def gaps(rng):
    normal = pick_gaps(rng)
    while True:
        yield BURST_GAP if burst["left"] else next(normal)


chaos_test.make_command, chaos_test.gaps = make_command, gaps

if __name__ == "__main__":
    # Skipping this often cancels most downloads before they finish, so the cache may never
    # fill: report evictions, but don't require one.
    chaos_test.main(__doc__, COMMANDS, RATE_SECONDS, prefix="navigation", require_eviction=False)
