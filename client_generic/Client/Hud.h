#ifndef _HUD_H_
#define _HUD_H_

#include "Renderer.h"
#include "Timer.h"
#include "base.h"
#include <map>

namespace Hud
{

// Color for all HUD and OSD text: the peach of the infinidream logo, the same
// as the OSD icons (COLOR in osd-icons/make_osd_icons.py, #FFDAB8). Status
// lights keep their own red/green/yellow.
inline Base::Math::CVector4 TextColor()
{
    return Base::Math::CVector4(1.0f, 0.855f, 0.722f, 1.0f);
}

/*
 */
class CHudEntry
{
    bool m_bVisible = false;

  protected:
    Base::Math::CRect m_Rect;
    double m_StartTime = 0, m_Duration = -1, m_Delta = 0;

  public:
    CHudEntry(Base::Math::CRect _rect) : m_Rect(_rect){};
    virtual ~CHudEntry(){};

    // Only CHudManager::Add calls this, and it shows the entry right after.
    // Don't hide it here: re-adding an entry that's on screen (pressing A/D
    // repeatedly re-pops the OSD) would hide its text for a frame (client#783).
    void SetTime(double _startTime, double _duration)
    {
        m_StartTime = _startTime;
        m_Duration = _duration;
        m_Delta = 0;
    };
    virtual bool Render(const double _time,
                        DisplayOutput::spCRenderer /*_spRenderer*/)
    {
        if (m_Duration > 0.0f)
        {
            m_Delta = (_time - m_StartTime) / m_Duration;
            if (m_Delta > 1.0)
            {
                return false;
            }
        }

        return true;
    };

    virtual void Visible(const bool _bState) { m_bVisible = _bState; };
    virtual bool Visible() const { return m_bVisible; };
};

MakeSmartPointers(CHudEntry);

/*
 */
class CHudManager
{
    //	Timer.
    Base::CTimer m_Timer;

    //	Entries.
    std::map<std::string, spCHudEntry> m_EntryMap;

  public:
    CHudManager();
    ~CHudManager();

    //	Add/Remove hud entry. (duration -1 means infinite...)
    bool Add(const std::string _name, spCHudEntry _entry,
             double _duration = -1);
    bool Remove(const std::string _name);

    //	Operators rule.
    spCHudEntry Get(const std::string _what) { return m_EntryMap[_what]; }

    bool Render(DisplayOutput::spCRenderer _spRenderer);
    void HideAll();
    void Toggle(const std::string _name);
    void Hide(const std::string _name);
};

MakeSmartPointers(CHudManager);

}; // namespace Hud

#endif
