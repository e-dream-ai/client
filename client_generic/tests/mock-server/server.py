#!/usr/bin/env python3
"""Minimal mock infinidream server for testing the client (client#733).

Serves the client API, the /remote-control Socket.IO namespace and media from one
port. It accepts any credentials. Videos come from a folder of <uuid>.mp4 files,
such as a client's content/mp4 cache, and are only ever read.

Every request and socket message goes to a JSON-lines event log, and
POST /control/send/<event> sends a remote-control event to the connected client, and
POST /control/media-rate?bytes_per_second=N limits media downloads to N bytes a second in
total, like one shared link. 0 means no limit.
"""
import argparse
import asyncio
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path

from aiohttp import WSMsgType, web

PLAYLIST_UUID = "00000000-0000-4000-8000-000000000001"
TIMESTAMP = 1700000000
QUOTA_BYTES = 50 * 2**30
NAMESPACE = "/remote-control"


def probe(path):
    """Returns (fps, frames) for a video, using ffprobe."""
    output = subprocess.check_output(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_packets",
         "-show_entries", "stream=nb_read_packets,r_frame_rate", "-of", "json", str(path)])
    stream = json.loads(output)["streams"][0]
    numerator, denominator = stream["r_frame_rate"].split("/")
    return round(int(numerator) / int(denominator)), int(stream["nb_read_packets"])


def load_dreams(media_dir, count, min_frames, pick="smallest"):
    """Builds records for the smallest or largest distinct videos, in the types the client expects."""
    videos = sorted(Path(media_dir).glob("*.mp4"), key=lambda p: p.stat().st_size,
                    reverse=pick == "largest")
    dreams = {}
    seen_md5 = set()
    for path in videos:
        fps, frames = probe(path)
        if frames < min_frames:
            continue
        md5 = hashlib.md5(path.read_bytes()).hexdigest()
        if md5 in seen_md5:
            continue
        seen_md5.add(md5)
        dreams[path.stem] = {
            "uuid": path.stem, "name": f"mock {path.stem[:8]}", "artist": "mock server",
            # size and fps are strings in the real API.
            "size": str(path.stat().st_size), "fps": str(fps), "frames": frames,
            "status": "processed", "md5": md5, "thumbnail": "", "frontendUrl": "",
            "upvotes": 0, "downvotes": 0, "nsfw": False, "activityLevel": 1.0,
            "video_timestamp": TIMESTAMP, "timestamp": TIMESTAMP,
        }
        if len(dreams) == count:
            return dreams
    raise SystemExit(f"Need {count} distinct videos of at least {min_frames} frames in {media_dir}")


def parse_range(header, size):
    """Returns (start, end) inclusive for a Range header, or None if it can't be satisfied."""
    if not header:
        return 0, size - 1
    match = re.fullmatch(r"bytes=(\d*)-(\d*)", header.strip())
    if not match or not (match[1] or match[2]):
        return None
    if match[1]:
        start, end = int(match[1]), int(match[2]) if match[2] else size - 1
    else:
        start, end = max(0, size - int(match[2])), size - 1
    end = min(end, size - 1)
    return (start, end) if start <= end else None


def ok(data):
    return web.json_response({"success": True, "data": data})


class MockServer:
    def __init__(self, media_dir, dreams, event_log, port, media_rate=0):
        self.media_dir = Path(media_dir)
        self.dreams = dreams
        self.event_log = event_log
        self.port = port
        self.sockets = set()
        self.media_rate = media_rate  # Bytes a second across all downloads; 0 means no limit.
        self.next_send = 0.0

    def log(self, kind, **fields):
        self.event_log.write(json.dumps({"t": time.time(), "kind": kind, **fields}) + "\n")

    async def log_response(self, request, response):
        # Runs as the response is sent, once a FileResponse has settled on 200, 206 or 416.
        self.log("http", method=request.method, path=request.path_qs, status=response.status,
                 range=request.headers.get("Range"),
                 api_key=request.headers.get("Api-Key"),
                 authorization=request.headers.get("Authorization"),
                 cookie=request.headers.get("Cookie"))

    async def hello(self, request):
        return ok({"quota": QUOTA_BYTES, "dislikesCount": 0, "currentPlaylistUUID": PLAYLIST_UUID})

    def contents(self):
        return [{"uuid": uuid, "timestamp": TIMESTAMP} for uuid in self.dreams]

    async def playlist(self, request):
        return ok({"playlist": {"name": "mock playlist", "artist": "mock server", "nsfw": False,
                                "timestamp": TIMESTAMP, "loops": 0, "transitionTime": 2,
                                "contents": self.contents()}})

    async def default_playlist(self, request):
        return ok({"playlist": self.contents()})

    async def dream(self, request):
        if request.method == "POST":
            uuids = (await request.json()).get("uuids", [])
        else:
            uuids = request.query.get("uuids", "").split(",")
        return ok({"dreams": [self.dreams[u] for u in uuids if u in self.dreams]})

    async def dream_url(self, request):
        return ok({"url": f"http://127.0.0.1:{self.port}/media/{request.match_info['uuid']}"})

    async def media(self, request):
        uuid = request.match_info["uuid"]
        if uuid not in self.dreams:
            raise web.HTTPNotFound()
        path = self.media_dir / f"{uuid}.mp4"
        size = path.stat().st_size
        byte_range = parse_range(request.headers.get("Range"), size)
        if byte_range is None:
            raise web.HTTPRequestRangeNotSatisfiable(headers={"Content-Range": f"bytes */{size}"})
        start, end = byte_range
        response = web.StreamResponse(status=206 if request.headers.get("Range") else 200)
        response.headers["Content-Type"] = "video/mp4"
        response.headers["Accept-Ranges"] = "bytes"
        response.headers["Content-Length"] = str(end - start + 1)
        if request.headers.get("Range"):
            response.headers["Content-Range"] = f"bytes {start}-{end}/{size}"
        await response.prepare(request)

        began = time.time()
        sent = 0
        try:
            with open(path, "rb") as file:
                file.seek(start)
                while sent < end - start + 1:
                    wanted = min(self.chunk_size(), end - start + 1 - sent)
                    chunk = file.read(wanted)
                    if not chunk:
                        break
                    await self.wait_for_link(len(chunk))
                    await response.write(chunk)
                    sent += len(chunk)
            await response.write_eof()
        except ConnectionError:
            # The client hung up, as it does on a skip or a seek.
            pass
        finally:
            self.log("media_end", uuid=uuid, range=request.headers.get("Range"), sent=sent,
                     wanted=end - start + 1, seconds=round(time.time() - began, 3))
        return response

    def chunk_size(self):
        # About 50 ms worth of data, so that a rate change takes effect quickly.
        return 256 * 1024 if not self.media_rate else max(4096, min(65536, self.media_rate // 20))

    async def wait_for_link(self, length):
        """Holds a chunk until the shared link has room for it."""
        if not self.media_rate:
            return
        now = asyncio.get_running_loop().time()
        start = max(self.next_send, now)
        self.next_send = start + length / self.media_rate
        if start > now:
            await asyncio.sleep(start - now)

    async def quota(self, request):
        return ok({"quota": QUOTA_BYTES})

    async def dislikes(self, request):
        return ok({"dislikes": []})

    async def telemetry(self, request):
        self.log("telemetry", body=await request.text())
        return ok({})

    async def socket_io(self, request):
        """Engine.IO v4 over WebSocket, with just the packets the client uses."""
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self.log("ws_open", query=request.query_string,
                 api_key=request.headers.get("Api-Key"), cookie=request.headers.get("Cookie"))
        await ws.send_str("0" + json.dumps({"sid": "mock-engine", "upgrades": [],
                                            "pingInterval": 25000, "pingTimeout": 60000,
                                            "maxPayload": 1000000}))

        async def send_pings():
            while True:
                await asyncio.sleep(25)
                await ws.send_str("2")

        pings = asyncio.create_task(send_pings())
        try:
            async for message in ws:
                if message.type != WSMsgType.TEXT:
                    continue
                self.log("ws_recv", data=message.data)
                if message.data.startswith(f"40{NAMESPACE}"):
                    await ws.send_str(f'40{NAMESPACE},{{"sid":"mock-socket"}}')
                    self.sockets.add(ws)
        finally:
            pings.cancel()
            self.sockets.discard(ws)
            self.log("ws_close")
        return ws

    async def control_health(self, request):
        return web.json_response({"dreams": list(self.dreams), "sockets": len(self.sockets)})

    async def control_send(self, request):
        event = request.match_info["event"]
        frame = f"42{NAMESPACE}," + json.dumps(["new_remote_control_event", {"event": event}])
        for ws in self.sockets:
            await ws.send_str(frame)
        self.log("control_send", event=event, sockets=len(self.sockets))
        return web.json_response({"sockets": len(self.sockets)})

    async def control_media_rate(self, request):
        self.media_rate = int(request.query["bytes_per_second"])
        self.next_send = 0.0
        self.log("media_rate", bytes_per_second=self.media_rate)
        return web.json_response({"bytes_per_second": self.media_rate})

    async def unhandled(self, request):
        self.log("unhandled", method=request.method, path=request.path_qs)
        return web.json_response({"success": False}, status=404)

    def app(self):
        app = web.Application()
        app.on_response_prepare.append(self.log_response)
        api = "/api/v1/client"
        app.add_routes([
            web.get(f"{api}/hello", self.hello),
            web.get(f"{api}/playlist/default", self.default_playlist),
            web.get(f"{api}/playlist/{{id}}", self.playlist),
            web.get(f"{api}/dream", self.dream),
            web.post(f"{api}/dream", self.dream),
            web.get(f"{api}/dream/{{uuid}}/url", self.dream_url),
            web.get(f"{api}/quota", self.quota),
            web.get(f"{api}/user/dislikes", self.dislikes),
            web.post(f"{api}/telemetry", self.telemetry),
            web.get("/media/{uuid}", self.media),
            web.get("/socket.io/", self.socket_io),
            web.get("/control/health", self.control_health),
            web.post("/control/send/{event}", self.control_send),
            web.post("/control/media-rate", self.control_media_rate),
            web.route("*", "/{tail:.*}", self.unhandled),
        ])
        return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-dir", required=True, help="folder of <uuid>.mp4 files")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--dreams", type=int, default=3, help="how many videos to serve")
    # Long enough that a dream doesn't end by itself during a test, which would look like
    # a command working.
    parser.add_argument("--min-frames", type=int, default=900, help="skip shorter videos")
    parser.add_argument("--pick", choices=["smallest", "largest"], default="smallest",
                        help="which videos to serve; largest needs the most bandwidth")
    parser.add_argument("--media-rate", type=int, default=0,
                        help="limit media to this many bytes a second in total; 0 means no limit")
    parser.add_argument("--event-log", default="events.jsonl")
    args = parser.parse_args()

    dreams = load_dreams(args.media_dir, args.dreams, args.min_frames, args.pick)
    with open(args.event_log, "w", buffering=1) as event_log:
        server = MockServer(args.media_dir, dreams, event_log, args.port, args.media_rate)
        web.run_app(server.app(), host="127.0.0.1", port=args.port, print=None)


if __name__ == "__main__":
    main()
