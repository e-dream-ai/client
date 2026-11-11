#!/usr/bin/env python3
"""Fixture sets for the mock server tests (client#733).

A fixture set is a folder of <uuid>.mp4 files plus a manifest.json giving each one's md5,
size, frames and fps. Tests serve a set with --set NAME instead of reading a client's live
cache, so a run doesn't change when the cache does. Sets live in ~/edream-fixtures/<name>/,
or under INFINIDREAM_FIXTURES if that's set.

    fixtures.py import-cache --set local-v1                # from the alpha client's cache
    fixtures.py import-cache --set local-v1 --from DIR --count 6 --min-frames 900
    fixtures.py list
    fixtures.py verify --set local-v1

Later (milestone 2b), "fetch" will download named sets of alpha dreams through the client API.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import time
from pathlib import Path

import server

DEFAULT_CACHE = "/Users/Shared/infinidream.ai/content/mp4"


def fixtures_root():
    return Path(os.environ.get("INFINIDREAM_FIXTURES", Path.home() / "edream-fixtures"))


def set_dir(name):
    """The folder for a set, given its name or a path to it."""
    path = Path(name).expanduser()
    return path if path.is_absolute() or name.startswith(".") else fixtures_root() / name


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(folder):
    """Checks every video in a set against its manifest. Returns a list of problems."""
    manifest_path = folder / "manifest.json"
    if not manifest_path.exists():
        return [f"{folder} has no manifest.json"]
    manifest = json.loads(manifest_path.read_text())
    problems = []
    for entry in manifest["dreams"]:
        path = folder / f"{entry['uuid']}.mp4"
        if not path.exists():
            problems.append(f"{path.name} is missing")
        elif md5_of(path) != entry["md5"]:
            problems.append(f"{path.name} doesn't match its md5")
    return problems


def import_cache(args):
    source = Path(args.source)
    target = set_dir(args.set)
    if target.exists() and any(target.iterdir()):
        sys.exit(f"{target} already exists; pick another set name or remove it")
    videos = sorted(source.glob("*.mp4"), key=lambda p: p.stat().st_size)
    chosen, seen = [], set()
    for path in videos:
        fps, frames = server.probe(path)
        if frames < args.min_frames:
            continue
        md5 = md5_of(path)
        if md5 in seen:
            continue
        seen.add(md5)
        chosen.append({"uuid": path.stem, "md5": md5, "size": path.stat().st_size,
                       "frames": frames, "fps": fps, "path": path})
    if not chosen:
        sys.exit(f"No videos of at least {args.min_frames} frames in {source}")
    if args.count and len(chosen) > args.count:
        # Keep a spread of sizes: the smallest, the largest and evenly spaced ones between.
        step = (len(chosen) - 1) / (args.count - 1) if args.count > 1 else 0
        chosen = [chosen[round(i * step)] for i in range(args.count)]
    target.mkdir(parents=True, exist_ok=True)
    for entry in chosen:
        shutil.copy2(entry.pop("path"), target / f"{entry['uuid']}.mp4")
    manifest = {"name": target.name, "source": str(source), "created": time.strftime("%Y-%m-%d"),
                "dreams": chosen}
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    total = sum(e["size"] for e in chosen)
    print(f"Imported {len(chosen)} videos ({total / 1e6:.0f} MB) into {target}")


def list_sets(args):
    root = fixtures_root()
    for folder in sorted(p for p in root.glob("*") if (p / "manifest.json").exists()):
        manifest = json.loads((folder / "manifest.json").read_text())
        dreams = manifest["dreams"]
        size = sum(d["size"] for d in dreams) / 1e6
        print(f"{folder.name:20} {len(dreams):3} videos {size:7.0f} MB  from {manifest.get('source', '?')}")


def verify_set(args):
    problems = verify(set_dir(args.set))
    for problem in problems:
        print(problem)
    print("OK" if not problems else f"{len(problems)} problems")
    sys.exit(1 if problems else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    imp = commands.add_parser("import-cache", help="make a set from a client's cache")
    imp.add_argument("--set", required=True, help="name of the new set")
    imp.add_argument("--from", dest="source", default=DEFAULT_CACHE, help="folder of <uuid>.mp4")
    imp.add_argument("--count", type=int, default=6, help="how many videos to keep (0 for all)")
    imp.add_argument("--min-frames", type=int, default=900,
                     help="skip shorter videos, so a dream can't end by itself during a test")
    imp.set_defaults(run=import_cache)
    commands.add_parser("list", help="list the sets").set_defaults(run=list_sets)
    ver = commands.add_parser("verify", help="check a set's videos against its manifest")
    ver.add_argument("--set", required=True)
    ver.set_defaults(run=verify_set)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
