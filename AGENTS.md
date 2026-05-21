# AGENTS.md — client

## Overview

Cross-platform native desktop application and screensaver for infinidream.ai (macOS, Windows, Linux). Displays AI-generated animated visual content and syncs with the backend server. See [README.md](README.md) for full build/release details.

## Stack

- **Language:** C++
- **Build Systems:** Xcode (macOS), MSVC / Visual Studio 2022 (Windows), CMake (Linux)
- **Rendering:** Metal (macOS), DirectX (Windows), Vulkan (Linux)
- **Auto-update:** Sparkle framework (EdDSA signatures) — macOS only
- **Dependencies:** vcpkg (manifest at `vcpkg.json`, submodule at `vcpkg/`); Linux uses system packages
- **Scripting:** Python (build/release scripts), Bash
- **Binary Assets:** Git LFS

## Project Structure

```
client_generic/
  MacBuild/
    e-dream.xcodeproj   # Xcode project
    build.py             # Build script (-r release, -s stage, -n notarize, -v version)
    release.py           # Publish release with Sparkle appcast generation
  MSVC/
    e-dream.sln          # Visual Studio solution (build Release | x64)
  WinBuild/
    build.py             # MSBuild driver (-v version, -r/-d, --platform, --run-vcpkg)
    release.py           # NSIS Setup.exe + portable ZIP packaging
  InstallerMSVC/
    nsis_installer.nsi   # NSIS installer script
  LinuxBuild/
    CMakeLists.txt       # CMake build (output: build/infinidream)
  (platform-agnostic C++ source)
vcpkg/                   # C++ dependency management (git submodule)
```

## Commands

Shared setup (all platforms): clone with submodules (`git submodule update --init --recursive`) and install Git LFS.

**macOS** (Xcode + vcpkg + Sparkle):

```bash
brew install git-lfs && git lfs install          # Required for binary assets
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
python build.py -v X.Y.Z                       :: build with embedded version
python release.py -v X.Y.Z                     :: NSIS Setup.exe (add --zip for portable ZIP)
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

Linux has no UI sign-in: authenticate with `INFINIDREAM_API_KEY` env var or `~/.config/infinidream/settings.json`.

## Key Patterns

- Dual mode: production (infinidream.ai) and staging (stage.infinidream.ai)
- Screensaver: embedded in the app bundle (macOS `.saver`); separate `infinidream.scr` on Windows; not applicable on Linux
- Code signing: macOS auto-discovers Developer ID from Keychain; Windows uses Authenticode (`release.py --sign`)
- Auto-update via Sparkle appcast XML — macOS only; Windows/Linux ship via GitHub releases without in-app update

## Runtime Logs

Runtime logs are written to one file per day (`YYYY_MM_DD.log`) in:
- **macOS:** `/Users/Shared/infinidream.ai/Logs/`
- **Windows:** `%LOCALAPPDATA%\Infinidream\Logs\` (e.g., `C:\Users\<user>\AppData\Local\Infinidream\Logs\`; stage build uses `Infinidream-stage`)
- **Linux:** `/tmp/infinidream-logs/`

## Deployment

GitHub releases as `infinidream-X.Y.Z.zip`. Appcast URLs:
- Production: `https://infinidream.ai/appcast.xml`
- Stage: `https://infinidream.ai/stage/appcast.xml`
