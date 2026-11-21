# sio-teardown

Standalone AddressSanitizer test for the Socket.IO shim
(`Networking/sio/sio_beast.cpp`) tearing down live `wss://` connections.

It builds `sio_beast.cpp` alone (with a stub `Log.h`) against the vcpkg Boost and
OpenSSL in `cache/univ-osx`, starts `server.py` (aiohttp, a minimal Engine.IO v4
server) on `wss://localhost:8766` with a throwaway CA trusted through
`SSL_CERT_FILE`, and runs three scenarios, each with and without `flood`
(the server streams large events nonstop so reads are in flight):

- `close`: connect, emit, `close()`, repeat.
- `reconnect`: `connect()` with a new query while open, repeat.
- `watchdog`: the server never pings, so the ping watchdog tears each
  connection down and the client reconnects.

```
./run.sh            # 200 iterations per scenario
./run.sh 500
SCENARIOS=watchdog ./run.sh
```

macOS only for now; needs the mock server's venv (`../mock-server/venv`).

Before the fix, `watchdog flood` crashed every run in OpenSSL's BIO code (the
0.17.2 Bugsnag crash in `bio_write_intern`), and `close`/`reconnect` hit a
heap-use-after-free on the frame being written.
