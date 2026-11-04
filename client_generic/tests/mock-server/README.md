# Mock server

A small stand-in for the infinidream backend, for testing the client without real
servers (#733). This is the first, basic version: no network shaping, faults or chaos yet.

- `server.py` serves the client API, the `/remote-control` socket and media on one port.
- `e2e_test.py` launches the client against it, watches it play, sends `next` and `pause`,
  and checks the results. It's Mac only for now.
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

## Videos

No videos are stored in this repo. The server reads `<uuid>.mp4` files from `--media-dir`,
which defaults to the alpha client's cache at `/Users/Shared/infinidream.ai/content/mp4`,
and serves the three smallest that have at least 900 frames, so a dream doesn't end by
itself during a test. Nothing in that folder is changed.
