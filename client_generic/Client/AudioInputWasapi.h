#ifndef AUDIO_INPUT_WASAPI_H_INCLUDED
#define AUDIO_INPUT_WASAPI_H_INCLUDED

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
};

#endif