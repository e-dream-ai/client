#pragma once
#include "TextureFlat.h"
class CTextureFlatD3D12 : public CTextureFlat
{
    /*
	    IDirect3DTexture9* m_pTextureDX9;
    IDirect3DDevice9* m_pDevice;

    //	Internal to keep track if size or format changed.
    DisplayOutput::eImageFormat m_Format;

  public:
    Base::Math::CRect m_Size;

    CTextureFlatDX(IDirect3DDevice9* _pDevice, const uint32_t _flags = 0);
    virtual ~CTextureFlatDX();

    virtual bool Upload(spCImage _spImage);
    virtual bool Bind(const uint32_t _index);
    virtual bool Unbind(const uint32_t _index);
    bool BindFrame(ContentDecoder::spCVideoFrame _pFrame);*/
};
