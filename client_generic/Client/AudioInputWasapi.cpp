#include "AudioInputWasapi.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

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

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();

    if (FAILED(hr) || !device)
    {
        return false;
    }

    device->Release();

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