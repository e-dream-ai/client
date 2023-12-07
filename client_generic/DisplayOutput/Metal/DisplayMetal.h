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
    static uint32 s_DefaultWidth;
    static uint32 s_DefaultHeight;
    CGraphicsContext m_GraphicsContext;

  public:
    CDisplayMetal();
    virtual ~CDisplayMetal();

    static const char* Description() { return "macOS Metal display"; };

    virtual bool Initialize(CGraphicsContext _graphicsContext, bool _bPreview);

    virtual void SetContext(CGraphicsContext context);
    virtual CGraphicsContext GetContext(void);

    virtual void ForceWidthAndHeight(uint32 _width, uint32 _height);

    static void SetDefaultWidthAndHeight(uint32 defWidth, uint32 defHeight);

    virtual bool HasShaders() { return true; };

    //
    virtual void Title(const std::string& _title);
    virtual void Update();

    void SwapBuffers();
};

}; // namespace DisplayOutput

#endif
