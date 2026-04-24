# Linux RIFE Frame Generation Plan

## Progress Snapshot (2026-04-24)

**Implementation complete. Remaining work: live RIFE playback smoke test on real content.**

Completed and verified:

- the Linux `RIFE` stub has been replaced with a real `CRifeInterpolatorNcnn` wrapper around upstream `rife-ncnn-vulkan`
- the backend is stateful and performs lazy one-time initialization
- midpoint-only interpolation is implemented
- RGBA decoded frames are converted to RGB tensors, run through RIFE, and returned as a new RGBA `CVideoFrame`
- runtime failure counters and timing stats are implemented
- repeated runtime RIFE failures fall back to `Blend_2X` instead of hard-failing playback
- clip-level logging now reports requested/active backend state and periodic timing summaries
- F2 HUD now has a `Frame generation timing` line and preserves honest `RIFE (Blend_2X fallback)` reporting
- Linux CMake now has `INFINIDREAM_ENABLE_RIFE`
- Linux build logic now fetches pinned `ncnn` and pinned upstream `rife-ncnn-vulkan`
- RIFE model assets are copied into `models/rife-v4.6/` next to the built binary
- `build_appimage.sh` now passes through `INFINIDREAM_ENABLE_RIFE` and copies bundled `models/` into the AppDir
- Linux build caches / outputs for this path were added to `.gitignore`
- full `infinidream` app build with `INFINIDREAM_ENABLE_RIFE=ON` succeeds (2026-04-24)
- `build/models/rife-v4.6/flownet.{param,bin}` land next to the binary as expected
- binary starts cleanly; GPU selected: NVIDIA GeForce RTX 4090
- startup logging shows frame generation state honestly

Remaining validation (requires live content):

- cycle `G` to RIFE mode with content playing; verify `RIFE (active)` in HUD and "First RIFE frame generated" log
- verify fallback cases: missing model dir, RIFE disabled at build time
- verify seek and transition safety
- verify inference timing appears in HUD (`Frame generation timing` line)

Important implementation note:

- the fetched `ncnn` source path needed two compatibility fixes during integration:
  - use the `ncnn-20240102-full-source.zip` release asset instead of the plain source tarball, because the plain tarball did not contain required submodules
  - patch vendored `glslang/SPIRV/SpvBuilder.h` to include `<cstdint>` under this toolchain, otherwise GCC 15 failed inside the fetched dependency build

## Summary

Implement a real Linux `RIFE` frame-generation backend behind the existing frame-generation scaffolding already present in this branch.

The current branch already has:

- frame-generation modes: `Off`, `Blend_2X`, `RIFE`
- a clip-local `FrameGenerationScheduler`
- Linux CLI enablement with `-ufg`
- runtime mode cycling with `G`
- F2 HUD stats and startup/debug logging
- a stub `CRifeInterpolatorNcnn` that always reports unavailable

This plan replaces the stub with a real GPU-backed RIFE implementation using `ncnn` Vulkan on Linux.

The v1 integration goal is:

- Linux only
- cross-vendor GPU inference via Vulkan (`ncnn`), not NVIDIA-only optical flow
- midpoint interpolation only (`t = 0.5`)
- generated output returned as a CPU-visible `CVideoFrame` in `AV_PIX_FMT_RGBA`
- reuse the existing upload path from `CVideoFrame` into the Vulkan texture path
- preserve current fallback behavior so the app remains usable if RIFE is not available

This is intentionally not a zero-copy design. V1 prioritizes a working, debuggable, shippable backend over a deeper renderer refactor.

## Current Branch State

The current branch shape matters, because the implementation should fit what is already there rather than invent a parallel system.

### Existing frame-generation entry points

- `client_generic/FrameGeneration/IFrameInterpolator.h`
- `client_generic/FrameGeneration/BlendFrameInterpolator.cpp`
- `client_generic/FrameGeneration/RifeInterpolatorNcnn.{h,cpp}`
- `client_generic/FrameGeneration/FrameGenerationScheduler.{h,cpp}`
- `client_generic/FrameGeneration/FrameGenerationMode.h`

### Existing clip/playback integration

- `client_generic/ContentDecoder/Clip.cpp`
- `client_generic/Client/Player.cpp`
- `client_generic/Client/client.h`

### Existing Linux build / packaging

- `client_generic/LinuxBuild/CMakeLists.txt`
- `client_generic/LinuxBuild/build_appimage.sh`

### Existing frame representation

Linux decode already converts frames to `AV_PIX_FMT_RGBA`, and the renderer already uploads RGBA data from CPU-side frame storage into Vulkan textures.

Relevant existing behavior:

- `CVideoFrame` owns an `AVFrame` and optional CPU storage buffer
- `BlendFrameInterpolator` already creates an RGBA `CVideoFrame`
- `Clip::UploadFrameToTexture()` already uploads `CVideoFrame` data to the current texture

That means the RIFE backend should produce the same output type as `BlendFrameInterpolator`: a new RGBA `CVideoFrame`.

## Goals

### Primary goal

Make `FrameGenerationMode::RIFE` actually generate motion-aware intermediate frames on Linux using GPU inference.

### Secondary goals

- preserve the current runtime mode UX
- preserve startup stability and deterministic fallback behavior
- preserve clip-local scheduling and clip-boundary safety
- keep the implementation observable enough to debug real-world failures

### Non-goals for v1

- Windows/macOS support
- arbitrary interpolation times other than midpoint
- zero-copy GPU interop with renderer textures
- multi-model UI selection
- model download at app runtime
- automatic remote model updates
- replacing the current cadence-selection logic

## Decisions Locked In

### Backend technology

Use `ncnn` Vulkan with a RIFE model.

Do not use:

- AMD FSR frame generation
- NVIDIA Optical Flow SDK
- CPU-only interpolation

Reason:

- `ncnn` Vulkan is the best fit for the current data model: two decoded images in, one interpolated image out
- it is cross-vendor on Linux
- it can be integrated as an isolated interpolator backend without redesigning the renderer

### v1 model

Use a single bundled model in v1:

- `rife-v4.6`

Do not add runtime model selection in v1.

Reason:

- one model keeps the build, packaging, and debugging surface small
- the app does not currently have a strong UX for model variants

### Integration strategy

Use GPU compute plus CPU handoff:

1. take two RGBA `CVideoFrame` inputs
2. convert them to `ncnn::Mat`
3. run RIFE on GPU via Vulkan
4. read the output back into CPU-visible memory
5. allocate a new RGBA `CVideoFrame`
6. copy the generated pixels into it
7. return it through the existing scheduler and upload path

Do not attempt direct Vulkan-image sharing or zero-copy texture production in v1.

### Release / packaging strategy

Implement optional RIFE support as a Linux build option:

- standard build: `INFINIDREAM_ENABLE_RIFE=OFF`
- RIFE-enabled build: `INFINIDREAM_ENABLE_RIFE=ON`

Package `ncnn` and the RIFE model at build/package time into cached, gitignored directories under `client_generic/LinuxBuild/`.

Do not commit the model files or third-party fetched binary artifacts into git.

Default release policy:

- prefer one Linux/AppImage build if the bundled RIFE payload remains modest
- if packaged size increase becomes excessive, split standard and RIFE-enabled release artifacts

Implementation should support either release strategy without code redesign.

## Detailed Implementation Plan

### 1. Build-time dependency and model acquisition

#### 1.1 Add Linux build option

Add a new CMake option in `client_generic/LinuxBuild/CMakeLists.txt`:

- `INFINIDREAM_ENABLE_RIFE`

Default:

- `OFF`

Expected behavior:

- when `OFF`, build should succeed without `ncnn`
- when `ON`, build should require/fetch/build `ncnn` and compile the real backend

#### 1.2 Add cached third-party directories

Use gitignored cache directories under `client_generic/LinuxBuild/`, for example:

- `client_generic/LinuxBuild/deps-cache/`
- `client_generic/LinuxBuild/model-cache/`

These are build/package caches only.

Do not place fetched third-party payloads inside `client_generic/Runtime/`.

#### 1.3 Fetch/build `ncnn`

When `INFINIDREAM_ENABLE_RIFE=ON`, add build logic to fetch or prepare `ncnn` with Vulkan enabled.

The implementation choice can be either:

- fetch source and build locally during CMake / packaging
- or fetch a prepared Linux package and link against it

But the plan target is:

- deterministic cached local build artifact
- Vulkan enabled
- no repo-tracked artifacts

Required outcome:

- headers available to the Linux target
- linkable `ncnn` library available for the Linux target

#### 1.4 Fetch the RIFE model at build/package time

Fetch exactly one model:

- `rife-v4.6`

The fetched model cache should contain the exact `.param` and `.bin` files needed by the chosen RIFE/ncnn backend.

The build/package scripts must pin a concrete upstream source/version rather than “latest”.

Add a simple version marker file in the cache so stale cache invalidation is easy.

### 2. Runtime asset layout

#### 2.1 Runtime model path

Place bundled model assets relative to the binary so both build-tree runs and AppImage runs can resolve them.

Recommended runtime layout:

- binary directory
- `models/rife-v4.6/...`

Examples:

- build tree: `client_generic/LinuxBuild/build/models/rife-v4.6/`
- AppImage: `AppDir/usr/bin/models/rife-v4.6/`

#### 2.2 Path resolution

Add a small helper in the Linux/client-common path logic to resolve the runtime model directory from the executable location.

The RIFE backend must not rely on cwd or user config paths for the bundled model.

Required search behavior:

1. check bundled model path next to executable
2. if not found, log a precise missing-path error
3. mark RIFE unavailable and allow fallback

Do not silently search many random locations in v1.

### 3. `CRifeInterpolatorNcnn` implementation

#### 3.1 Replace the stub with a real stateful backend

`CRifeInterpolatorNcnn` should own its own backend state, not rebuild everything every frame.

Add members for:

- one-time init flag
- availability flag
- unavailability reason string
- model directory path
- chosen GPU index / device info
- `ncnn` network instance(s)
- Vulkan allocator / compute resources as needed by `ncnn`
- performance counters for last and rolling-average inference time

#### 3.2 One-time initialization

Initialization should happen lazily on first use or first `IsAvailable()` call.

Initialization steps:

1. confirm build includes RIFE support
2. confirm Vulkan-capable `ncnn` runtime can initialize
3. locate model directory
4. load model files
5. create reusable network/extractor resources
6. set availability state

If any step fails:

- cache the failure reason
- return unavailable
- avoid retrying every frame

#### 3.3 Input validation

`Interpolate(previous, next, t)` should return `nullptr` immediately when:

- either frame is null
- widths/heights differ
- either frame format is not `AV_PIX_FMT_RGBA`
- `t` is not midpoint in v1

Midpoint condition:

- treat `t` values very close to `0.5f` as valid
- all other values are unsupported in v1

Log unsupported `t` only once or at low frequency to avoid spam.

#### 3.4 RGBA input conversion

Convert the two RGBA input frames into the format expected by the RIFE/ncnn implementation.

Requirements:

- handle stride correctly; do not assume tightly packed rows
- normalize/convert to the tensor layout expected by the model
- avoid allocating new helper objects every frame when practical

If the chosen helper path in `ncnn` requires RGB rather than RGBA:

- strip alpha during tensor creation
- set output alpha to opaque or preserve alpha from the source frames, depending on implementation convenience

Chosen default:

- preserve alpha as fully opaque in the generated frame unless exact alpha preservation is trivial

Reason:

- current decoded video path is opaque video content
- alpha correctness is not a visible product concern here

#### 3.5 Inference execution

Run RIFE inference on GPU via Vulkan.

V1 constraints:

- only generate one midpoint frame per adjacent pair
- no recursive interpolation
- no batch generation

Measure inference time for each call.

Track:

- last inference ms
- rolling average inference ms
- total successful RIFE frame count
- total failed inference count

#### 3.6 Output frame construction

Allocate a new `ContentDecoder::CVideoFrame` in `AV_PIX_FMT_RGBA`.

Fill it with the generated frame data.

Copy metadata from the previous source frame in the same style as `BlendFrameInterpolator`:

- file name
- dream name
- author
- decode fps
- seam=false
- frame index metadata copied from the previous frame base

Do not invent new metadata fields in v1.

### 4. Clip and scheduler integration

#### 4.1 Keep `IFrameInterpolator` unchanged

Do not redesign the scheduler interface for v1.

The existing `IFrameInterpolator` contract is sufficient:

- name
- GPU-backed flag
- availability
- `Interpolate(previous, next, t)`

This reduces risk and keeps `Blend_2X` and `RIFE` interchangeable at the scheduler layer.

#### 4.2 Preserve current mode-selection flow

In `Clip.cpp`, keep the current logic shape:

- `Off` -> no interpolator
- `Blend2X` -> `CBlendFrameInterpolator`
- `RIFE` -> `CRifeInterpolatorNcnn`

If `RIFE` is selected but unavailable:

- fall back to `CBlendFrameInterpolator`
- keep current selected presentation fps
- expose the fallback honestly via logs and F2 HUD

#### 4.3 Preserve clip-local safety boundaries

Never interpolate:

- across dream boundaries
- across seam frames
- across a seek reset
- across transition/crossfade boundaries between two clips

Existing clip reset behavior should continue to clear scheduler/interpolator state whenever playback is reloaded or seeked.

Do not add cross-clip frame generation logic in v1.

### 5. HUD, logs, and debugging

#### 5.1 Startup logs

Add explicit startup logs that show:

- binary built with RIFE support: yes/no
- frame generation mode requested at startup
- model path used for RIFE
- whether RIFE backend initialization succeeded
- exact fallback reason when it did not

#### 5.2 Clip-level logs

When a clip enables RIFE successfully, log:

- dream UUID
- source fps
- presentation fps
- chosen backend name
- chosen GPU / device if easy to obtain from `ncnn`

When the first real RIFE-generated frame is produced, log that once.

Periodically log inference summary, e.g. every N generated frames:

- generated count
- real frame count
- average inference ms
- last inference ms

#### 5.3 F2 HUD

F2 must distinguish clearly between:

- `Frame generation Off`
- `Blend_2X (active)`
- `RIFE (active)`
- `RIFE (Blend_2X fallback)`

Add RIFE-specific timing info to HUD if there is already a good place for it.

Recommended additional stat lines when FG is enabled:

- `Output cadence`
- `Generated frames`
- `Frame generation`
- `Frame generation timing`

If the HUD gets too noisy, timing can be one compact line:

- `RIFE 8.4 ms avg / 9.1 ms last`

### 6. Failure handling

RIFE must fail safe.

Failure cases to handle explicitly:

- build without RIFE support
- model path missing
- model files missing or corrupt
- Vulkan-capable `ncnn` initialization failure
- unsupported GPU or broken driver path
- per-frame inference failure

Required behavior:

- no crash
- no stall waiting forever for generated output
- fallback to `Blend_2X` when mode is `RIFE` but backend is unavailable
- fallback should be visible in HUD and logs

If inference fails repeatedly at runtime after successful startup:

- log the failure
- disable the RIFE backend for the remainder of the clip or session
- fall back to `Blend_2X`

Chosen default:

- disable for the remainder of the session after repeated runtime inference failures

Reason:

- repeated per-frame failures should not keep spamming logs or causing unstable playback

### 7. Linux build and AppImage packaging

#### 7.1 CMake build-tree support

When `INFINIDREAM_ENABLE_RIFE=ON`:

- compile/link the real `CRifeInterpolatorNcnn`
- copy the bundled/fetched model directory into the build output next to the binary
- ensure build-tree runs can use `./infinidream` directly without extra manual setup

When `INFINIDREAM_ENABLE_RIFE=OFF`:

- either compile a stub backend or compile the same file with a disabled codepath
- startup/HUD should still behave honestly

#### 7.2 AppImage bundling

Extend `build_appimage.sh` to:

- fetch/cache `ncnn` and the RIFE model when RIFE build is enabled
- bundle required `ncnn` runtime libraries
- copy `models/rife-v4.6/` into `AppDir/usr/bin/models/rife-v4.6/`

The AppImage should not depend on internet access at runtime.

#### 7.3 Release-size decision gate

After implementation, measure packaged size impact.

Decision:

- if added RIFE payload is modest, keep one Linux AppImage build
- if it materially bloats the download, produce separate standard and RIFE-enabled AppImages

This is a release engineering decision, not a blocker for code implementation.

The code/path layout should support both.

## Concrete Implementation Order

Perform the work in this order:

1. Add Linux build option and disabled-by-default compile path for RIFE support.
2. Add cached fetch/build support for `ncnn` and pinned RIFE model assets.
3. Add runtime model path resolution relative to executable.
4. Implement `CRifeInterpolatorNcnn` initialization and `IsAvailable()`.
5. Implement midpoint `Interpolate()` returning a real RGBA `CVideoFrame`.
6. Wire timing/debug data through the interpolator and into existing clip/HUD reporting.
7. Update build-tree asset copying and AppImage packaging.
8. Test fallback cases.
9. Test real playback on at least one Linux GPU that supports Vulkan.

Do not start by refactoring scheduler or renderer code. The existing abstractions are sufficient for v1.

## Test Plan

### Build tests

#### Standard Linux build

Build with:

- `INFINIDREAM_ENABLE_RIFE=OFF`

Verify:

- build succeeds
- app runs
- `RIFE` mode can still be selected in UI/runtime cycle
- selecting `RIFE` reports unavailable and falls back cleanly

#### RIFE-enabled Linux build

Build with:

- `INFINIDREAM_ENABLE_RIFE=ON`

Verify:

- build succeeds
- model directory is copied next to the binary
- startup logs show RIFE support built in

### Runtime behavior tests

#### Playback with RIFE active

Run:

- app with frame generation on
- cycle `G` to `RIFE`

Verify:

- HUD shows `RIFE (active)`
- logs show model path and backend init success
- first generated RIFE frame is logged
- generated frame count increases

#### Fallback behavior

Simulate each failure:

- missing model directory
- corrupt model file
- RIFE disabled at build time
- backend init failure

Verify:

- no crash
- logs explain why
- HUD shows `RIFE (Blend_2X fallback)` or equivalent honest fallback state

#### Seek and transition safety

Verify:

- seek resets the scheduler cleanly
- no stale generated frame is displayed after seeking
- no interpolation occurs across clip seams or crossfade boundaries

#### Runtime toggle behavior

Verify:

- pressing `G` cycles through `Off -> Blend_2X -> RIFE -> Off`
- switching to `RIFE` during playback reloads cleanly
- HUD reflects the new effective backend

### Performance/debug tests

Verify:

- inference timing is logged and/or shown in HUD
- playback remains stable under repeated RIFE generation
- runtime failures disable RIFE instead of thrashing

Target test hardware if available:

- one AMD Linux system
- one NVIDIA Linux system

Intel is useful too, but AMD/NVIDIA are the priority for this branch.

## Acceptance Criteria

The work is complete when all of the following are true:

1. `RIFE` mode on Linux performs real GPU-backed frame generation rather than falling back unconditionally.
2. Generated output goes through the existing `CVideoFrame` -> Vulkan texture upload path.
3. Standard builds without RIFE support still compile and run cleanly.
4. Missing models or backend failures do not crash the app.
5. F2/HUD and logs report the effective backend honestly.
6. AppImage packaging can include the RIFE model assets for RIFE-enabled builds.
7. Runtime `G` cycling still works and correctly updates the effective backend state.

## Handoff Notes

If another agent continues this work, the most relevant touched files are:

- `client_generic/FrameGeneration/RifeInterpolatorNcnn.{h,cpp}`
- `client_generic/FrameGeneration/FrameGenerationScheduler.{h,cpp}`
- `client_generic/ContentDecoder/Clip.{h,cpp}`
- `client_generic/Client/Player.{h,cpp}`
- `client_generic/Client/client.h`
- `client_generic/LinuxBuild/CMakeLists.txt`
- `client_generic/LinuxBuild/build_appimage.sh`
- `.gitignore`

Commands:

Standard build (no RIFE):
- `cmake -S client_generic/LinuxBuild -B client_generic/LinuxBuild/build -DINFINIDREAM_ENABLE_RIFE=OFF`
- `cmake --build client_generic/LinuxBuild/build -j$(nproc) --target infinidream`

RIFE-enabled build:
- `cmake -S client_generic/LinuxBuild -B client_generic/LinuxBuild/build-rife -DINFINIDREAM_ENABLE_RIFE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5`
- `cmake --build client_generic/LinuxBuild/build-rife -j$(nproc) --target infinidream`

Both `build/` and `build-rife/` are gitignored. Source deps are cached in `deps-cache/` and `model-cache/` (also gitignored), so a clean reconfigure skips the download step but recompiles ncnn/rife.

Next step: live RIFE playback smoke test (requires content playing):

1. run `./build-rife/infinidream -ufg`
2. press `G` twice to reach RIFE mode
3. verify `RIFE (active)` in F2 HUD and "First RIFE frame generated" in logs

## Assumptions and Defaults

- Linux only in v1
- `ncnn` Vulkan backend
- model: `rife-v4.6`
- midpoint interpolation only
- GPU inference + CPU handoff
- runtime assets resolved relative to executable
- fetched third-party payloads are cached locally and gitignored
- one build artifact preferred if bundled size remains modest; otherwise split standard / RIFE builds

## Notes for the Implementation Pass

- Do not spend time making the renderer “more correct” before the backend works.
- Do not redesign frame pacing as part of this task.
- Do not block the implementation on perfect zero-copy GPU interop.
- Keep the app honest: if `RIFE` is selected but unavailable, say so clearly in both logs and HUD.
- Reuse the current branch’s scaffolding instead of adding a second frame-generation system.
