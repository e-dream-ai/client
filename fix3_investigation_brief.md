# Fix 3 Investigation Brief — VAAPI→GPU Zero-Copy for RIFE

## Context

`infinidream --framegen=rife` currently burns ~24–28% CPU on the `ReadFrames` decoder
thread. Profiling (perf + INFINIDREAM_PERF_LOG) attributes this to `libswscale`
performing YUV→RGBA conversion on CPU for every decoded frame. The pipeline today is:

```
VAAPI decode (GPU) → av_hwframe_transfer_data → CPU RAM (NV12/YUV)
  → sws_scale (CPU) → RGBA in CPU RAM
    → CRifeInterpolatorNcnn::Interpolate: RGBA→RGB copy (CPU)
      → ncnn Vulkan compute (GPU)
        → RGB→RGBA copy (CPU) → CVideoFrame ready for render
```

There are three CPU round-trips for data that starts and ends on the GPU. The goal is to
eliminate at least the most expensive one (the `sws_scale` pass).

## Key Files to Read

| File | What to look for |
|------|-----------------|
| `client_generic/ContentDecoder/ContentDecoder.cpp` | `av_hwframe_transfer_data` call (~line 651), `sws_scale` call (~line 840), how `m_WantedPixelFormat` flows in |
| `client_generic/ContentDecoder/Clip.cpp` | Lines 83–97: where `AV_PIX_FMT_RGBA` is hardcoded as the wanted pixel format — this is the format contract RIFE depends on |
| `client_generic/FrameGeneration/RifeInterpolatorNcnn.cpp` | Lines 226–240 and 301–308: the RGBA→RGB and RGB→RGBA CPU loops that wrap the GPU call. Check what ncnn `Mat` element sizes are supported |
| `client_generic/DisplayOutput/Vulkan/RendererVulkan.cpp` | How `CVideoFrame` data gets uploaded to a Vulkan texture — does it go through CPU staging or can it accept a dmabuf/VkImage? |
| `client_generic/ContentDecoder/Frame.h` | `CVideoFrame` structure — does it hold an `AVFrame*`, a CPU buffer, or could it hold a GPU handle? |

## Candidate Approaches (investigate feasibility of each)

### A — Skip av_hwframe_transfer_data; feed NV12 directly into ncnn

VAAPI gives us `AVFrame` with `data[3]` = a `VASurface` handle. We could:
1. Call `vaGetImage` or `vaDeriveImage` to map the NV12 surface
2. Upload NV12 directly via a Vulkan staging buffer or compute shader
3. Do NV12→RGB conversion in a Vulkan compute pass before ncnn inference

**Investigate:** Does ncnn's RIFE `process()` require packed RGB `ncnn::Mat`, or can it
accept a `VkImageView` / `VkBuffer` in NV12/YUV420 layout? Look at ncnn's
`rife.cpp` / `RIFE::process()` signature and whether ncnn exposes a Vulkan-native
input path.

### B — VK_EXT_external_memory_dma_buf import

Export the VAAPI surface as a DRM PRIME fd (via `vaExportSurfaceHandle`), then import
it as a `VkImage` using `VK_EXT_external_memory_dma_buf`. This gives a zero-copy
`VkImage` on the GPU.

**Investigate:**
- Does the system Vulkan driver (NVIDIA on RTX 4090) actually support
  `VK_EXT_external_memory_dma_buf` for VAAPI-exported surfaces? VAAPI on NVIDIA uses
  the iHD driver or NVDEC — check which is active in the log (`VAAPI hardware decoding enabled`).
- Does ncnn expose an API to run inference on an externally-owned `VkImage`?
  Search ncnn headers for `import`, `external`, `VkImageMat`, or `VkBufferMat`.

### C — Replace sws_scale with a Vulkan compute NV12→RGBA pass (cheapest win)

Even without zero-copy, we could skip `av_hwframe_transfer_data` and instead:
1. Transfer NV12 from VAAPI to a CPU staging buffer (one memcpy instead of sws_scale)
2. Upload NV12 to a Vulkan texture
3. Run a simple compute shader: NV12→RGBA in one GPU pass

This avoids `sws_scale` entirely (the ~13% CPU hotspot) and feeds RIFE an RGBA `VkImage`
on the GPU. Cost: writing a small compute shader.

**Investigate:** Is there already a Vulkan compute infrastructure in
`DisplayOutput/Vulkan/` that could host a NV12→RGBA shader? Or would this be
entirely new?

### D — Shorter-term: replace sws_scale with libyuv (CPU, but SIMD-faster)

`sws_scale` for NV12→RGBA is not as well-optimised as `libyuv`. This doesn't eliminate
the CPU work but may halve it.

**Investigate:** Is `libyuv` already a dependency? Check `LinuxBuild/CMakeLists.txt`.
This is a low-risk fallback if approaches A–C aren't feasible.

## What We Know from Profiling

- `perf stat` showed **47% L3 cache miss rate** — the 18 MB of RIFE pixel buffers
  thrash L3. Even approach C (GPU NV12→RGBA) would eliminate the large CPU buffers in
  `RifeInterpolatorNcnn.cpp` (lines 220–224) if ncnn can accept a VkImage input.
- RIFE inference time dropped from ~25ms to ~14ms in the async fix, suggesting the GPU
  was previously contended with the render thread. Zero-copy would further reduce
  inference time by eliminating the CPU→GPU re-upload of pixel data.
- Source video is 1920×1080 @ 20 fps. Frame size: 1920×1080×4 bytes RGBA ≈ 7.9 MB.
  RIFE uses three such buffers (prev, next, out) = ~24 MB touched per generated frame.

## Questions to Answer Before Designing

1. Which VAAPI backend is active? (Intel iHD vs NVIDIA NVDEC vs VA-API via VDPAU).
   Look for the DRI device path in startup logs or `/dev/dri/`.
2. Does ncnn's Vulkan RIFE backend expose any non-CPU-Mat input path?
3. Does the RIFE model accept NV12/YUV input natively, or does it require RGB?
   (It was trained on RGB — YUV input would need colour-space conversion somewhere.)
4. What does `CVideoFrame` need to look like for the renderer to upload it to a
   Vulkan texture? Can it hold a `VkImage` handle instead of a CPU buffer?
