"""Minimal wss:// Engine.IO v4 server for sio_teardown_test.

The query parameter `ping` picks the handshake's pingInterval/pingTimeout in
ms (default 25000/60000). With a small value and no pings sent, the client's
ping watchdog fires and tears the connection down while a read is pending.
With `flood=1` the server streams large events nonstop, so TLS reads are
always completing when the client tears a connection down.
"""
import asyncio
import argparse
import json
import ssl

from aiohttp import WSMsgType, web

NAMESPACE = "/remote-control"


async def socket_io(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    ping = int(request.query.get("ping", "0"))
    interval, timeout = (ping, ping) if ping else (25000, 60000)
    await ws.send_str("0" + json.dumps({"sid": "teardown", "upgrades": [],
                                        "pingInterval": interval, "pingTimeout": timeout,
                                        "maxPayload": 1000000}))
    flood = None
    if request.query.get("flood"):
        async def send_flood():
            frame = f'42{NAMESPACE},["flood","{"x" * 60000}"]'
            while True:
                await ws.send_str(frame)
                await asyncio.sleep(0)
        flood = asyncio.create_task(send_flood())
    try:
        await serve(ws)
    except (ConnectionError, asyncio.CancelledError):
        pass
    finally:
        if flood:
            flood.cancel()
    return ws


async def serve(ws):
    async for message in ws:
        if message.type != WSMsgType.TEXT:
            continue
        if message.data.startswith(f"40{NAMESPACE}"):
            await ws.send_str(f'40{NAMESPACE},{{"sid":"teardown"}}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()
    ctx = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    ctx.load_cert_chain(args.cert, args.key)
    app = web.Application()
    app.router.add_get("/socket.io/", socket_io)
    web.run_app(app, host="127.0.0.1", port=args.port, ssl_context=ctx, print=None)


if __name__ == "__main__":
    main()
