#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "IFrameInterpolator.h"

namespace FrameGeneration
{

class CFrameGenerationScheduler
{
  public:
    using FrameProvider = std::function<ContentDecoder::spCVideoFrame()>;

    ~CFrameGenerationScheduler();

    void Configure(bool enabled, double sourceFps, double targetFps,
                   spIFrameInterpolator interpolator);
    void Reset();

    bool Enabled() const { return m_enabled && m_interpolator != nullptr; }
    double SourceFps() const { return m_sourceFps; }
    double PresentationFps() const { return Enabled() ? m_targetFps : m_sourceFps; }
    std::string ModeName() const
    {
        return m_interpolator ? std::string(m_interpolator->Name()) : std::string("off");
    }
    spIFrameInterpolator GetInterpolator() const { return m_interpolator; }
    const ContentDecoder::spCVideoFrame& CurrentDisplayFrame() const { return m_displayFrame; }
    double CurrentDisplayPhase() const { return m_displayPhase; }
    uint64_t GeneratedFrameCount() const { return m_generatedFrameCount; }
    uint64_t RealFrameCount() const { return m_realFrameCount; }
    double InterpolatorLastTimeMs() const;
    double InterpolatorAverageTimeMs() const;
    uint64_t InterpolatorFailureCount() const;
    bool InterpolatorFallingBack() const;

    bool Advance(const FrameProvider& frameProvider, std::string* reason = nullptr);

  private:
    bool prepareNextRealFrame(const FrameProvider& frameProvider, std::string* reason);
    bool canGenerateBetween(const ContentDecoder::spCVideoFrame& previous,
                            const ContentDecoder::spCVideoFrame& next) const;

    // Async worker — pre-computes the next interpolated frame off the render thread.
    void startAsyncWorker();
    void stopAsyncWorker();
    void asyncWorkerLoop();
    bool submitAsyncJob(const ContentDecoder::spCVideoFrame& prev,
                        const ContentDecoder::spCVideoFrame& next,
                        const spIFrameInterpolator& interpolator);
    ContentDecoder::spCVideoFrame takeAsyncResult(
        const ContentDecoder::spCVideoFrame& expectedPrev,
        const ContentDecoder::spCVideoFrame& expectedNext);
    void maybePreFetchAndSubmit(const FrameProvider& frameProvider,
                                double synthsPerGap,
                                const ContentDecoder::spCVideoFrame& prevFrame);

    bool m_enabled = false;
    double m_sourceFps = 0.0;
    double m_targetFps = 0.0;
    spIFrameInterpolator m_interpolator;

    ContentDecoder::spCVideoFrame m_currentRealFrame;
    ContentDecoder::spCVideoFrame m_nextRealFrame;
    ContentDecoder::spCVideoFrame m_generatedFrame;
    ContentDecoder::spCVideoFrame m_displayFrame;
    bool m_displayGeneratedNext = false;
    double m_displayPhase = 0.0;
    double m_pendingSyntheticFrames = 0.0;
    uint64_t m_generatedFrameCount = 0;
    uint64_t m_realFrameCount = 0;

    // Frame pre-fetched from the decoder ahead of when it's needed, so the async
    // worker can start RIFE on the (current, prefetched) pair immediately.
    ContentDecoder::spCVideoFrame m_prefetchedNextRealFrame;

    // Async worker thread state — all fields guarded by m_asyncMutex.
    std::thread m_asyncThread;
    std::mutex m_asyncMutex;
    std::condition_variable m_asyncWorkCv;
    std::condition_variable m_asyncResultCv;
    bool m_asyncStop = false;

    // Submitted job
    ContentDecoder::spCVideoFrame m_jobPrev;
    ContentDecoder::spCVideoFrame m_jobNext;
    spIFrameInterpolator m_jobInterpolator; // captured at submit time; safe against Configure()
    bool m_jobPending = false;              // submitted, not yet picked up by worker
    bool m_jobRunning = false;              // worker has started executing

    // Result from the most recently completed job
    ContentDecoder::spCVideoFrame m_asyncResult;
    ContentDecoder::spCVideoFrame m_resultForPrev; // frames the result was computed for
    ContentDecoder::spCVideoFrame m_resultForNext;
    bool m_resultReady = false;
};

} // namespace FrameGeneration
