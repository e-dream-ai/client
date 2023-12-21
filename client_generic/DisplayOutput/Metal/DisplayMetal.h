#ifndef _DISPLAYMETAL__H_
#define _DISPLAYMETAL__H_

#include "../DisplayOutput.h"

namespace DisplayOutput
{

/*
        CDisplayMetal.
        Metal output.
*/
class CDisplayMetal : public CDisplayOutput
{
    static uint32_t s_DefaultWidth;
    static uint32_t s_DefaultHeight;
    CGraphicsContext m_GraphicsContext;

  public:
    CDisplayMetal();
    virtual ~CDisplayMetal();

    static const char* Description() { return "macOS Metal display"; };

    virtual bool Initialize(CGraphicsContext _graphicsContext, bool _bPreview);

    virtual void SetContext(CGraphicsContext context);
    virtual CGraphicsContext GetContext(void);

    virtual void ForceWidthAndHeight(uint32_t _width, uint32_t _height);

    static void SetDefaultWidthAndHeight(uint32_t defWidth, uint32_t defHeight);

    virtual bool HasShaders() { return true; };

    //
    virtual void Title(const std::string& _title);
    virtual void Update();

    void SwapBuffers();
};

}; // namespace DisplayOutput

#endif
