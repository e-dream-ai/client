# AGENTS.md — client

## Overview

Native macOS desktop application and screensaver for infinidream.ai. Displays AI-generated animated visual content and syncs with the backend server.

## Stack

- **Language:** C++
- **Build System:** Xcode (macOS)
- **Auto-update:** Sparkle framework (EdDSA signatures)
- **Dependencies:** vcpkg
- **Scripting:** Python (build/release scripts), Bash
- **Binary Assets:** Git LFS

## Project Structure

```
client_generic/
  MacBuild/
    e-dream.xcodeproj   # Xcode project
    build.py             # Build script (-r release, -s stage, -n notarize, -v version)
    release.py           # Publish release with appcast generation
  (platform-agnostic C++ source)
vcpkg/                   # C++ dependency management
build_mac_libs_vcpkg.sh  # Dependency setup
```

## Commands

```bash
brew install git-lfs && git lfs install          # Required for binary assets
./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install  # Install C++ deps
open client_generic/MacBuild/e-dream.xcodeproj   # Open in Xcode
cd client_generic/MacBuild && ./build.py               # Build app
cd client_generic/MacBuild && ./build.py -r -n         # Release build with notarization
cd client_generic/MacBuild && ./release.py -v X.Y.Z    # Publish release
```

## Key Patterns

- Dual mode: production (infinidream.ai) and staging (stage.infinidream.ai)
- Embedded screensaver within app bundle
- Code signing auto-discovers Developer ID from Keychain
- Auto-update via Sparkle appcast XML

## Runtime Logs

Runtime logs are written to one file per day (`YYYY_MM_DD.log`) in:
- **macOS:** `/Users/Shared/infinidream.ai/Logs/`
- **Windows:** `%LOCALAPPDATA%\Infinidream\Logs\` (e.g., `C:\Users\<user>\AppData\Local\Infinidream\Logs\`; stage build uses `Infinidream-stage`)
- **Linux:** `/tmp/infinidream-logs/`

## Deployment

GitHub releases as `infinidream-X.Y.Z.zip`. Appcast URLs:
- Production: `https://infinidream.ai/appcast.xml`
- Stage: `https://infinidream.ai/stage/appcast.xml`
