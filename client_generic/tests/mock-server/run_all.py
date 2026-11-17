#!/usr/bin/env python3
"""Runs every mock-server test, one after another, and prints a summary.

Each test gets the same --app and any extra options given after it (for example
--keep or --no-keep-awake). Each test's output goes to <name>.log in the output folder,
and a test's own run folder is kept when it fails, as usual. Takes about 20 minutes.

    venv/bin/python run_all.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python run_all.py --app ... --only moon resume
    venv/bin/python run_all.py --app ... --keep

A new test goes in TESTS below. The run fails if a *_test.py here isn't listed, so none
gets left out.
"""
import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent

# (name, extra options). Every test also gets --app and the options passed to run_all.py.
TESTS = [
    ("e2e", []),
    ("commands", []),
    ("crash", []),
    ("throttle", []),
    ("moon", []),
    ("resume", []),
    ("playlist_fade", []),
    ("playlist_interrupt", []),
    ("slow_link", []),
    ("chaos", []),
    ("navigation_chaos", []),
    ("keyframe", []),  # serves the wanderlust-v1 set by default
    ("repro", []),
    ("speed_preload", []),  # serves alpha-v1's Wankel playlist by default
    ("uncached_playlist", []),  # serves alpha-v1 by default
    ("resume_past_end", []),
    ("bad_setting", []),
    ("zero_quota", []),
]


def unlisted():
    listed = {name for name, _ in TESTS}
    return sorted(p.name for p in HERE.glob("*_test.py")
                  if p.name[:-len("_test.py")] not in listed)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="client binary inside the .app")
    ap.add_argument("--only", nargs="+", metavar="NAME",
                    help="run only these tests (names as in TESTS, e.g. moon)")
    ap.add_argument("--out", help="folder for the logs; a new temp folder if not given")
    args, passthrough = ap.parse_known_args()

    missing = unlisted()
    if missing:
        print("Not in run_all.py's TESTS: " + ", ".join(missing))
        return 2

    tests = TESTS
    if args.only:
        names = {name for name, _ in TESTS}
        bad = [n for n in args.only if n not in names]
        if bad:
            print("Unknown tests: " + ", ".join(bad))
            return 2
        tests = [t for t in TESTS if t[0] in args.only]

    out = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix="infinidream-all-"))
    out.mkdir(parents=True, exist_ok=True)
    print(f"Logs: {out}\n")

    results = []
    for name, extra in tests:
        log = out / f"{name}.log"
        cmd = [sys.executable, str(HERE / f"{name}_test.py"), "--app", args.app,
               *extra, *passthrough]
        print(f"{name:20s} ", end="", flush=True)
        start = time.monotonic()
        with open(log, "w") as f:
            code = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=HERE)
        secs = time.monotonic() - start
        text = log.read_text(errors="replace")
        m = re.findall(r"(\d+) of (\d+) checks passed", text)
        checks = f"{m[-1][0]}/{m[-1][1]} checks" if m else "no summary"
        status = "PASS" if code == 0 else f"FAIL (exit {code})"
        print(f"{status:16s} {checks:16s} {secs:5.0f} s")
        if code != 0:
            for line in text.splitlines():
                if line.lstrip().startswith("FAIL") or line.startswith("Run folder"):
                    print(f"    {line.strip()}")
        results.append(code == 0)

    passed = sum(results)
    print(f"\n{passed} of {len(results)} tests passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
