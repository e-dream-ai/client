#!/usr/bin/env python3
"""Navigation stress test (client#733): chaos_test.py limited to moving around, over a link
whose rate changes every 1 to 4 seconds. Sends next (right arrow), previous (left arrow),
forward and backward (10 s), play_playlist and play_dream (often at a random frame or near
the end of the clip), quickly and at random, then checks the client recovers. Takes the same
options as chaos_test.py, including --seed to replay a run. Mac only for now.

    venv/bin/python navigation_chaos_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
"""
import chaos_test

COMMANDS = [
    ("next", 10), ("previous", 8), ("forward", 6), ("backward", 6),
    ("play_playlist", 4), ("play_dream", 6),
]
RATE_SECONDS = (1.0, 4.0)

if __name__ == "__main__":
    chaos_test.main(__doc__, COMMANDS, RATE_SECONDS, prefix="navigation")
