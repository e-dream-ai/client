#include "AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
        m_Features.volume *= 0.92f;
        m_Features.bass *= 0.92f;
        m_Features.mid *= 0.92f;
        m_Features.high *= 0.92f;
        m_Features.hasSignal = false;
        return;
    }

    float sumSquares = 0.0f;

    for (float sample : samples)
    {
        sumSquares += sample * sample;
    }

    float rawLevel = std::sqrt(sumSquares / samples.size());

    const int fftSize = std::min<int>(512, static_cast<int>(samples.size()));
    float bassEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        float real = 0.0f;
        float imag = 0.0f;

        for (int n = 0; n < fftSize; ++n)
        {
            const float angle = 2.0f * 3.1415926535f *
                                static_cast<float>(bin * n) /
                                static_cast<float>(fftSize);

            real += samples[n] * std::cos(angle);
            imag -= samples[n] * std::sin(angle);
        }

        const float magnitude = std::sqrt(real * real + imag * imag);

        float sampleRate = 48000.0f;
        float frequency = (sampleRate * bin) / fftSize;

        if (frequency < 200.0f)
            bassEnergy += magnitude;
        else if (frequency < 2000.0f)
            midEnergy += magnitude;
        else
            highEnergy += magnitude;
    }

    if (rawLevel < 0.01f)
        rawLevel = 0.0f;

    rawLevel *= 1.5f;
    if (rawLevel > 1.0f)
        rawLevel = 1.0f;

    float attack = 0.35f;
    float release = 0.08f;

    float smoothing = rawLevel > m_Features.volume ? attack : release;

    float level =
        (m_Features.volume * (1.0f - smoothing)) + (rawLevel * smoothing);

    static float peak = 0.0f;

    if (level > peak)
        peak = level;

    peak *= 0.95f;

    bassEnergy *= 0.004f;
    midEnergy *= 0.004f;
    highEnergy *= 0.004f;

    bassEnergy = std::sqrt(bassEnergy);
    midEnergy = std::sqrt(midEnergy);
    highEnergy = std::sqrt(highEnergy);

    if (bassEnergy > 1.0f)
        bassEnergy = 1.0f;
    if (midEnergy > 1.0f)
        midEnergy = 1.0f;
    if (highEnergy > 1.0f)
        highEnergy = 1.0f;

    auto smoothBand = [](float previous, float current)
    {
        const float attack = 0.45f;
        const float release = 0.12f;
        const float amount = current > previous ? attack : release;

        return (previous * (1.0f - amount)) + (current * amount);
    };

    m_Features.volume = level;
    m_Features.bass = smoothBand(m_Features.bass, bassEnergy);
    m_Features.mid = smoothBand(m_Features.mid, midEnergy);
    m_Features.high = smoothBand(m_Features.high, highEnergy);
    m_Features.hasSignal = level > 0.001f;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }