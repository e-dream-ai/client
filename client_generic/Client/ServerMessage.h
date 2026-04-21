#ifndef _SERVERMESSAGE_H_
#define _SERVERMESSAGE_H_

#ifdef WIN32
#include "boost/date_time/posix_time/posix_time.hpp"
#endif

#include "StatsConsole.h"
#include "Hud.h"
#include "Rect.h"
#include "Utf8.h"

namespace Hud
{
static boost::posix_time::ptime m_ServerMessageStartTimer =
    boost::posix_time::second_clock::local_time();
static bool m_bServerMessageStartTimer = false;
/*
        CServerMessage.

*/
class CServerMessage : public CConsole
{
    std::string m_Message;
    DisplayOutput::CFontDescription m_Desc;
    DisplayOutput::spCBaseText m_spText;
    float m_MoveMessageCounter;

  public:
    CServerMessage(std::string& _msg, Base::Math::CRect _rect,
                   const uint32_t _fontHeight)
        : CConsole(_rect)
    {
        DisplayOutput::CFontDescription fontDesc;

        m_Desc.AntiAliased(true);
        m_Desc.Height(_fontHeight);
        m_Desc.Style(DisplayOutput::CFontDescription::Normal);
        m_Desc.Italic(false);
        m_Desc.TypeFace("Lato");

        m_Message = Utf8WrapLinesByByteLimit(_msg, 100);
        // Renderer can be unavailable during early startup; lazily init font/text in Render().
        m_spFont = nullptr;
        m_spText = nullptr;
        m_MoveMessageCounter = 0.;
    }

    virtual ~CServerMessage() {}

    //	Override to make it always visible.
    virtual bool Visible() const override { return true; };

    virtual void Visible(const bool _bState) override
    {
        CHudEntry::Visible(_bState);
        if (m_spText)
            m_spText->SetEnabled(_bState);
    }

    //
    virtual bool Render(const double _time,
                        DisplayOutput::spCRenderer _spRenderer) override
    {
        if (!CHudEntry::Render(_time, _spRenderer))
            return false;

        if (_spRenderer && (!m_spFont || !m_spText))
        {
            if (!m_spFont)
                m_spFont = _spRenderer->GetFont(m_Desc);
            if (m_spFont && !m_spText)
            {
                m_spText = _spRenderer->NewText(m_spFont, m_Message);
                if (m_spText)
                    m_spText->SetEnabled(true);
            }
        }

        if (!m_spText)
            return true; // keep alive; try again next frame

        if (m_bServerMessageStartTimer == false)
        {
            m_bServerMessageStartTimer = true;
            m_ServerMessageStartTimer =
                boost::posix_time::second_clock::local_time();
        }
        // float step = (float)m_Desc.Height() /
        // (float)_spRenderer->Display()->Height();
        auto spDisplay = _spRenderer ? _spRenderer->Display() : nullptr;
        float edge = (spDisplay && spDisplay->Width() > 0) ? (24 / (float)spDisplay->Width()) : 24.f;

        if (m_spText && spDisplay)
            m_spText->SyncLayoutDisplay(spDisplay->Width(), spDisplay->Height());

        //	Figure out text extent for all strings.
        Base::Math::CRect extent;
        Base::Math::CVector2 size = m_spText->GetExtent();
        extent = extent.Union(Base::Math::CRect(0, 0, size.m_X + (edge * 2),
                                                size.m_Y + (edge * 2)));

        boost::posix_time::time_duration td =
            boost::posix_time::second_clock::local_time() -
            m_ServerMessageStartTimer;
        if (td.hours() >= 1)
        {
            m_MoveMessageCounter += 0.0005f;
            if (m_MoveMessageCounter >= 1.f)
                m_MoveMessageCounter -= 1.f + edge * 2 + float(size.m_Y);
        }
        //	Draw quad.
        _spRenderer->Reset(DisplayOutput::eTexture | DisplayOutput::eShader |
                           DisplayOutput::eBlend);

        Base::Math::CRect r(
            0.5f - (extent.Width() * 0.5f), extent.m_Y0 + m_MoveMessageCounter,
            0.5f + (extent.Width() * 0.5f), extent.m_Y1 + m_MoveMessageCounter);

        _spRenderer->SetBlend("alphablend");
        _spRenderer->Apply();

        //@TODO: not needed on Metal. do we need this on DX?
        // dasvo - terrible hack - redo!!
        if (m_spFont)
            m_spFont->Reupload();
        m_spText->SetRect(
            Base::Math::CRect(r.m_X0 + edge, r.m_Y0 + edge, r.m_X1, r.m_Y1));
        _spRenderer->DrawText(m_spText, Base::Math::CVector4(1, 1, 1, 1));

        return true;
    }
};

MakeSmartPointers(CServerMessage);

}; // namespace Hud

#endif
