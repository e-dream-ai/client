# sio-teardown

Standalone AddressSanitizer test for the Socket.IO shim
(`Networking/sio/sio_beast.cpp`) tearing down live `wss://` connections.

It builds `sio_beast.cpp` alone (with a stub `Log.h`) against Boost.JSON and
OpenSSL (macOS: the vcpkg build in `cache/univ-osx`; Linux: system packages;
Windows: `vcpkg_installed/x64-windows`), starts `server.py` (aiohttp, a minimal
Engine.IO v4 server) on `wss://127.0.0.1:8766` with a throwaway CA trusted through
`SSL_CERT_FILE`, and runs three scenarios, each with and without `flood`
(the server streams large events nonstop so reads are in flight):

- `close`: connect, emit, `close()`, repeat.
- `reconnect`: `connect()` with a new query while open, repeat.
- `watchdog`: the server never pings, so the ping watchdog tears each
  connection down and the client reconnects.

```
./run.py                          # 200 iterations per scenario
./run.py 500
./run.py --scenarios watchdog     # or SCENARIOS=watchdog
```

Runs on macOS (clang++), Linux (g++, or set `CXX`) and Windows (MSVC, found with
vswhere; `python run.py`). Needs the mock server's venv (`../mock-server/venv`) and
`openssl` (on Windows, Git's copy is used if none is on PATH).

On Windows the client trusts the system certificate store, not `SSL_CERT_FILE`, so
the build links `stub/winstore_shim.cpp`, which makes that load fail and the client
fall back to OpenSSL's default paths. The client connects to `127.0.0.1` rather than
`localhost`: Windows tries `::1` first and takes about 2 s to give up on each connect.

Before the fix, `watchdog flood` crashed every run in OpenSSL's BIO code (the
0.17.2 Bugsnag crash in `bio_write_intern`), and `close`/`reconnect` hit a
heap-use-after-free on the frame being written.
