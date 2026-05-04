#include "AudioInputWasapi.h"

AudioInputWasapi::AudioInputWasapi() {}

AudioInputWasapi::~AudioInputWasapi() { Stop(); }

bool AudioInputWasapi::Start()
{
    m_Running = true;
    m_Level = 0.0f;
    return true;
}

void AudioInputWasapi::Stop()
{
    m_Running = false;
    m_Level = 0.0f;
}

bool AudioInputWasapi::IsRunning() const { return m_Running; }

float AudioInputWasapi::GetLevel() const { return m_Level; }