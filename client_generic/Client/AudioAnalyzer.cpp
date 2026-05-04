#include "AudioAnalyzer.h"

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

    m_Features.volume = level;
    m_Features.bass = peak;
    m_Features.mid = level;
    m_Features.high = level * 0.5f;
    m_Features.hasSignal = level > 0.001f;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }