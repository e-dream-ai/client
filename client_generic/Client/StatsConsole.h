#ifndef _STATSCONSOLE_H_
#define _STATSCONSOLE_H_

#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "Console.h"
#include "Hud.h"
#include "Rect.h"
#include "Text.h"

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
    };

    std::vector<std::pair<std::string, StatText>> m_Stats;
    DisplayOutput::CFontDescription m_Desc;

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

        m_spFont = g_Player().Renderer()->GetFont(m_Desc);

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

    void Add(CStat* _pStat)
    {
        m_Stats.emplace_back(
            _pStat->m_Name,
            StatText{_pStat, g_Player().Renderer()->NewText(m_spFont, "")});
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

    bool Render(const double _time, DisplayOutput::spCRenderer _spRenderer)
    {
        CHudEntry::Render(_time, _spRenderer);

        float step =
            (float)m_Desc.Height() / (float)_spRenderer->Display()->Height();
        float pos = 0;
        float edge = 24 / (float)_spRenderer->Display()->Width();

        //	Figure out text extent for all strings.
        Base::Math::CRect extent;
        std::queue<Base::Math::CVector2> sizeq;
        for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
        {
            CStat* e = i->second.stat;
            DisplayOutput::spCBaseText& text = i->second.text;
            if (text)
            {
                text->SetEnabled(e->Visible());
            }
            if (e && e->Visible())
            {
                text->SetText(e->Report(_time));
                sizeq.push(text->GetExtent());
                extent = extent.Union(
                    Base::Math::CRect(0, pos, sizeq.back().m_X + (edge * 2),
                                      sizeq.back().m_Y + (pos) + (edge * 2)));
                pos += sizeq.back().m_Y;
            }
        }

        // align soft quad at bottom
        extent.m_Y0 = 1.f - extent.m_Y1;
        extent.m_Y1 = 1.f;

        //	Draw quad.
        _spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader |
                           DisplayOutput::eBlend);
        _spRenderer->SetBlend("alphablend");
        _spRenderer->Apply();
        _spRenderer->DrawSoftQuad(extent, Base::Math::CVector4(0, 0, 0, 0.375f),
                                  16);

        //_spRenderer->NewText(m_spFont)

        // align text at bottom
        pos = extent.m_Y0 + edge;
        for (auto i = m_Stats.begin(); i != m_Stats.end(); ++i)
        {
            CStat* e = i->second.stat;
            if (e && e->Visible())
            {
                Base::Math::CVector2 size = sizeq.front();
                sizeq.pop();
                DisplayOutput::spCBaseText& text = i->second.text;
                text->SetRect(
                    Base::Math::CRect(edge, pos, 1, size.m_Y + pos + step));
                _spRenderer->DrawText(text, Base::Math::CVector4(1, 1, 1, 1));
                pos += size.m_Y;
            }
        }

        return true;
    }
};

MakeSmartPointers(CStatsConsole);

}; // namespace Hud

#endif
