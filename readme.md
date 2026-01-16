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

# dev docs

this repository uses git LFS, be sure to run

    brew install git-lfs
    git lfs install

The general C++ dependencies are handled by vcpkg. Run these commands to build them:

    git submodule update --init
    ./vcpkg/bootstrap-vcpkg.sh
    ./vcpkg/vcpkg install

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
./build.sh [options]
```

### Options
- `-r` : Build in Release mode (default: Debug)
- `-s` : Build stage version (default: production)
- `-n` : Enable notarization (requires `-r`)
- `-v VERSION` : Set version string (e.g., `0.12.0`) for zip naming and appcast
- `-a` : Generate appcast.xml for Sparkle auto-updates

### Code Signing
Auto-discovers Developer ID certificate and Team ID from keychain.

Override via environment variables:
```bash
DEVELOPER_ID_CERT="Developer ID Application: Your Name (TEAM123)" \
TEAM_ID="TEAM123" \
KEYCHAIN_PROFILE="your-profile" \
./build.sh -r
```

Default keychain profile: `infinidream-notarization`

### Examples
```bash
# Debug build (default)
./build.sh

# Release build
./build.sh -r

# Stage debug build
./build.sh -s

# Release with notarization
./build.sh -r -n

# Release with notarization and version (creates infinidream-0.12.0.zip)
./build.sh -r -n -v 0.12.0

# Full release with appcast generation for Sparkle auto-updates
./build.sh -r -n -v 0.12.0 -a
```

### Output
- Screensaver: `build/DerivedData/Build/Products/{Debug|Release}/infinidream.saver`
- Application: `build/{Debug|Release}/AppExport/infinidream.app`

The app bundle contains the embedded screensaver at `infinidream.app/Contents/Resources/infinidream.saver`.

## to release (with Sparkle auto-update)

### 1. Tag the version
```bash
git tag X.Y.Z
git push --tags
```

### 2. Build for release with appcast
```bash
cd client_generic/MacBuild
./build.sh -r -n -v X.Y.Z -a
```

This creates:
- `build/Release/infinidream-X.Y.Z.zip` - the app bundle
- `build/Release/appcast.xml` - Sparkle update feed

### 3. Upload to GitHub Releases

1. Go to https://github.com/e-dream-ai/client/releases/new
2. Select tag `X.Y.Z`
3. Write release notes
4. Upload `infinidream-X.Y.Z.zip`
5. Publish

### 4. Upload appcast.xml

Upload `build/Release/appcast.xml` to https://infinidream.ai/appcast.xml

This enables existing users to receive the update automatically via Sparkle.

### 5. Update frontend (for new installs)

Update `APP_VERSION` in the frontend repository:
`src/components/pages/install/install.page.tsx`

Push and Cloudflare will deploy in a few minutes.

### How Sparkle Auto-Update Works

1. App checks `https://infinidream.ai/appcast.xml` on launch
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
- `infinidream-App-Info.plist`
- `infinidream App copy-Info.plist`
- `ScreenSaver-Info.plist`

Existing users won't be able to update if the public key changes, so only regenerate keys if the private key is lost.

## to test (manually)

On stage test with https://stage.infinidream.ai/playlist/ab76a874-928c-45b1-88b6-b059ee54ef94
and https://stage.infinidream.ai/playlist/c823b48f-9eda-4157-b4d4-64fd2a8702e7

On alpha test with https://alpha.infinidream.ai/playlist/13489b20-cc0b-4923-8ea8-3f64015fe389
and https://alpha.infinidream.ai/playlist/bd5615c2-5a68-4e33-b9c1-6649fb09dc03
and https://alpha.infinidream.ai/playlist/d9726526-b8f2-4221-a0bf-67fbacc01f4d

With fully downloaded playlists, go through all navigation acommands:
next, prev, forward, backward, faster, slower, pause, both from
keyboard and with remote control. Remote only interactions: switching
playlists, playing from filmstrip.

Then do it again but after "rm -rf /Users/Shared/infinidream.ai-stage/content/"
to test while streaming.

Then do a basic test after "rm -rf /Users/Shared/infinidream.ai-stage/"
to test sign-in flow.

Then uninstall the screen saver, run app and make sure it installs and
selects the screen saver.

## doc below outdated by build.sh?

Upload the symbols to bugsnag, on a terminal: 
- if first time install the upload tool `brew install bugsnag/tap/bugsnag-dsym-upload`
- Go to https://app.bugsnag.com/settings/e-dream-dot-ai/projects/client-macos/missing-dsyms and copy the first missing UUID corresponding to the tag
- run `mdfind YOUR_UUID_HERE`. This will output a path to the dsym `path/to/dsyms/MyApp.dSYM`
- run `bugsnag-dsym-upload path/to/dsyms` (note that it's the path TO the dsyms)

the screensaver is more complicated because Xcode can't automatically
manage the signing. So

1) Use Archive in Xcode and export in a folder

2) In that folder, go into the mess of subfolders until you find infinidream.saver. 
OR ALTERNATIVELY (replace {username} with the macOS account name that created the build)

```
mv Products/Users/{username}/Library/Screen\ Savers/infinidream.saver/ ./
```

3) Zip the saver so it can be submitted

```
/usr/bin/ditto -c -k --keepParent "infinidream.saver" "infinidream.zip"
```

4) Assuming  you have created a keychain profile called "infinidream", this will launch the notarization process and wait until it's done

```
xcrun notarytool submit infinidream.zip --keychain-profile "infinidream" --wait
```

5) if successful staple the receipt (this lets people install the screensaver without connecting to apple servers for verification of the notarization)

```
xcrun stapler staple infinidream.saver
```

6) Zip the final thing

```
/usr/bin/ditto -c -k --keepParent "infinidream.saver" "infinidream-master-notarized+stapled.zip"
```

see https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
and https://support.apple.com/en-us/102654
