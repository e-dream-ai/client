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
    infinidream.xcodeproj   # Xcode project
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
open client_generic/MacBuild/infinidream.xcodeproj   # Open in Xcode
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

## Platform-Specific Code

**Shared code asks for platform facts; it never states them.** A hardcoded
`~/.config/infinidream/settings.json` in shared code compiles cleanly on all
three platforms and is wrong on two of them, with no build-time signal — that is
how [#675](https://github.com/e-dream-ai/client/issues/675) happened.

The seam is `client_generic/Client/PlatformUtils.h`: one header of `static`
declarations, with exactly one implementation compiled per build system.

| Platform | Implementation | Wired into |
|---|---|---|
| Windows | `client_generic/MSVC/PlatformUtils_win.cpp` | `MSVC/electricsheep.vcxproj` |
| macOS | `client_generic/MacBuild/PlatformUtils_Mac.mm` | `MacBuild/infinidream.xcodeproj` |
| Linux | `client_generic/LinuxBuild/PlatformUtils_Linux.cpp` | `LinuxBuild/CMakeLists.txt` |

Because the declarations are unconditional, **adding a method and forgetting an
implementation is a link error on that platform** — loud and immediate. That is
the property an `#ifdef` cannot give you: a missing or wrong `#ifdef` branch
fails silently at runtime, usually in a user's log.

Rules:

- Need a platform-specific value or behaviour in shared code? Add a
  `PlatformUtils` method and implement it in **all three** files. Where a
  platform has nothing to do, write an explicit no-op with a comment saying why.
- Never add an `#ifdef WIN32` / `MAC` / `LINUX_GNU` to `PlatformUtils.h` itself.
  Platform-private helpers go in that platform's own internal header — see
  `LinuxBuild/PlatformUtils_Internal.h`.
- For the settings file location, use `g_Settings()->ConfigPath()`. It returns
  the path the client actually opened, so it cannot drift from reality.
- To put an error in front of a user, call `g_Log->Error()`. It already forwards
  to `PlatformUtils::NotifyError()` (see `CLog::Error` in `Common/Log.cpp`), so
  calling the seam directly as well reports the same failure twice.
  `NotifyError()` is currently a `TODO` stub on Windows and Mac — the log file is
  the only channel that reaches a user there.
- `#ifdef` is still fine for `#include`s and for genuinely platform-shaped APIs
  (a function taking an `HWND`). It is not fine for facts that shared code could
  have asked for.

`scripts/check_platform_paths.py` enforces the path half of this in CI
(`.github/workflows/lint.yml`). Run it locally with
`python scripts/check_platform_paths.py`.

## Runtime Logs

Runtime logs are written to one file per day (`YYYY_MM_DD.log`) in:
- **macOS:** `/Users/Shared/infinidream.ai/Logs/`
- **Windows:** `%LOCALAPPDATA%\Infinidream\Logs\` (e.g., `C:\Users\<user>\AppData\Local\Infinidream\Logs\`; stage build uses `Infinidream-stage`)
- **Linux:** `/tmp/infinidream-logs/`

## Clean Install (testing)

To simulate a fresh first-run ("clean the install"), quit the app and delete the per-user data root. Logs live inside it on macOS/Windows, separately on Linux. Stage builds use a parallel folder so prod and stage state don't collide.
- **macOS:** `/Users/Shared/infinidream.ai/` (stage: `/Users/Shared/infinidream.ai-stage/`)
- **Windows:** `%LOCALAPPDATA%\Infinidream\` (stage: `Infinidream-stage`)
- **Linux:** `~/.config/infinidream/` plus `/tmp/infinidream-logs/`

The screensaver pointer at `HKCU\Control Panel\Desktop\SCRNSAVE.EXE` (Windows) is left alone unless you're specifically re-testing the screensaver opt-in.

## Deployment

GitHub releases as `infinidream-X.Y.Z.zip`. Appcast URLs:
- Production: `https://infinidream.ai/appcast.xml`
- Stage: `https://infinidream.ai/stage/appcast.xml`
