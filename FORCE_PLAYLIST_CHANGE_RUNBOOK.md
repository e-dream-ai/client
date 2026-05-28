# Force Playlist Change Runbook

## Symptom

Client connects, authenticates, and plays fine — but downloads nothing despite available quota and disk space. The log shows:

```
warning: All N dreams in playlist "..." are already cached — nothing to download.
         If unexpected, check your server-side playlist assignment.
```

Or the playlist name in the settings Disk tab looks like a dev/test playlist (e.g. "test ltx seamless by spot").

## Root Cause

`/api/v1/client/hello` returns `currentPlaylistUUID` from the user's server-side account record. The client uses exactly that UUID and overwrites any local `settings.json` value on startup. If the account was ever pointed at a small test playlist, it stays there until explicitly changed server-side.

**There is no REST endpoint to change this.** The only path is a Socket.IO event on the `/remote-control` namespace.

## Confirm the Problem

```bash
SEALED=$(python -c "import json; print(json.load(open('/home/<user>/.config/infinidream/settings.json'))['settings']['content']['sealed_session'])")
curl -s "https://api-alpha.infinidream.ai/api/v1/client/hello" -H "Cookie: wos-session=$SEALED" | python -m json.tool
```

Check `currentPlaylistUUID` in the response. If it's a test/unexpected UUID, proceed.

## Find a Production Playlist

Browse ranked playlists:

```bash
curl -s "https://api-alpha.infinidream.ai/api/v1/feed/ranked?limit=20" \
  -H "Cookie: wos-session=$SEALED" | python -c "
import json, sys
feed = json.loads(sys.stdin.read()).get('data', {}).get('feed', [])
for item in feed:
    pl = (item.get('playlistItem') or {}).get('playlist') or {}
    if pl.get('uuid'):
        print(pl['uuid'], '|', pl.get('name'))
"
```

Verify dream count for a candidate UUID:

```bash
curl -s "https://api-alpha.infinidream.ai/api/v1/client/playlist/<uuid>" \
  -H "Cookie: wos-session=$SEALED" | python -c "
import json, sys
pl = json.loads(sys.stdin.read()).get('data', {}).get('playlist', {})
print(pl.get('name'), '| dreams:', len(pl.get('contents', [])))
"
```

Known good production playlist (alpha): `e2723fbb-c855-43db-bf63-3a74211277fd` — **Ultimate Ambient** (164 dreams).

## Switch the Playlist

Install the Socket.IO client if needed:

```bash
pip install "python-socketio[client]" --break-system-packages
```

Run this script:

```python
import socketio, time, json

sealed = json.loads(open('/home/<user>/.config/infinidream/settings.json').read())['settings']['content']['sealed_session']
target = 'e2723fbb-c855-43db-bf63-3a74211277fd'  # replace with desired playlist UUID

sio = socketio.Client(logger=False, engineio_logger=False, reconnection=False)
events = []

@sio.event(namespace='/remote-control')
def connect():
    sio.emit('new_remote_control_event', {'event': 'play_playlist', 'uuid': target}, namespace='/remote-control')

@sio.on('new_remote_control_event', namespace='/remote-control')
def on_rc(data):
    print('Done:', data)  # success: {'event': 'play_playlist', 'uuid': '...', 'name': '...'}
    events.append('ok')

sio.connect(
    'https://api-alpha.infinidream.ai',
    headers={'Cookie': f'wos-session={sealed}'},
    transports=['websocket'],
    namespaces=['/remote-control'],
)
for _ in range(20):
    if events: break
    time.sleep(0.5)
sio.disconnect()
```

## Confirm the Fix

Re-run the `hello` check — `currentPlaylistUUID` should now show the new UUID. Then restart the client; it will fetch the new playlist and begin downloading uncached dreams immediately.

## Key Facts

- **Wrong namespace = silent failure.** The handler lives on `io.of("remote-control")` (`backend/src/server.ts`). Connecting to the default `/` namespace accepts the connection but ignores all events — no error, no echo, nothing.
- Auth uses the `wos-session` cookie (the `sealed_session` value from `~/.config/infinidream/settings.json`).
- `setUserCurrentPlaylist` returns silently if the playlist UUID doesn't exist in the DB — no error is sent to the client.
- Editing `current_playlist_uuid` in local `settings.json` does nothing — it is overwritten by `hello` at every startup.
- The client re-checks the playlist every **60 minutes** (`m_checkInterval{60}` in `PlaylistManager.h`). Restarting the client is faster.
