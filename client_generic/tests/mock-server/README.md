# Mock server

A small stand-in for the infinidream backend, for testing the client without real
servers (#733). It has a total rate limit on media and can send any socket message, including
malformed ones, but no other faults or chaos yet. Mac only for now.

- `server.py` serves the client API, the `/remote-control` socket and media on one port. Media
  can be limited to a set total rate with `--media-rate`, or during a run with
  `POST /control/media-rate?bytes_per_second=N`. `--pick largest` serves the biggest videos.
  `POST /control/emit` sends any Socket.IO event, or a raw frame, to the client.
- `harness.py` is shared by the tests: it launches the client in its own data folder, drives
  it over the socket, stops it with the test-only `quit` command, and makes the checks every
  test needs, including crash and hang detection (below).
- `e2e_test.py` watches the client play, sends `next` and `pause`, and checks the results,
  including screenshots of the window.
- `commands_test.py` sends `next` five times, `previous`, `play_dream` with and without a
  frame number, `forward` and `backward`, and checks each one's effect in the heartbeat.
- `crash_test.py` sends malformed remote-control messages, one per fresh client, and checks
  the client survives each. `--list` shows the cases; `--case NAME` runs one.
- `throttle_test.py` serves the largest videos over a limited link, drops the rate below
  what the first dream needs and raises it again, and checks that the client rebuffers and
  recovers while its render loop keeps going. It takes about 45 seconds, and runs
  without screenshots, because taking them stalls the render loop.
- `fixtures.py` makes and checks fixture sets (see "Videos").
- `screens.py` takes about one screenshot of the client's window a second, to catch a black
  or frozen picture. It can't see individual dropped frames.

## Running the tests

```bash
python3 -m venv venv
venv/bin/pip install -r requirements.txt
brew install ffmpeg   # for ffprobe

APP="/path/to/infinidream.app/Contents/MacOS/infinidream"
venv/bin/python e2e_test.py --app "$APP"
venv/bin/python commands_test.py --app "$APP"
venv/bin/python crash_test.py --app "$APP"
venv/bin/python throttle_test.py --app "$APP"
```

Each takes `--set NAME` to serve a fixture set, and `--keep` to keep the run folder (logs,
server events, crash reports, stack samples) even when everything passes; it's always kept
when a check fails.

The screen checks need Screen Recording permission for your terminal (System Settings >
Privacy & Security); without it they're skipped, and the test says so.

The client opens a window, and the screen has to stay unlocked, because a locked screen
stops the render loop. `caffeinate -dimsu venv/bin/python ...` keeps the screen saver and
sleep away during a run. Quit any other running client first, or the check that real installs
are unchanged may fail. A test that crashes the client makes macOS show a crash dialog;
`defaults write com.apple.CrashReporter DialogType none` turns those off (the reports are
still written) and `defaults delete com.apple.CrashReporter DialogType` turns them back on.

## Crashes and hangs

Every test ends with these checks:

- **The client kept running** until the test stopped it.
- **It quit cleanly when asked.** The harness sends the test-only `quit` command, which takes
  the same path as Cmd-Q and which the client honors only when `INFINIDREAM_DATA_DIR` is set.
  If the client doesn't exit within 15 s, the harness samples its stacks, then sends SIGTERM.
  After a clean quit, no partial (`.tmp`) downloads may be left.
- **No crash report.** macOS crash reports for the client's process are copied into the run
  folder and summarized. On master the client catches SIGSEGV and calls `exit(0)` (#769), so
  a report often shows a second crash during that exit; the summary then also shows the
  original fault, the frames just below the client's signal handler.
- **No crash hidden by that handler:** `RECEIVED SIGSEGV` in the log fails the run even when
  no report was written.
- **No hang:** no heartbeat for 3 s, or the current clip's frame not moving for 5 s while the
  client says it's neither paused nor buffering. The stacks are sampled (`stacks-hang.txt`).
  A locked screen also stops heartbeats, so it's reported as that instead.

## Frame timing

With `INFINIDREAM_HEARTBEAT_SECONDS=1`, the client logs a heartbeat every second with late
ticks (more than 1.5 display intervals), ticks clamped at 100 ms and the time they lost, and
for each clip the frames discarded to catch up and the number of rebuffers. To check these
counters, `INFINIDREAM_TEST_STALL_MS=150` makes the render thread spin for 150 ms every
`INFINIDREAM_TEST_STALL_EVERY_MS` (default 5000). A stall of 3 s or more trips the hang check.

## Videos

No videos are stored in this repo. By default the server reads `<uuid>.mp4` files from
`--media-dir`, which defaults to the alpha client's cache at
`/Users/Shared/infinidream.ai/content/mp4`, and serves the three smallest (or with
`--pick largest`, the largest) that have at least 900 frames, so a dream doesn't end by itself
during a test. Nothing in that folder is changed.

That cache changes as the client runs, so for repeatable runs make a **fixture set**, a frozen
copy with a manifest of md5s, and serve it with `--set`:

```bash
venv/bin/python fixtures.py import-cache --set local-v1   # 6 videos spread across sizes
venv/bin/python fixtures.py list
venv/bin/python e2e_test.py --app "$APP" --set local-v1
```

Sets live in `~/edream-fixtures/<name>/` (or under `INFINIDREAM_FIXTURES`), and each run checks
the set's md5s before starting. Shared sets of alpha dreams fetched through the client API come
later (#733, milestone 2b).
