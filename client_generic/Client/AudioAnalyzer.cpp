#include "AudioAnalyzer.h"

extern "C" {
#include "aubio.h"
}

#include "BeatDetektor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <thread>
#include <vector>
#include "Settings.h"

namespace
{
constexpr uint_t kSampleRate = 44100;
constexpr uint_t kWinSize   = 2048;
constexpr uint_t kHopSize   = 512;
constexpr uint_t kNumBins   = kWinSize / 2 + 1;

float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float SmoothAttackRelease(float previous, float current, float attack, float release)
{
    const float a = current > previous ? attack : release;
    return previous * (1.0f - a) + current * a;
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

constexpr uint_t kMelBands = 40;

struct AubioState
{
    aubio_pvoc_t*       pvoc       = nullptr;
    cvec_t*             fftgrain   = nullptr;
    fvec_t*             hopIn      = nullptr;
    fvec_t*             tempoOut   = nullptr;
    aubio_tempo_t*      tempo      = nullptr;
    aubio_filterbank_t* filterbank = nullptr;
    fvec_t*             melOut     = nullptr;
    bool                ready      = false;

    void Init()
    {
        hopIn    = new_fvec(kHopSize);
        fftgrain = new_cvec(kWinSize);
        tempoOut = new_fvec(2);
        melOut   = new_fvec(kMelBands);

        pvoc       = new_aubio_pvoc(kWinSize, kHopSize);
        tempo      = new_aubio_tempo("complex", kWinSize, kHopSize, kSampleRate);
        filterbank = new_aubio_filterbank(kMelBands, kWinSize);
        if (filterbank)
            aubio_filterbank_set_mel_coeffs(filterbank, (smpl_t)kSampleRate, 20.0f, 11000.0f);

        if (pvoc && tempo && filterbank)
            ready = true;
    }

    ~AubioState()
    {
        if (pvoc)       del_aubio_pvoc(pvoc);
        if (fftgrain)   del_cvec(fftgrain);
        if (hopIn)      del_fvec(hopIn);
        if (tempoOut)   del_fvec(tempoOut);
        if (tempo)      del_aubio_tempo(tempo);
        if (filterbank) del_aubio_filterbank(filterbank);
        if (melOut)     del_fvec(melOut);
    }
};

// ── Manual BPM control ────────────────────────────────────────────────────────
static std::atomic<float>    s_bpmOverride{0.0f};
static std::atomic<float>    s_liveBpm{120.0f};
static std::atomic<float>    s_liveBeatPhase{0.0f};
static std::atomic<uint32_t> s_liveBeatCount{0};
static std::atomic<bool>     s_resetBeatPhase{false};

void AudioAnalyzer_SetBpmOverride(float bpm)
{
    s_bpmOverride.store(bpm, std::memory_order_relaxed);
}

float AudioAnalyzer_GetCurrentBpm()
{
    return s_liveBpm.load(std::memory_order_relaxed);
}

float AudioAnalyzer_GetBeatPhase()
{
    return s_liveBeatPhase.load(std::memory_order_relaxed);
}

uint32_t AudioAnalyzer_GetBeatCount()
{
    return s_liveBeatCount.load(std::memory_order_relaxed);
}

void AudioAnalyzer_ResetBeatPhase()
{
    s_resetBeatPhase.store(true, std::memory_order_relaxed);
    s_liveBeatCount.store(0, std::memory_order_relaxed);
}

float AudioAnalyzer_TapTempo()
{
    using Clock = std::chrono::steady_clock;
    using Sec   = std::chrono::duration<double>;

    static constexpr int kMaxTaps = 8;
    static Clock::time_point s_taps[kMaxTaps];
    static int s_tapCount = 0;

    const auto now = Clock::now();

    if (s_tapCount > 0)
    {
        if (Sec(now - s_taps[(s_tapCount - 1) % kMaxTaps]).count() > 3.0)
            s_tapCount = 0;
    }

    s_taps[s_tapCount % kMaxTaps] = now;
    ++s_tapCount;

    if (s_tapCount < 2)
        return 0.0f;

    const int n      = std::min(s_tapCount, kMaxTaps);
    const int oldest = (s_tapCount - n) % kMaxTaps;
    const int newest = (s_tapCount - 1) % kMaxTaps;
    const double totalSec = Sec(s_taps[newest] - s_taps[oldest]).count();
    if (totalSec <= 0.0)
        return 0.0f;

    const float bpm = static_cast<float>(60.0 * (n - 1) / totalSec);
    return (bpm >= 40.0f && bpm <= 250.0f) ? bpm : 0.0f;
}

// ── Background thread ─────────────────────────────────────────────────────────

void AudioAnalyzer::Start()
{
    if (m_Running.exchange(true))
        return; // already running
    m_Thread = std::thread(&AudioAnalyzer::ThreadProc, this);
}

void AudioAnalyzer::Stop()
{
    if (!m_Running.exchange(false))
        return; // already stopped
    if (m_Thread.joinable())
        m_Thread.join();
}

AudioFeatures AudioAnalyzer::GetFeaturesSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_FeaturesMutex);
    return m_Features;
}

void AudioAnalyzer::ThreadProc()
{
    using Clock = std::chrono::steady_clock;

    std::deque<float> sampleBuffer;
    AubioState        aubio;

    // Per-thread state (was static locals in Update())
    float bassPeak = 0.001f, midPeak = 0.001f, highPeak = 0.001f;
    int   warmup   = 0;
    float s_prevBass = 0.0f, s_prevMid = 0.0f, s_prevHigh = 0.0f;
    float s_acfBpm    = 120.0f;
    uint_t s_totalHops = 0;
    BeatDetektor bd_slow(60.0f, 119.0f);   // covers 60–119 BPM
    BeatDetektor bd_fast(120.0f, 239.0f);  // covers 120–239 BPM
    std::vector<float> bdFftVec(kNumBins, 0.0f);
    float s_beatPhase    = 0.0f;
    float s_beatInterval = 60.0f / 120.0f;
    int   s_beatCount    = 0;
    float volume         = 0.0f;
    float spectralCentroid = 0.0f;
    float kick = 0.0f, snare = 0.0f, transient = 0.0f;

    // Silence tracking uses wall clock since audio may be sparse
    auto silenceStart = Clock::now();
    bool inSilence    = false;

    if (!m_AudioInput.IsRunning())
        m_AudioInput.Start();

    aubio.Init();

    while (m_Running.load(std::memory_order_relaxed))
    {
        // Pull new samples from WASAPI
        std::vector<float> newSamples = m_AudioInput.GetSamples();
        for (float s : newSamples)
            sampleBuffer.push_back(s);

        // Bound the backlog (~200 ms) so start-up after a stall stays smooth
        constexpr size_t kMaxBufferSamples = kHopSize * 18;
        if (sampleBuffer.size() > kMaxBufferSamples)
            sampleBuffer.erase(sampleBuffer.begin(),
                               sampleBuffer.begin() +
                                   static_cast<std::ptrdiff_t>(sampleBuffer.size() - kMaxBufferSamples));

        if (sampleBuffer.size() < kHopSize)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!aubio.ready)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Process all available hops — thread runs continuously so no per-call cap needed
        float bassEnergyAcc = 0.0f, midEnergyAcc = 0.0f, highEnergyAcc = 0.0f;
        float centroidNum   = 0.0f, centroidDen  = 0.0f;
        float rmsAcc        = 0.0f;
        int   hops          = 0;
        bool  beatFired     = false;

        while (sampleBuffer.size() >= kHopSize)
        {
            for (uint_t i = 0; i < kHopSize; ++i)
            {
                const float s            = sampleBuffer[i];
                aubio.hopIn->data[i]     = s;
                rmsAcc                  += s * s;
            }
            for (uint_t i = 0; i < kHopSize; ++i)
                sampleBuffer.pop_front();

            aubio_pvoc_do(aubio.pvoc, aubio.hopIn, aubio.fftgrain);

            for (uint_t bin = 1; bin < kNumBins; ++bin)
            {
                const float mag = aubio.fftgrain->norm[bin];
                const float f   = (float)kSampleRate * bin / (float)kWinSize;
                if (f >= 20.0f && f < 20000.0f)
                {
                    centroidNum += f * mag;
                    centroidDen += mag;
                }
            }

            aubio_filterbank_do(aubio.filterbank, aubio.fftgrain, aubio.melOut);
            float bassEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
            for (uint_t b = 0;  b < 8;  ++b) bassEnergy += aubio.melOut->data[b];
            for (uint_t b = 8;  b < 28; ++b) midEnergy  += aubio.melOut->data[b];
            for (uint_t b = 28; b < 40; ++b) highEnergy += aubio.melOut->data[b];
            bassEnergy /= 8.0f;
            midEnergy  /= 20.0f;
            highEnergy /= 12.0f;

            bassEnergyAcc += bassEnergy;
            midEnergyAcc  += midEnergy;
            highEnergyAcc += highEnergy;

            aubio_tempo_do(aubio.tempo, aubio.hopIn, aubio.tempoOut);
            if (aubio.tempoOut->data[0] > 0.0f) beatFired = true;

            // Feed BeatDetektor — reuse aubio's per-hop FFT magnitudes, zero extra FFT cost
            ++s_totalHops;
            {
                const float hopTimeSec = float(s_totalHops) * float(kHopSize) / float(kSampleRate);
                for (uint_t bin = 0; bin < kNumBins; ++bin)
                    bdFftVec[bin] = aubio.fftgrain->norm[bin];
                bd_slow.process(hopTimeSec, bdFftVec);
                bd_fast.process(hopTimeSec, bdFftVec);
            }

            ++hops;
        }

        if (hops == 0)
            continue;

        const float bassEnergy = bassEnergyAcc / hops;
        const float midEnergy  = midEnergyAcc  / hops;
        const float highEnergy = highEnergyAcc / hops;
        const float rawLevel   = std::sqrt(rmsAcc / (hops * (int)kHopSize));

        // RMS volume
        float clampedLevel = rawLevel;
        if (clampedLevel < DbToLinear(-60.0f))
            clampedLevel = 0.0f;
        clampedLevel = Clamp01(clampedLevel * 3.0f);
        volume = SmoothAttackRelease(volume, clampedLevel, 0.35f, 0.08f);

        // Spectral centroid
        float rawCentroid = 0.0f;
        if (centroidDen > 0.0f)
            rawCentroid = centroidNum / centroidDen;
        const float logMin = std::log(20.0f);
        const float logMax = std::log(20000.0f);
        float logCentroid  = 0.0f;
        if (rawCentroid > 0.0f)
            logCentroid = (std::log(rawCentroid) - logMin) / (logMax - logMin);
        spectralCentroid = SmoothAttackRelease(spectralCentroid, Clamp01(logCentroid), 0.3f, 0.1f);

        // Adaptive normalisation — read settings once per thread iteration, not per hop
        const float kPeakDecay = g_Settings()->Get("settings.player.audio_peak_decay", 0.999f);
        const float kPeakFloor = g_Settings()->Get("settings.player.audio_peak_floor", 0.01f);
        const float bassMult   = g_Settings()->Get("settings.player.audio_bass_mult",  0.7f);
        const float midMult    = g_Settings()->Get("settings.player.audio_mid_mult",   0.8f);
        const float highMult   = g_Settings()->Get("settings.player.audio_high_mult",  0.7f);

        bassPeak = std::max({bassEnergy, bassPeak * kPeakDecay, kPeakFloor});
        midPeak  = std::max({midEnergy,  midPeak  * kPeakDecay, kPeakFloor});
        highPeak = std::max({highEnergy, highPeak * kPeakDecay, kPeakFloor});

        // gamma=2: squares the normalised ratio so quiet passages (e.g. 20% of peak)
        // read ~0.04 instead of 0.20. Peaks still reach 1.0; only the curve changes.
        constexpr float kGamma = 2.0f;

        float normBass, normMid, normHigh;
        if (warmup < 90)
        {
            ++warmup;
            normBass = Clamp01(std::pow(bassEnergy * 120.0f, kGamma));
            normMid  = Clamp01(std::pow(midEnergy  *  80.0f, kGamma));
            normHigh = Clamp01(std::pow(highEnergy *  60.0f, kGamma));
        }
        else
        {
            normBass = Clamp01(std::pow(bassEnergy / bassPeak, kGamma) * bassMult);
            normMid  = Clamp01(std::pow(midEnergy  / midPeak,  kGamma) * midMult);
            normHigh = Clamp01(std::pow(highEnergy / highPeak,  kGamma) * highMult);
        }

        // Kick / snare / transient
        // volume is RMS-based and doesn't adapt away like normBass/normMid, so it's a reliable
        // gate against ambient content in quiet sections where the adaptive peak has decayed to floor.
        const float bassDelta      = normBass - s_prevBass;
        const float midDelta       = normMid  - s_prevMid;
        const float highDelta      = normHigh - s_prevHigh;
        // Require onset from a low prior level: percussion attacks from near-silence;
        // sustained bass/mid content (guitar, synth) keeps s_prevBass/s_prevMid elevated.
        const bool  kickFired      = volume > 0.30f && normBass > 0.35f && bassDelta > 0.22f && s_prevBass < 0.25f;
        const bool  snareFired     = volume > 0.30f && normMid  > 0.25f && midDelta  > 0.22f && midDelta > bassDelta * 1.5f && s_prevMid < 0.20f;
        const bool  transientFired = volume > 0.25f && std::max({normBass, normMid, normHigh}) > 0.20f &&
                                     (bassDelta + midDelta + highDelta) / 3.0f > 0.26f;
        s_prevBass = normBass; s_prevMid = normMid; s_prevHigh = normHigh;

        // BPM — BeatDetektor primary; aubio onset confidence still drives phase correction below
        const float overrideBpm = s_bpmOverride.load(std::memory_order_relaxed);
        const float aubioConf   = aubio_tempo_get_confidence(aubio.tempo);
        if (overrideBpm > 0.0f)
        {
            s_acfBpm = overrideBpm;
        }
        else
        {
            const bool  slowWins  = (bd_slow.win_val >= bd_fast.win_val);
            const float bdPeriod  = slowWins ? bd_slow.winning_bpm : bd_fast.winning_bpm; // seconds per beat
            const float bdConf    = slowWins ? bd_slow.win_val     : bd_fast.win_val;
            const float bdBpm     = (bdPeriod > 0.0f) ? (60.0f / bdPeriod) : 0.0f;

            // win_val >= 20 is the minimum reliable reading; 60 = finish line (high confidence)
            if (bdConf >= 20.0f && bdBpm >= 40.0f && bdBpm <= 250.0f)
            {
                // Fold octave errors: BeatDetektor sometimes locks onto 2x or 0.5x the true tempo
                float candidate = bdBpm;
                if (s_acfBpm > 0.0f)
                {
                    const float r = candidate / s_acfBpm;
                    if      (r > 1.8f && r < 2.2f)   candidate *= 0.5f;
                    else if (r > 0.45f && r < 0.55f) candidate *= 2.0f;
                }
                // Scale alpha by proximity: full rate within 8% of current estimate, zero at 20%+
                // This prevents a bad detection frame from yanking the estimate far off track.
                const float baseAlpha = Clamp01((bdConf - 20.0f) / 40.0f) * 0.05f;
                const float deviation = (s_acfBpm > 0.0f) ? std::abs(candidate / s_acfBpm - 1.0f) : 0.0f;
                const float alpha     = baseAlpha * Clamp01(1.0f - deviation / 0.15f);
                s_acfBpm = s_acfBpm * (1.0f - alpha) + candidate * alpha;
                s_acfBpm = std::max(40.0f, std::min(250.0f, s_acfBpm));
            }
        }
        s_liveBpm.store(s_acfBpm, std::memory_order_relaxed);

        // Beat phase — advance driven by BeatDetektor BPM; aubio onsets correct phase
        if (s_resetBeatPhase.exchange(false, std::memory_order_relaxed))
        {
            s_beatPhase = 0.0f;
            s_beatCount = 0;
        }

        if (beatFired && aubioConf > 0.12f) ++s_beatCount;

        s_beatInterval = 60.0f / s_acfBpm;

        const float audioDt = float(hops) * float(kHopSize) / float(kSampleRate);
        s_beatPhase += audioDt / s_beatInterval;

        if (s_beatPhase >= 1.0f)
        {
            const uint32_t wraps = static_cast<uint32_t>(s_beatPhase);
            s_beatPhase = std::fmod(s_beatPhase, 1.0f);
            s_liveBeatCount.fetch_add(wraps, std::memory_order_relaxed);
        }

        const bool bpmLocked = (overrideBpm > 0.0f);
        if (!bpmLocked && beatFired && aubioConf > 0.12f && s_beatCount >= 4)
        {
            const float err  = s_beatPhase > 0.5f ? s_beatPhase - 1.0f : s_beatPhase;
            const float gain = 0.5f + aubioConf * 0.4f;
            s_beatPhase -= err * gain;
            if (s_beatPhase < 0.0f) s_beatPhase += 1.0f;
        }

        s_liveBeatPhase.store(s_beatPhase, std::memory_order_relaxed);

        // Silence gate — use wall clock (audio thread runs even when signal is absent)
        bool hasSignal;
        if (clampedLevel > 0.005f)
        {
            silenceStart = Clock::now();
            inSilence    = false;
            hasSignal    = true;
        }
        else
        {
            using FSec = std::chrono::duration<float>;
            const float silenceSec = FSec(Clock::now() - silenceStart).count();
            inSilence = (silenceSec > 1.5f);
            hasSignal = !inSilence;
        }

        // Publish features — brief lock, render thread gets a copy
        kick      = SmoothAttackRelease(kick,      kickFired      ? 1.0f : 0.0f, 0.90f, 0.06f);
        snare     = SmoothAttackRelease(snare,     snareFired     ? 1.0f : 0.0f, 0.85f, 0.07f);
        transient = SmoothAttackRelease(transient, transientFired ? 1.0f : 0.0f, 0.80f, 0.08f);

        {
            std::lock_guard<std::mutex> lock(m_FeaturesMutex);
            m_Features.volume          = volume;
            m_Features.bass            = normBass;
            m_Features.mid             = normMid;
            m_Features.high            = normHigh;
            m_Features.spectralCentroid = spectralCentroid;
            m_Features.kick            = kick;
            m_Features.snare           = snare;
            m_Features.transient       = transient;
            m_Features.beatPhase       = s_beatPhase;
            m_Features.bpm             = s_acfBpm;
            m_Features.hasSignal       = hasSignal;
            m_Features.sampleCount     = static_cast<int>(sampleBuffer.size());
        }
    }
}
