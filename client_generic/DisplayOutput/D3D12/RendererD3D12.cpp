#include "RendererD3D12.h"
#include "DisplayD3D12.h"
#include "TextureFlatD3D12.h"

namespace DisplayOutput
{
CRendererD3D12::CRendererD3D12() { 
	m_WindowHandle = NULL; 
}

CRendererD3D12::~CRendererD3D12() {}

bool CRendererD3D12::Initialize(spCDisplayOutput _spDisplay) 
{ 
    m_spDisplay = _spDisplay;
    m_pDevice = m_spDisplay->GetDevice();   // Grab DX12 device from display, it's created there

    return true; 
}

void CRendererD3D12::Defaults() {}

bool CRendererD3D12::BeginFrame(void) { return true;  }
bool CRendererD3D12::EndFrame(bool drawn) { return true; }


void CRendererD3D12::Apply() {}
void CRendererD3D12::Reset(const uint32_t _flags) {}

bool CRendererD3D12::TestResetDevice() { return true; }

spCTextureFlat CRendererD3D12::NewTextureFlat(const uint32_t flags) 
{
    spCTextureFlat texture = std::make_shared<CTextureFlatD3D12>(m_spDisplay, flags);

    return texture;
}

spCTextureFlat CRendererD3D12::NewTextureFlat(spCImage _spImage, const uint32_t flags) 
{
    spCTextureFlat texture = std::make_shared<CTextureFlatD3D12>(m_spDisplay, flags);
    texture->Upload(_spImage);

    return texture;
}

spCBaseFont CRendererD3D12::NewFont(CFontDescription& _desc) { return spCBaseFont(); }

void CRendererD3D12::Text(spCBaseFont _spFont, const std::string& _text,
                          const Base::Math::CVector4& _color,
                          const Base::Math::CRect& _rect, uint32_t _flags)
{
}

Base::Math::CVector2 CRendererD3D12::GetTextExtent(spCBaseFont _spFont, const std::string& _text) { return Base::Math::CVector2(); }



} // namespace DisplayOutput