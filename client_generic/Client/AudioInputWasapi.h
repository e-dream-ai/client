#ifndef AUDIO_INPUT_WASAPI_H_INCLUDED
#define AUDIO_INPUT_WASAPI_H_INCLUDED

#include <audioclient.h>
#include <mmdeviceapi.h>

class AudioInputWasapi
{
  public:
    AudioInputWasapi();
    ~AudioInputWasapi();

    bool Start();
    void Stop();

    bool IsRunning() const;
    float GetLevel() const;

  private:
    bool m_Running = false;
    float m_Level = 0.0f;

    IAudioClient* m_AudioClient = nullptr;
    IAudioCaptureClient* m_CaptureClient = nullptr;
    IMMDevice* m_Device = nullptr;
};

#endif