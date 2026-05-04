#include "AudioInputWasapi.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cmath>

#pragma comment(lib, "Ole32.lib")

AudioInputWasapi::AudioInputWasapi() {}

AudioInputWasapi::~AudioInputWasapi() { Stop(); }

bool AudioInputWasapi::Start()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator));

    if (FAILED(hr) || !enumerator)
    {
        return false;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_Device);
    enumerator->Release();

    if (FAILED(hr) || !m_Device)
    {
        return false;
    }

    hr = m_Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                            reinterpret_cast<void**>(&m_AudioClient));

    if (FAILED(hr) || !m_AudioClient)
    {
        return false;
    }

    WAVEFORMATEX* mixFormat = nullptr;
    hr = m_AudioClient->GetMixFormat(&mixFormat);

    if (FAILED(hr) || !mixFormat)
    {
        return false;
    }

    hr = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0,
                                   mixFormat, nullptr);

    CoTaskMemFree(mixFormat);

    if (FAILED(hr))
    {
        return false;
    }

    hr = m_AudioClient->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void**>(&m_CaptureClient));

    if (FAILED(hr) || !m_CaptureClient)
    {
        return false;
    }

    hr = m_AudioClient->Start();

    if (FAILED(hr))
    {
        return false;
    }

    m_Running = true;
    m_Level = 0.0f;

    return true;
}

void AudioInputWasapi::Stop()
{
    if (m_AudioClient)
    {
        m_AudioClient->Stop();
    }

    if (m_CaptureClient)
    {
        m_CaptureClient->Release();
        m_CaptureClient = nullptr;
    }

    if (m_AudioClient)
    {
        m_AudioClient->Release();
        m_AudioClient = nullptr;
    }

    if (m_Device)
    {
        m_Device->Release();
        m_Device = nullptr;
    }

    m_Running = false;
    m_Level = 0.0f;
}

bool AudioInputWasapi::IsRunning() const { return m_Running; }

float AudioInputWasapi::GetLevel() const
{
    if (!m_CaptureClient)
        return 0.0f;

    UINT32 packetLength = 0;
    HRESULT hr = m_CaptureClient->GetNextPacketSize(&packetLength);

    float level = 0.0f;
    int sampleCount = 0;

    while (SUCCEEDED(hr) && packetLength > 0)
    {
        BYTE* data = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;

        hr = m_CaptureClient->GetBuffer(&data, &numFrames, &flags, nullptr,
                                        nullptr);

        if (FAILED(hr))
            break;

        // assume float samples (common for WASAPI mix format)
        float* samples = reinterpret_cast<float*>(data);

        for (UINT32 i = 0; i < numFrames; ++i)
        {
            float s = samples[i];
            level += s * s;
            sampleCount++;
        }

        m_CaptureClient->ReleaseBuffer(numFrames);

        hr = m_CaptureClient->GetNextPacketSize(&packetLength);
    }

    if (sampleCount > 0)
    {
        level = std::sqrtf(level / sampleCount);
    }

    return level;
}