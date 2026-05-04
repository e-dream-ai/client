#include "AudioAnalyzer.h"

#include "kiss_fft.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr float kSampleRate = 44100.0f;
constexpr int kAnalysisSize = 2048;

float Clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

float SmoothAttackRelease(float previous, float current, float attack,
                          float release)
{
    const float amount = current > previous ? attack : release;
    return (previous * (1.0f - amount)) + (current * amount);
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

float ApplyFloor(float value, float floor)
{
    return value < floor ? 0.0f : value;
}
} // namespace

void AudioAnalyzer::Update(double deltaSeconds)
{
    m_Phase += deltaSeconds;

    if (!m_AudioInput.IsRunning())
    {
        m_AudioInput.Start();
    }

    std::vector<float> samples = m_AudioInput.GetSamples();

    if (samples.empty())
    {
        m_Features.volume *= 0.80f;
        m_Features.bass *= 0.85f;
        m_Features.kick *= 0.65f;
        m_Features.mid *= 0.80f;
        m_Features.high *= 0.80f;

        if (m_Features.volume < 0.001f)
        {
            m_Features.volume = 0.0f;
            m_Features.bass = 0.0f;
            m_Features.kick = 0.0f;
            m_Features.mid = 0.0f;
            m_Features.high = 0.0f;
        }

        m_Features.hasSignal = false;
        return;
    }

    if (samples.size() > kAnalysisSize)
    {
        samples.erase(samples.begin(), samples.end() - kAnalysisSize);
    }

    const int fftSize =
        std::min<int>(kAnalysisSize, static_cast<int>(samples.size()));

    if (fftSize <= 0)
        return;

    float sumSquares = 0.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        sumSquares += samples[i] * samples[i];
    }

    float rawLevel = std::sqrt(sumSquares / std::max(1, fftSize));

    if (rawLevel < DbToLinear(-36.0f))
        rawLevel = 0.0f;

    rawLevel = Clamp01(rawLevel * 3.0f);

    const float level =
        SmoothAttackRelease(m_Features.volume, rawLevel, 0.35f, 0.08f);

    static kiss_fft_cfg fftConfig = nullptr;
    static int configuredSize = 0;

    if (!fftConfig || configuredSize != fftSize)
    {
        if (fftConfig)
        {
            kiss_fft_free(fftConfig);
            fftConfig = nullptr;
        }

        fftConfig = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
        configuredSize = fftSize;
    }

    if (!fftConfig)
        return;

    std::vector<kiss_fft_cpx> fftIn(fftSize);
    std::vector<kiss_fft_cpx> fftOut(fftSize);

    for (int n = 0; n < fftSize; ++n)
    {
        const float window =
            0.5f * (1.0f - std::cos((2.0f * 3.1415926535f * n) /
                                    static_cast<float>(fftSize - 1)));

        fftIn[n].r = samples[n] * window;
        fftIn[n].i = 0.0f;
    }

    kiss_fft(fftConfig, fftIn.data(), fftOut.data());

    float bassEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;
    float bassFlux = 0.0f;

    int bassBins = 0;
    int midBins = 0;
    int highBins = 0;

    static std::vector<float> previousMagnitude(kAnalysisSize / 2, 0.0f);

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const float real = fftOut[bin].r;
        const float imag = fftOut[bin].i;
        const float magnitude =
            std::sqrt((real * real) + (imag * imag)) / fftSize;

        const float frequency = (kSampleRate * bin) / fftSize;

        if (frequency >= 40.0f && frequency < 120.0f)
        {
            bassEnergy += magnitude;
            ++bassBins;

            bassFlux += std::max(0.0f, magnitude - previousMagnitude[bin]);
        }
        else if (frequency >= 150.0f && frequency < 3500.0f)
        {
            midEnergy += magnitude;
            ++midBins;
        }
        else if (frequency >= 3500.0f && frequency < 12000.0f)
        {
            highEnergy += magnitude;
            ++highBins;
        }

        previousMagnitude[bin] = magnitude;
    }

    if (bassBins > 0)
    {
        bassEnergy /= bassBins;
        bassFlux /= bassBins;
    }

    if (midBins > 0)
        midEnergy /= midBins;

    if (highBins > 0)
        highEnergy /= highBins;

    bassEnergy = Clamp01(bassEnergy * 18.0f);
    bassFlux = Clamp01(bassFlux * 240.0f);
    midEnergy = Clamp01(midEnergy * 150.0f);
    highEnergy = Clamp01(highEnergy * 50.0f);

    bassEnergy = ApplyFloor(bassEnergy, 0.65f);
    bassFlux = ApplyFloor(bassFlux, 0.08f);
    midEnergy = ApplyFloor(midEnergy, 0.03f);
    highEnergy = ApplyFloor(highEnergy, 0.04f);

    static float fluxAverage = 0.0f;
    fluxAverage = (fluxAverage * 0.94f) + (bassFlux * 0.06f);

    static double kickCooldownSeconds = 0.0;
    if (kickCooldownSeconds > 0.0)
    {
        kickCooldownSeconds -= deltaSeconds;
        if (kickCooldownSeconds < 0.0)
            kickCooldownSeconds = 0.0;
    }

    float kickPulse = 0.0f;

    const bool bassIsPresent = bassEnergy > 0.65f;
    const bool fluxIsSignificant =
        bassFlux > 0.10f && bassFlux > (fluxAverage * 1.45f);

    if (bassIsPresent && fluxIsSignificant && kickCooldownSeconds <= 0.0)
    {
        kickPulse = Clamp01((bassFlux - fluxAverage) * 2.2f);
        kickCooldownSeconds = 0.08;
    }

    const float sustainedBass =
        SmoothAttackRelease(m_Features.bass, bassEnergy, 0.30f, 0.08f);

    const float kick =
        SmoothAttackRelease(m_Features.kick, kickPulse, 0.45f, 0.08f);

    m_Features.volume = level;
    m_Features.bass = sustainedBass;
    m_Features.kick = kick;
    m_Features.mid =
        SmoothAttackRelease(m_Features.mid, midEnergy, 0.35f, 0.12f);
    m_Features.high =
        SmoothAttackRelease(m_Features.high, highEnergy, 0.45f, 0.12f);
    m_Features.hasSignal = level > 0.001f;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }