#ifdef USE_METAL

#include "DisplayMetal.h"
#include "Log.h"
#include "Settings.h"

#include "Exception.h"

namespace DisplayOutput
{

uint32 CDisplayMetal::s_DefaultWidth = 0;
uint32 CDisplayMetal::s_DefaultHeight = 0;

/*
 CDisplayMetal().

*/
CDisplayMetal::CDisplayMetal() : CDisplayOutput() {}

/*
~CMacGL().

*/
CDisplayMetal::~CDisplayMetal() {}

void CDisplayMetal::ForceWidthAndHeight(uint32 _width, uint32 _height)
{
    m_Width = _width;
    m_Height = _height;
}

void CDisplayMetal::SetDefaultWidthAndHeight(uint32 defWidth, uint32 defHeight)
{
    s_DefaultWidth = defWidth;
    s_DefaultHeight = defHeight;
}

bool CDisplayMetal::Initialize(CGraphicsContext _graphicsContext,
                               bool /*_bPreview*/)
{
    m_Width = s_DefaultWidth;
    m_Height = s_DefaultHeight;
    m_GraphicsContext = _graphicsContext;

    return true;
}

void CDisplayMetal::SetContext(CGraphicsContext _graphicsContext)
{
    m_GraphicsContext = _graphicsContext;
}

CGraphicsContext CDisplayMetal::GetContext() { return m_GraphicsContext; }

//
void CDisplayMetal::Title(const std::string& /*_title*/) {}

//
void CDisplayMetal::Update() {}

/*
 */
void CDisplayMetal::SwapBuffers() {}

}; // namespace DisplayOutput

#endif /*USE_METAL*/
