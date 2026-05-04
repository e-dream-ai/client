#include "AudioAnalyzer.h"

#include <cmath>

void AudioAnalyzer::Update(double deltaSeconds)
{
    m_Phase += deltaSeconds;

    if (!m_AudioInput.IsRunning())
    {
        m_AudioInput.Start();
    }

    float rawLevel = m_AudioInput.GetLevel();

    // Boost raw input before smoothing
    rawLevel *= 1.5f;
    if (rawLevel > 1.0f)
        rawLevel = 1.0f;
    // Noise floor clamp
    if (rawLevel < 0.01f)
        rawLevel = 0.0f;

    // Fast rise, slow fall
    float attack = 0.35f;
    float release = 0.08f;

    float smoothing = rawLevel > m_Features.volume ? attack : release;

    float level =
        (m_Features.volume * (1.0f - smoothing)) + (rawLevel * smoothing);

    static float peak = 0.0f;

    // quick peak capture
    if (level > peak)
        peak = level;

    // decay
    peak *= 0.95f;

    m_Features.volume = level;
    m_Features.bass = level;
    m_Features.mid = level * 0.7f;
    m_Features.high = level * 0.4f;
    m_Features.hasSignal = level > 0.001f;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }