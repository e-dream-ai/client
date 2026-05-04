#include "AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr float kPi = 3.1415926535f;
constexpr float kSampleRate = 48000.0f;

// Bigger than 512 so bass has usable frequency resolution.
// 2048 @ 48kHz = ~23.4 Hz/bin.
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
        m_Features.bass *= 0.80f;
        m_Features.mid *= 0.80f;
        m_Features.high *= 0.80f;
        m_Features.hasSignal = false;
        return;
    }

    if (samples.size() > kAnalysisSize)
    {
        samples.erase(samples.begin(), samples.end() - kAnalysisSize);
    }

    const int fftSize =
        std::min<int>(kAnalysisSize, static_cast<int>(samples.size()));

    float sumSquares = 0.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        sumSquares += samples[i] * samples[i];
    }

    float rawLevel = std::sqrt(sumSquares / std::max(1, fftSize));

    // Overall volume gate. -30 dBFS-ish.
    if (rawLevel < DbToLinear(-30.0f))
        rawLevel = 0.0f;

    rawLevel = Clamp01(rawLevel * 3.0f);

    const float level =
        SmoothAttackRelease(m_Features.volume, rawLevel, 0.35f, 0.08f);

    float bassEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;

    int bassBins = 0;
    int midBins = 0;
    int highBins = 0;

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        float real = 0.0f;
        float imag = 0.0f;

        for (int n = 0; n < fftSize; ++n)
        {
            // Hann window reduces spectral smear. Yes, finally, less brute-force caveman DSP.
            const float window =
                0.5f * (1.0f - std::cos((2.0f * kPi * n) / (fftSize - 1)));

            const float sample = samples[n] * window;
            const float angle = 2.0f * kPi * static_cast<float>(bin * n) /
                                static_cast<float>(fftSize);

            real += sample * std::cos(angle);
            imag -= sample * std::sin(angle);
        }

        const float magnitude = std::sqrt(real * real + imag * imag) / fftSize;
        const float frequency = (kSampleRate * bin) / fftSize;

        if (frequency >= 40.0f && frequency < 120.0f)
        {
            bassEnergy += magnitude;
            ++bassBins;
        }
        else if (frequency >= 120.0f && frequency < 2500.0f)
        {
            midEnergy += magnitude;
            ++midBins;
        }
        else if (frequency >= 2500.0f && frequency < 12000.0f)
        {
            highEnergy += magnitude;
            ++highBins;
        }
    }

    if (bassBins > 0)
        bassEnergy /= bassBins;
    if (midBins > 0)
        midEnergy /= midBins;
    if (highBins > 0)
        highEnergy /= highBins;

    // Convert tiny spectral magnitudes into usable 0..1-ish values.
    bassEnergy = Clamp01(bassEnergy * 28.0f);
    midEnergy = Clamp01(midEnergy * 35.0f);
    highEnergy = Clamp01(highEnergy * 50.0f);

    // Floors per band. These are linear thresholds after scaling.
    bassEnergy = ApplyFloor(bassEnergy, 0.18f);
    midEnergy = ApplyFloor(midEnergy, 0.05f);
    highEnergy = ApplyFloor(highEnergy, 0.04f);

    // Bass should care more about sudden low-end changes than constant rumble.
    static float previousBassEnergy = 0.0f;
    const float bassRise = std::max(0.0f, bassEnergy - previousBassEnergy);
    previousBassEnergy = bassEnergy;

    const float kickEnergy = Clamp01((bassEnergy * 0.6f) + (bassRise * 1.4f));

    m_Features.volume = level;
    m_Features.bass = m_Features.bass =
        SmoothAttackRelease(m_Features.bass, kickEnergy, 0.35f, 0.08f);
    m_Features.mid =
        SmoothAttackRelease(m_Features.mid, midEnergy, 0.35f, 0.08f);
    m_Features.high =
        SmoothAttackRelease(m_Features.high, highEnergy, 0.45f, 0.12f);
    m_Features.hasSignal = level > 0.001f;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }