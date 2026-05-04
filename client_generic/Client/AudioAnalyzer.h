#ifndef AUDIO_ANALYZER_H_INCLUDED
#define AUDIO_ANALYZER_H_INCLUDED

#include "AudioInputWasapi.h"

struct AudioFeatures
{
    float bass = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float volume = 0.0f;
    bool hasSignal = false;
};

class AudioAnalyzer
{
  public:
    void Update(double deltaSeconds);
    const AudioFeatures& GetFeatures() const;

  private:

      AudioInputWasapi m_AudioInput;
    double m_Phase = 0.0;
    AudioFeatures m_Features;
};

#endif