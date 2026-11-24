# AGENTS.md — client

## Overview

Cross-platform native desktop application and screensaver for infinidream.ai (macOS, Windows, Linux). Displays AI-generated animated visual content and syncs with the backend server. See [README.md](README.md) for full build/release details.

## Stack

- **Language:** C++
- **Build Systems:** Xcode (macOS), MSVC / Visual Studio 2022 (Windows), CMake (Linux)
- **Rendering:** Metal (macOS), DirectX (Windows), Vulkan (Linux)
- **Auto-update:** Sparkle framework (EdDSA signatures) — macOS only; Windows users upgrade through winget
- **Dependencies:** vcpkg (manifest at `vcpkg.json`, submodule at `vcpkg/`); Linux uses system packages
- **Scripting:** Python (build/release scripts), Bash

## Project Structure

```
client_generic/
  MacBuild/
    e-dream.xcodeproj   # Xcode project
    build.py             # Build script (-r release, -s stage, -n notarize, -v version)
    release.py           # Promote a release: appcast, frontend version, Latest, Homebrew, winget
  MSVC/
    e-dream.sln          # Visual Studio solution (build Release | x64)
  WinBuild/
    build.py             # MSBuild driver (-v version, -r/-d, --platform, --run-vcpkg)
    release.py           # NSIS Setup.exe packaging (version from build.py -v)
  InstallerMSVC/
    nsis_installer.nsi   # NSIS installer script
  LinuxBuild/
    CMakeLists.txt       # CMake build (output: build/infinidream)
  (platform-agnostic C++ source)
vcpkg/                   # C++ dependency management (git submodule)
```

## Commands

Shared setup (all platforms): clone with submodules (`git submodule update --init --recursive`).

**macOS** (Xcode + vcpkg + Sparkle):

```bash
./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install  # Install C++ deps
open client_generic/MacBuild/e-dream.xcodeproj   # Open in Xcode
cd client_generic/MacBuild && ./build.py               # Build app (Debug)
cd client_generic/MacBuild && ./build.py -r -n         # Release build with notarization
cd client_generic/MacBuild && ./release.py -v X.Y.Z    # Publish release (appcast)
```

**Windows** (MSVC + vcpkg + NSIS). Set `VCPKG_ROOT` to the `vcpkg/` checkout:

```cmd
vcpkg\bootstrap-vcpkg.bat
vcpkg\vcpkg.exe install --triplet x64-windows
cd client_generic\WinBuild
python build.py                               :: Release | x64 (infinidream.exe / .scr)
python build.py -v X.Y.Z                       :: release build (without -v: dev build, versioned from the latest tag)
python build.py --sanitize asan               :: ASan test build of the app in MSVC\build-asan\ (for tests/mock-server)
python release.py                             :: NSIS Setup.exe, versioned as built
```

Or open `client_generic/MSVC/e-dream.sln` in Visual Studio 2022 and build **Release | x64**. There is no Sparkle auto-update on Windows.

**Linux** (CMake + Vulkan + system packages, no vcpkg). Install deps via the distro package manager (see [README.md](README.md) for the Arch package list), then:

```bash
git submodule update --init
cd client_generic/LinuxBuild
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/infinidream                            # binary at build/infinidream
```

Linux signs in with an emailed code (the first-run wizard, or a console prompt when no GUI wizard is registered), which stores a sealed session in `~/.config/infinidream/settings.json`. `INFINIDREAM_API_KEY` or `settings.content.api_key` still works as a fallback, but API-key auth skips the sealed-session refresh paths.

Linux build type doesn't select the backend: Debug and Release both target `api-alpha`. For stage, configure a separate tree with `-DINFINIDREAM_STAGE=ON` (defines `STAGE`: `api-stage`, stage web links, data in `~/.config/infinidream-stage/`):

```bash
cmake -B build-stage -DCMAKE_BUILD_TYPE=Release -DINFINIDREAM_STAGE=ON
cmake --build build-stage -j$(nproc)
```

`build_appimage.py` packages a release AppImage (static OpenSSL/FFmpeg/Boost, glibc shim); it writes `infinidream-<version>-<arch>.AppImage` (`--stage`: `infinidream-<version>-stage-<arch>.AppImage`); `-v X.Y.Z` sets the version (default: latest git tag) and `-g` uploads to GitHub release `X.Y.Z`.

## Key Patterns

- Dual mode: production (infinidream.ai) and staging (stage.infinidream.ai)
- Screensaver: embedded in the app bundle (macOS `.saver`); separate `infinidream.scr` on Windows; not applicable on Linux
- Code signing: macOS auto-discovers Developer ID from Keychain; Windows uses Authenticode via Azure Artifact Signing (`release.py --sign`, after `az login`; see README "Signing")
- Auto-update via Sparkle appcast XML — macOS only; Windows/Linux ship via GitHub releases without in-app update

## Runtime Logs

Runtime logs are written to one file per day (`YYYY_MM_DD.log`) in:
- **macOS:** `/Users/Shared/infinidream.ai/Logs/`
- **Windows:** `%LOCALAPPDATA%\Infinidream\Logs\` (e.g., `C:\Users\<user>\AppData\Local\Infinidream\Logs\`; stage build uses `Infinidream-stage`)
- **Linux:** `~/.config/infinidream/Logs/` (stage: `~/.config/infinidream-stage/Logs/`)

## Clean Install (testing)

To simulate a fresh first-run ("clean the install"), quit the app and delete the per-user data root. Logs live inside it on every platform. Stage builds use a parallel folder so prod and stage state don't collide.
- **macOS:** `/Users/Shared/infinidream.ai/` (stage: `/Users/Shared/infinidream.ai-stage/`)
- **Windows:** `%LOCALAPPDATA%\Infinidream\` (stage: `Infinidream-stage`)
- **Linux:** `~/.config/infinidream/` (stage: `~/.config/infinidream-stage/`)

The screensaver pointer at `HKCU\Control Panel\Desktop\SCRNSAVE.EXE` (Windows) is left alone unless you're specifically re-testing the screensaver opt-in.

## Deployment

Releases are run by hand from the build machines with the scripts below, never in CI. Full
steps in README "to prerelease" and "to release".

1. **Mac** `build.py -r -n -g -v X.Y.Z`: creates tag `X.Y.Z` (no `v`) and a GitHub prerelease,
   uploads `infinidream-X.Y.Z.dmg`.
2. **Windows** `release.py --sign -g` uploads `infinidream-X.Y.Z.exe`; **Linux**
   `build_appimage.py -v X.Y.Z -g` uploads `infinidream-X.Y.Z-x86_64.AppImage`. Both need the
   release from step 1.
3. **Mac** `release.py -v X.Y.Z` promotes it: appcast to landing-page `public/alpha/appcast.xml`
   (`-s`: `public/stage/appcast.xml` and nothing else), `APP_VERSION` in frontend
   `src/version.ts`, marks the release Latest, updates the Homebrew cask in
   `e-dream-ai/homebrew-tap`, and opens a winget PR (`komac update infinidream.Infinidream`,
   from the org fork `e-dream-ai/winget-pkgs`). The Windows exe must be uploaded first.

Prereleases (steps 1–2 only) reach testers without Sparkle. Once a version is promoted, never
replace its files: Homebrew and winget pin their sha256. Ship a new version instead.

Appcast URLs:
- Production: `https://infinidream.ai/appcast.xml`
- Stage: `https://infinidream.ai/stage/appcast.xml`
