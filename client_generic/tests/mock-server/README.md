# Mock server

A small stand-in for the infinidream backend, for testing the client without real
servers (#733). This is an early version: a total rate limit on media, but no faults or chaos yet.

- `server.py` serves the client API, the `/remote-control` socket and media on one port. Media
  can be limited to a set total rate with `--media-rate`, or during a run with
  `POST /control/media-rate?bytes_per_second=N`. `--pick largest` serves the biggest videos.
- `e2e_test.py` launches the client against it, watches it play, sends `next` and `pause`,
  and checks the results. It's Mac only for now.
- `throttle_test.py` serves the largest videos over a limited link, drops the rate below
  what the first dream needs and raises it again, and checks that the client rebuffers and
  recovers while its render loop keeps going. It takes about 45 seconds, and runs
  without screenshots, because taking them stalls the render loop.
- `screens.py` takes about one screenshot of the client's window a second, to catch a black
  or frozen picture. It can't see individual dropped frames.

## Running the test

```bash
python3 -m venv venv
venv/bin/pip install -r requirements.txt
brew install ffmpeg   # for ffprobe

venv/bin/python e2e_test.py --app "/path/to/infinidream stage.app/Contents/MacOS/infinidream stage"
```

The screen checks need Screen Recording permission for your terminal (System Settings >
Privacy & Security); without it they're skipped, and the test says so.

The client opens a window, and the screen has to stay unlocked, because a locked screen
stops the render loop. Quit any other running client first, or the check that real installs
are unchanged may fail. The run's logs are kept in a temp folder if a check fails, or with `--keep`.

The throttle test runs the same way: `venv/bin/python throttle_test.py --app ...`.

## Frame timing

With `INFINIDREAM_HEARTBEAT_SECONDS=1`, the client logs a heartbeat every second with late
ticks (more than 1.5 display intervals), ticks clamped at 100 ms and the time they lost, and
for each clip the frames discarded to catch up and the number of rebuffers. To check these
counters, `INFINIDREAM_TEST_STALL_MS=150` makes the render thread spin for 150 ms every
`INFINIDREAM_TEST_STALL_EVERY_MS` (default 5000).

## Videos

No videos are stored in this repo. The server reads `<uuid>.mp4` files from `--media-dir`,
which defaults to the alpha client's cache at `/Users/Shared/infinidream.ai/content/mp4`,
and serves the three smallest (or with `--pick largest`, the largest) that have at least 900
frames, so a dream doesn't end by itself during a test. Nothing in that folder is changed.
