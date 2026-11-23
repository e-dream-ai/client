# Mock server

A small stand-in for the infinidream backend, for testing the client without real
servers (#733). It has a total rate limit on media and can send any socket message, including
malformed ones, but no other faults or chaos yet. Written for the Mac; all the tests also run on
Windows (see "On Windows").

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
  frame number, `forward` and `backward`, and checks each one's effect in the heartbeat. The
  `next`s run over a slow link, so some start a dream that is still downloading, and each
  must add its dream to the play history (#771).
- `chaos_test.py` sends about 200 random commands a minute (including `play_dream` at random
  frames and near the end, and `play_playlist` across the server's four playlists) while
  changing the media rate every few seconds, then checks the client recovers. `--seed N`
  replays a run; every action goes to `actions.jsonl` in the run folder. It finds more
  with a sanitized build (see "Sanitizers").
- `navigation_chaos_test.py` is the same with only `next`, `previous`, `forward`, `backward`,
  `play_playlist` and `play_dream`, and the rate changing every 1 to 4 seconds.
- `keyframe_test.py` plays a keyframed playlist from a snapshot set (default `wanderlust-v1`)
  and checks every move against its keyframe graph: along a chain with `next`, when dreams end
  by themselves, from a dead end and inside a cycle. A move must take an unplayed dream that
  starts where the current one ends, if there is one, and be seamless exactly when it does. A
  natural end may only go to a cached dream (by design, #269), so there only cached matches
  count. `--rate N` streams over a limited link.
- `repro_test.py` reproduces client bugs found with these tests, one fresh client per case:
  `seek-past-end` (#775), `seek-near-end` (its control), `previous-after-wrap` and
  `previous-two-dreams` (#776). Each checks the right behavior, so it fails until the bug is
  fixed and then guards against it coming back.
- `crash_test.py` sends malformed remote-control messages, one per fresh client, and checks
  the client survives each. `--list` shows the cases; `--case NAME` runs one.
- `throttle_test.py` serves the largest videos over a limited link, drops the rate below
  the client's 10% adaptive-speed floor and raises it again, and checks that the client rebuffers and
  recovers while its render loop keeps going, and that the buffering moon is up while it's starved
  (the client logs `Buffering moon shown` / `hidden`). It takes about 45 seconds, and runs
  without screenshots, because taking them stalls the render loop. It plays at 30 fps
  (`--fps`), above the client's default 20, because a rebuffer used to wait for a second of
  frames, more than the decoder queues above 21 fps, and never end.
- `moon_test.py` lets the client download every dream, then sends `next` 8 times between
  cached dreams and checks the buffering moon never shows (#823). The incoming decoder takes
  ~100 ms to fill its first frames, which holds the crossfade briefly, and that used to flash the
  moon nearly every time. About a minute.
- `resume_test.py` lets the client download every dream, then pauses and resumes it 6 times
  and checks that no tick is clamped (#747): the first tick after a resume used to count the
  whole pause as one frame and jump the timeline 100 ms. Then it switches between speed 1 and 9
  6 times, each at a different point within a frame, and checks that no frame is discarded:
  speeding up used to carry up to half a second into the faster rate and skip up to 12 frames.
  About a minute.
- `playlist_fade_test.py` lets the client download every dream, then 3 times seeks the middle
  dream to 10 s before its end with `play_dream` and times `play_playlist` so that the old
  dream's end-of-dream fade window opens during the switch's 1 s crossfade. It checks that
  happened, and that no crossfade went back (#826): the end-of-dream crossfade used to start
  on top of the switch's and restart it. Needs at least 3 dreams. About a minute and a half.
- `playlist_interrupt_test.py` serves 4 dreams with a 5 s end-of-dream fade
  (`--transition-time`), and sends `play_playlist` at 10, 30, 40, 60, 70 and 90% of the way through
  an end-of-dream crossfade (`--fractions`), while both pipelines are busy. It prints how far
  each interrupted crossfade had got and whether the outgoing dream ran out before the switch's
  own fade ended (the client logs `Playlist switch interrupted crossfade ...` and
  `Crossfade to ... cut short ...`), and checks the client settles on the new playlist. About two
  minutes.
- `slow_link_test.py` checks adaptive playback speed (#770) over a limited link, on the
  `alpha-v1` set's two long Pink Floyd dreams by default: at 0.6x the first dream's rate it
  slows below 100% and keeps moving with at most 2 rebuffers; at 0.15x it goes below the old
  25% floor; a `play_dream` into the middle of the other dream holds its crossfade while the
  new dream buffers, and no crossfade goes back (the client logs both); and full speed
  returns when the rate goes back up. About three minutes.
- `speed_preload_test.py` serves the `alpha-v1` set's Wankel Rotary Engine playlist (193-frame
  dreams) at 80 fps, and changes the speed right after the next dream has been preloaded: down to
  speed 7, then up to speed 9. It checks that the preloaded dream, and the one after it, take as
  long as their frames at the new speed (#833): the preloaded clip used to keep the old speed for
  its whole length. About a minute and a half.
- `uncached_playlist_test.py` starts on a one-dream playlist of the `alpha-v1` set at 80 fps,
  waits for the downloader to go idle, switches to the Wankel Rotary Engine playlist with none of
  its dreams cached, and checks that the first 4 dreams after the switch are all different and in
  order (#835): the first dream used to replay until the downloader, asleep for up to 10 s, had
  fetched the second. Runs twice, on a fast link and on a 1.5 MB/s one, where the next dream must
  be streamed. About two minutes.
- `resume_past_end_test.py` starts a client set to resume its first dream at 3 times its
  length (`last_played_frame`) and checks it plays that dream from the start (#666). The decoder
  used to find no frames there, so the dream ended unseen. About a minute.
- `bad_setting_test.py` stores `last_played_frame` as 1e23 and as 120.5, which boost::json's
  `as_uint64()` throws on, and as 120.0 as a control. One fresh client per case; each must play,
  quit cleanly and leave an integer behind (#666). The first two crash the client until #666's
  JSONStorage fix lands, and make macOS show a crash dialog (see below).
- `zero_quota_test.py` serves with `--quota 0`, so nothing is downloaded in the background, and
  starts a client set to resume its first dream at its last frame. It checks the streamed dream
  is cached and played again when it ends, without busy-looping (#666, #725). A few seconds.
- `run_all.py` runs every test above in turn and prints a summary (see "Running the tests").
- `fixtures.py` makes and checks fixture sets (see "Videos").
- `screens.py` takes about one screenshot of the client's window a second, to catch a black
  or frozen picture. It can't see individual dropped frames.

## Running the tests

```bash
python3 -m venv venv
venv/bin/pip install -r requirements.txt
brew install ffmpeg   # for ffprobe

APP="/path/to/infinidream.app/Contents/MacOS/infinidream"
venv/bin/python run_all.py --app "$APP"              # all 18, about 27 minutes
venv/bin/python run_all.py --app "$APP" --only moon resume
venv/bin/python moon_test.py --app "$APP"            # or any one test by itself
```

`run_all.py` runs the tests one after another, writes each one's output to `<name>.log` in a
temp folder (or `--out DIR`), and prints a line per test and a total. Options after `--app`
that it doesn't know, like `--keep`, go to every test. A new test must be added to `TESTS` in
`run_all.py`: the run fails if a `*_test.py` in this folder isn't listed. `keyframe_test.py`
needs the `wanderlust-v1` set, `slow_link_test.py`, `speed_preload_test.py` and
`uncached_playlist_test.py` need `alpha-v1`, and the rest serve `local-v1` (see "Videos").

Each takes `--set NAME` to serve a different fixture set, `--media-dir DIR` to serve a folder of
videos instead, `--fullscreen` to start the client
fullscreen instead of in a window, `--no-keep-awake` to let the screen sleep (see below), and
`--keep` to keep the run folder (logs, server events, crash reports, stack samples) even when
everything passes; it's always kept when a check fails.

The screen checks need Screen Recording permission for your terminal (System Settings >
Privacy & Security); without it they're skipped, and the test says so.

The client opens a window, and the screen has to stay unlocked, because a locked screen
stops the render loop. Each test keeps the screen saver, lock and sleep away until it exits,
with `caffeinate` on the Mac, `gnome-session-inhibit` (or `systemd-inhibit`) on Linux and
`SetThreadExecutionState` plus turning the screen saver off for the session on Windows; `--no-keep-awake` turns that off. Quit any other running
client first, or the check that real installs are unchanged may fail. A test that crashes the
client makes macOS show a crash dialog; `defaults write com.apple.CrashReporter DialogType none`
turns those off (the reports are still written) and
`defaults delete com.apple.CrashReporter DialogType` turns them back on.

### On Windows

All 18 tests have been run on Windows (Release | x64 build).
You need Python 3 and `ffprobe` (`winget install Gyan.FFmpeg`). The video cache defaults to
`%LOCALAPPDATA%\Infinidream\content\mp4`, and the real-installs check looks at
`%LOCALAPPDATA%\Infinidream` and `Infinidream-stage`.

```bat
python -m venv venv
venv\Scripts\pip install -r requirements.txt
set APP=C:\path\to\client_generic\MSVC\Release\infinidream.exe
venv\Scripts\python fixtures.py fetch --set alpha-v1 --env-file ..\..\..\..\python-api\.env
venv\Scripts\python slow_link_test.py --app %APP%
venv\Scripts\python throttle_test.py --app %APP% --set alpha-v1 --playlist bd5615c2-5a68-4e33-b9c1-6649fb09dc03
```

`throttle_test.py` needs `--playlist` with `alpha-v1`, which has only two dreams of 900 frames or
more; the Pink Floyd playlist serves those two. Crash reports, stack samples, screenshots and
the screen-lock check are Mac only, so on Windows a locked screen shows up as a hang. A partial
`.tmp` download can be left after a clean quit, because the shutdown watchdog ends the process
before the decoder that would delete it is torn down; the client removes it on its next start.
Until that's fixed (#800), the harness prints a `NOTE` for it on Windows instead of failing.

### On Linux

All the tests except `chaos_test.py`, `navigation_chaos_test.py` and `keyframe_test.py` have
been run on Linux (Ubuntu 26.04, Vulkan build). You need `ffmpeg` (for `ffprobe`) and
`python3-venv`. The video cache defaults to `~/.config/infinidream/content/mp4`, and the
real-installs check looks at `~/.config/infinidream` and `~/.config/infinidream-stage`.

```bash
APP=~/e-dream-ai/client/client_generic/LinuxBuild/build/infinidream
mkdir -p ~/.cache/infinidream-tests
export TMPDIR=~/.cache/infinidream-tests   # see below
venv/bin/python fixtures.py fetch --set alpha-v1 --env-file ~/e-dream-ai/python-api/.env \
    --from-cache ~/.config/infinidream/content/mp4
venv/bin/python e2e_test.py --app "$APP" --set alpha-v1 --playlist e45d0e8c-ac8d-47c4-b32b-c615066d3663
```

Run folders, including each test client's video cache, go in the temp folder. On Ubuntu `/tmp`
is a RAM disk of half the memory, and a few kept runs fill it past the client's 5 GB
low-disk limit, which turns on the red Disk light. Set `TMPDIR` to a folder on disk.

Crash reports, stack samples, screenshots and the screen-lock check are Mac only.
A `.tmp` can be left after a clean quit here too (#800), and the harness only notes it, as on
Windows. On Linux it has been seen with a streamed download that had finished well before the
quit, mostly in `bad_setting_test.py`.
`repro_test.py`'s `previous-*` cases need exactly 2 or 3 dreams, so they skip with
`--playlist`; run them on the default media folder instead.

## Crashes and hangs

Every test ends with these checks:

- **The client kept running** until the test stopped it.
- **It quit cleanly when asked.** The harness sends the test-only `quit` command, which takes
  the same path as Cmd-Q and which the client honors only when `INFINIDREAM_DATA_DIR` is set.
  If the client doesn't exit within 15 s, the harness samples its stacks, then sends SIGTERM.
  After a clean quit, no partial (`.tmp`) downloads may be left (on Windows and Linux, only noted; #800).
- **No crash report.** macOS crash reports for the client's process are copied into the run
  folder and summarized. On master the client catches SIGSEGV and calls `exit(0)` (#769), so
  a report often shows a second crash during that exit; the summary then also shows the
  original fault, the frames just below the client's signal handler.
- **No crash hidden by that handler:** `RECEIVED SIGSEGV` in the log fails the run even when
  no report was written.
- **No hang:** no heartbeat for 3 s, or the current clip's frame not moving for 5 s while the
  client says it's neither paused nor buffering. Holding a seek crossfade while the incoming
  dream buffers counts as buffering: the outgoing dream waits on its last frame under the
  buffering moon, which a burst of `next`s over a slow link can stretch past 5 s. The stacks are sampled (`stacks-hang.txt`).
  A locked screen also stops heartbeats, so it's reported as that instead.

## Sanitizers

Any test, or `run_all.py`, can run against a sanitized client build (#871). On the Mac:

    cd client_generic/MacBuild
    python3 build.py --sanitize asan    # AddressSanitizer + UndefinedBehaviorSanitizer
    python3 build.py --sanitize tsan    # ThreadSanitizer (can't be combined with ASan)

Each builds only the app, Debug and unsigned, incrementally in `build-asan/` or `build-tsan/`,
and prints the binary's path for `--app`. The screensaver isn't sanitized: a `.saver` runs
inside `legacyScreenSaver`, and a sanitizer runtime must be loaded when the process starts.

On Linux, configure a separate CMake tree with `INFINIDREAM_SANITIZE` (GCC or Clang):

    cd client_generic/LinuxBuild
    cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DINFINIDREAM_SANITIZE=asan
    cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DINFINIDREAM_SANITIZE=tsan
    cmake --build build-tsan -j$(nproc)    # binary: build-tsan/infinidream

ASan comes with LeakSanitizer on Linux, but it never reports: the client ends with `_exit()`,
which skips the leak check at exit.

The harness finds out from the binary which sanitizer it was built with; no option is needed.
It then:

- sets `ASAN_OPTIONS`/`UBSAN_OPTIONS` or `TSAN_OPTIONS`. Under ASan the client's SIGSEGV
  handler is kept out (`allow_user_segv_handler=0`), so a segfault gets a full ASan report
  instead of being swallowed. TSan reports every race and carries on. Suppressions, for code we
  don't own, go in `asan.supp` and `tsan.supp` here, each with its reason. On Linux,
  `tsan.supp` drops races inside GLib, GIO, Pango, fontconfig, glycin and PulseAudio (threads
  started by the window decorations and font loading) and ffmpeg's lock-order report; without
  it a short e2e run has over 200 reports, none in our code.
- multiplies the hang limits by 3 (ASan) or 5 (TSan), since sanitized builds run slower.
- adds the check **No asan report** (or tsan): any `AddressSanitizer`, `runtime error:` or
  `ThreadSanitizer` report in the client's output fails it, and all reports are saved to
  `sanitizer-reports.txt` in the run folder.

ffmpeg, Boost and OpenSSL come prebuilt (from vcpkg, or the distro on Linux) without instrumentation, so bugs inside
them aren't seen. Heap misuse in our own code still is, since ASan replaces malloc.

## Frame timing

With `INFINIDREAM_HEARTBEAT_SECONDS=1`, the client logs a heartbeat every second with late
ticks (more than 1.5 display intervals), ticks clamped at 100 ms and the time they lost, and
for each clip the frames discarded to catch up and the number of rebuffers. To check these
counters, `INFINIDREAM_TEST_STALL_MS=150` makes the render thread spin for 150 ms every
`INFINIDREAM_TEST_STALL_EVERY_MS` (default 5000). A stall of 3 s or more trips the hang check.

## Videos

No videos are stored in this repo. The tests serve a **fixture set**, a frozen folder of
`<uuid>.mp4` files with a manifest of md5s, so every run plays the same videos. Unless a test
picks its own (`keyframe_test.py` serves `wanderlust-v1`; `slow_link_test.py`,
`speed_preload_test.py` and `uncached_playlist_test.py` serve `alpha-v1`), it serves `local-v1`:
6 dreams of 900 frames, 4.5 to 113 MB. The server serves the three smallest (or with
`--pick largest`, the largest) that have at least 900 frames, so a dream doesn't end by itself
during a test. `--set NAME` serves another set.

`--media-dir DIR` serves a folder of videos instead, such as the alpha client's cache at
`/Users/Shared/infinidream.ai/content/mp4`. Nothing in it is changed, but that cache changes as
the client runs, so the videos a test gets change with it: a new, mostly black dream once failed
`e2e_test.py`'s black-screen check. To make a set from it:

```bash
venv/bin/python fixtures.py import-cache --set local-v1   # 6 videos spread across sizes
venv/bin/python fixtures.py list
venv/bin/python e2e_test.py --app "$APP" --set local-v1
```

Sets live in `~/edream-fixtures/<name>/` (or under `INFINIDREAM_FIXTURES`), and each run checks
the set's md5s before starting.

`fixtures.py snapshot` makes a set from real playlists on alpha, through the same client API
the client uses, with an API key read from `INFINIDREAM_FIXTURES_KEY` or an env file (it is
sent only to the API server and never printed). It saves the raw API responses under `api/`,
and can be rerun to resume. The server then serves that set's real dream records and its real
playlists (cut to the dreams taken, keyframes kept) beside the made-up ones.

```bash
venv/bin/python fixtures.py snapshot --set alpha-v1 --env-file ~/e-dream-ai/python-api/.env \
    --playlist e45d0e8c-ac8d-47c4-b32b-c615066d3663:20 \
    --playlist bd5615c2-5a68-4e33-b9c1-6649fb09dc03:2 \
    --playlist d9726526-b8f2-4221-a0bf-67fbacc01f4d:40
venv/bin/python chaos_test.py --app "$APP" --set alpha-v1 --dreams 0 --min-frames 0
```

Every test also takes `--playlist ID` with a snapshot set: the server then serves only that
real playlist's dreams, whatever their length, and starts the client on it. Tests that assume
long dreams can fail there for reasons of their own; `throttle_test.py` says it doesn't apply
when the first dream is shorter than 30 s.

`alpha-v1` is 62 dreams and 2.8 GB: the first 20 of Infinite Visions (31-68 MB, 727 frames),
the first 2 of Pink Floyd (605-787 MB, 11-12k frames) and the first 40 of Wankel Rotary Engine
(4-9 MB, 193 frames). Most are shorter than 900 frames, so they end by themselves during a
run; tests that rely on a dream not ending can still pick long ones with `--min-frames 900`.

### Getting the sets on another machine

The videos aren't in git, but each set's manifest and recorded API responses are, in
`fixture-sets/<name>/`: `alpha-v1`, `wanderlust-v1` and `local-v1`. `fixtures.py fetch`
rebuilds a set from that record into `~/edream-fixtures/<name>/`, downloading every video
through alpha's client API (`--env-file` or `INFINIDREAM_FIXTURES_KEY`, as for `snapshot`) and
checking every md5. It skips videos already there, and `--from-cache DIR` copies matching ones
from a folder instead of downloading.

```bash
venv/bin/python fixtures.py fetch --set alpha-v1 --env-file ~/e-dream-ai/python-api/.env
```

After making a new set, `fixtures.py record --set NAME` copies its manifest and API responses
into `fixture-sets/` to commit. It drops URL query strings on the way, since those are signed
links into storage and this repo is public; the mock server doesn't use those URLs. If alpha
re-processes or deletes a dream in a set, `fetch` fails its md5 check, and the set needs a new
version (`-v2`).
