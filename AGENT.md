# AGENT.md — client

## Overview
Native macOS desktop application and screensaver for infinidream.ai dream playback.

## Stack
- **Language:** C++ / Objective-C
- **Platform:** macOS (Xcode project)
- **Build:** Xcode + custom build script
- **Dependencies:** Git LFS (for large assets)

## Project Structure
```
client_generic/
  MacBuild/
    e-dream.xcodeproj   # Xcode project
    build_installer.sh   # DMG installer builder
```

## Commands
```bash
brew install git-lfs && git lfs install
open client_generic/MacBuild/e-dream.xcodeproj    # Open in Xcode
./client_generic/MacBuild/build_installer.sh 0.1.0 e-dream-0.1.0.dmg  # Build installer
```

## Key Patterns
- Xcode project for macOS app/screensaver
- Uses Git LFS for binary assets
- Code formatting via uncrustify (see `uncrustify.cfg`)
