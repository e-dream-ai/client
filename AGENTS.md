# AGENTS.md — client

## Overview

Native desktop application and screensaver for infinidream.ai. Displays AI-generated animated visual content and syncs with the backend server. Targets macOS (primary) and Linux (Vulkan/Wayland).

## Stack

- **Language:** C++
- **Build System:** Xcode (macOS), CMake (Linux)
- **Auto-update:** Sparkle framework (EdDSA signatures) — macOS only
- **Dependencies:** vcpkg (macOS), system packages (Linux)
- **Scripting:** Python (build/release scripts), Bash
- **Binary Assets:** Git LFS

## Project Structure

```
client_generic/
  Client/           # CElectricSheep base + platform subclasses, CPlayer, Hud, main.cpp
  ContentDecoder/   # CContentDecoder (FFmpeg), CClip, CVideoFrame
  FrameGeneration/  # CFrameGenerationScheduler, RifeInterpolatorNcnn, BlendFrameInterpolator
  DisplayOutput/    # CDisplayVulkan, CRendererVulkan, font/texture/shader abstractions
  Common/           # base.h (PROFILER macros), Timer, Log, Settings
  Network/          # EDreamClient (auth, playlist), CurlTransfer, WebSocket remote control
  TupleStorage/     # JSONStorage — settings persistence backend
  MacBuild/         # Xcode project, build.py, release.py
  LinuxBuild/       # CMakeLists.txt; build output at LinuxBuild/build/
vcpkg/              # C++ dependency management (macOS)
build_mac_libs_vcpkg.sh
```

## Commands

### macOS
```bash
brew install git-lfs && git lfs install          # Required for binary assets
./vcpkg/bootstrap-vcpkg.sh && ./vcpkg/vcpkg install  # Install C++ deps
open client_generic/MacBuild/e-dream.xcodeproj   # Open in Xcode
cd client_generic/MacBuild && ./build.py               # Build app
cd client_generic/MacBuild && ./build.py -r -n         # Release build with notarization
cd client_generic/MacBuild && ./release.py -v X.Y.Z    # Publish release
```

### Linux
```bash
cd client_generic/LinuxBuild/build
make -j$(nproc)                        # Build (always run from this directory)
./infinidream                          # Run
./infinidream --framegen=rife          # Run with RIFE frame generation
./infinidream --framegen=blend_2x      # Run with blend frame generation
./infinidream --cached                 # Offline/cached-content mode
INFINIDREAM_PERF_LOG=1 ./infinidream   # Enable per-frame timing logs to stdout
```

`INFINIDREAM_ENABLE_RIFE` is a CMake option (check `LinuxBuild/CMakeLists.txt`). RIFE model files must exist at `<binary_dir>/models/rife-v4.6/flownet.{param,bin}`.

## Architecture

### Startup Flow
`main.cpp` parses `--cached` / `--framegen={blend_2x|rife}` CLI flags → creates platform client (`CElectricSheep_Linux` / `_Mac`) → `Startup()` → `Run()` → `Shutdown()`.

### Frame Flow (critical path)
```
CContentDecoder::ReadFramesThread()   [decoder thread, one per active clip]
  → FFmpeg/VAAPI decode → enqueues CVideoFrame

CClip::Update()                       [render thread]
  → PopVideoFrame() from decoder queue
  → CFrameGenerationScheduler::Advance()
      → CRifeInterpolatorNcnn::Interpolate()  or  CBlendFrameInterpolator
  → CClip::DrawFrame() → Vulkan texture upload → CRendererVulkan present
```

### Key Class Relationships

**`CPlayer`** (singleton via `g_Player()`) owns all `DisplayUnit`s (one per monitor). Each `DisplayUnit` pairs a `CDisplayVulkan` (window/surface) with a `CRendererVulkan` (device/pipelines).

**`CClip`** owns one `CContentDecoder` and one `CFrameGenerationScheduler`. Manages crossfade alpha, buffering state, and the playback clock. Clips preload asynchronously before they're needed — duplicate log lines around clip creation during startup are normal (one for preflight, one for active playback).

**`CFrameGenerationScheduler`** sits between decode and render. Tracks a fractional `m_pendingSyntheticFrames` accumulator to decide when to insert a generated frame. Calls the interpolator synchronously in the render thread.

**`CRifeInterpolatorNcnn`** wraps ncnn+Vulkan RIFE. Initialized lazily via `std::call_once`. Holds `m_mutex` for the full ~25ms GPU inference duration. Falls back to blend after 3 consecutive inference failures.

### Threading Model

| Thread | Purpose |
|--------|---------|
| `Main` | Render loop (`CElectricSheep::Run()`), all Vulkan present calls |
| `ReadFrames` | `CContentDecoder::ReadFramesThread()` — one per active clip |
| WebSocket (background) | Remote control socket; commands queued to main thread via `m_CommandQueue`, dequeued in `HandleEvents()` |

### Display FPS Auto-Detection (Linux)
`CPlayer` maintains a rolling vsync sample buffer. When the measured display Hz stabilizes, `ApplyNewDisplayFpsLocked()` reconfigures all active clips' frame generation. Expect "ReconfigureFrameGeneration" log lines shortly after startup; a 500-sample cooldown prevents feedback loops.

### Linux Render Loop Pacing
`shouldCap = false` on Linux+Vulkan — the loop relies on Vulkan FIFO present to throttle to display refresh. If `BeginDisplayFrame` returns false before a swapchain image is active (e.g. during pre-video startup), no throttle applies and the loop spins freely.

### Settings
`g_Settings()` returns a `CSettings` singleton backed by JSON in `~/.config/infinidream/` (Linux) or `~/Library/Application Support/infinidream/` (macOS). Hierarchical dot-separated keys (e.g. `settings.player.display_fps`).

## Runtime Logs

- **Linux:** stdout; settings/state in `~/.config/infinidream/`
- **macOS:** `/Users/Shared/infinidream.ai/Logs/YYYY_MM_DD.log`

## Key Patterns

- Dual mode: production (infinidream.ai) and staging (stage.infinidream.ai)
- Embedded screensaver within app bundle (macOS)
- Code signing auto-discovers Developer ID from Keychain (macOS)
- Auto-update via Sparkle appcast XML (macOS)

## Deployment

GitHub releases as `infinidream-X.Y.Z.zip`. Appcast URLs:
- Production: `https://infinidream.ai/appcast.xml`
- Stage: `https://infinidream.ai/stage/appcast.xml`
