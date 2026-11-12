#!/usr/bin/env python3
"""Keyframe test (client#733): plays a keyframed playlist from a snapshot fixture set and checks
that the client follows its keyframes, on next and when a dream ends by itself.

In a keyframed playlist each dream goes from a start keyframe to an end keyframe, so the
playlist is a graph. After dream A, the client should play a dream whose start keyframe is A's
end keyframe and which it hasn't played yet, with a seamless transition; only when there is
none should it fall back to playlist order with a crossfade (PlaylistManager::preflightNextDream).

Every move is logged as "moveToNextDream : <uuid> (pos: N, transition: T)", and the test checks
each one against the graph. A dream counts as played once it has been on screen in this run.
The client's own play history only ever holds fewer dreams (it resets, and play_dream doesn't
add to it), so if a match the test calls unplayed was skipped, the client really skipped it.

A dream that ends by itself only moves to a dream that is already cached, by design (client#269,
"Natural transition - don't allow streaming" in Player.cpp): so for those moves, only matches
fully downloaded before the client chose count, and skipping an uncached one is reported, not
failed. next may stream, so every unplayed match counts.

Phases: walk the longest unplayed chain with next; let dreams end by themselves at high speed;
next from a dead end; next inside a cycle. Mac only for now.

    venv/bin/python keyframe_test.py --app ".../infinidream.app/Contents/MacOS/infinidream"
    venv/bin/python keyframe_test.py --app ... --rate 2000000   # stream over a 2 MB/s link
"""
import argparse
import json
import re
import sys
import tempfile
import time
from collections import defaultdict
from pathlib import Path

import harness as h

WANDERLUST = "13489b20-cc0b-4923-8ea8-3f64015fe389"
SEAMLESS = 3  # PlaylistManager::TransitionType::Seamless
MOVE = re.compile(r"moveToNextDream : (?P<uuid>[0-9a-f-]{36}) \(pos: \d+, transition: (?P<type>\d+)\)")
CACHED = re.compile(r"(?:Successfully downloaded and saved dream to|Successfully renamed cache file from \S+ to) "
                    r"\S*?(?P<uuid>[0-9a-f-]{36})\.mp4")
EVICTED = re.compile(r"Evicted oldest video: (?P<uuid>[0-9a-f-]{36})")
NATURAL_PREFLIGHT = "forceNext: 0)"
COMMAND_TIMEOUT = 30
NATURAL_TIMEOUT = 60  # for one dream to end by itself at high speed
FASTER_PRESSES = 16   # playback_faster multiplies the speed by 1.12 each time: 20 fps -> ~127 fps


class Graph:
    """A snapshot set's playlist as keyframe edges: dream uuid -> (start keyframe, end keyframe)."""

    def __init__(self, media_dir, playlist):
        folder = Path(media_dir)
        manifest = json.loads((folder / "manifest.json").read_text())
        taken = next((set(p["uuids"]) for p in manifest.get("playlists", []) if p["id"] == playlist), None)
        if taken is None:
            sys.exit(f"{folder} has no playlist {playlist}; make it with fixtures.py snapshot")
        contents = json.loads((folder / "api" / f"playlist_{playlist}.json").read_text())["data"]["playlist"]["contents"]
        self.edges = {c["uuid"]: (c.get("start_keyframe"), c.get("end_keyframe"))
                      for c in contents if c["uuid"] in taken and (folder / f"{c['uuid']}.mp4").exists()}
        self.leaving = defaultdict(list)
        for uuid, (start, _) in self.edges.items():
            if start:
                self.leaving[start].append(uuid)
        if not any(end for _, end in self.edges.values()):
            sys.exit(f"Playlist {playlist} in {folder} has no keyframes")

    def matches(self, uuid):
        """Dreams that can follow this one seamlessly."""
        end = self.edges[uuid][1]
        return [d for d in self.leaving.get(end, []) if d != uuid] if end else []

    def longest_chain(self, played, limit=40):
        """The longest run of unplayed dreams joined by keyframes."""
        best = []
        def walk(uuid, path):
            nonlocal best
            if len(path) > len(best):
                best = list(path)
            if len(path) >= limit:
                return
            for following in self.matches(uuid):
                if following not in played and following not in path:
                    path.append(following)
                    walk(following, path)
                    path.pop()
        for uuid in self.edges:
            if uuid not in played:
                walk(uuid, [uuid])
        return best

    def dead_ends(self):
        """Dreams whose end keyframe no dream leaves from."""
        return [u for u, (_, end) in self.edges.items() if end and not self.leaving.get(end)]

    def cycle_pairs(self):
        """(a, b) where b follows a and a can follow b again: a short cycle."""
        return [(a, b) for a in self.edges for b in self.matches(a) if a in self.matches(b)]


class Checker:
    """Checks each move the client logs against the graph."""

    def __init__(self, graph):
        self.graph = graph
        self.played = set()
        self.followed = defaultdict(int)  # phase -> moves that followed a keyframe
        self.moves = defaultdict(int)
        self.violations, self.wrong_type = [], []
        self.uncached_skips = []  # natural ends that passed over a match not yet downloaded

    def see(self, uuid):
        self.played.add(uuid)

    def check_moves(self, phase, start, moves, cached_at=None):
        """moves: [(uuid, type, log position)] in order, starting from dream start. cached_at,
        for moves that may only go to cached dreams, gives the dreams cached when the client
        chose the move at that log position."""
        current = start
        for uuid, kind, position in moves:
            self.moves[phase] += 1
            matches = self.graph.matches(current) if current in self.graph.edges else []
            unplayed = [m for m in matches if m not in self.played]
            if cached_at and uuid not in matches:
                cached = cached_at(position)
                skipped = [m for m in unplayed if m not in cached]
                if skipped:
                    self.uncached_skips.append(f"{current[:8]} -> {uuid[:8]} passed over "
                                               f"{', '.join(m[:8] for m in skipped)}")
                unplayed = [m for m in unplayed if m in cached]
            if uuid in matches:
                self.followed[phase] += 1
                if kind != SEAMLESS:
                    self.wrong_type.append(f"{current[:8]} -> {uuid[:8]} followed a keyframe with transition {kind}")
            else:
                if unplayed:
                    self.violations.append(f"{phase}: after {current[:8]} played {uuid[:8]}, though "
                                           f"{', '.join(u[:8] for u in unplayed)} start where it ends")
                if kind == SEAMLESS:
                    self.wrong_type.append(f"{current[:8]} -> {uuid[:8]} doesn't share a keyframe but was seamless")
            self.see(uuid)
            current = uuid
        return current


def moves_since(run, offset):
    """Moves logged after offset: (uuid, transition type, position in the log)."""
    return [(m["uuid"], int(m["type"]), offset + m.start())
            for m in MOVE.finditer(run.log_text()[offset:])]


def cached_at(run):
    """A function giving the dreams cached when the client made the natural-end decision for
    the move at a log position: as of the last natural preflight before it."""
    text = run.log_text()
    events = sorted([(m.start(), "+", m["uuid"]) for m in CACHED.finditer(text)] +
                    [(m.start(), "-", m["uuid"]) for m in EVICTED.finditer(text)])
    def cached(position):
        decided = text.rfind(NATURAL_PREFLIGHT, 0, position)
        decided = position if decided < 0 else decided
        have = set()
        for at, kind, uuid in events:
            if at > decided:
                break
            (have.add if kind == "+" else have.discard)(uuid)
        return have
    return cached


def current_uuid(run, graph):
    """The full uuid of the dream on screen; heartbeats may shorten it."""
    run.require_alive()
    beats = run.heartbeats()
    short = beats[-1].get("current", "-") if beats else "-"
    found = [u for u in graph.edges if u.startswith(short)] if short != "-" else []
    return found[0] if len(found) == 1 else None


def play(run, graph, checker, uuid):
    """Plays a dream with play_dream, waits for it, and returns the log offset after it."""
    run.send("play_dream", uuid=uuid)
    h.wait_for(f"play_dream {uuid[:8]}", lambda: current_uuid(run, graph) == uuid, COMMAND_TIMEOUT)
    checker.see(uuid)
    return len(run.log_text())


def send_nexts(run, graph, count):
    for number in range(count):
        before = current_uuid(run, graph)
        run.send("next")
        h.wait_for(f"next #{number + 1} to change the dream",
                   lambda: current_uuid(run, graph) not in (None, before), COMMAND_TIMEOUT)
        time.sleep(2)


def scenario(run, graph, args):
    checker = Checker(graph)
    run.wait_until_ready()
    first = run.wait_until_playing()
    h.check("Client is playing the keyframed playlist", current_uuid(run, graph) is not None,
            f"{first.get('current')}, {len(graph.edges)} dreams in it")
    for uuid, _, _ in moves_since(run, 0):
        checker.see(uuid)
    checker.see(current_uuid(run, graph))

    # 1. Walk the longest chain nothing has played yet, with next.
    chain = graph.longest_chain(checker.played)
    steps = min(args.nexts, len(chain) - 1)
    print(f"Walking a chain of {len(chain)} dreams from {chain[0][:8]}, {steps} nexts", flush=True)
    offset = play(run, graph, checker, chain[0])
    send_nexts(run, graph, steps)
    checker.check_moves("next", chain[0], moves_since(run, offset))
    h.check(f"next followed the keyframes along a chain", checker.followed["next"] >= steps - 1,
            f"{checker.followed['next']} of {checker.moves['next']} moves followed a keyframe")

    # 2. Let dreams end by themselves, sped up.
    chain = graph.longest_chain(checker.played)
    print(f"Letting {args.natural} dreams end by themselves from {chain[0][:8]} "
          f"(chain of {len(chain)})", flush=True)
    offset = play(run, graph, checker, chain[0])
    for _ in range(FASTER_PRESSES):
        run.send("playback_faster")
        time.sleep(0.05)
    try:
        h.wait_for(f"{args.natural} dreams to end by themselves",
                   lambda: len(moves_since(run, offset)) >= args.natural or run.require_alive(),
                   NATURAL_TIMEOUT * args.natural)
    except h.Failed:
        pass
    natural = moves_since(run, offset)
    checker.check_moves("natural", chain[0], natural, cached_at(run))
    h.check(f"{args.natural} dreams ended by themselves", len(natural) >= args.natural, f"{len(natural)}")
    print(f"Natural ends: {checker.followed['natural']} of {len(natural)} followed a keyframe; "
          f"{len(checker.uncached_skips)} passed over a match that wasn't downloaded yet"
          + (f" ({'; '.join(checker.uncached_skips[:3])})" if checker.uncached_skips else ""), flush=True)
    if not args.rate:
        # Over an unlimited link everything is cached in time, so the chain should hold.
        h.check("Natural ends followed the keyframes",
                checker.followed["natural"] >= min(len(natural), len(chain) - 1) - 1,
                f"{checker.followed['natural']} of {len(natural)} moves followed a keyframe")
    run.send("set_speed_7")  # 2^4 = 16 fps, about the default

    # 3. next from a dead end must move on, with a crossfade.
    dead = [u for u in graph.dead_ends()]
    if dead:
        offset = play(run, graph, checker, dead[0])
        send_nexts(run, graph, 1)
        moved = moves_since(run, offset)
        checker.check_moves("dead end", dead[0], moved)
        h.check("next from a dead end moved on", bool(moved) and moved[-1][0] != dead[0],
                f"{dead[0][:8]} -> {moved[-1][0][:8]}" if moved else "no move logged")
    else:
        print("NOTE  No dead ends in this set", flush=True)

    # 4. next inside a short cycle goes to the other dream, if it hasn't played.
    pairs = [(a, b) for a, b in graph.cycle_pairs() if b not in checker.played]
    if pairs:
        a, b = pairs[0]
        offset = play(run, graph, checker, a)
        send_nexts(run, graph, 1)
        moved = moves_since(run, offset)
        checker.check_moves("cycle", a, moved)
        h.check("next inside a cycle followed it", bool(moved) and moved[0][0] == b,
                f"{a[:8]} -> {moved[0][0][:8] if moved else '?'}, expected {b[:8]}")
    else:
        print("NOTE  No short cycle left unplayed in this set", flush=True)

    h.check("Every move took an unplayed keyframe match when there was one", not checker.violations,
            "; ".join(checker.violations[:3]))
    h.check("Keyframe moves were seamless and the rest weren't", not checker.wrong_type,
            "; ".join(checker.wrong_type[:3]))
    time.sleep(2)
    run.require_alive()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, help="client binary inside the .app")
    h.add_media_args(parser)
    parser.set_defaults(set="wanderlust-v1", playlist=WANDERLUST)
    parser.add_argument("--rate", type=int, default=0, help="limit media to this many bytes a second")
    parser.add_argument("--nexts", type=int, default=12, help="nexts along the chain")
    parser.add_argument("--natural", type=int, default=4, help="dreams to let end by themselves")
    parser.add_argument("--keep", action="store_true", help="keep the run folder even if it passes")
    args = parser.parse_args()
    h.resolve_media(args)
    graph = Graph(args.media_dir, args.playlist)

    if h.screen_locked():
        sys.exit("The screen is locked, which stops the render loop. Unlock it and run again.")

    work = tempfile.mkdtemp(prefix="infinidream-keyframe-")
    print(f"Run folder: {work}", flush=True)
    before = h.snapshot_real_installs()
    run = h.Run(work, args.app, args.media_dir, screenshots=False, server_args=(
        "--dreams", "0", "--min-frames", "0", "--default-playlist", args.playlist,
        "--media-rate", str(args.rate)))
    try:
        run.start()
        scenario(run, graph, args)
    except h.Failed as failure:
        h.check("Scenario", False, str(failure))
    finally:
        run.stop()
    if run.client:
        h.verify_process(run)
        h.verify_server(run, before)
    h.finish(work, args.keep)


if __name__ == "__main__":
    main()
