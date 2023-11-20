#ifndef	_RENDERERMETAL_H_
#define	_RENDERERMETAL_H_

#include <string>
#include "base.h"
#include "SmartPtr.h"
#include "Renderer.h"
#include "TextureFlatMetal.h"
#include "Image.h"
#include <CoreFoundation/CFBase.h>



namespace	DisplayOutput
{

/*
	CRendererMetal().

*/
class CRendererMetal : public CRenderer
{
	
	spCTextureFlat		m_spSoftCorner;
	
	spCImage			m_spTextImage;
	
	spCTextureFlat		m_spTextTexture;
	
	Base::Math::CRect	m_textRect;
    CFTypeRef           m_pRendererContext;
    boost::shared_mutex m_textureMutex;

	public:
            CRendererMetal();
			virtual ~CRendererMetal();

			virtual eRenderType	Type( void ) const {	return eMetal;	};
			virtual const std::string	Description( void ) const { return "Metal"; }

			//
			bool	Initialize( spCDisplayOutput _spDisplay );

			//
			void	Defaults();

			//
			bool	BeginFrame( void );
			bool	EndFrame( bool drawn = true );

			//
			void	Apply();
			void	Reset( const uint32 _flags );

			//
			spCTextureFlat	NewTextureFlat( const uint32 flags = 0 );
			spCTextureFlat	NewTextureFlat( spCImage _spImage, const uint32 flags = 0 );

			//
            spCBaseFont     NewFont( CFontDescription &_desc );
            spCBaseText     NewText( spCBaseFont _font, const std::string& _text );
            void            DrawText( spCBaseText _text, const Base::Math::CVector4& _color, const Base::Math::CRect &_rect );
			Base::Math::CVector2	GetTextExtent( spCBaseFont _spFont, const std::string &_text );

			//
            spCShader		NewShader( const char *_pVertexShader, const char *_pFragmentShader, std::vector<std::pair<std::string, eUniformType>> uniforms = {} );
    
            void Clear();

			//
			void	DrawQuad( const Base::Math::CRect	&_rect, const Base::Math::CVector4 &_color );
			void	DrawQuad( const Base::Math::CRect	&_rect, const Base::Math::CVector4 &_color, const Base::Math::CRect &_uvRect );
			void	DrawSoftQuad( const Base::Math::CRect &_rect, const Base::Math::CVector4 &_color, const fp4 _width );
	
#ifdef __OBJC__
            void    SetBoundSlot(uint32_t _slot, CTextureFlatMetal* _texture);
            bool    CreateMetalTextureFromDecoderFrame(CVPixelBufferRef pixelBuffer, CVMetalTextureRef* _pMetalTextureRef, uint32_t plane);
#endif
private:
    void BuildDepthTexture();
};

MakeSmartPointers( CRendererMetal );

}

#endif
