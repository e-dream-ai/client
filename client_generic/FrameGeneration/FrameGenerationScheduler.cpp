#include "FrameGenerationScheduler.h"

#include "RifeInterpolatorNcnn.h"

namespace FrameGeneration
{

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CFrameGenerationScheduler::~CFrameGenerationScheduler()
{
    stopAsyncWorker();
}

void CFrameGenerationScheduler::Configure(bool enabled, double sourceFps,
                                          double targetFps,
                                          spIFrameInterpolator interpolator)
{
    // Stop any existing worker before swapping the interpolator so the worker
    // thread never races against a replaced m_interpolator pointer.
    stopAsyncWorker();

    m_enabled = enabled && sourceFps > 0.0 && targetFps > sourceFps &&
                targetFps <= sourceFps * 2.0 && interpolator != nullptr;
    m_sourceFps = sourceFps;
    m_targetFps = targetFps;
    m_interpolator = std::move(interpolator);
    Reset();

    // Start the background worker only for GPU-backed interpolators; CPU-only
    // ones (blend) are fast enough that async overhead is not worthwhile.
    if (m_enabled && m_interpolator && m_interpolator->IsGpuBacked())
        startAsyncWorker();
}

void CFrameGenerationScheduler::Reset()
{
    // Flush async state — any in-flight job will finish and its result will be
    // discarded by takeAsyncResult() when frame pointers no longer match.
    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        m_resultReady = false;
        m_asyncResult.reset();
        m_resultForPrev.reset();
        m_resultForNext.reset();
        // Do NOT clear m_jobPending/m_jobRunning: the worker owns those frames
        // until it finishes; they'll be naturally superseded on the next job.
    }

    m_prefetchedNextRealFrame.reset();
    m_currentRealFrame.reset();
    m_nextRealFrame.reset();
    m_generatedFrame.reset();
    m_displayFrame.reset();
    m_displayGeneratedNext = false;
    m_displayPhase = 0.0;
    m_pendingSyntheticFrames = 0.0;
    m_generatedFrameCount = 0;
    m_realFrameCount = 0;
}

// ---------------------------------------------------------------------------
// Async worker
// ---------------------------------------------------------------------------

void CFrameGenerationScheduler::startAsyncWorker()
{
    m_asyncStop = false;
    m_asyncThread = std::thread(&CFrameGenerationScheduler::asyncWorkerLoop, this);
}

void CFrameGenerationScheduler::stopAsyncWorker()
{
    if (!m_asyncThread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        m_asyncStop = true;
    }
    m_asyncWorkCv.notify_all();
    m_asyncThread.join();
}

void CFrameGenerationScheduler::asyncWorkerLoop()
{
    while (true)
    {
        ContentDecoder::spCVideoFrame prev, next;
        spIFrameInterpolator interpolator;
        {
            std::unique_lock<std::mutex> lock(m_asyncMutex);
            m_asyncWorkCv.wait(lock,
                [this] { return m_asyncStop || m_jobPending; });
            if (m_asyncStop)
                return;
            prev         = m_jobPrev;
            next         = m_jobNext;
            interpolator = m_jobInterpolator;
            m_jobPending = false;
            m_jobRunning = true;
        }

        auto result = interpolator->Interpolate(prev, next, 0.5f);

        {
            std::lock_guard<std::mutex> lock(m_asyncMutex);
            m_jobRunning   = false;
            m_asyncResult  = std::move(result);
            m_resultForPrev = std::move(prev);
            m_resultForNext = std::move(next);
            m_resultReady   = true;
        }
        m_asyncResultCv.notify_one();
    }
}

bool CFrameGenerationScheduler::submitAsyncJob(
    const ContentDecoder::spCVideoFrame& prev,
    const ContentDecoder::spCVideoFrame& next,
    const spIFrameInterpolator& interpolator)
{
    std::lock_guard<std::mutex> lock(m_asyncMutex);
    // Don't overwrite a job that is already in-flight or a result not yet consumed.
    if (m_jobPending || m_jobRunning || m_resultReady)
        return false;
    m_jobPrev        = prev;
    m_jobNext        = next;
    m_jobInterpolator = interpolator;
    m_jobPending     = true;
    m_asyncWorkCv.notify_one();
    return true;
}

ContentDecoder::spCVideoFrame CFrameGenerationScheduler::takeAsyncResult(
    const ContentDecoder::spCVideoFrame& expectedPrev,
    const ContentDecoder::spCVideoFrame& expectedNext)
{
    std::unique_lock<std::mutex> lock(m_asyncMutex);

    if (!m_resultReady)
    {
        if (m_jobPending)
        {
            // Worker hasn't even started yet — not worth waiting; caller falls back to sync.
            return nullptr;
        }
        // Worker is running — it should finish within a frame budget; wait briefly.
        m_asyncResultCv.wait_for(lock, std::chrono::milliseconds(50),
            [this] { return m_resultReady || m_asyncStop; });
    }

    if (!m_resultReady || !m_asyncResult)
        return nullptr;

    // Validate that the result is for the frame pair we actually need right now.
    if (m_resultForPrev != expectedPrev || m_resultForNext != expectedNext)
    {
        m_resultReady = false;
        m_asyncResult.reset();
        return nullptr;
    }

    m_resultReady = false;
    return std::move(m_asyncResult);
}

void CFrameGenerationScheduler::maybePreFetchAndSubmit(
    const FrameProvider& frameProvider,
    double synthsPerGap,
    const ContentDecoder::spCVideoFrame& prevFrame)
{
    if (!m_asyncThread.joinable() || !m_interpolator || !prevFrame)
        return;

    // Only pre-fetch if the next gap is actually going to need a synthetic frame.
    if (m_pendingSyntheticFrames + synthsPerGap < 1.0)
        return;

    // Don't clobber a job or result already in the pipeline for this same pair.
    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        if (m_jobPending || m_jobRunning || m_resultReady)
            return;
    }

    // Pull the next real frame from the decoder now so the worker can start immediately.
    auto nextFrame = frameProvider();
    if (!nextFrame)
        return;

    m_prefetchedNextRealFrame = nextFrame;

    if (!canGenerateBetween(prevFrame, m_prefetchedNextRealFrame))
        return;

    submitAsyncJob(prevFrame, m_prefetchedNextRealFrame, m_interpolator);
}

// ---------------------------------------------------------------------------
// Stats forwarding (unchanged)
// ---------------------------------------------------------------------------

double CFrameGenerationScheduler::InterpolatorLastTimeMs() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->LastInferenceMs() : 0.0;
}

double CFrameGenerationScheduler::InterpolatorAverageTimeMs() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->AverageInferenceMs() : 0.0;
}

uint64_t CFrameGenerationScheduler::InterpolatorFailureCount() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->FailedInferenceCount() : 0;
}

bool CFrameGenerationScheduler::InterpolatorFallingBack() const
{
    auto rife = std::dynamic_pointer_cast<CRifeInterpolatorNcnn>(m_interpolator);
    return rife ? rife->IsFallingBack() : false;
}

// ---------------------------------------------------------------------------
// Internal helpers (unchanged)
// ---------------------------------------------------------------------------

bool CFrameGenerationScheduler::prepareNextRealFrame(const FrameProvider& frameProvider,
                                                     std::string* reason)
{
    if (m_nextRealFrame)
        return true;

    if (!frameProvider)
    {
        if (reason)
            *reason = "No frame provider configured.";
        return false;
    }

    m_nextRealFrame = frameProvider();
    if (!m_nextRealFrame)
    {
        if (reason)
            *reason = "Decoder queue is empty.";
        return false;
    }

    return true;
}

bool CFrameGenerationScheduler::canGenerateBetween(
    const ContentDecoder::spCVideoFrame& previous,
    const ContentDecoder::spCVideoFrame& next) const
{
    if (!previous || !next)
        return false;

    if (previous->Width() != next->Width() || previous->Height() != next->Height())
        return false;

    const auto& prevMeta = previous->GetMetaData();
    const auto& nextMeta = next->GetMetaData();
    if (prevMeta.isSeam || nextMeta.isSeam)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Advance — drives the render thread one presentation step forward.
//
// The async path works as follows:
//   • When we show a real frame, we immediately pre-fetch the frame after it
//     from the decoder and submit RIFE(current, prefetched) to the worker.
//   • On the next call that needs a generated frame, takeAsyncResult() collects
//     the pre-computed result — typically already ready since RIFE (~25 ms) fits
//     inside the presentation interval (~28 ms at 36 fps).
//   • A sync fallback fires on cold start or when the result arrives late.
// ---------------------------------------------------------------------------

bool CFrameGenerationScheduler::Advance(const FrameProvider& frameProvider, std::string* reason)
{
    if (!Enabled())
    {
        if (reason)
            *reason = "Frame generation is disabled.";
        return false;
    }

    const double synthsPerGap =
        (m_sourceFps > 0.0) ? ((m_targetFps / m_sourceFps) - 1.0) : 0.0;

    // -----------------------------------------------------------------------
    // Bootstrap: grab the very first real frame.
    // -----------------------------------------------------------------------
    if (!m_currentRealFrame)
    {
        m_currentRealFrame = frameProvider ? frameProvider() : nullptr;
        if (!m_currentRealFrame)
        {
            if (reason)
                *reason = "No initial decoded frame available.";
            return false;
        }

        m_displayFrame = m_currentRealFrame;
        m_displayPhase = 0.0;
        m_displayGeneratedNext = false;
        ++m_realFrameCount;

        // Pre-fetch the frame after this one so the worker can start early.
        maybePreFetchAndSubmit(frameProvider, synthsPerGap, m_currentRealFrame);
        return true;
    }

    // -----------------------------------------------------------------------
    // Not yet showing a generated frame: decide whether to generate one.
    // -----------------------------------------------------------------------
    if (!m_displayGeneratedNext)
    {
        // Use the pre-fetched frame if available, otherwise pop from decoder.
        if (m_prefetchedNextRealFrame)
        {
            m_nextRealFrame = std::move(m_prefetchedNextRealFrame);
        }
        else if (!prepareNextRealFrame(frameProvider, reason))
        {
            return false;
        }

        m_pendingSyntheticFrames += synthsPerGap;
        const bool shouldGenerateThisGap = m_pendingSyntheticFrames >= 1.0;

        if (shouldGenerateThisGap && canGenerateBetween(m_currentRealFrame, m_nextRealFrame))
        {
            // Try the async pre-computed result first.
            if (m_asyncThread.joinable())
                m_generatedFrame = takeAsyncResult(m_currentRealFrame, m_nextRealFrame);

            // Fall back to synchronous if the async result isn't ready.
            if (!m_generatedFrame)
                m_generatedFrame = m_interpolator->Interpolate(m_currentRealFrame, m_nextRealFrame, 0.5f);
        }
        else
        {
            m_generatedFrame.reset();
        }

        if (m_generatedFrame)
        {
            m_pendingSyntheticFrames -= 1.0;
            m_displayFrame = m_generatedFrame;
            m_displayPhase = 0.5;
            m_displayGeneratedNext = true;
            ++m_generatedFrameCount;

            // While we display this generated frame, pre-fetch the frame after
            // m_nextRealFrame and start the next RIFE job in the background.
            maybePreFetchAndSubmit(frameProvider, synthsPerGap, m_nextRealFrame);
            return true;
        }

        // No generated frame this gap — advance directly to the next real frame.
        m_currentRealFrame = m_nextRealFrame;
        m_nextRealFrame.reset();
        m_displayFrame = m_currentRealFrame;
        m_displayPhase = 0.0;
        m_displayGeneratedNext = false;
        ++m_realFrameCount;

        maybePreFetchAndSubmit(frameProvider, synthsPerGap, m_currentRealFrame);
        return true;
    }

    // -----------------------------------------------------------------------
    // Just showed a generated frame: advance to the next real frame.
    // -----------------------------------------------------------------------
    if (!m_nextRealFrame)
    {
        if (reason)
            *reason = "No decoded frame available for real-frame step.";
        return false;
    }

    m_currentRealFrame = m_nextRealFrame;
    m_nextRealFrame.reset();
    m_generatedFrame.reset();
    m_displayFrame = m_currentRealFrame;
    m_displayPhase = 0.0;
    m_displayGeneratedNext = false;
    ++m_realFrameCount;

    // Pre-fetch and submit the next job while the render thread displays this real frame.
    maybePreFetchAndSubmit(frameProvider, synthsPerGap, m_currentRealFrame);
    return true;
}

} // namespace FrameGeneration
