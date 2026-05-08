#include "AudioAnalyzer.h"

extern "C" {
#include "aubio.h"
}

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
#include "Settings.h"

namespace
{
constexpr uint_t kSampleRate = 44100;
constexpr uint_t kWinSize   = 2048;
constexpr uint_t kHopSize   = 512;
constexpr uint_t kNumBins   = kWinSize / 2 + 1;

// ACF BPM estimator: onset-flux autocorrelation over a rolling history window.
// Lag range maps to ~50-200 BPM at 44100/512 hop rate (~86 hops/sec).
constexpr int kOssLen    = 512;   // ~5.9 sec of onset history
constexpr int kBpmMinLag = 26;    // ~200 BPM
constexpr int kBpmMaxLag = 104;   // ~50 BPM
constexpr int kAcfEvery  = 43;    // recompute every ~0.5 sec

float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float SmoothAttackRelease(float previous, float current, float attack, float release)
{
    const float a = current > previous ? attack : release;
    return previous * (1.0f - a) + current * a;
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

struct AubioState
{
    aubio_pvoc_t*   pvoc      = nullptr;
    cvec_t*         fftgrain  = nullptr;
    fvec_t*         hopIn     = nullptr;
    fvec_t*         tempoOut  = nullptr;
    aubio_tempo_t*  tempo     = nullptr;
    bool            ready     = false;

    void Init()
    {
        hopIn    = new_fvec(kHopSize);
        fftgrain = new_cvec(kWinSize);
        tempoOut = new_fvec(2);

        pvoc  = new_aubio_pvoc(kWinSize, kHopSize);
        tempo = new_aubio_tempo("complex", kWinSize, kHopSize, kSampleRate);

        if (pvoc && tempo)
            ready = true;
    }

    ~AubioState()
    {
        if (pvoc)     del_aubio_pvoc(pvoc);
        if (fftgrain) del_cvec(fftgrain);
        if (hopIn)    del_fvec(hopIn);
        if (tempoOut) del_fvec(tempoOut);
        if (tempo)    del_aubio_tempo(tempo);
    }
};

void AudioAnalyzer::Update(double deltaSeconds)
{
    m_Phase += deltaSeconds;

    if (!m_AudioInput.IsRunning())
        m_AudioInput.Start();

    std::vector<float> newSamples = m_AudioInput.GetSamples();

    static std::deque<float> sampleBuffer;
    for (float s : newSamples)
        sampleBuffer.push_back(s);

    m_Features.sampleCount = static_cast<int>(sampleBuffer.size());

    if (sampleBuffer.size() < kHopSize)
        return;

    static AubioState s_aubio;
    if (!s_aubio.ready)
        s_aubio.Init();
    if (!s_aubio.ready)
        return;

    float bassEnergyAcc = 0.0f, midEnergyAcc = 0.0f, highEnergyAcc = 0.0f;
    float centroidNum   = 0.0f, centroidDen  = 0.0f;
    float rmsAcc        = 0.0f;
    int   hops          = 0;
    bool beatFired = false;

    // OSS circular buffer — written inside the hop loop, read by ACF after it
    static float s_prevHopBass = 0.0f, s_prevHopMid = 0.0f, s_prevHopHigh = 0.0f;
    static float s_ossArr[kOssLen] = {};
    static int   s_ossHead         = 0;

    while (sampleBuffer.size() >= kHopSize)
    {
        for (uint_t i = 0; i < kHopSize; ++i)
        {
            const float s         = sampleBuffer[i];
            s_aubio.hopIn->data[i] = s;
            rmsAcc                += s * s;
        }
        for (uint_t i = 0; i < kHopSize; ++i)
            sampleBuffer.pop_front();

        // Spectrum via phase vocoder
        aubio_pvoc_do(s_aubio.pvoc, s_aubio.hopIn, s_aubio.fftgrain);

        float bassEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
        int   bassBins = 0, midBins = 0, highBins = 0;

        for (uint_t bin = 1; bin < kNumBins; ++bin)
        {
            const float mag = s_aubio.fftgrain->norm[bin];
            const float f   = (float)kSampleRate * bin / (float)kWinSize;

            if (f >= 20.0f && f < 20000.0f)
            {
                centroidNum += f * mag;
                centroidDen += mag;
            }
            if (f >= 30.0f && f < 200.0f)            { bassEnergy += mag; ++bassBins; }
            else if (f >= 200.0f  && f < 3500.0f)    { midEnergy  += mag; ++midBins;  }
            else if (f >= 3500.0f && f < 20000.0f)   { highEnergy += mag; ++highBins; }
        }

        if (bassBins > 0) bassEnergy /= bassBins;
        if (midBins  > 0) midEnergy  /= midBins;
        if (highBins > 0) highEnergy /= highBins;

        bassEnergyAcc += bassEnergy;
        midEnergyAcc  += midEnergy;
        highEnergyAcc += highEnergy;

        // Onset flux: positive energy rise per band, bass-weighted for kick dominance
        const float hopFlux = std::max(0.0f, bassEnergy - s_prevHopBass) * 2.0f
                            + std::max(0.0f, midEnergy  - s_prevHopMid)
                            + std::max(0.0f, highEnergy - s_prevHopHigh) * 0.5f;
        s_prevHopBass = bassEnergy; s_prevHopMid = midEnergy; s_prevHopHigh = highEnergy;
        s_ossArr[s_ossHead % kOssLen] = hopFlux;
        ++s_ossHead;

        aubio_tempo_do(s_aubio.tempo, s_aubio.hopIn, s_aubio.tempoOut);
        if (s_aubio.tempoOut->data[0] > 0.0f) beatFired = true;

        ++hops;
    }

    if (hops == 0)
        return;

    const float bassEnergy = bassEnergyAcc / hops;
    const float midEnergy  = midEnergyAcc  / hops;
    const float highEnergy = highEnergyAcc / hops;
    const float rawLevel   = std::sqrt(rmsAcc / (hops * (int)kHopSize));

    // -------------------------------------------------------------------
    // RMS volume
    // -------------------------------------------------------------------
    float clampedLevel = rawLevel;
    if (clampedLevel < DbToLinear(-60.0f))
        clampedLevel = 0.0f;
    clampedLevel = Clamp01(clampedLevel * 3.0f);

    const float level = SmoothAttackRelease(m_Features.volume, clampedLevel, 0.35f, 0.08f);

    // -------------------------------------------------------------------
    // Spectral centroid
    // -------------------------------------------------------------------
    float rawCentroid = 0.0f;
    if (centroidDen > 0.0f)
        rawCentroid = centroidNum / centroidDen;

    const float logMin = std::log(20.0f);
    const float logMax = std::log(20000.0f);
    float logCentroid  = 0.0f;
    if (rawCentroid > 0.0f)
        logCentroid = (std::log(rawCentroid) - logMin) / (logMax - logMin);

    m_Features.spectralCentroid = SmoothAttackRelease(
        m_Features.spectralCentroid, Clamp01(logCentroid), 0.3f, 0.1f);

    // -------------------------------------------------------------------
    // Adaptive normalisation
    // -------------------------------------------------------------------
    static float bassPeak = 0.001f, midPeak = 0.001f, highPeak = 0.001f;
    static int   warmup   = 0;

    const float kPeakDecay = g_Settings()->Get("settings.player.audio_peak_decay", 0.999f);
    bassPeak = std::max(bassEnergy, bassPeak * kPeakDecay);
    midPeak  = std::max(midEnergy,  midPeak  * kPeakDecay);
    highPeak = std::max(highEnergy, highPeak * kPeakDecay);

    float normBass, normMid, normHigh;
    if (warmup < 90)
    {
        ++warmup;
        normBass = Clamp01(bassEnergy * 120.0f);
        normMid  = Clamp01(midEnergy  *  80.0f);
        normHigh = Clamp01(highEnergy *  60.0f);
    }
    else
    {
        const float bassMult = g_Settings()->Get("settings.player.audio_bass_mult", 0.7f);
        const float midMult  = g_Settings()->Get("settings.player.audio_mid_mult",  0.8f);
        const float highMult = g_Settings()->Get("settings.player.audio_high_mult", 0.7f);
        normBass = Clamp01((bassEnergy / bassPeak) * bassMult);
        normMid  = Clamp01((midEnergy  / midPeak)  * midMult);
        normHigh = Clamp01((highEnergy / highPeak)  * highMult);
    }

    // -------------------------------------------------------------------
    // Kick / snare / transient — band-energy delta detection
    // -------------------------------------------------------------------
    static float s_prevBass = 0.0f, s_prevMid = 0.0f, s_prevHigh = 0.0f;
    const float bassDelta  = normBass - s_prevBass;
    const float midDelta   = normMid  - s_prevMid;
    const float highDelta  = normHigh - s_prevHigh;
    const bool kickFired      = bassDelta > 0.15f;
    const bool snareFired     = midDelta > 0.15f && midDelta > bassDelta * 1.2f;
    const bool transientFired = (bassDelta + midDelta + highDelta) / 3.0f > 0.22f;
    s_prevBass = normBass; s_prevMid = normMid; s_prevHigh = normHigh;

    // -------------------------------------------------------------------
    // BPM via onset-flux autocorrelation (recomputed every ~0.5 sec)
    // -------------------------------------------------------------------
    static float s_acfBpm   = 120.0f;
    static int   s_acfSince = 0;
    s_acfSince += hops;

    if (s_acfSince >= kAcfEvery && s_ossHead >= kOssLen)
    {
        s_acfSince = 0;

        // Linearise circular buffer into a contiguous array, oldest sample first
        float oss[kOssLen];
        for (int i = 0; i < kOssLen; ++i)
            oss[i] = s_ossArr[(s_ossHead + i) % kOssLen];

        // Score each candidate lag with ACF + sub-harmonic reinforcement.
        // Dividing by mult down-weights harmonics so the fundamental wins.
        float bestScore = -1.0f;
        int   bestLag   = 43; // 120 BPM fallback

        for (int lag = kBpmMinLag; lag <= kBpmMaxLag; ++lag)
        {
            float score = 0.0f;
            for (int mult = 1; mult <= 3; ++mult)
            {
                const int l = lag * mult;
                if (l >= kOssLen) break;
                float acf = 0.0f;
                for (int i = l; i < kOssLen; ++i)
                    acf += oss[i] * oss[i - l];
                score += (acf / (kOssLen - l)) / static_cast<float>(mult);
            }
            if (score > bestScore) { bestScore = score; bestLag = lag; }
        }

        // Convert lag (hops) to BPM, then fold into the 60-180 BPM range
        float bpmRaw = static_cast<float>(kSampleRate) / kHopSize * 60.0f / bestLag;
        while (bpmRaw > 180.0f) bpmRaw *= 0.5f;
        while (bpmRaw <  60.0f) bpmRaw *= 2.0f;

        // Slow IIR to resist single-frame outliers
        s_acfBpm = s_acfBpm * 0.7f + bpmRaw * 0.3f;
    }

    // -------------------------------------------------------------------
    // Beat phase — ACF provides the period; aubio fires the beat events
    // -------------------------------------------------------------------
    static float s_beatPhase    = 0.0f;
    static float s_beatInterval = 60.0f / 120.0f;
    static int   s_beatCount    = 0;

    const float confidence = aubio_tempo_get_confidence(s_aubio.tempo);
    if (beatFired && confidence > 0.12f) ++s_beatCount;

    s_beatInterval = 60.0f / s_acfBpm;

    s_beatPhase += static_cast<float>(deltaSeconds) / s_beatInterval;
    if (s_beatPhase >= 1.0f)
        s_beatPhase = std::fmod(s_beatPhase, 1.0f);

    // On a confident beat, nudge phase toward 0 (PLL-style) rather than hard-reset
    if (beatFired && confidence > 0.12f && s_beatCount >= 4)
    {
        const float err = s_beatPhase > 0.5f ? s_beatPhase - 1.0f : s_beatPhase;
        s_beatPhase -= err * 0.4f;
        if (s_beatPhase < 0.0f) s_beatPhase += 1.0f;
    }

    // -------------------------------------------------------------------
    // Write features
    // -------------------------------------------------------------------
    m_Features.volume    = level;
    m_Features.bass      = normBass;
    m_Features.mid       = normMid;
    m_Features.high      = normHigh;
    m_Features.transient = SmoothAttackRelease(m_Features.transient, transientFired ? 1.0f : 0.0f, 0.80f, 0.08f);
    m_Features.kick      = SmoothAttackRelease(m_Features.kick,      kickFired      ? 1.0f : 0.0f, 0.90f, 0.06f);
    m_Features.snare     = SmoothAttackRelease(m_Features.snare,     snareFired     ? 1.0f : 0.0f, 0.85f, 0.07f);
    m_Features.beatPhase = s_beatPhase;
    m_Features.bpm       = s_acfBpm;

    // Signal gate
    static double silenceSeconds = 0.0;
    if (clampedLevel > 0.005f)
    {
        silenceSeconds = 0.0;
        m_Features.hasSignal = true;
    }
    else
    {
        silenceSeconds += deltaSeconds;
        if (silenceSeconds > 1.5)
            m_Features.hasSignal = false;
    }
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }
