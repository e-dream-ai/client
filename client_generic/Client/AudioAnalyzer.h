#ifndef AUDIO_ANALYZER_H_INCLUDED
#define AUDIO_ANALYZER_H_INCLUDED

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

// Manual BPM control — callable from any thread/UI
void     AudioAnalyzer_SetBpmOverride(float bpm); // 0 = release override, resume auto-detect
float    AudioAnalyzer_TapTempo();                 // call on each tap; returns new BPM (0 if < 2 taps yet)
float    AudioAnalyzer_GetCurrentBpm();            // live read of the current BPM estimate
float    AudioAnalyzer_GetBeatPhase();             // 0..1 position within current beat cycle
uint32_t AudioAnalyzer_GetBeatCount();             // monotonically increasing beat counter (wraps at 4B beats)
void     AudioAnalyzer_ResetBeatPhase();           // snap beat phase to 0 (beat 1) on next process loop

#include "AudioInputWasapi.h"

struct AudioFeatures
{
    float volume          = 0.0f;
    float bass            = 0.0f;
    float mid             = 0.0f;
    float high            = 0.0f;
    float spectralCentroid = 0.0f;
    float kick            = 0.0f;  // narrowband onset pulse: 30–200 Hz
    float snare           = 0.0f;  // narrowband onset pulse: 200–800 Hz
    float transient       = 0.0f;  // broadband onset pulse: any transient
    float beatPhase       = 0.0f;  // 0..1 position within current beat cycle
    float bpm             = 120.0f;
    bool  hasSignal       = false;
    int   sampleCount     = 0;
};

class AudioAnalyzer
{
  public:
    ~AudioAnalyzer() { Stop(); }

    void Start(); // launch background processing thread
    void Stop();  // stop thread and join (safe to call multiple times)

    // Thread-safe snapshot for the render loop — never blocks for more than a mutex acquire.
    AudioFeatures GetFeaturesSnapshot() const;

  private:
    void ThreadProc();

    AudioInputWasapi       m_AudioInput;
    AudioFeatures          m_Features;
    mutable std::mutex     m_FeaturesMutex;
    std::thread            m_Thread;
    std::atomic<bool>      m_Running{false};
};

#endif
