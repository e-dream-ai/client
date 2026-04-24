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

    std::vector<unsigned char> previousRgb(static_cast<size_t>(width) * height * 3u);
    std::vector<unsigned char> nextRgb(static_cast<size_t>(width) * height * 3u);

    for (int row = 0; row < height; ++row)
    {
        const uint8_t* prevRow = prevFrame->data[0] + row * prevFrame->linesize[0];
        const uint8_t* nextRow = nextFrame->data[0] + row * nextFrame->linesize[0];
        unsigned char* prevDst = previousRgb.data() + static_cast<size_t>(row) * width * 3u;
        unsigned char* nextDst = nextRgb.data() + static_cast<size_t>(row) * width * 3u;

        for (int col = 0; col < width; ++col)
        {
            prevDst[col * 3 + 0] = prevRow[col * 4 + 0];
            prevDst[col * 3 + 1] = prevRow[col * 4 + 1];
            prevDst[col * 3 + 2] = prevRow[col * 4 + 2];
            nextDst[col * 3 + 0] = nextRow[col * 4 + 0];
            nextDst[col * 3 + 1] = nextRow[col * 4 + 1];
            nextDst[col * 3 + 2] = nextRow[col * 4 + 2];
        }
    }

    Base::CTimer timer;
    timer.Reset();

    std::vector<unsigned char> outRgbData(static_cast<size_t>(width) * height * 3u);
    ncnn::Mat in0(width, height, previousRgb.data(), (size_t)3u);
    ncnn::Mat in1(width, height, nextRgb.data(), (size_t)3u);
    ncnn::Mat out(width, height, (void*)outRgbData.data(), (size_t)3u);

    int processResult = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_available || !m_impl || !m_impl->backend)
            return nullptr;
        if (m_runtimeDisabled)
        {
            m_runtimeFallbackActive = true;
            return blendFallback.Interpolate(previous, next, 0.5f);
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
        return blendFallback.Interpolate(previous, next, 0.5f);
    }

    auto output = std::make_shared<ContentDecoder::CVideoFrame>(
        width,
        height,
        AV_PIX_FMT_RGBA,
        previous->GetMetaData().fileName);
    AVFrame* dstFrame = output->Frame();
    if (!dstFrame || !dstFrame->data[0])
        return nullptr;

    for (int row = 0; row < height; ++row)
    {
        const unsigned char* srcRow = outRgbData.data() + static_cast<size_t>(row) * width * 3u;
        uint8_t* dstRow = dstFrame->data[0] + row * dstFrame->linesize[0];
        for (int col = 0; col < width; ++col)
        {
            dstRow[col * 4 + 0] = srcRow[col * 3 + 0];
            dstRow[col * 4 + 1] = srcRow[col * 3 + 1];
            dstRow[col * 4 + 2] = srcRow[col * 3 + 2];
            dstRow[col * 4 + 3] = 255;
        }
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
