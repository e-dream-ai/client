#ifndef	_SERVERMESSAGE_H_
#define _SERVERMESSAGE_H_

#include	<sstream>
#include	"Hud.h"
#include	"Rect.h"
#include	"Console.h"
#ifdef WIN32
#include	"boost/date_time/posix_time/posix_time.hpp"
#endif

namespace	Hud
{
static boost::posix_time::ptime m_ServerMessageStartTimer = boost::posix_time::second_clock::local_time();
static bool m_bServerMessageStartTimer = false;
/*
	CServerMessage.

*/
class	CServerMessage : public CConsole
{
	std::string	m_Message;
	DisplayOutput::CFontDescription m_Desc;
    DisplayOutput::spCBaseText        m_spText;
	fp4 m_MoveMessageCounter;

	public:
			CServerMessage( std::string &_msg, Base::Math::CRect _rect, const uint32 _fontHeight ) : CConsole( _rect )
			{
				DisplayOutput::CFontDescription	fontDesc;

				m_Desc.AntiAliased( true );
				m_Desc.Height( _fontHeight );
				m_Desc.Style( DisplayOutput::CFontDescription::Normal );
				m_Desc.Italic( false );
				m_Desc.TypeFace( "Lato" );

				size_t offs = _msg.find_last_of( '\n', _msg.size() );
				m_Message = _msg.substr( 0, offs );

				m_spFont = g_Player().Renderer()->GetFont( m_Desc );
                m_spText = g_Player().Renderer()->NewText( m_spFont, _msg );
				m_MoveMessageCounter = 0.;
			}

			virtual ~CServerMessage()
			{
			}

			//	Override to make it always visible.
			virtual bool	Visible() const	{	return true;	};
            
            virtual void Visible(const bool _bState) override
            {
                CHudEntry::Visible(_bState);
                m_spText->SetEnabled(_bState);
            }

			//
			bool	Render( const fp8 _time, DisplayOutput::spCRenderer _spRenderer )
			{
				if( !CHudEntry::Render( _time, _spRenderer ) )
					return false;
				
				if (m_bServerMessageStartTimer == false)
				{
					m_bServerMessageStartTimer = true;
					m_ServerMessageStartTimer = boost::posix_time::second_clock::local_time();
				}
				//fp4 step = (fp4)m_Desc.Height() / (fp4)_spRenderer->Display()->Height();
				fp4 edge = 24 / (fp4)_spRenderer->Display()->Width();

				std::map<std::string, CStat *>::const_iterator i;

				//	Figure out text extent for all strings.
				Base::Math::CRect	extent;
                Base::Math::CVector2 size = m_spText->GetExtent();
				extent = extent.Union( Base::Math::CRect( 0, 0, size.m_X+(edge*2), size.m_Y+(edge*2) ) );

				boost::posix_time::time_duration td = boost::posix_time::second_clock::local_time() - m_ServerMessageStartTimer;
				if (td.hours() >= 1)
				{
					m_MoveMessageCounter += 0.0005f;
					if (m_MoveMessageCounter >= 1.f)
						m_MoveMessageCounter -= 1.f + edge*2 + fp4(size.m_Y);
				}
				//	Draw quad.
				_spRenderer->Reset( DisplayOutput::eTexture | DisplayOutput::eShader | DisplayOutput::eBlend );

				Base::Math::CRect r( 0.5f - (extent.Width()*0.5f), extent.m_Y0 + m_MoveMessageCounter,
									 0.5f + (extent.Width()*0.5f), extent.m_Y1 + m_MoveMessageCounter);

				_spRenderer->SetBlend( "alphablend" );
				_spRenderer->Apply();
				_spRenderer->DrawSoftQuad( r, Base::Math::CVector4( 0, 0, 0, 0.5 ), 16 );
				
				//dasvo - terrible hack - redo!!
				if (m_spFont)
					m_spFont->Reupload();
                m_spText->SetRect(Base::Math::CRect( r.m_X0+edge, r.m_Y0+edge, r.m_X1, r.m_Y1 ));
                g_Player().Renderer()->DrawText( m_spText, Base::Math::CVector4( 1, 1, 1, 1 ));

				return true;
			}
};

MakeSmartPointers( CServerMessage );

};

#endif


