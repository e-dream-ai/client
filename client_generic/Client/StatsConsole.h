#ifndef _STATSCONSOLE_H_
#define _STATSCONSOLE_H_

#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "Console.h"
#include "Hud.h"
#include "Rect.h"
#include "Text.h"
#include "PlatformUtils.h"

namespace Hud
{

/*
 */
class CStat
{
    bool m_bVisible;

  public:
    CStat(const std::string _name) : m_bVisible(true), m_Name(_name){};
    virtual ~CStat(){};

    std::string m_Name;

    virtual const std::string Report(const double _time) = PureVirtual;

    void Visible(const bool _bState) { m_bVisible = _bState; };
    bool Visible(void) const { return m_bVisible; };
};

// MakeSmartPointers( CStat );

/*
 */
class CStringStat : public CStat
{
  protected:
    std::string m_PreString, m_Value;

  public:
    CStringStat(const std::string _name, const std::string _pre,
                const std::string _init)
        : CStat(_name), m_PreString(_pre), m_Value(_init){};
    virtual ~CStringStat(){};

    virtual const std::string Report(const double /*_time*/)
    {
        std::stringstream s;
        s << m_PreString << m_Value;

        std::string res = s.str();
        return res;
    }

    void SetSample(const std::string _val) { m_Value = _val; };
};

// MakeSmartPointers( CStringStat );

/*
 */
class CIntCounter : public CStat
{
  protected:
    std::string m_PreString, m_PostString;
    double m_Value;

  public:
    CIntCounter(const std::string _name, const std::string _pre,
                const std::string _post)
        : CStat(_name), m_PreString(_pre), m_PostString(_post), m_Value(0){};
    virtual ~CIntCounter(){};

    virtual const std::string Report(const double /*_time*/)
    {
        std::stringstream s;
        s << m_PreString << uint32_t(m_Value) << m_PostString;
        return s.str();
    }

    void SetSample(const int32_t& _val) { m_Value = _val; };
    void AddSample(const int32_t& _val) { m_Value += _val; };
};

// MakeSmartPointers( CIntCounter );

/*
 */
class CAverageCounter : public CIntCounter
{
    double m_Rate;
    double m_Time;
    std::string m_Average;

  public:
    CAverageCounter(const std::string _name, const std::string _pre,
                    const std::string _post, const double _rateInSeconds)
        : CIntCounter(_name, _pre, _post)
    {
        m_Rate = _rateInSeconds;
        m_Time = 0.0;
        m_Average = m_PreString;
        m_Average += "?";
        m_Average += m_PostString;
    };
    virtual ~CAverageCounter(){};

    virtual const std::string Report(const double _time)
    {
        if (m_Time < 0.0005)
        {
            m_Time = _time;
            m_Value = 0;

            return m_Average;
        }

        if (_time - m_Time > m_Rate)
        {
            m_Value /= _time - m_Time / m_Rate;
            m_Value += 1.0;

            m_Average = CIntCounter::Report(_time);
            m_Value = 0;
            m_Time = _time;
        }

        return m_Average;
    }
};

// MakeSmartPointers( CAverageCounter );

/*
 */
class CTimeCountDownStat : public CStat
{
  protected:
    std::string m_PreString, m_PreValue, m_PostValue;
    double m_EndTime;

    bool m_ShowMinutes;

    Base::CTimer m_Timer;

  public:
    CTimeCountDownStat(const std::string _name, const std::string _pre,
                       const std::string _init)
        : CStat(_name), m_PreString(_pre)
    {
        m_Timer.Reset();

        m_ShowMinutes = true;

        SetSample(_init);
    };

    virtual ~CTimeCountDownStat(){};

    virtual const std::string Report(const double /*_time*/)
    {
        std::stringstream s;
        s << m_PreString << m_PreValue;

        if (m_EndTime > 0.001)
        {
            double delaysec = ceil(m_EndTime - m_Timer.Time());

            if (delaysec < 0.0)
                delaysec = 0.0;

            s << std::fixed << std::setprecision(0);

            if (m_ShowMinutes && delaysec > 59.0)
            {
                double delaymin = ceil(delaysec / 60.0);

                s << delaymin << ((delaymin == 1.0) ? " minute" : " minutes");
            }
            else
            {
                s << delaysec << ((delaysec == 1.0) ? " second" : " seconds");
            }
        }

        s << m_PostValue;

        std::string res = s.str();
        return res;
    }

    void SetSample(const std::string& _val)
    {
        m_EndTime = 0.0;

        m_ShowMinutes = true;

        size_t len = _val.size();

        size_t start = 0, end = len;

        bool found = false;

        for (size_t i = 0; i < len; i++)
        {
            char ch = _val[i];

            if (ch == '{')
            {
                start = i;
                found = true;
            }

            if (ch == '}')
                end = i + 1;
        }

        if (found)
        {
            int secs = 0;

            sscanf(_val.substr(start, end - start).c_str(), "{%d}", &secs);

            if (secs < 120)
            {
                m_ShowMinutes = false;
            }

            m_EndTime = m_Timer.Time() + secs;

            m_PreValue = _val.substr(0, start);
            m_PostValue = _val.substr(end, len - end);
        }
        else
        {
            m_PreValue = _val;
            m_PostValue.clear();
        }
    };
};

// MakeSmartPointers( CTimeCountDownStat );

/*
        CStatsConsole.

*/
class CStatsConsole : public CConsole
{
    struct StatText
    {
        CStat* stat;
        DisplayOutput::spCBaseText text;
        bool isRightAligned = false;
        Base::Math::CVector4 color = {1, 1, 1, 1};  // Default white
        std::string alignWithStat;  // For right-aligned stats, which left stat to align with
    };

    std::vector<std::pair<std::string, StatText>> m_Stats;
    DisplayOutput::CFontDescription m_Desc;
    Base::Math::CRect m_TotalExtent;

  public:
    CStatsConsole(Base::Math::CRect _rect, const std::string& _FontName,
                  const uint32_t _fontHeight)
        : CConsole(_rect)
    {
        DisplayOutput::CFontDescription fontDesc;

        m_Desc.AntiAliased(true);
        m_Desc.Height(_fontHeight);
        m_Desc.Style(DisplayOutput::CFontDescription::Normal);
        m_Desc.Italic(false);
        m_Desc.TypeFace(_FontName);
        // Renderer can be unavailable during early startup; lazily acquire font in Render().
        m_spFont = nullptr;

        m_Stats.clear();
    }

    virtual ~CStatsConsole()
    {
        auto ii = m_Stats.begin();
        while (m_Stats.end() != ii)
        {
            delete ii->second.stat;
            ++ii;
        }
        m_Stats.clear();
    }

    void Add(CStat* _pStat, bool _isRightAligned = false, Base::Math::CVector4 _color = {1, 1, 1, 1}, const std::string& _alignWithStat = "")
    {
        // Check if stat already exists to prevent duplicates
        auto it = std::find_if(m_Stats.begin(), m_Stats.end(),
                               [=](const auto& i) { return i.first == _pStat->m_Name; });
        if (it != m_Stats.end()) {
            g_Log->Warning("Stat '%s' already exists, skipping duplicate", _pStat->m_Name.c_str());
            delete _pStat; // Clean up the duplicate stat
            return;
        }

        // Text object may be created later in Render() once a renderer is available.
        DisplayOutput::spCBaseText newText = nullptr;
        if (auto spRenderer = g_Player().Renderer(); spRenderer && m_spFont)
        {
            newText = spRenderer->NewText(m_spFont, "");
        }

        m_Stats.emplace_back(_pStat->m_Name,
                             StatText{_pStat, newText, _isRightAligned, _color, _alignWithStat});
    }

    void SetColor(std::string_view _name, Base::Math::CVector4 _color)
    {
        auto it = std::find_if(m_Stats.begin(), m_Stats.end(),
                               [=](auto i) { return i.first == _name; });
        if (it != m_Stats.end()) {
            it->second.color = _color;
        } else {
            g_Log->Warning("SetColor: stat '%s' not found", std::string(_name).c_str());
        }
    }

    CStat* Get(std::string_view _name)
    {
        auto it = std::find_if(m_Stats.begin(), m_Stats.end(),
                               [=](auto i) { return i.first == _name; });
        if (it == m_Stats.end())
            return nullptr;
        return it->second.stat;
    }

    virtual void Visible(const bool _bState) override
    {
        CHudEntry::Visible(_bState);
        if (!_bState)
        {
            for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
            {
                if (i->second.text)
                {
                    i->second.text->SetEnabled(false);
                }
            }
        }
    }

    bool Render(const double _time,
                DisplayOutput::spCRenderer _spRenderer) override
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (g_Player().Stopped() || m_Stats.empty() || !g_Player().HasStarted())
            return true; // Skip rendering but keep entry alive (false = remove from HudManager)

        // Lazily initialize font/text now that we have a renderer.
        if (_spRenderer)
        {
            if (!m_spFont)
            {
                m_spFont = _spRenderer->GetFont(m_Desc);
            }
            if (m_spFont)
            {
                for (auto& entry : m_Stats)
                {
                    if (!entry.second.text)
                    {
                        entry.second.text = _spRenderer->NewText(m_spFont, "");
                    }
                }
            }
        }

        auto spDisplay = _spRenderer->Display();
        if (!spDisplay)
            return true;

        float step = (float)m_Desc.Height() /
                     (float)spDisplay->Height();
#ifdef SCREEN_SAVER
        step *= 0.5f;
#endif
        float pos = 0;
        float edge = 24 / (float)spDisplay->Width();

        // First pass: update text content and calculate layout.
        // Always reserve space for every stat (visible or not) so that
        // toggling visibility doesn't shift other lines vertically.
        std::unordered_map<std::string, Base::Math::CVector2> sizes;
        m_TotalExtent = {0, 0, 0, 0};
        for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
        {
            CStat* e = i->second.stat;
            DisplayOutput::spCBaseText& text = i->second.text;
            if (text && e)
            {
                text->SetEnabled(e->Visible());

                float lineHeight = step;
                if (e->Visible())
                {
                    text->SetText(e->Report(_time));
                    Base::Math::CVector2 size = text->GetExtent();
                    lineHeight = std::max(size.m_Y, step);
                    sizes[i->first] = {size.m_X, lineHeight};
                }
                else
                {
                    sizes[i->first] = {0, step};
                }

                if (!i->second.isRightAligned)
                {
                    m_TotalExtent = m_TotalExtent.Union(Base::Math::CRect(
                        0, pos, sizes[i->first].m_X + (edge * 2),
                        lineHeight + (pos) + (edge * 2)));
                    pos += lineHeight;
                }
            }
        }

        m_TotalExtent.m_Y0 = 1.f - m_TotalExtent.m_Y1;
        m_TotalExtent.m_Y1 = 1.f;

        // Second pass: position all stats at their fixed slots.
        pos = m_TotalExtent.m_Y0 + edge;
        std::unordered_map<std::string, float> leftStatPositions;

        for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
        {
            CStat* e = i->second.stat;
            if (!e)
                continue;

            Base::Math::CVector2 size = sizes[i->first];

            if (i->second.isRightAligned)
            {
                if (e->Visible())
                {
                    DisplayOutput::spCBaseText& text = i->second.text;
                    if (text)
                    {
                        float baseY = leftStatPositions[i->second.alignWithStat];
                        float rightX = 1.0f - edge - size.m_X;
                        text->SetRect(Base::Math::CRect(rightX, baseY, 1.0f - edge,
                                                        size.m_Y + baseY + step));
                    }
                }
            }
            else
            {
                leftStatPositions[i->first] = pos;
                if (e->Visible())
                {
                    DisplayOutput::spCBaseText& text = i->second.text;
                    if (text)
                    {
                        text->SetRect(Base::Math::CRect(edge, pos, 1,
                                                        size.m_Y + pos + step));
                    }
                }
                pos += size.m_Y;
            }
        }

        _spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader |
                           DisplayOutput::eBlend);
        _spRenderer->SetBlend("alphablend");
        _spRenderer->Apply();
        _spRenderer->DrawSoftQuad(m_TotalExtent,
                                  Base::Math::CVector4(0, 0, 0, 0.375f), 16);

        for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
        {
            CStat* e = i->second.stat;
            if (e && e->Visible())
            {
                DisplayOutput::spCBaseText& text = i->second.text;
                if (text)
                {
                    _spRenderer->DrawText(text, i->second.color);
                }
            }
        }

        return true;
    }
};

MakeSmartPointers(CStatsConsole);

}; // namespace Hud

#endif
