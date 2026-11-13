#ifndef _STEPTIMER_H_
#define _STEPTIMER_H_

#include <chrono>
#include <cstdlib>

#include "Log.h"

namespace Base
{

// Opt-in step timing for hunting frame hitches (first used for client#798;
// see "Timing diagnostics" in the README). Off unless INFINIDREAM_STEP_TIMING_MS
// is set; then any timed step that takes at least that many milliseconds logs
// "Slow step <label> ms=<n>". Set it to 0 to log every timed step.
//
// To time a step, put a CStepTimer on the stack for its scope:
//     Base::CStepTimer timer("decoder avcodec_open2");
// Labels must be string literals (the pointer is kept, not copied).
inline double StepTimingThresholdMs()
{
    static const double thresholdMs = []() {
        const char* env = std::getenv("INFINIDREAM_STEP_TIMING_MS");
        return (env && *env) ? std::atof(env) : -1.0;
    }();
    return thresholdMs;
}

inline bool StepTimingEnabled() { return StepTimingThresholdMs() >= 0.0; }

class CStepTimer
{
  public:
    // minMs raises the threshold for this step only. Use it for steps that
    // legitimately take most of a frame, such as waiting for vsync, so they
    // log only when they run over.
    explicit CStepTimer(const char* label, double minMs = 0.0)
        : m_Label(label), m_MinMs(minMs),
          m_Start(std::chrono::steady_clock::now())
    {
    }
    ~CStepTimer() { Stop(); }

    CStepTimer(const CStepTimer&) = delete;
    CStepTimer& operator=(const CStepTimer&) = delete;

    // Ends the step early; the destructor then does nothing.
    void Stop()
    {
        if (!m_Label)
            return;
        double thresholdMs = StepTimingThresholdMs();
        if (thresholdMs >= 0.0)
        {
            if (m_MinMs > thresholdMs)
                thresholdMs = m_MinMs;
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - m_Start)
                                  .count();
            if (ms >= thresholdMs)
                g_Log->Info("Slow step %s ms=%.1f", m_Label, ms);
        }
        m_Label = nullptr;
    }

  private:
    const char* m_Label;
    double m_MinMs;
    std::chrono::steady_clock::time_point m_Start;
};

} // namespace Base

#endif
