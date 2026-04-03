Copyright e-dream, inc

```
2025.05     renamed e-dream to infinidream
2023.07     forked to work with new server and repository, renamed from Electric Sheep to e-dream
2015.05     moved from code.google.com repo
2011.01.30  based on revision 1546 on sf.net
```

infinidream: visuals for your vibe.
a platform for generative visuals.
this repository has the native client.
See the [backend](https://github.com/e-dream-ai/backend) for the server that it connects to.

Originally known as [Electric Sheep](https://github.com/scottdraves/electricsheep).

# dev docs

this repository uses git LFS, be sure to run

    brew install git-lfs
    git lfs install

Also, if you are doing releases you will need:
    brew install gh

The general C++ dependencies are handled by vcpkg. The Microsoft vcpkg repo is included as a **git submodule** at `vcpkg/`. After cloning this repo, fetch it and build dependencies:

    git submodule update --init --recursive
    ./vcpkg/bootstrap-vcpkg.sh
    ./vcpkg/vcpkg install

On Windows, use `vcpkg\bootstrap-vcpkg.bat` instead of the shell bootstrap script. For a fresh clone you can use `git clone --recurse-submodules <url>` so `vcpkg/` is populated immediately; otherwise run `git submodule update --init` after clone.

on Mac, open client_generic/MacBuild/infinidream.xcodeproj

Use the "File>Packages>Update to Latest" menu to load the
Mac specific dependencies.


There are four targets: app, screensaver, staging app, and staging
screensaver. The staging targets have their own directory
/Users/Shared/infinidream.ai-stage that can coexist with the normal one
/Users/Shared/infinidream.ai

## Build on macOS

### Prerequisites
- Xcode 14.0 or later
- macOS 12.4 or later

### Quick Build for Debugging

The just run the app in xcode with the debugger and without
making the embedded screensaver:

   cd client_generic/MacBuild
   mkdir -p Resources/infinidream.saver

Then in xcode just use command-R.


### Build Script
```bash
cd client_generic/MacBuild
./build.py [options]
```

### Options
- `-r` : Build in Release mode (default: Debug)
- `-s` : Build stage version (default: production)
- `-n` : Enable notarization (requires `-r`)
- `-v VERSION` : Set version string (e.g., `0.12.0`) for zip naming and GitHub release
- `-g` : Create GitHub release with tag (requires `-v`)

### Code Signing
Auto-discovers Developer ID certificate and Team ID from keychain.

Override via environment variables:
```bash
DEVELOPER_ID_CERT="Developer ID Application: Your Name (TEAM123)" \
TEAM_ID="TEAM123" \
KEYCHAIN_PROFILE="your-profile" \
./build.py -r
```

Default keychain profile: `infinidream-notarization`

### Examples
```bash
# Debug build (default)
./build.py

# Release build
./build.py -r

# Stage debug build
./build.py -s

# Release with notarization
./build.py -r -n

# Release with notarization and version (creates infinidream-0.12.0.zip)
./build.py -r -n -v 0.12.0

# Full release with appcast and GitHub release
./build.py -r -n -v 0.12.0 -g
```

### Output
- Screensaver: `build/DerivedData/Build/Products/{Debug|Release}/infinidream.saver`
- Application: `build/{Debug|Release}/AppExport/infinidream.app`

The app bundle contains the embedded screensaver at `infinidream.app/Contents/Resources/infinidream.saver`.

## to release (with Sparkle auto-update)

### 1. Build, notarize, and create GitHub release
```bash
cd client_generic/MacBuild
./build.py -r -n -v X.Y.Z -g
```

This:
- Builds and notarizes the app
- Creates git tag `X.Y.Z` (overwrites if exists)
- Creates GitHub prerelease `vX.Y.Z` with auto-generated notes
- Uploads `infinidream-X.Y.Z.zip` to the release

### 2. Test the app
```bash
open build/Release/AppExport/infinidream.app
```

### 3. Edit release notes (optional)

Edit the release notes on GitHub if needed:
https://github.com/e-dream-ai/client/releases

### 4. Publish the release

Preview what will be published:
```bash
./release.py -v X.Y.Z --dry-run
```

When ready, publish for real:
```bash
./release.py -v X.Y.Z        # production (alpha)
./release.py -v X.Y.Z -s     # stage
```

This:
- Fetches release notes from GitHub
- Generates appcast.xml (with linked issue references)
- Publishes appcast.xml to the landing-page repo
- Marks the GitHub release as latest (removes prerelease label)

### 5. Update frontend (for new installs)

Update `APP_VERSION` in the frontend repository:
`src/components/pages/install/install.page.tsx`

Push and Cloudflare will deploy in a few minutes.

### How Sparkle Auto-Update Works

1. App checks appcast URL on launch:
   - Production app: `https://infinidream.ai/appcast.xml`
   - Stage app: `https://infinidream.ai/stage/appcast.xml`
2. Compares `sparkle:version` in appcast with app's `CFBundleVersion`
3. If newer version found, shows update dialog
4. User clicks "Update Now" → downloads zip from GitHub → installs → relaunches

### Sparkle Tools Setup (one-time)

Download Sparkle tools from https://github.com/sparkle-project/Sparkle/releases

Extract and copy to `client_generic/MacBuild/bin/`:
- `generate_appcast` - generates appcast.xml with signatures
- `sign_update` - signs update archives
- `generate_keys` - generates EdDSA key pair (one-time use)

### EdDSA Key Pair

Sparkle uses EdDSA (Ed25519) signatures to verify updates are authentic.

**Public key** is in `Info.plist`:
```xml
<key>SUPublicEDKey</key>
<string>pkDT5qmpWtyaZxw5X6Ca7DPHueEfBEKrxkrKzSN/qS0=</string>
```

**Private key** is stored in macOS Keychain under the name `Sparkle EdDSA Key`.

To generate a new key pair (only if needed):
```bash
cd client_generic/MacBuild
./bin/generate_keys
```

This will:
1. Create a new EdDSA key pair
2. Store the private key in Keychain
3. Print the public key to add to `Info.plist`

**Important**: If you generate new keys, you must update `SUPublicEDKey` in:
- `App-Info.plist`
- `App-Stage-Info.plist`
- `Screensaver-Info.plist`

Existing users won't be able to update if the public key changes, so only regenerate keys if the private key is lost.

## to test (manually)

On stage, running with xcode, test with playlists
[Keyframe Test](https://stage.infinidream.ai/playlist/ab76a874-928c-45b1-88b6-b059ee54ef94)
and
[Basic Playlist](https://stage.infinidream.ai/playlist/c823b48f-9eda-4157-b4d4-64fd2a8702e7).

Then do it again but after `rm -rf /Users/Shared/infinidream.ai-stage/content/`
to test while streaming.

Then do a basic test after `rm -rf /Users/Shared/infinidream.ai-stage/`
to test sign-in flow.

After building, test on alpha with
[Wanderlust](https://alpha.infinidream.ai/playlist/13489b20-cc0b-4923-8ea8-3f64015fe389)
and [Pink
Floyd](https://alpha.infinidream.ai/playlist/bd5615c2-5a68-4e33-b9c1-6649fb09dc03)
and [Wankel Rotary
Engine](https://alpha.infinidream.ai/playlist/d9726526-b8f2-4221-a0bf-67fbacc01f4d).

With fully downloaded playlists, go through all navigation acommands:
next, prev, forward, backward, faster, slower, pause, both from
keyboard and with remote control. Remote only interactions: switching
playlists, playing from filmstrip.

Then do it again but after `rm -rf /Users/Shared/infinidream.ai/content/`
to test while streaming.

Then do a basic test after `rm -rf /Users/Shared/infinidream.ai/`
to test sign-in flow.

Then disable the screensaver, then uninstall the screen saver, then
run the app and make sure it installs and selects the screen saver.

## Upload symbols to BugSnag

Upload the symbols to bugsnag, on a terminal: 
- if first time install the upload tool `brew install bugsnag/tap/bugsnag-dsym-upload`
- Go to https://app.bugsnag.com/settings/e-dream-dot-ai/projects/client-macos/missing-dsyms and copy the first missing UUID corresponding to the tag
- run `mdfind YOUR_UUID_HERE`. This will output a path to the dsym `path/to/dsyms/MyApp.dSYM`
- run `bugsnag-dsym-upload path/to/dsyms` (note that it's the path TO the dsyms)
