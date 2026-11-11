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

"snapshot" downloads dreams from a real server through the client API, as the client does:
each playlist, the dream records, then each video through /dream/<uuid>/url, checking md5s.
It saves the raw API responses under api/ and skips videos it already has, so it can be rerun
to resume. The API key comes from INFINIDREAM_FIXTURES_KEY or from an env file holding
API_KEY= and BACKEND_URL= (such as python-api's .env). It is sent only to the API server and
never printed.

    fixtures.py snapshot --set alpha-v1 --env-file ~/e-dream-ai/python-api/.env \\
        --playlist e45d0e8c-ac8d-47c4-b32b-c615066d3663:20 --playlist <uuid>:2

With --from-cache DIR, videos are copied from a client's cache when their md5 matches, instead
of downloaded; --only-cached also leaves out the dreams that aren't there, so a set can be what
a real client chose to download from a playlist:

    fixtures.py snapshot --set wanderlust-v1 --env-file ... --playlist <uuid> \\
        --from-cache /Users/Shared/infinidream.ai/content/mp4 --only-cached
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

import server

DEFAULT_CACHE = "/Users/Shared/infinidream.ai/content/mp4"
DEFAULT_SERVER = "https://api-alpha.infinidream.ai/api/v1"
USER_AGENT = "infinidream-fixtures"


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


def read_env_file(path):
    values = {}
    for line in Path(path).expanduser().read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"').strip("'")
    return values


class Api:
    """The client API of a real server. The key goes only in requests to this server."""

    def __init__(self, base, key):
        self.base, self.key = base.rstrip("/"), key

    def get(self, path):
        request = urllib.request.Request(f"{self.base}{path}", headers={
            "Authorization": f"Api-Key {self.key}", "User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                body = json.load(response)
        except urllib.error.HTTPError as error:
            raise SystemExit(f"GET {path} failed: HTTP {error.code}") from None
        if not body.get("success"):
            raise SystemExit(f"GET {path} failed: {body.get('message', 'no success flag')}")
        return body


def download(url, target, expected_md5, size):
    """Downloads url to target through a .part file, checking the md5. No credentials sent."""
    if urllib.parse.urlparse(url).scheme != "https":
        raise SystemExit(f"Refusing a non-https video URL for {target.name}")
    part = target.with_suffix(".part")
    digest, done, started, shown = hashlib.md5(), 0, time.time(), 0.0
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response, open(part, "wb") as file:
        for chunk in iter(lambda: response.read(1 << 20), b""):
            file.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            if time.time() - shown > 5:
                shown = time.time()
                rate = done / max(0.001, shown - started) / 1e6
                print(f"    {done / 1e6:.0f}/{size / 1e6:.0f} MB, {rate:.1f} MB/s", flush=True)
    if digest.hexdigest() != expected_md5:
        part.unlink()
        raise SystemExit(f"{target.name}: md5 {digest.hexdigest()} doesn't match the server's {expected_md5}")
    part.rename(target)


def snapshot(args):
    env = read_env_file(args.env_file) if args.env_file else {}
    key = os.environ.get("INFINIDREAM_FIXTURES_KEY") or env.get("API_KEY")
    if not key:
        sys.exit("No API key: set INFINIDREAM_FIXTURES_KEY or pass --env-file with API_KEY=")
    api = Api(args.server or env.get("BACKEND_URL") or DEFAULT_SERVER, key)
    target = set_dir(args.set)
    (target / "api").mkdir(parents=True, exist_ok=True)

    playlists, order = [], []
    for spec in args.playlist:
        playlist_id, _, take = spec.partition(":")
        body = api.get(f"/client/playlist/{playlist_id}")
        (target / "api" / f"playlist_{playlist_id}.json").write_text(json.dumps(body, indent=1) + "\n")
        playlist = body["data"]["playlist"]
        contents = playlist["contents"][:int(take)] if take else playlist["contents"]
        uuids = [item["uuid"] for item in contents]
        playlists.append({"id": playlist_id, "name": playlist.get("name"),
                          "total": len(playlist["contents"]), "take": len(uuids), "uuids": uuids})
        order += [u for u in uuids if u not in order]
        print(f"Playlist {playlist.get('name')!r}: taking {len(uuids)} of {len(playlist['contents'])}")

    records = {}
    for start in range(0, len(order), 50):
        batch = order[start:start + 50]
        body = api.get("/client/dream?uuids=" + ",".join(batch))
        for dream in body["data"]["dreams"]:
            records[dream["uuid"]] = dream
    (target / "api" / "dreams.json").write_text(json.dumps(
        {"success": True, "data": {"dreams": [records[u] for u in order if u in records]}}, indent=1) + "\n")
    missing = [u for u in order if u not in records or not records[u].get("md5")]
    if missing:
        sys.exit(f"No record or md5 for {len(missing)} dreams, e.g. {missing[0]}")

    cache, cached = (Path(args.from_cache) if args.from_cache else None), {}
    if cache:
        for uuid in order:
            path = cache / f"{uuid}.mp4"
            if path.exists() and md5_of(path) == records[uuid]["md5"]:
                cached[uuid] = path
        print(f"{len(cached)} of {len(order)} dreams are in {cache} with the right md5", flush=True)
        if args.only_cached:
            order = [u for u in order if u in cached]
            for playlist in playlists:
                playlist["uuids"] = [u for u in playlist["uuids"] if u in cached]
                playlist["take"] = len(playlist["uuids"])
            (target / "api" / "dreams.json").write_text(json.dumps(
                {"success": True, "data": {"dreams": [records[u] for u in order]}}, indent=1) + "\n")

    total = sum(int(records[u]["size"]) for u in order)
    print(f"{len(order)} dreams, {total / 1e9:.2f} GB in {target}", flush=True)
    for number, uuid in enumerate(order, 1):
        record, path = records[uuid], target / f"{uuid}.mp4"
        if path.exists() and md5_of(path) == record["md5"]:
            continue
        if uuid in cached:
            shutil.copy2(cached[uuid], path)
            continue
        print(f"[{number}/{len(order)}] {uuid} {record.get('name', '')!r}, "
              f"{int(record['size']) / 1e6:.0f} MB", flush=True)
        url = api.get(f"/client/dream/{uuid}/url")["data"]["url"]
        download(url, path, record["md5"], int(record["size"]))

    manifest = {"name": target.name, "source": api.base, "created": time.strftime("%Y-%m-%d"),
                "playlists": playlists,
                "dreams": [{"uuid": u, "md5": records[u]["md5"], "size": int(records[u]["size"]),
                            "frames": records[u].get("frames"), "fps": float(records[u].get("fps") or 0),
                            "name": records[u].get("name")} for u in order]}
    (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Done: {len(order)} dreams in {target}")


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
    snap = commands.add_parser("snapshot", help="download dreams from a server's playlists")
    snap.add_argument("--set", required=True, help="name of the set to make or resume")
    snap.add_argument("--playlist", action="append", required=True,
                      help="playlist uuid, optionally :N to take only its first N dreams; repeatable")
    snap.add_argument("--env-file", help="file with API_KEY= and BACKEND_URL=, e.g. python-api/.env")
    snap.add_argument("--from-cache", help="copy videos from this folder of <uuid>.mp4 when the md5 matches")
    snap.add_argument("--only-cached", action="store_true",
                      help="with --from-cache, take only the dreams found there")
    snap.add_argument("--server", help=f"API base URL (default: BACKEND_URL from the env file, else {DEFAULT_SERVER})")
    snap.set_defaults(run=snapshot)
    ver = commands.add_parser("verify", help="check a set's videos against its manifest")
    ver.add_argument("--set", required=True)
    ver.set_defaults(run=verify_set)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
