#include "AudioAnalyzer.h"

#include "kiss_fft.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace
{
constexpr float kSampleRate = 44100.0f;
constexpr int kAnalysisSize = 2048; // ~46ms window - enough for bass cycles
constexpr float kPeakDecay = 0.999f;

float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float SmoothAttackRelease(float previous, float current, float attack,
                          float release)
{
    const float a = current > previous ? attack : release;
    return previous * (1.0f - a) + current * a;
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

void AudioAnalyzer::Update(double deltaSeconds)
{
    m_Phase += deltaSeconds;

    if (!m_AudioInput.IsRunning())
        m_AudioInput.Start();

    std::vector<float> newSamples = m_AudioInput.GetSamples();

    // Accumulate samples into a rolling buffer
    // This gives us a stable 2048-sample window regardless of how many
    // samples arrive per frame, solving the bass strobing problem
    static std::deque<float> sampleBuffer;

    for (float s : newSamples)
        sampleBuffer.push_back(s);

    // Keep buffer at exactly kAnalysisSize
    while ((int)sampleBuffer.size() > kAnalysisSize)
        sampleBuffer.pop_front();

    m_Features.sampleCount = static_cast<int>(sampleBuffer.size());

    if (newSamples.empty())
    {
        // No new samples this frame - hold current values
        // WASAPI delivers samples every other frame so this is normal
        return;
    }

    // Wait for a full buffer
    if ((int)sampleBuffer.size() < kAnalysisSize)
        return;

    // Copy to vector for FFT
    std::vector<float> samples(sampleBuffer.begin(), sampleBuffer.end());

    const int fftSize = kAnalysisSize;

    // -------------------------------------------------------------------
    // RMS volume - use only the newest samples for responsiveness
    // -------------------------------------------------------------------
    float sumSquares = 0.0f;
    int rmsWindow = static_cast<int>(newSamples.size());
    if (rmsWindow < 1)
        rmsWindow = 1;
    for (int i = fftSize - rmsWindow; i < fftSize; ++i)
        sumSquares += samples[i] * samples[i];

    float rawLevel = std::sqrt(sumSquares / rmsWindow);
    if (rawLevel < DbToLinear(-60.0f))
        rawLevel = 0.0f;
    rawLevel = Clamp01(rawLevel * 3.0f);

    const float level =
        SmoothAttackRelease(m_Features.volume, rawLevel, 0.35f, 0.08f);

    // -------------------------------------------------------------------
    // FFT on the full 2048-sample window
    // -------------------------------------------------------------------
    static kiss_fft_cfg fftConfig = nullptr;
    if (!fftConfig)
        fftConfig = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);

    if (!fftConfig)
        return;

    std::vector<kiss_fft_cpx> fftIn(fftSize), fftOut(fftSize);

    for (int n = 0; n < fftSize; ++n)
    {
        const float w =
            0.5f * (1.0f - std::cos(2.0f * 3.1415926535f * n / (fftSize - 1)));
        fftIn[n].r = samples[n] * w;
        fftIn[n].i = 0.0f;
    }

    kiss_fft(fftConfig, fftIn.data(), fftOut.data());

    float bassEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
    int bassBins = 0, midBins = 0, highBins = 0;
    float weightedFreqSum = 0.0f, magnitudeSum = 0.0f;

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const float r = fftOut[bin].r;
        const float im = fftOut[bin].i;
        const float mag = std::sqrt(r * r + im * im) / fftSize;
        const float f = kSampleRate * bin / fftSize;

        if (f >= 20.0f && f < 20000.0f)
        {
            weightedFreqSum += f * mag;
            magnitudeSum += mag;
        }

        if (f >= 30.0f && f < 200.0f)
        {
            bassEnergy += mag;
            ++bassBins;
        }
        else if (f >= 200.0f && f < 3500.0f)
        {
            midEnergy += mag;
            ++midBins;
        }
        else if (f >= 3500.0f && f < 20000.0f)
        {
            highEnergy += mag;
            ++highBins;
        }
    }

    if (bassBins > 0)
        bassEnergy /= bassBins;
    if (midBins > 0)
        midEnergy /= midBins;
    if (highBins > 0)
        highEnergy /= highBins;

    // -------------------------------------------------------------------
    // Adaptive normalisation
    // -------------------------------------------------------------------
    static float bassPeak = 0.001f, midPeak = 0.001f, highPeak = 0.001f;
    static int warmup = 0;

    bassPeak = std::max(bassEnergy, bassPeak * kPeakDecay);
    midPeak = std::max(midEnergy, midPeak * kPeakDecay);
    highPeak = std::max(highEnergy, highPeak * kPeakDecay);

    float normBass, normMid, normHigh;
    if (warmup < 90)
    {
        ++warmup;
        normBass = Clamp01(bassEnergy * 120.0f);
        normMid = Clamp01(midEnergy * 80.0f);
        normHigh = Clamp01(highEnergy * 60.0f);
    }
    else
    {
        normBass = Clamp01((bassEnergy / bassPeak) * 1.1f);
        normMid = Clamp01((midEnergy / midPeak) * 1.3f);
        normHigh = Clamp01((highEnergy / highPeak) * 1.1f);
    }

    // -------------------------------------------------------------------
    // Spectral centroid
    // -------------------------------------------------------------------
    float rawCentroid = 0.0f;
    if (magnitudeSum > 0.0f)
        rawCentroid = weightedFreqSum / magnitudeSum;

    const float logMin = std::log(20.0f);
    const float logMax = std::log(20000.0f);
    float logCentroid = 0.0f;
    if (rawCentroid > 0.0f)
        logCentroid = (std::log(rawCentroid) - logMin) / (logMax - logMin);

    m_Features.spectralCentroid = SmoothAttackRelease(
        m_Features.spectralCentroid, Clamp01(logCentroid), 0.3f, 0.1f);

    // -------------------------------------------------------------------
    // Kick detection
    // -------------------------------------------------------------------
    static float kickCooldown = 0.0f;

    if (kickCooldown > 0.0f)
        kickCooldown -= static_cast<float>(deltaSeconds);

    float kickPulse = 0.0f;

    const float kickFloor = 0.25f;

    if (normBass > kickFloor && kickCooldown <= 0.0f)
    {
        kickPulse =
            Clamp01(((normBass - kickFloor) / (1.0f - kickFloor)) * 2.0f);
        kickCooldown = 0.45f;
    }

    // -------------------------------------------------------------------
    // Write features
    // -------------------------------------------------------------------
    m_Features.volume = level;

    // Two-stage bass smoothing
    static float fastBass = 0.0f;
    fastBass = SmoothAttackRelease(fastBass, normBass, 0.50f, 0.985f);

    static float slowBass = 0.0f;
    slowBass = SmoothAttackRelease(slowBass, normBass, 0.02f, 0.9999f);

    m_Features.bass = std::max(fastBass, slowBass);
    m_Features.kick =
        SmoothAttackRelease(m_Features.kick, kickPulse, 0.70f, 0.05f);
    m_Features.mid = SmoothAttackRelease(m_Features.mid, normMid, 0.35f, 0.12f);
    m_Features.high =
        SmoothAttackRelease(m_Features.high, normHigh, 0.45f, 0.12f);

    // Signal gate with hysteresis
    static double silenceSeconds = 0.0;
    if (rawLevel > 0.005f)
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