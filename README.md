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
    brew install bugsnag/tap/bugsnag-cli

The general C++ dependencies are handled by vcpkg. The Microsoft vcpkg repo is included as a **git submodule** at `vcpkg/`. After cloning this repo, fetch it and build dependencies.

**macOS**

    git submodule update --init --recursive
    ./vcpkg/bootstrap-vcpkg.sh
    ./vcpkg/vcpkg install

**Windows** (from repo root; typical desktop build is **x64**)

    git submodule update --init --recursive
    vcpkg\bootstrap-vcpkg.bat
    vcpkg\vcpkg.exe install --triplet x64-windows

The root [vcpkg.json](vcpkg.json) is the manifest. Set **`VCPKG_ROOT`** to the full path of the `vcpkg` folder (for example `D:\src\client\vcpkg`) so Visual Studio MSBuild integration can resolve packages.

For a fresh clone you can use `git clone --recurse-submodules <url>` so `vcpkg/` is populated immediately; otherwise run `git submodule update --init` after clone.

On Mac, open `client_generic/MacBuild/infinidream.xcodeproj`. On Windows, open `client_generic/MSVC/e-dream.sln`.

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

## Build on Windows

Covers the native **MSVC** client ([`e-dream.sln`](client_generic/MSVC/e-dream.sln)), **vcpkg** manifest dependencies, and the **NSIS installer** / ZIP distribution via [`client_generic/WinBuild/`](client_generic/WinBuild/) (same style as [`MacBuild/build.py`](client_generic/MacBuild/build.py) and [`release.py`](client_generic/MacBuild/release.py)).

### Prerequisites

Everything used to build and release on Windows can be installed with **winget** (ships with Windows 10 1809+ / Windows 11). Open an elevated `cmd`/PowerShell and run:

```cmd
winget install --id Git.Git -e
winget install --id Python.Python.3.12 -e
winget install --id Microsoft.VisualStudio.2022.Community -e
winget install --id NSIS.NSIS -e
winget install --id GitHub.cli -e
```

| Package | Purpose | Notes |
|---------|---------|-------|
| `Git.Git` | Clone, submodules. | Bundles Git LFS; after install run `git lfs install` once. |
| `Python.Python.3.12` | Drives [`WinBuild/build.py`](client_generic/WinBuild/build.py) and [`release.py`](client_generic/WinBuild/release.py). Any 3.10+ works. | |
| `Microsoft.VisualStudio.2022.Community` | MSBuild, C++ toolchain, Windows 10/11 SDK, `signtool.exe`. Use `Microsoft.VisualStudio.2022.BuildTools` for headless/CI. | The winget install launches the VS Installer but **does not select workloads** — add **Desktop development with C++** (and the Windows SDK) in that UI, or pass `--override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"` to winget. |
| `NSIS.NSIS` | [`makensis`](client_generic/InstallerMSVC/nsis_installer.nsi) for the Windows installer. | Installs to `C:\Program Files (x86)\NSIS\` but does **not** add itself to `PATH`; [`release.py`](client_generic/WinBuild/release.py) auto-discovers it there, or honors `MAKENSIS=<path>`. |
| `GitHub.cli` | `release.py --github-release TAG` uploads artifacts via `gh`. | Run `gh auth login` once. |

`vcpkg` is **not** a separate install — it's a git submodule at [`vcpkg/`](vcpkg/) that is bootstrapped with `vcpkg\bootstrap-vcpkg.bat` (see [dev docs](#dev-docs) at the top of this file).

`signtool.exe` for Authenticode signing (`release.py --sign`) ships with the Windows SDK that the VS workload above installs — no separate package needed.

### Quick build in Visual Studio

Open `client_generic/MSVC/e-dream.sln`, choose **Release** and **x64**, build. Typical output directory:

`client_generic/MSVC/Release/` → **`infinidream.exe`**, **`infinidream.scr`**

**Toolsets:** the solution mixes toolsets — **x64** configurations use **v143**; **Win32** configurations may still reference **v140** and legacy **Boost** paths inside [`electricsheep.vcxproj`](client_generic/MSVC/electricsheep.vcxproj). Prefer **Release | x64** unless you maintain Win32 explicitly (the installer is 64-bit only).

**Drop-in runtime files:** place extra DLLs or assets under **`client_generic/MSVC/dll/Release/`** or **`dll/Debug/`** (folder name matches the MSBuild configuration — `Release`, `Debug`, `DebugMD`, etc.). After each successful link, MSBuild flattens every file under that folder into `$(OutDir)` (nested paths land flat; duplicate basenames overwrite). If `dll/<Configuration>/` doesn't exist, the step is skipped.

**vcpkg on Windows:** [`electricsheep.vcxproj`](client_generic/MSVC/electricsheep.vcxproj) enables `VcpkgEnableManifest`; Visual Studio resolves packages when `VCPKG_ROOT` points at your vcpkg submodule checkout. Set `VCPKG_ROOT` as a user/system env var (e.g. `D:\src\client\vcpkg`) and **restart Visual Studio**. For Win32 builds you may need the **`x86-windows`** triplet.

### Build script

```bat
cd client_generic\WinBuild
python build.py
```

Defaults: **Release**, **x64**, runs MSBuild **restore** then **build** (uses `MSBUILD` env or **vswhere** to locate MSBuild).

### Options (summary)

| Flag | Description |
|------|-------------|
| `-v SEMVER`, `--version SEMVER` | Embed this version in the binary (Settings / API). Omit to take `VER_*` from [`Common/clientversion.h`](client_generic/Common/clientversion.h). Also embeds `git rev-parse --short HEAD` (or `unknown` without a repo). |
| `-r`, `--release` | Configuration **Release** (default). |
| `-d`, `--debug` | Configuration **Debug** (outputs `infinidreamd.exe`). |
| `--configuration NAME` | Override configuration (e.g. `DebugMD`). |
| `--platform Win32` \| `x64` | Default **x64**. |
| `--run-vcpkg` | Run `vcpkg install` for `--triplet` first (default triplet **x64-windows**). |
| `--triplet TRIPLET` | Triplet for `--run-vcpkg`. |
| `--no-restore` | Skip MSBuild `/restore`. |
| `--msbuild PATH` | Force a specific `MSBuild.exe`. |

**MSBuild discovery:** uses the `MSBUILD` env var if set; otherwise **vswhere** (`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`) to locate `MSBuild.exe`. You can also add the MSBuild `Bin` folder to `PATH` so `msbuild.exe` resolves without vswhere.

### Examples

```bat
cd client_generic\WinBuild
python build.py
python build.py -d --platform x64
python build.py --run-vcpkg --triplet x64-windows
```

### Release installer and ZIP

Build with the same semver you will ship (writes `MSVC/edream_build_version_generated.h` then compiles):

```bat
python client_generic\WinBuild\build.py -v 0.14.0
```

Then package an NSIS `Setup.exe` (default) and optionally a portable ZIP:

```bat
python client_generic\WinBuild\release.py -v 0.14.0                        :: NSIS Setup.exe
python client_generic\WinBuild\release.py -v 0.14.0 --zip                  :: also writes a portable ZIP
python client_generic\WinBuild\release.py -v 0.14.0 --no-installer --zip   :: ZIP only
```

The installer packages the entire `client_generic\MSVC\Release\` directory (binaries, vcpkg-linked DLLs, fonts, OSD assets) directly — there is no separate staging step. License content is embedded from `client_generic\RuntimeMSVC\License.rtf`.

Output:

- `client_generic\WinBuild\dist\infinidream-windows-{version}-setup.exe` (NSIS)
- `client_generic\WinBuild\dist\infinidream-windows-{version}.zip` (with `--zip`)

Options:

| Flag | Description |
|------|-------------|
| `-v`, `--version` | Version string baked into the installer and file names (default `0.0.0`). |
| `--configuration` | MSVC folder to package (default **Release**). |
| `--zip` | Also build the portable ZIP. |
| `--no-installer` | Skip NSIS; useful when `makensis` is unavailable. |
| `--output-dir DIR` | Where to write artifacts. |
| `--sign` | Authenticode-sign artifacts via `SIGN_THUMBPRINT` or `SIGN_PFX`. |
| `--github-release TAG` | Upload with `gh`. |

#### Installer behavior

- **x64 Windows 10+** only; the installer aborts otherwise.
- Installs everything to `%ProgramFiles%\Infinidream\` (admin required).
- Creates Start Menu shortcuts (launch, windowed launch, website, uninstall).
- On the finish page, user can opt-in to set Infinidream as the **current screensaver** (writes `HKCU\Control Panel\Desktop\SCRNSAVE.EXE` to the full path of `infinidream.scr`).
- Uninstaller prompts to preserve or delete `%ProgramData%\Infinidream\` (downloaded content and logs). Existing installs auto-migrate from the legacy `%ProgramData%\e-dream\` on first launch.

The NSIS script lives at [`client_generic/InstallerMSVC/nsis_installer.nsi`](client_generic/InstallerMSVC/nsis_installer.nsi).

### Windows crash symbols

Windows builds produce **PDB** files next to binaries (e.g. under `client_generic\MSVC\Release\`). Upload PDBs to BugSnag following their Windows symbol-upload guidance; macOS dSYM instructions below do not apply on Windows.

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

### Windows release

There is **no** Sparkle appcast on Windows in this repo. Typical flow:

1. Build **Release \| x64** (Visual Studio or `client_generic/WinBuild/build.py`).
2. Package:

   ```bat
   cd client_generic\WinBuild
   python release.py -v X.Y.Z --github-release vX.Y.Z
   ```

   Produces **`client_generic/WinBuild/dist/infinidream-windows-X.Y.Z-setup.exe`** (NSIS) and uploads it to the release. Add `--zip` for the portable archive.

See [Build on Windows](#build-on-windows) above for the full flag list, installer behavior, and signing notes.

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
