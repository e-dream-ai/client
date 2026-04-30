#include "RifeInterpolatorNcnn.h"

#include <cmath>
#include <filesystem>
#include <mutex>
#include <vector>

#include "BlendFrameInterpolator.h"
#include "Log.h"
#include "PlatformUtils.h"
#include "Timer.h"

extern "C" {
#include "libavutil/pixfmt.h"
}

#if defined(INFINIDREAM_ENABLE_RIFE)
#include "gpu.h"
#include "net.h"
#include "rife.h"
#endif

namespace FrameGeneration
{

namespace
{
constexpr float kMidpointTolerance = 0.001f;
constexpr uint64_t kMaxRuntimeFailures = 3;

#if defined(INFINIDREAM_ENABLE_RIFE)
std::mutex& GpuInstanceMutex()
{
    static std::mutex mutex;
    return mutex;
}

uint32_t& GpuInstanceRefCount()
{
    static uint32_t count = 0;
    return count;
}

void AcquireGpuRuntime()
{
    std::lock_guard<std::mutex> lock(GpuInstanceMutex());
    if (GpuInstanceRefCount() == 0)
        ncnn::create_gpu_instance();
    ++GpuInstanceRefCount();
}

void ReleaseGpuRuntime()
{
    std::lock_guard<std::mutex> lock(GpuInstanceMutex());
    if (GpuInstanceRefCount() == 0)
        return;
    --GpuInstanceRefCount();
    if (GpuInstanceRefCount() == 0)
        ncnn::destroy_gpu_instance();
}
#endif
} // namespace

#if defined(INFINIDREAM_ENABLE_RIFE)
class CRifeInterpolatorNcnn::Impl
{
  public:
    ~Impl()
    {
        backend.reset();
        ReleaseGpuRuntime();
    }

    int gpuIndex = -1;
    std::unique_ptr<RIFE> backend;
};
#else
class CRifeInterpolatorNcnn::Impl
{
};
#endif

CRifeInterpolatorNcnn::CRifeInterpolatorNcnn() = default;
CRifeInterpolatorNcnn::~CRifeInterpolatorNcnn() = default;

bool CRifeInterpolatorNcnn::BuiltWithSupport()
{
#if defined(INFINIDREAM_ENABLE_RIFE)
    return true;
#else
    return false;
#endif
}

const char* CRifeInterpolatorNcnn::RuntimeModelName()
{
    return "rife-v4.6";
}

void CRifeInterpolatorNcnn::setUnavailableLocked(const std::string& reason) const
{
    m_available = false;
    m_unavailabilityReason = reason;
}

bool CRifeInterpolatorNcnn::initialize(std::string* reason) const
{
    std::call_once(m_initializeOnce, [this]() {
        std::lock_guard<std::mutex> lock(m_mutex);

#if !defined(INFINIDREAM_ENABLE_RIFE)
        setUnavailableLocked("Built without RIFE support.");
        g_Log->Info("RIFE backend support compiled in: no");
#else
        g_Log->Info("RIFE backend support compiled in: yes");

        AcquireGpuRuntime();
        m_impl = std::make_unique<Impl>();

        const int gpuCount = ncnn::get_gpu_count();
        if (gpuCount <= 0)
        {
            setUnavailableLocked("No Vulkan-capable ncnn GPU devices were found.");
            return;
        }

        m_impl->gpuIndex = 0;
        m_gpuLabel = "GPU 0";

        const std::filesystem::path appPath(PlatformUtils::GetAppPath());
        const std::filesystem::path modelDir =
            appPath.parent_path() / "models" / RuntimeModelName();
        m_modelDirectory = modelDir.string();

        if (!std::filesystem::exists(modelDir))
        {
            setUnavailableLocked("Bundled model directory missing: " + m_modelDirectory);
            return;
        }

        const std::filesystem::path modelParam = modelDir / "flownet.param";
        const std::filesystem::path modelBin = modelDir / "flownet.bin";
        if (!std::filesystem::exists(modelParam) || !std::filesystem::exists(modelBin))
        {
            setUnavailableLocked("Bundled RIFE model files are missing under: " + m_modelDirectory);
            return;
        }

        auto backend = std::make_unique<RIFE>(m_impl->gpuIndex, false, false, false, 1, false, true);
        if (backend->load(m_modelDirectory) != 0)
        {
            setUnavailableLocked("Failed to load bundled RIFE model from: " + m_modelDirectory);
            return;
        }

        m_impl->backend = std::move(backend);
        m_available = true;
        m_unavailabilityReason.clear();
        g_Log->Info("RIFE backend initialized successfully (model=%s gpu=%s)",
                    m_modelDirectory.c_str(),
                    m_gpuLabel.c_str());
#endif
    });

    std::lock_guard<std::mutex> lock(m_mutex);
    if (reason)
    {
        *reason = m_available
            ? std::string("RIFE backend ready (model=") + m_modelDirectory + ", gpu=" + m_gpuLabel + ")"
            : m_unavailabilityReason;
    }
    return m_available;
}

bool CRifeInterpolatorNcnn::IsAvailable(std::string* reason) const
{
    return initialize(reason);
}

ContentDecoder::spCVideoFrame CRifeInterpolatorNcnn::Interpolate(
    const ContentDecoder::spCVideoFrame& previous,
    const ContentDecoder::spCVideoFrame& next,
    float t)
{
    if (!initialize(nullptr))
        return nullptr;

    if (!previous || !next)
        return nullptr;

    if (previous->Width() != next->Width() || previous->Height() != next->Height())
        return nullptr;

    AVFrame* prevFrame = previous->Frame();
    AVFrame* nextFrame = next->Frame();
    if (!prevFrame || !nextFrame)
        return nullptr;

    if (prevFrame->format != AV_PIX_FMT_RGBA || nextFrame->format != AV_PIX_FMT_RGBA)
        return nullptr;

    if (std::fabs(t - 0.5f) > kMidpointTolerance)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_loggedUnsupportedT)
        {
            g_Log->Warning("RIFE midpoint-only backend received unsupported t=%.3f", t);
            m_loggedUnsupportedT = true;
        }
        return nullptr;
    }

#if !defined(INFINIDREAM_ENABLE_RIFE)
    return nullptr;
#else
    const int width = static_cast<int>(previous->Width());
    const int height = static_cast<int>(previous->Height());
    CBlendFrameInterpolator blendFallback;

    // Reuse per-instance buffers to avoid per-frame heap allocations (~18 MB/frame)
    const size_t rgbBytes = static_cast<size_t>(width) * height * 3u;
    if (m_prevRgbBuf.size() < rgbBytes) m_prevRgbBuf.resize(rgbBytes);
    if (m_nextRgbBuf.size() < rgbBytes) m_nextRgbBuf.resize(rgbBytes);
    if (m_outRgbBuf.size()  < rgbBytes) m_outRgbBuf.resize(rgbBytes);

    // RGBA→RGB: pointer-step form allows auto-vectorisation
    {
        const int pixels = width * height;
        const uint8_t* src = prevFrame->data[0];
        unsigned char* dst = m_prevRgbBuf.data();
        for (int i = 0; i < pixels; ++i, src += 4, dst += 3)
            { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }
    }
    {
        const int pixels = width * height;
        const uint8_t* src = nextFrame->data[0];
        unsigned char* dst = m_nextRgbBuf.data();
        for (int i = 0; i < pixels; ++i, src += 4, dst += 3)
            { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }
    }

    // Timer starts before GPU dispatch so the reported time covers the full cost
    Base::CTimer timer;
    timer.Reset();

    ncnn::Mat in0(width, height, m_prevRgbBuf.data(), (size_t)3u);
    ncnn::Mat in1(width, height, m_nextRgbBuf.data(), (size_t)3u);
    ncnn::Mat out(width, height, (void*)m_outRgbBuf.data(), (size_t)3u);

    int processResult = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_available || !m_impl || !m_impl->backend)
            return nullptr;
        if (m_runtimeDisabled)
        {
            m_runtimeFallbackActive = true;
            auto result = blendFallback.Interpolate(previous, next, 0.5f);
            const double elapsed = timer.Time() * 1000.0;
            // m_mutex already held by the enclosing lock_guard
            m_lastInferenceMs = elapsed;
            m_totalInferenceMs += elapsed;
            ++m_successfulFrames;
            return result;
        }
        processResult = m_impl->backend->process(in0, in1, 0.5f, out);
    }

    const double elapsedMs = timer.Time() * 1000.0;

    if (processResult != 0 || out.empty())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_failedFrames;
        if (m_failedFrames >= kMaxRuntimeFailures)
        {
            m_runtimeDisabled = true;
            setUnavailableLocked("RIFE disabled after repeated runtime inference failures.");
        }
        m_runtimeFallbackActive = true;
        g_Log->Warning("RIFE inference failed (count=%llu, model=%s)",
                       static_cast<unsigned long long>(m_failedFrames),
                       m_modelDirectory.c_str());
        auto result = blendFallback.Interpolate(previous, next, 0.5f);
        const double elapsed = timer.Time() * 1000.0;
        m_lastInferenceMs = elapsed;   // still inside lock (lock_guard above covers this block)
        m_totalInferenceMs += elapsed;
        ++m_successfulFrames;
        return result;
    }

    auto output = std::make_shared<ContentDecoder::CVideoFrame>(
        width,
        height,
        AV_PIX_FMT_RGBA,
        previous->GetMetaData().fileName);
    AVFrame* dstFrame = output->Frame();
    if (!dstFrame || !dstFrame->data[0])
        return nullptr;

    // RGB→RGBA: pointer-step form for auto-vectorisation
    {
        const int pixels = width * height;
        const unsigned char* src = m_outRgbBuf.data();
        uint8_t* dst = dstFrame->data[0];
        for (int i = 0; i < pixels; ++i, src += 3, dst += 4)
            { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastInferenceMs = elapsedMs;
        m_totalInferenceMs += elapsedMs;
        ++m_successfulFrames;
        m_runtimeFallbackActive = false;

        if (!m_loggedFirstGeneratedFrame)
        {
            g_Log->Info("First RIFE frame generated successfully for model %s on %s (w=%d h=%d)",
                        m_modelDirectory.c_str(),
                        m_gpuLabel.c_str(),
                        width,
                        height);
            m_loggedFirstGeneratedFrame = true;
        }
    }

    const auto& meta = previous->GetMetaData();
    output->SetMetaData_FileName(meta.fileName);
    output->SetMetaData_DreamName(meta.name);
    output->SetMetaData_DreamAuthor(meta.author);
    output->SetMetaData_DecodeFps(meta.decodeFps);
    output->SetMetaData_IsSeam(false);
    output->SetMetaData_FrameIdx(meta.frameIdx);
    output->SetMetaData_MaxFrameIdx(meta.maxFrameIdx);

    return output;
#endif
}

std::string CRifeInterpolatorNcnn::ModelDirectory() const
{
    initialize(nullptr);
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_modelDirectory;
}

std::string CRifeInterpolatorNcnn::GpuLabel() const
{
    initialize(nullptr);
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_gpuLabel;
}

std::string CRifeInterpolatorNcnn::UnavailabilityReason() const
{
    initialize(nullptr);
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_unavailabilityReason;
}

double CRifeInterpolatorNcnn::LastInferenceMs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastInferenceMs;
}

double CRifeInterpolatorNcnn::AverageInferenceMs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_successfulFrames
        ? (m_totalInferenceMs / static_cast<double>(m_successfulFrames))
        : 0.0;
}

uint64_t CRifeInterpolatorNcnn::SuccessfulFrameCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_successfulFrames;
}

uint64_t CRifeInterpolatorNcnn::FailedInferenceCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_failedFrames;
}

bool CRifeInterpolatorNcnn::IsFallingBack() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_runtimeFallbackActive;
}

} // namespace FrameGeneration
