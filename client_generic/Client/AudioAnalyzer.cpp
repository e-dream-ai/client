#include "AudioAnalyzer.h"

#include <cmath>

void AudioAnalyzer::Update(double deltaSeconds)
{
    m_Phase += deltaSeconds;

    m_Features.bass = 0.5f + 0.5f * static_cast<float>(std::sin(m_Phase * 3.0));
    m_Features.mid = 0.5f + 0.5f * static_cast<float>(std::sin(m_Phase * 5.0));
    m_Features.high =
        0.5f + 0.5f * static_cast<float>(std::sin(m_Phase * 11.0));

    m_Features.volume =
        (m_Features.bass + m_Features.mid + m_Features.high) / 3.0f;

    m_Features.hasSignal = true;
}

const AudioFeatures& AudioAnalyzer::GetFeatures() const { return m_Features; }